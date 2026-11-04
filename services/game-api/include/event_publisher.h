#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

struct amqp_connection_state_t_;

namespace mini_checkers {

// Publishes game events to a durable RabbitMQ queue. rabbitmq-c is blocking,
// so messages are handed over to a dedicated thread and HTTP handlers never
// wait for the broker. A message is removed from the in-memory queue only
// after the broker confirms it (publisher confirms).
class EventPublisher {
 public:
  struct Config {
    std::string host;
    int port = 5672;
    std::string user;
    std::string password;
    std::string queue;
  };

  explicit EventPublisher(Config config);
  ~EventPublisher();

  EventPublisher(const EventPublisher&) = delete;
  EventPublisher& operator=(const EventPublisher&) = delete;

  void Publish(std::string type, std::string event_id, std::string body);

 private:
  struct Message {
    std::string type;
    std::string event_id;
    std::string body;
  };

  void Run();
  bool Connect();
  void Disconnect(bool graceful);
  bool Send(const Message& message);
  void WaitBeforeRetry();

  Config config_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Message> queue_;
  bool stopping_ = false;
  std::chrono::steady_clock::time_point stop_deadline_;
  amqp_connection_state_t_* connection_ = nullptr;
  std::thread thread_;
};

}  // namespace mini_checkers
