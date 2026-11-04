// Event Worker: consumes game events from RabbitMQ and writes each of them to
// the log as a JSON line. A message is acknowledged only after it has been
// processed, so events published while the worker is down wait in the
// durable queue. The worker never touches PostgreSQL or Redis.
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr amqp_channel_t kChannel = 1;
constexpr int kHeartbeatSeconds = 30;
constexpr uint16_t kPrefetchCount = 10;
constexpr timeval kNetworkTimeout{5, 0};

std::atomic<bool> g_stop{false};

void HandleStopSignal(int) { g_stop = true; }

struct Config {
  std::string host;
  int port = 5672;
  std::string user;
  std::string password;
  std::string queue;
};

std::string Env(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? value : fallback;
}

std::string JsonEscape(std::string_view text) {
  std::string escaped;
  for (unsigned char c : text) {
    switch (c) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          escaped += buffer;
        } else {
          escaped += static_cast<char>(c);
        }
    }
  }
  return escaped;
}

std::string NowUtc() {
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
  gmtime_r(&now, &utc);
  char buffer[sizeof("2026-01-01T00:00:00Z")];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer;
}

// `fields` is a pre-rendered JSON fragment such as `,"key":"value"`.
void Log(std::string_view level, std::string_view message,
         const std::string& fields = "") {
  std::cout << R"({"ts":")" << NowUtc() << R"(","service":"event-worker",)"
            << R"("level":")" << level << R"(","msg":")" << JsonEscape(message)
            << '"' << fields << '}' << std::endl;
}

std::string ToString(amqp_bytes_t bytes) {
  return std::string(static_cast<const char*>(bytes.bytes), bytes.len);
}

std::string Property(const amqp_basic_properties_t& properties,
                     amqp_flags_t flag, amqp_bytes_t value) {
  return (properties._flags & flag) != 0 ? ToString(value) : "";
}

bool IsOk(const amqp_rpc_reply_t& reply) {
  return reply.reply_type == AMQP_RESPONSE_NORMAL;
}

bool Connect(amqp_connection_state_t connection, const Config& config) {
  amqp_socket_t* socket = amqp_tcp_socket_new(connection);
  if (socket == nullptr ||
      amqp_socket_open_noblock(socket, config.host.c_str(), config.port,
                               &kNetworkTimeout) != AMQP_STATUS_OK) {
    return false;
  }
  amqp_set_rpc_timeout(connection, &kNetworkTimeout);
  return IsOk(amqp_login(connection, "/", 0, AMQP_DEFAULT_FRAME_SIZE,
                         kHeartbeatSeconds, AMQP_SASL_METHOD_PLAIN,
                         config.user.c_str(), config.password.c_str())) &&
         amqp_channel_open(connection, kChannel) != nullptr &&
         amqp_queue_declare(connection, kChannel,
                            amqp_cstring_bytes(config.queue.c_str()), 0, 1, 0,
                            0, amqp_empty_table) != nullptr &&
         amqp_basic_qos(connection, kChannel, 0, kPrefetchCount, 0) !=
             nullptr &&
         amqp_basic_consume(connection, kChannel,
                            amqp_cstring_bytes(config.queue.c_str()),
                            amqp_empty_bytes, 0, 0, 0,
                            amqp_empty_table) != nullptr;
}

// The "simple action" of the consumer: a structured log record per event.
void ProcessEvent(const amqp_envelope_t& envelope, uint64_t processed_total) {
  const auto& properties = envelope.message.properties;
  const std::string type =
      Property(properties, AMQP_BASIC_TYPE_FLAG, properties.type);
  const std::string event_id =
      Property(properties, AMQP_BASIC_MESSAGE_ID_FLAG, properties.message_id);
  Log("info", "event processed",
      R"(,"event_type":")" + JsonEscape(type) + R"(","event_id":")" +
          JsonEscape(event_id) + R"(","redelivered":)" +
          (envelope.redelivered ? "true" : "false") +
          R"(,"processed_total":)" + std::to_string(processed_total) +
          R"(,"payload":")" + JsonEscape(ToString(envelope.message.body)) +
          '"');
}

void Consume(amqp_connection_state_t connection, uint64_t& processed_total) {
  while (!g_stop) {
    amqp_maybe_release_buffers(connection);
    amqp_envelope_t envelope{};
    timeval timeout{1, 0};
    const amqp_rpc_reply_t reply =
        amqp_consume_message(connection, &envelope, &timeout, 0);
    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
        reply.library_error == AMQP_STATUS_TIMEOUT) {
      continue;
    }
    if (!IsOk(reply)) {
      if (!g_stop) Log("warn", "connection to RabbitMQ lost");
      return;
    }
    ProcessEvent(envelope, ++processed_total);
    const int ack = amqp_basic_ack(connection, envelope.channel,
                                   envelope.delivery_tag, 0);
    amqp_destroy_envelope(&envelope);
    if (ack != AMQP_STATUS_OK) {
      Log("warn", "failed to acknowledge a message");
      return;
    }
  }
}

}  // namespace

int main() {
  struct sigaction action {};
  action.sa_handler = HandleStopSignal;
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);

  const Config config{
      .host = Env("RABBITMQ_HOST", "rabbitmq"),
      .port = std::stoi(Env("RABBITMQ_PORT", "5672")),
      .user = Env("RABBITMQ_USER", "mini_checkers"),
      .password = Env("RABBITMQ_PASSWORD", ""),
      .queue = Env("EVENTS_QUEUE", "game.events"),
  };
  if (config.password.empty()) {
    Log("error", "environment variable RABBITMQ_PASSWORD is required");
    return 1;
  }

  uint64_t processed_total = 0;
  while (!g_stop) {
    amqp_connection_state_t connection = amqp_new_connection();
    if (Connect(connection, config)) {
      Log("info", "consuming events",
          R"(,"queue":")" + JsonEscape(config.queue) + '"');
      Consume(connection, processed_total);
      if (g_stop) {
        amqp_channel_close(connection, kChannel, AMQP_REPLY_SUCCESS);
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
      }
    } else {
      Log("warn", "RabbitMQ is unavailable, retrying",
          R"(,"host":")" + JsonEscape(config.host) + '"');
    }
    amqp_destroy_connection(connection);
    for (int i = 0; i < 20 && !g_stop; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  Log("info", "stopped",
      R"(,"processed_total":)" + std::to_string(processed_total));
  return 0;
}
