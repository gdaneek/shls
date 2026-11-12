#include <gtest/gtest.h>

#include <drogon/HttpAppFramework.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "shls/game_api.h"
#include "shls/event_publisher.h"

using namespace mini_checkers;

class RecordingDbClient : public drogon::orm::DbClient {
 public:
  RecordingDbClient() { type_ = drogon::orm::ClientType::PostgreSQL; }

  std::string sql;
  bool fail = false;
  bool notify_first_query = false;
  std::promise<void> first_query;

  std::shared_ptr<drogon::orm::Transaction> newTransaction(
      const std::function<void(bool)>&) override { return nullptr; }
  void newTransactionAsync(const std::function<void(
      const std::shared_ptr<drogon::orm::Transaction>&)>&) override {}
  bool hasAvailableConnections() const noexcept override { return true; }
  void setTimeout(double) override {}
  void closeAll() override {}

 private:
  void execSql(const char* text, size_t length, size_t,
               std::vector<const char*>&&, std::vector<int>&&, std::vector<int>&&,
               drogon::orm::ResultCallback&& success,
               std::function<void(const std::exception_ptr&)>&& error) override {
    sql.assign(text, length);
    if (notify_first_query) {
      notify_first_query = false;
      first_query.set_value();
    }
    if (fail) {
      error(std::make_exception_ptr(std::runtime_error("database unavailable")));
    } else {
      success(drogon::orm::Result(nullptr));
    }
  }
};

TEST(GameApi, RejectsMissingOrEmptySchema) {
  GameApi api(nullptr, nullptr, 60);
  EXPECT_THROW(api.CreateSchema("/dev/null/schema.sql"), std::runtime_error);
  EXPECT_THROW(api.CreateSchema("/dev/null"), std::runtime_error);
}

TEST(GameApi, LoadsSchemaIntoDatabase) {
  auto db = std::make_shared<RecordingDbClient>();
  GameApi api(db, nullptr, 60);
  std::ifstream file(SHLS_SCHEMA_PATH);
  ASSERT_TRUE(file);
  const std::string expected{std::istreambuf_iterator<char>(file), {}};
  ASSERT_FALSE(expected.empty());
  api.CreateSchema(SHLS_SCHEMA_PATH);
  EXPECT_EQ(db->sql, expected);
}

TEST(GameApi, PropagatesSchemaDatabaseErrors) {
  auto db = std::make_shared<RecordingDbClient>();
  db->fail = true;
  GameApi api(db, nullptr, 60);
  EXPECT_THROW(api.CreateSchema(SHLS_SCHEMA_PATH), std::runtime_error);
  EXPECT_FALSE(db->sql.empty());
}

TEST(GameApi, RegistersGameRoutes) {
  GameApi api(nullptr, nullptr, 60);
  api.RegisterRoutes();
  const auto handlers = drogon::app().getHandlersInfo();
  const auto registered = [&handlers](std::string_view path, drogon::HttpMethod method) {
    return std::any_of(handlers.begin(), handlers.end(), [&](const auto& handler) {
      return std::get<0>(handler) == path && std::get<1>(handler) == method;
    });
  };
  EXPECT_TRUE(registered("/games", drogon::Post));
  EXPECT_TRUE(registered("/games/{id}", drogon::Get));
  EXPECT_TRUE(registered("/games/{id}/join", drogon::Post));
  EXPECT_TRUE(registered("/games/{id}/moves", drogon::Post));
  EXPECT_TRUE(registered("/games/{id}/finish", drogon::Post));
}

TEST(EventPublisher, ReadsOutboxAndStopsWhenDatabaseFails) {
  auto db = std::make_shared<RecordingDbClient>();
  db->fail = true;
  db->notify_first_query = true;
  auto query = db->first_query.get_future();
  {
    EventPublisher publisher({.host = "rabbitmq", .queue = "events"}, db, nullptr);
    publisher.Start();
    EXPECT_EQ(query.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  }
  EXPECT_NE(db->sql.find("FROM outbox_events"), std::string::npos);
}
