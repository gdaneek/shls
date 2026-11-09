#include <drogon/drogon.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

#include "event_publisher.h"
#include "game_api.h"

namespace {

std::string Env(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? value : fallback;
}

std::string RequiredEnv(const char* name) {
  const std::string value = Env(name, "");
  if (value.empty()) {
    throw std::runtime_error(std::string("environment variable ") + name +
                             " is required");
  }
  return value;
}

int IntEnv(const char* name, int fallback) {
  return std::stoi(Env(name, std::to_string(fallback)));
}

// libpq connection string value: quoted, with ' and \ escaped.
std::string PgQuote(const std::string& value) {
  std::string quoted = "'";
  for (char c : value) {
    if (c == '\'' || c == '\\') quoted += '\\';
    quoted += c;
  }
  return quoted + "'";
}

// Drogon's Redis client needs an IP address rather than a host name.
std::string ResolveIpv4(const std::string& host) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) return "";
  char ip[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr,
            ip, sizeof(ip));
  freeaddrinfo(result);
  return ip;
}

void CreateSchemaWithRetry(mini_checkers::GameApi& api, const std::string& path) {
  constexpr int kAttempts = 10;
  for (int attempt = 1;; ++attempt) {
    try {
      api.CreateSchema(path);
      return;
    } catch (const drogon::orm::DrogonDbException& error) {
      if (attempt == kAttempts) throw;
      LOG_WARN << "PostgreSQL is not ready (" << error.base().what()
               << "), retrying";
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }
}

}  // namespace

int main() {
  // Container logs are read through a pipe; flush every line.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  const std::string pg_connection =
      "host=" + PgQuote(Env("POSTGRES_HOST", "postgres")) +
      " port=" + PgQuote(Env("POSTGRES_PORT", "5432")) +
      " dbname=" + PgQuote(Env("POSTGRES_DB", "mini_checkers")) +
      " user=" + PgQuote(Env("POSTGRES_USER", "mini_checkers")) +
      " password=" + PgQuote(RequiredEnv("POSTGRES_PASSWORD")) +
      " connect_timeout=5 target_session_attrs=read-write application_name=game-api";
  auto db = drogon::orm::DbClient::newPgClient(pg_connection, 2);
  db->setTimeout(5.0);

  drogon::nosql::RedisClientPtr redis;
  const std::string redis_host = Env("REDIS_HOST", "redis");
  const std::string redis_ip = ResolveIpv4(redis_host);
  if (redis_ip.empty()) {
    LOG_WARN << "cannot resolve Redis host " << redis_host
             << ", the cache is disabled";
  } else {
    redis = drogon::nosql::RedisClient::newRedisClient(
        trantor::InetAddress(redis_ip, IntEnv("REDIS_PORT", 6379)), 2);
    redis->setTimeout(1.0);
  }

  mini_checkers::EventPublisher publisher({
      .host = Env("RABBITMQ_HOST", "rabbitmq,rabbitmq-2,rabbitmq-3"),
      .port = IntEnv("RABBITMQ_PORT", 5672),
      .user = Env("RABBITMQ_USER", "mini_checkers"),
      .password = RequiredEnv("RABBITMQ_PASSWORD"),
      .queue = Env("EVENTS_QUEUE", "game.events.quorum"),
  }, db, redis);

  mini_checkers::GameApi api(db, redis,
                             IntEnv("CACHE_TTL_SECONDS", 60));
  CreateSchemaWithRetry(api, Env("POSTGRES_SCHEMA_FILE", "infrastructure/postgres/schema.sql"));
  publisher.Start();
  api.RegisterRoutes();

  drogon::app().registerHandler(
      "/health/live",
      [](drogon::HttpRequestPtr) -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value body;
        body["status"] = "alive";
        co_return drogon::HttpResponse::newHttpJsonResponse(body);
      }, {drogon::Get});
  drogon::app().registerHandler(
      "/health/ready",
      [db](drogon::HttpRequestPtr) -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value body;
        bool ready = false;
        try {
          co_await db->execSqlCoro("SELECT 1 FROM outbox_events LIMIT 1");
          ready = true;
        } catch (const drogon::orm::DrogonDbException&) {
        }
        // Redis is disposable; RabbitMQ can be down while outbox rows wait.
        body["status"] = ready ? "ready" : "not_ready";
        auto response = drogon::HttpResponse::newHttpJsonResponse(body);
        response->setStatusCode(ready ? drogon::k200OK
                                      : drogon::k503ServiceUnavailable);
        co_return response;
      }, {drogon::Get});

  drogon::app().registerHandler(
      "/hello",
      [](const drogon::HttpRequestPtr&,
         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        Json::Value body;
        body["message"] = "hello-world";
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
      },
      {drogon::Get});

  drogon::app()
      .setLogLevel(trantor::Logger::kInfo)
      .addListener("0.0.0.0", 8000)
      .setThreadNum(2)
      .setClientMaxBodySize(1024)
      // Drogon creates its upload directories at startup; the container
      // root filesystem is read-only, so keep them on the /tmp tmpfs.
      .setUploadPath("/tmp/game-api-uploads")
      .run();
  return 0;
}
