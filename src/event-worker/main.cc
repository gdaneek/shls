// Event Worker: consumes game events from RabbitMQ and writes each of them to
// the log as a JSON line. A message is acknowledged only after it has been
// processed, so events published while the worker is down wait in the
// durable queue. The worker never touches PostgreSQL or Redis.
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <signal.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr amqp_channel_t kChannel = 1;
constexpr int kHeartbeatSeconds = 30;
constexpr uint16_t kPrefetchCount = 10;
constexpr timeval kNetworkTimeout{5, 0};

std::atomic<bool> g_stop{false};
static_assert(std::atomic<bool>::is_always_lock_free);

constexpr char kHeartbeatFile[] = "/tmp/event-worker-heartbeat";

void MarkProgress() {
  std::ofstream file("/tmp/event-worker-heartbeat.new");
  file << std::time(nullptr);
  file.close();
  if (!file || std::rename("/tmp/event-worker-heartbeat.new", kHeartbeatFile)) {
    throw std::runtime_error("cannot update worker heartbeat");
  }
}

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

bool Connect(amqp_connection_state_t connection, const Config& config,
             const std::string& host) {
  amqp_socket_t* socket = amqp_tcp_socket_new(connection);
  if (socket == nullptr ||
      amqp_socket_open_noblock(socket, host.c_str(), config.port,
                               &kNetworkTimeout) != AMQP_STATUS_OK) {
    return false;
  }
  amqp_set_rpc_timeout(connection, &kNetworkTimeout);
  amqp_set_handshake_timeout(connection, &kNetworkTimeout);
  setsockopt(amqp_get_sockfd(connection), SOL_SOCKET, SO_SNDTIMEO,
             &kNetworkTimeout, sizeof(kNetworkTimeout));
  amqp_table_entry_t entries[2]{};
  entries[0].key = amqp_cstring_bytes("x-queue-type");
  entries[0].value.kind = AMQP_FIELD_KIND_UTF8;
  entries[0].value.value.bytes = amqp_cstring_bytes("quorum");
  entries[1].key = amqp_cstring_bytes("x-delivery-limit");
  entries[1].value.kind = AMQP_FIELD_KIND_I32;
  entries[1].value.value.i32 = -1;
  const amqp_table_t arguments{2, entries};
  return IsOk(amqp_login(connection, "/", 0, AMQP_DEFAULT_FRAME_SIZE,
                         kHeartbeatSeconds, AMQP_SASL_METHOD_PLAIN,
                         config.user.c_str(), config.password.c_str())) &&
         amqp_channel_open(connection, kChannel) != nullptr &&
         amqp_queue_declare(connection, kChannel,
                            amqp_cstring_bytes(config.queue.c_str()), 0, 1, 0,
                            0, arguments) != nullptr &&
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
      MarkProgress();
      continue;
    }
    if (!IsOk(reply)) {
      if (!g_stop) Log("warn", "connection to RabbitMQ lost");
      return;
    }
    ProcessEvent(envelope, ++processed_total);
    if (!std::cout.good()) {
      amqp_destroy_envelope(&envelope);
      throw std::runtime_error("event log write failed; message not acknowledged");
    }
    const int ack = amqp_basic_ack(connection, envelope.channel,
                                   envelope.delivery_tag, 0);
    amqp_destroy_envelope(&envelope);
    if (ack != AMQP_STATUS_OK) {
      Log("warn", "failed to acknowledge a message");
      return;
    }
    MarkProgress();
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--healthcheck") {
    std::time_t heartbeat = 0;
    std::ifstream file(kHeartbeatFile);
    file >> heartbeat;
    const auto age = std::time(nullptr) - heartbeat;
    return file && age >= 0 && age < 15 ? 0 : 1;
  }
  std::remove(kHeartbeatFile);
  struct sigaction action {};
  action.sa_handler = HandleStopSignal;
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);

  const Config config{
      .host = Env("RABBITMQ_HOST", "rabbitmq,rabbitmq-2,rabbitmq-3"),
      .port = std::stoi(Env("RABBITMQ_PORT", "5672")),
      .user = Env("RABBITMQ_USER", "mini_checkers"),
      .password = Env("RABBITMQ_PASSWORD", ""),
      .queue = Env("EVENTS_QUEUE", "game.events.quorum"),
  };
  if (config.password.empty()) {
    Log("error", "environment variable RABBITMQ_PASSWORD is required");
    return 1;
  }

  uint64_t processed_total = 0;
  std::size_t next_host = 0;
  while (!g_stop) {
    const auto end = config.host.find(',', next_host);
    const auto host = config.host.substr(next_host, end - next_host);
    next_host = end == std::string::npos ? 0 : end + 1;
    amqp_connection_state_t connection = amqp_new_connection();
    if (Connect(connection, config, host)) {
      MarkProgress();
      Log("info", "consuming events",
          R"(,"queue":")" + JsonEscape(config.queue) + R"(","host":")" +
              JsonEscape(host) + '"');
      Consume(connection, processed_total);
      if (g_stop) {
        amqp_channel_close(connection, kChannel, AMQP_REPLY_SUCCESS);
        amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
      }
    } else {
      Log("warn", "RabbitMQ is unavailable, retrying",
          R"(,"host":")" + JsonEscape(host) + '"');
    }
    std::remove(kHeartbeatFile);
    amqp_destroy_connection(connection);
    for (int i = 0; i < 20 && !g_stop; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  Log("info", "stopped",
      R"(,"processed_total":)" + std::to_string(processed_total));
  return 0;
}
