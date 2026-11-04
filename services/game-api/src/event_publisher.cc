#include "event_publisher.h"

#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <trantor/utils/Logger.h>

#include <utility>

namespace mini_checkers {
namespace {

constexpr amqp_channel_t kChannel = 1;
constexpr std::size_t kMaxQueuedMessages = 10000;
constexpr auto kRetryDelay = std::chrono::seconds(1);
constexpr auto kShutdownFlushTime = std::chrono::seconds(3);
constexpr timeval kNetworkTimeout{5, 0};

bool IsOk(const amqp_rpc_reply_t& reply) {
  return reply.reply_type == AMQP_RESPONSE_NORMAL;
}

amqp_bytes_t ToBytes(const std::string& value) {
  return amqp_bytes_t{value.size(), const_cast<char*>(value.data())};
}

}  // namespace

EventPublisher::EventPublisher(Config config)
    : config_(std::move(config)), thread_([this] { Run(); }) {}

EventPublisher::~EventPublisher() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    stop_deadline_ = std::chrono::steady_clock::now() + kShutdownFlushTime;
  }
  cv_.notify_all();
  thread_.join();
}

void EventPublisher::Publish(std::string type, std::string event_id,
                             std::string body) {
  {
    std::lock_guard lock(mutex_);
    if (queue_.size() >= kMaxQueuedMessages) {
      LOG_ERROR << "event queue is full, dropping event type=" << type
                << " event_id=" << event_id;
      return;
    }
    queue_.push_back({std::move(type), std::move(event_id), std::move(body)});
  }
  cv_.notify_one();
}

void EventPublisher::Run() {
  while (true) {
    Message message;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) break;
      if (stopping_ && std::chrono::steady_clock::now() > stop_deadline_) {
        LOG_ERROR << "shutting down with " << queue_.size()
                  << " unpublished event(s)";
        break;
      }
      message = queue_.front();
    }

    if (connection_ == nullptr && !Connect()) {
      WaitBeforeRetry();
      continue;
    }
    if (!Send(message)) {
      LOG_WARN << "failed to publish event_id=" << message.event_id
               << ", reconnecting";
      Disconnect(false);
      WaitBeforeRetry();
      continue;
    }
    LOG_INFO << "event published type=" << message.type
             << " event_id=" << message.event_id;
    std::lock_guard lock(mutex_);
    queue_.pop_front();
  }
  Disconnect(true);
}

bool EventPublisher::Connect() {
  connection_ = amqp_new_connection();
  amqp_socket_t* socket = amqp_tcp_socket_new(connection_);
  if (socket == nullptr ||
      amqp_socket_open_noblock(socket, config_.host.c_str(), config_.port,
                               &kNetworkTimeout) != AMQP_STATUS_OK) {
    LOG_WARN << "RabbitMQ is unavailable at " << config_.host << ":"
             << config_.port;
    Disconnect(false);
    return false;
  }
  amqp_set_rpc_timeout(connection_, &kNetworkTimeout);

  const bool ok =
      IsOk(amqp_login(connection_, "/", 0, AMQP_DEFAULT_FRAME_SIZE, 0,
                      AMQP_SASL_METHOD_PLAIN, config_.user.c_str(),
                      config_.password.c_str())) &&
      amqp_channel_open(connection_, kChannel) != nullptr &&
      amqp_queue_declare(connection_, kChannel,
                         amqp_cstring_bytes(config_.queue.c_str()), 0, 1, 0, 0,
                         amqp_empty_table) != nullptr &&
      amqp_confirm_select(connection_, kChannel) != nullptr;
  if (!ok) {
    LOG_WARN << "RabbitMQ login or queue setup failed";
    Disconnect(false);
    return false;
  }
  LOG_INFO << "connected to RabbitMQ, queue=" << config_.queue;
  return true;
}

void EventPublisher::Disconnect(bool graceful) {
  if (connection_ == nullptr) return;
  if (graceful) {
    amqp_channel_close(connection_, kChannel, AMQP_REPLY_SUCCESS);
    amqp_connection_close(connection_, AMQP_REPLY_SUCCESS);
  }
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
