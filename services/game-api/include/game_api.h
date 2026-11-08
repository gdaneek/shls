#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include <optional>
#include <string>

#include "game_rules.h"

namespace mini_checkers {

// HTTP endpoints of the game. PostgreSQL is the source of truth, Redis is a
// cache-aside accelerator (`game:{id}`), RabbitMQ receives an event after
// every successful change.
class GameApi {
 public:
  GameApi(drogon::orm::DbClientPtr db, drogon::nosql::RedisClientPtr redis,
          int cache_ttl_seconds);

  void CreateSchema(const std::string& path);
  void RegisterRoutes();

 private:
  using Response = drogon::HttpResponsePtr;
  using Request = drogon::HttpRequestPtr;

  drogon::Task<Response> CreateGame(Request request);
  drogon::Task<Response> JoinGame(Request request, std::string game_id);
  drogon::Task<Response> GetGame(Request request, std::string game_id);
  drogon::Task<Response> MakeMove(Request request, std::string game_id);
  drogon::Task<Response> FinishGame(Request request, std::string game_id);

  drogon::Task<std::optional<Side>> FindPlayer(const std::string& game_id,
                                               const std::string& token);
  drogon::Task<> InvalidateCache(const std::string& game_id);

  drogon::orm::DbClientPtr db_;
  drogon::nosql::RedisClientPtr redis_;
  int cache_ttl_seconds_;
};

}  // namespace mini_checkers
