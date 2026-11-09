#pragma once

#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/DbClient.h>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

struct amqp_connection_state_t_;

namespace mini_checkers {

// Publishes game events to a durable RabbitMQ queue. rabbitmq-c is blocking,
// so a dedicated thread drains the PostgreSQL outbox. A row is deleted only
// after a publisher confirm. A crash may cause redelivery, but not loss of
// committed events. This prototype runs one publisher (one API instance).
class EventPublisher {
 public:
  struct Config {
    std::string host;  // Comma-separated broker addresses, tried in rotation.
    int port = 5672;
    std::string user;
    std::string password;
    std::string queue;
  };

  EventPublisher(Config config, drogon::orm::DbClientPtr db,
                 drogon::nosql::RedisClientPtr redis);
  ~EventPublisher();

  EventPublisher(const EventPublisher&) = delete;
  EventPublisher& operator=(const EventPublisher&) = delete;

  // Start only after the schema has been created.
  void Start();

 private:
  struct Message {
    std::string type;
    std::string event_id;
    std::string body;
  };

  void Run();
  bool Connect();
  void Disconnect();
  bool Send(const Message& message);
  void WaitBeforeRetry();

  Config config_;
  drogon::orm::DbClientPtr db_;
  drogon::nosql::RedisClientPtr redis_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
  amqp_connection_state_t_* connection_ = nullptr;
  std::size_t next_host_ = 0;
  std::thread thread_;
};

}  // namespace mini_checkers
