#include "event_publisher.h"

#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <trantor/utils/Logger.h>
#include <sys/socket.h>

#include <chrono>
#include <utility>

namespace mini_checkers {
namespace {

constexpr amqp_channel_t kChannel = 1;
constexpr auto kRetryDelay = std::chrono::seconds(1);
constexpr timeval kNetworkTimeout{5, 0};

bool IsOk(const amqp_rpc_reply_t& reply) {
  return reply.reply_type == AMQP_RESPONSE_NORMAL;
}

amqp_bytes_t ToBytes(const std::string& value) {
  return amqp_bytes_t{value.size(), const_cast<char*>(value.data())};
}

}  // namespace

EventPublisher::EventPublisher(Config config, drogon::orm::DbClientPtr db,
                               drogon::nosql::RedisClientPtr redis)
    : config_(std::move(config)), db_(std::move(db)), redis_(std::move(redis)) {}

void EventPublisher::Start() { thread_ = std::thread([this] { Run(); }); }

EventPublisher::~EventPublisher() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void EventPublisher::Run() {
  while (true) {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) break;
    }
    try {
      auto rows = db_->execSqlSync(
          "SELECT event_id::text, game_id::text, type, "
          "jsonb_build_object('event_id', event_id, 'game_id', game_id, "
          "'type', type, 'request_id', request_id, 'occurred_at', created_at, "
          "'data', data)::text AS body FROM outbox_events "
          "ORDER BY sequence LIMIT 1");
      if (!rows.empty()) {
        const auto row = rows[0];
        Message message{row["type"].as<std::string>(),
                        row["event_id"].as<std::string>(),
                        row["body"].as<std::string>()};
        // Also invalidate on replay after a crash between COMMIT and DEL.
        // Redis is optional: a timeout must not block delivery indefinitely.
        if (redis_) {
          try {
            const auto key = "game:" + row["game_id"].as<std::string>();
            redis_->execCommandSync(
                [](const auto& result) { return result.asInteger(); },
                "DEL %s", key.c_str());
          } catch (const std::exception&) {
            LOG_WARN << "outbox cache invalidation unavailable";
          }
        }
        if ((connection_ != nullptr || Connect()) && Send(message)) {
          db_->execSqlSync("DELETE FROM outbox_events WHERE event_id = $1::uuid",
                           message.event_id);
          LOG_INFO << "event published type=" << message.type
                   << " event_id=" << message.event_id;
          continue;
        }
        Disconnect();
      }
    } catch (const drogon::orm::DrogonDbException&) {
      LOG_WARN << "outbox storage unavailable, retrying";
    } catch (const std::exception&) {
      LOG_ERROR << "outbox iteration failed, retrying";
    }
    WaitBeforeRetry();
  }
  Disconnect();  // Pending rows remain durable; no shutdown flush needed.
}

bool EventPublisher::Connect() {
  const auto end = config_.host.find(',', next_host_);
  const auto host = config_.host.substr(next_host_, end - next_host_);
  next_host_ = end == std::string::npos ? 0 : end + 1;
  connection_ = amqp_new_connection();
  amqp_socket_t* socket = amqp_tcp_socket_new(connection_);
  if (socket == nullptr ||
      amqp_socket_open_noblock(socket, host.c_str(), config_.port,
                               &kNetworkTimeout) != AMQP_STATUS_OK) {
    LOG_WARN << "RabbitMQ is unavailable at " << host << ":"
             << config_.port;
    Disconnect();
    return false;
  }
  amqp_set_rpc_timeout(connection_, &kNetworkTimeout);
  amqp_set_handshake_timeout(connection_, &kNetworkTimeout);
  // Bound blocking writes even if a connected broker stops reading.
  setsockopt(amqp_get_sockfd(connection_), SOL_SOCKET, SO_SNDTIMEO,
             &kNetworkTimeout, sizeof(kNetworkTimeout));

  amqp_table_entry_t entries[2]{};
  entries[0].key = amqp_cstring_bytes("x-queue-type");
  entries[0].value.kind = AMQP_FIELD_KIND_UTF8;
  entries[0].value.value.bytes = amqp_cstring_bytes("quorum");
  // Logging has no poison-message side effect; don't silently drop messages
  // after the quorum queue's default limit of 20 interrupted deliveries.
  entries[1].key = amqp_cstring_bytes("x-delivery-limit");
  entries[1].value.kind = AMQP_FIELD_KIND_I32;
  entries[1].value.value.i32 = -1;
  const amqp_table_t arguments{2, entries};
  const bool ok =
      IsOk(amqp_login(connection_, "/", 0, AMQP_DEFAULT_FRAME_SIZE, 0,
                      AMQP_SASL_METHOD_PLAIN, config_.user.c_str(),
                      config_.password.c_str())) &&
      amqp_channel_open(connection_, kChannel) != nullptr &&
      amqp_queue_declare(connection_, kChannel,
                         amqp_cstring_bytes(config_.queue.c_str()), 0, 1, 0, 0,
                         arguments) != nullptr &&
      amqp_confirm_select(connection_, kChannel) != nullptr;
  if (!ok) {
    LOG_WARN << "RabbitMQ login or queue setup failed";
    Disconnect();
    return false;
  }
  LOG_INFO << "connected to RabbitMQ host=" << host << ", queue=" << config_.queue;
  return true;
}

void EventPublisher::Disconnect() {
  if (connection_ == nullptr) return;
  amqp_destroy_connection(connection_);
  connection_ = nullptr;
}

bool EventPublisher::Send(const Message& message) {
  amqp_basic_properties_t properties{};
  properties._flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
                      AMQP_BASIC_DELIVERY_MODE_FLAG |
                      AMQP_BASIC_MESSAGE_ID_FLAG | AMQP_BASIC_TYPE_FLAG;
  properties.content_type = amqp_cstring_bytes("application/json");
  properties.delivery_mode = AMQP_DELIVERY_PERSISTENT;
  properties.message_id = ToBytes(message.event_id);
  properties.type = ToBytes(message.type);

  // The default exchange routes by queue name.
  if (amqp_basic_publish(connection_, kChannel, amqp_empty_bytes,
                         amqp_cstring_bytes(config_.queue.c_str()), 0, 0,
                         &properties, ToBytes(message.body)) != AMQP_STATUS_OK) {
    return false;
  }
  amqp_publisher_confirm_t confirm{};
  const amqp_rpc_reply_t reply =
      amqp_publisher_confirm_wait(connection_, &kNetworkTimeout, &confirm);
  amqp_maybe_release_buffers(connection_);
  return IsOk(reply) && confirm.method == AMQP_BASIC_ACK_METHOD;
}

void EventPublisher::WaitBeforeRetry() {
  std::unique_lock lock(mutex_);
  cv_.wait_for(lock, kRetryDelay, [this] { return stopping_; });
}

}  // namespace mini_checkers
