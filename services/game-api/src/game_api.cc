#include "game_api.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/orm/Exception.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace mini_checkers {
namespace {

using drogon::HttpStatusCode;
using drogon::Task;

constexpr std::array kSchema = {
    R"sql(
    CREATE TABLE IF NOT EXISTS games (
      id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
      status text NOT NULL CHECK (status IN ('waiting', 'active', 'finished')),
      board text NOT NULL CHECK (char_length(board) = 64),
      current_turn text NOT NULL CHECK (current_turn IN ('white', 'black')),
      winner text CHECK (winner IN ('white', 'black')),
      version integer NOT NULL DEFAULT 1,
      created_at timestamptz NOT NULL DEFAULT now(),
      updated_at timestamptz NOT NULL DEFAULT now()
    ))sql",
    R"sql(
    CREATE TABLE IF NOT EXISTS game_players (
      game_id uuid NOT NULL REFERENCES games (id) ON DELETE CASCADE,
      side text NOT NULL CHECK (side IN ('white', 'black')),
      token_hash text NOT NULL,
      PRIMARY KEY (game_id, side)
    ))sql",
    R"sql(
    CREATE TABLE IF NOT EXISTS moves (
      id bigserial PRIMARY KEY,
      game_id uuid NOT NULL REFERENCES games (id) ON DELETE CASCADE,
      move_no integer NOT NULL,
      player_side text NOT NULL CHECK (player_side IN ('white', 'black')),
      from_cell integer NOT NULL CHECK (from_cell BETWEEN 0 AND 63),
      to_cell integer NOT NULL CHECK (to_cell BETWEEN 0 AND 63),
      created_at timestamptz NOT NULL DEFAULT now(),
      UNIQUE (game_id, move_no)
    ))sql",
    R"sql(
    CREATE TABLE IF NOT EXISTS outbox_events (
      sequence bigserial PRIMARY KEY,
      event_id uuid NOT NULL UNIQUE DEFAULT gen_random_uuid(),
      game_id uuid NOT NULL,
      type text NOT NULL,
      request_id text NOT NULL,
      data jsonb NOT NULL DEFAULT '{}',
      created_at timestamptz NOT NULL DEFAULT now()
    ))sql",
    "ALTER TABLE games ADD COLUMN IF NOT EXISTS capture_from integer "
    "CHECK (capture_from BETWEEN 0 AND 63)",
    "ALTER TABLE games ADD COLUMN IF NOT EXISTS captured_cells jsonb NOT NULL DEFAULT '[]' "
    "CHECK (jsonb_typeof(captured_cells) = 'array')",
    "ALTER TABLE moves ADD COLUMN IF NOT EXISTS captured_cell integer "
    "CHECK (captured_cell BETWEEN 0 AND 63)",
    "ALTER TABLE moves ADD COLUMN IF NOT EXISTS promoted boolean NOT NULL DEFAULT false",
    // Existing games from the earlier one-step rules may have uncrowned men
    // on their promotion rank. Preserve those games, without touching the
    // opposing men initially placed on the same rank.
    "UPDATE games SET board = regexp_replace(substr(board, 1, 8), 'w', 'W', 'g') "
    "|| substr(board, 9, 48) || regexp_replace(substr(board, 57, 8), 'b', 'B', 'g'), "
    "version = version + 1, updated_at = now() "
    "WHERE substr(board, 1, 8) LIKE '%w%' OR substr(board, 57, 8) LIKE '%b%'",
};

constexpr char kSelectGame[] =
    "SELECT id::text, status, board, current_turn, winner, version, capture_from, captured_cells "
    "FROM games WHERE id = $1::uuid";

constexpr char kCreateGame[] = R"sql(
    WITH created AS (
      INSERT INTO games (status, board, current_turn)
      VALUES ('waiting', $1, 'white')
      RETURNING id), player AS (
      INSERT INTO game_players (game_id, side, token_hash)
      SELECT id, 'white', $2 FROM created RETURNING game_id)
    INSERT INTO outbox_events (game_id, type, request_id)
    SELECT game_id, 'game.created', $3 FROM player
    RETURNING game_id::text)sql";

constexpr char kJoinGame[] = R"sql(
    WITH joined AS (
      UPDATE games SET status = 'active', version = version + 1,
                       updated_at = now()
      WHERE id = $1::uuid AND status = 'waiting'
      RETURNING id), player AS (
      INSERT INTO game_players (game_id, side, token_hash)
      SELECT id, 'black', $2 FROM joined RETURNING game_id)
    INSERT INTO outbox_events (game_id, type, request_id)
    SELECT game_id, 'player.joined', $3 FROM player
    RETURNING game_id)sql";

// The version check makes the update atomic with respect to concurrent moves:
// only one of two requests based on the same version can succeed.
constexpr char kMakeMove[] = R"sql(
    WITH updated AS (
      UPDATE games SET board = $2, current_turn = $3, version = version + 1,
                       updated_at = now(), capture_from = NULLIF($9, -1),
                       captured_cells = $10::jsonb, status = $11, winner = NULLIF($12, '')
      WHERE id = $1::uuid AND version = $4 AND status = 'active'
      RETURNING id), moved AS (
      INSERT INTO moves (game_id, move_no, player_side, from_cell, to_cell, captured_cell, promoted)
      SELECT id, (SELECT count(*) + 1 FROM moves WHERE game_id = $1::uuid)::int,
             $5, $6, $7, NULLIF($14, -1), $15 FROM updated RETURNING game_id, move_no), event AS (
      INSERT INTO outbox_events (game_id, type, request_id, data)
      SELECT game_id, 'move.made', $8,
             $13::jsonb || jsonb_build_object('move_no', move_no, 'side', $5::text,
               'from', jsonb_build_array($6::int / 8, $6::int % 8),
               'to', jsonb_build_array($7::int / 8, $7::int % 8),
               'version', $4::int + 1)
      FROM moved), finished_event AS (
      INSERT INTO outbox_events (game_id, type, request_id, data)
      SELECT game_id, 'game.finished', $8,
             jsonb_build_object('winner', $12::text, 'version', $4::int + 1)
      FROM moved WHERE $11 = 'finished')
    SELECT move_no FROM moved)sql";

constexpr char kFinishGame[] = R"sql(
    WITH finished AS (
    UPDATE games SET status = 'finished', winner = NULLIF($2, ''),
                     version = version + 1, updated_at = now(), board = $5,
                     capture_from = NULL, captured_cells = '[]'
    WHERE id = $1::uuid AND version = $3 AND status <> 'finished'
    RETURNING id, version, winner), event AS (
      INSERT INTO outbox_events (game_id, type, request_id, data)
      SELECT id, 'game.finished', $4,
             jsonb_build_object('winner', winner, 'version', version)
      FROM finished)
    SELECT version FROM finished)sql";

struct GameState {
  std::string id;
  std::string status;
  std::string board;
  std::string current_turn;
  std::string winner;  // empty when there is no winner
  int version = 0;
  int capture_from = -1;
  std::vector<int> captured;
};

Json::Value ParseJson(const std::string& text) {
  Json::Value value;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  std::string errors;
  if (!reader->parse(text.data(), text.data() + text.size(), &value, &errors)) {
    throw std::runtime_error("invalid stored JSON");
  }
  return value;
}

Position ToPosition(const GameState& game) {
  return {game.board, ParseSide(game.current_turn).value(), game.capture_from, game.captured};
}

Json::Value CellJson(int index) {
  if (index < 0) return Json::Value();
  Json::Value cell(Json::arrayValue);
  cell.append(index / 8);
  cell.append(index % 8);
  return cell;
}

GameState ReadGameState(const drogon::orm::Row& row) {
  GameState game;
  game.id = row["id"].as<std::string>();
  game.status = row["status"].as<std::string>();
  game.board = row["board"].as<std::string>();
  game.current_turn = row["current_turn"].as<std::string>();
  if (!row["winner"].isNull()) game.winner = row["winner"].as<std::string>();
  game.version = row["version"].as<int>();
  if (!row["capture_from"].isNull()) game.capture_from = row["capture_from"].as<int>();
  for (const auto& index : ParseJson(row["captured_cells"].as<std::string>())) {
    game.captured.push_back(index.asInt());
  }
  return game;
}

Json::Value ToJson(const GameState& game) {
  Json::Value json;
  json["game_id"] = game.id;
  json["status"] = game.status;
  json["current_turn"] = game.current_turn;
  json["winner"] = game.winner.empty() ? Json::Value() : Json::Value(game.winner);
  json["version"] = game.version;
  Json::Value board(Json::arrayValue);
  for (int row = 0; row < kBoardSize; ++row) {
    board.append(game.board.substr(row * kBoardSize, kBoardSize));
  }
  json["board"] = board;
  json["rules_version"] = 2;
  json["capture_from"] = CellJson(game.capture_from);
  json["captured_cells"] = Json::Value(Json::arrayValue);
  for (int index : game.captured) json["captured_cells"].append(CellJson(index));
  json["legal_moves"] = Json::Value(Json::arrayValue);
  if (game.status == "active") {
    for (const auto& move : LegalMoves(ToPosition(game))) {
      Json::Value value;
      value["from"] = CellJson(CellIndex(move.from));
      value["to"] = CellJson(CellIndex(move.to));
      value["capture"] = CellJson(move.captured_cell);
      json["legal_moves"].append(value);
    }
  }
  return json;
}

std::string Serialize(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

drogon::HttpResponsePtr JsonResponse(HttpStatusCode code,
                                     const Json::Value& body) {
  auto response = drogon::HttpResponse::newHttpJsonResponse(body);
  response->setStatusCode(code);
  return response;
}

drogon::HttpResponsePtr ErrorResponse(HttpStatusCode code,
                                      std::string_view message) {
  Json::Value body;
  body["error"] = std::string(message);
  return JsonResponse(code, body);
}

drogon::HttpResponsePtr GameNotFound() {
  return ErrorResponse(drogon::k404NotFound, "game not found");
}

drogon::HttpResponsePtr ServiceUnavailable(const std::string& reason) {
  LOG_ERROR << "database error: " << reason;
  return ErrorResponse(drogon::k503ServiceUnavailable,
                       "storage is temporarily unavailable");
}

// Game ids are UUIDs; they are normalized to lower case so that the cache key
// is the same for every spelling of the id.
std::optional<std::string> NormalizeGameId(std::string id) {
  if (id.size() != 36) return std::nullopt;
  for (std::size_t i = 0; i < id.size(); ++i) {
    const bool dash_position = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash_position ? id[i] != '-'
                      : !std::isxdigit(static_cast<unsigned char>(id[i]))) {
      return std::nullopt;
    }
    id[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(id[i])));
  }
  return id;
}

std::string CacheKey(const std::string& game_id) { return "game:" + game_id; }

std::string NewPlayerToken() {
  std::array<unsigned char, 32> bytes{};
  if (!drogon::utils::secureRandomBytes(bytes.data(), bytes.size())) {
    throw std::runtime_error("secure random generator is unavailable");
  }
  static constexpr char kHex[] = "0123456789abcdef";
  std::string token;
  for (unsigned char byte : bytes) {
    token += kHex[byte >> 4];
    token += kHex[byte & 0x0f];
  }
  return token;
}

std::string HashToken(const std::string& token) {
  return drogon::utils::getSha256(token);
}

std::string RequestId(const drogon::HttpRequestPtr& request) {
  const std::string& header = request->getHeader("x-request-id");
  const bool valid = !header.empty() && header.size() <= 64 &&
                     std::all_of(header.begin(), header.end(), [](char c) {
                       return std::isalnum(static_cast<unsigned char>(c)) ||
                              c == '-';
                     });
  return valid ? header : drogon::utils::getUuid();
}

std::optional<Cell> ParseCell(const Json::Value& value) {
  if (!value.isArray() || value.size() != 2 || !value[0].isInt() ||
      !value[1].isInt()) {
    return std::nullopt;
  }
  return Cell{value[0].asInt(), value[1].asInt()};
}

Json::Value PlayerJoined(const std::string& game_id, Side side,
                         const std::string& token) {
  Json::Value body;
  body["game_id"] = game_id;
  body["side"] = std::string(SideName(side));
  body["player_token"] = token;
  return body;
}

}  // namespace

GameApi::GameApi(drogon::orm::DbClientPtr db,
                 drogon::nosql::RedisClientPtr redis,
                 int cache_ttl_seconds)
    : db_(std::move(db)),
      redis_(std::move(redis)),
      cache_ttl_seconds_(cache_ttl_seconds) {}

void GameApi::CreateSchema() {
  for (const char* statement : kSchema) db_->execSqlSync(statement);
}

void GameApi::RegisterRoutes() {
  auto& app = drogon::app();
  app.registerHandler(
      "/games",
      [this](Request request) -> Task<Response> {
        co_return co_await CreateGame(std::move(request));
      },
      {drogon::Post});
  app.registerHandler(
      "/games/{id}",
      [this](Request request, std::string id) -> Task<Response> {
        co_return co_await GetGame(std::move(request), std::move(id));
      },
      {drogon::Get});
  app.registerHandler(
      "/games/{id}/join",
      [this](Request request, std::string id) -> Task<Response> {
        co_return co_await JoinGame(std::move(request), std::move(id));
      },
      {drogon::Post});
  app.registerHandler(
      "/games/{id}/moves",
      [this](Request request, std::string id) -> Task<Response> {
        co_return co_await MakeMove(std::move(request), std::move(id));
      },
      {drogon::Post});
  app.registerHandler(
      "/games/{id}/finish",
      [this](Request request, std::string id) -> Task<Response> {
        co_return co_await FinishGame(std::move(request), std::move(id));
      },
      {drogon::Post});
}

Task<GameApi::Response> GameApi::CreateGame(Request request) {
  const std::string request_id = RequestId(request);
  const std::string token = NewPlayerToken();
  std::string game_id;
  try {
    auto result =
        co_await db_->execSqlCoro(kCreateGame, InitialBoard(), HashToken(token),
                                 request_id);
    game_id = result[0]["game_id"].as<std::string>();
  } catch (const drogon::orm::DrogonDbException& error) {
    co_return ServiceUnavailable(error.base().what());
  }
  co_await InvalidateCache(game_id);
  LOG_INFO << "game created game_id=" << game_id
           << " request_id=" << request_id;
  co_return JsonResponse(drogon::k201Created,
                         PlayerJoined(game_id, Side::kWhite, token));
}

Task<GameApi::Response> GameApi::JoinGame(Request request,
                                          std::string raw_game_id) {
  const auto game_id = NormalizeGameId(std::move(raw_game_id));
  if (!game_id) co_return GameNotFound();
  const std::string request_id = RequestId(request);
  const std::string token = NewPlayerToken();
  try {
    auto joined =
        co_await db_->execSqlCoro(kJoinGame, *game_id, HashToken(token), request_id);
    if (joined.empty()) {
      auto existing = co_await db_->execSqlCoro(kSelectGame, *game_id);
      if (existing.empty()) co_return GameNotFound();
      co_return ErrorResponse(drogon::k409Conflict,
                              "the game is not waiting for a second player");
    }
  } catch (const drogon::orm::DrogonDbException& error) {
    co_return ServiceUnavailable(error.base().what());
  }
  co_await InvalidateCache(*game_id);
  LOG_INFO << "player joined game_id=" << *game_id
           << " request_id=" << request_id;
  co_return JsonResponse(drogon::k200OK,
                         PlayerJoined(*game_id, Side::kBlack, token));
}

Task<GameApi::Response> GameApi::GetGame(Request request,
                                         std::string raw_game_id) {
  const auto game_id = NormalizeGameId(std::move(raw_game_id));
  if (!game_id) co_return GameNotFound();
  const std::string key = CacheKey(*game_id);

  std::string cache_status = "MISS";
  if (!redis_) {
    cache_status = "BYPASS";
  } else {
    try {
      auto cached = co_await redis_->execCommandCoro("GET %s", key.c_str());
      // Entries written before this rule/schema upgrade have no continuation
      // context. Treat them as a miss; keep existing cache keys/TTL unchanged.
      if (!cached.isNil() && ParseJson(cached.asString()).get("rules_version", 0).asInt() == 2) {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        response->setBody(cached.asString());
        response->addHeader("X-Cache", "HIT");
        LOG_INFO << "game read game_id=" << *game_id << " cache=HIT";
        co_return response;
      }
    } catch (const std::exception& error) {
      LOG_WARN << "Redis read failed, falling back to PostgreSQL: "
               << error.what();
      cache_status = "BYPASS";
    }
  }

  GameState game;
  try {
    auto result = co_await db_->execSqlCoro(kSelectGame, *game_id);
    if (result.empty()) co_return GameNotFound();
    game = ReadGameState(result[0]);
  } catch (const drogon::orm::DrogonDbException& error) {
    co_return ServiceUnavailable(error.base().what());
  }

  const Json::Value body = ToJson(game);
  if (cache_status == "MISS") {
    try {
      co_await redis_->execCommandCoro("SET %s %s EX %d", key.c_str(),
                                       Serialize(body).c_str(),
                                       cache_ttl_seconds_);
    } catch (const std::exception& error) {
      LOG_WARN << "Redis write failed: " << error.what();
    }
  }
  LOG_INFO << "game read game_id=" << *game_id << " cache=" << cache_status;
  auto response = JsonResponse(drogon::k200OK, body);
  response->addHeader("X-Cache", cache_status);
  co_return response;
}

Task<GameApi::Response> GameApi::MakeMove(Request request,
                                          std::string raw_game_id) {
  const auto game_id = NormalizeGameId(std::move(raw_game_id));
  if (!game_id) co_return GameNotFound();
  const std::string token = request->getHeader("x-player-token");
  if (token.empty()) {
    co_return ErrorResponse(drogon::k401Unauthorized,
                            "the X-Player-Token header is required");
  }
  const auto json = request->getJsonObject();
  const auto from = json ? ParseCell((*json)["from"]) : std::nullopt;
  const auto to = json ? ParseCell((*json)["to"]) : std::nullopt;
  if (!from || !to) {
    co_return ErrorResponse(
        drogon::k400BadRequest,
        R"(expected a JSON body like {"from": [5, 0], "to": [4, 1]})");
  }
  const std::string request_id = RequestId(request);

  GameState game;
  std::string mover;
  int move_no = 0;
  try {
    auto result = co_await db_->execSqlCoro(kSelectGame, *game_id);
    if (result.empty()) co_return GameNotFound();
    game = ReadGameState(result[0]);

    const auto side = co_await FindPlayer(*game_id, token);
    if (!side) {
      co_return ErrorResponse(drogon::k403Forbidden, "invalid player token");
    }
    if (game.status != "active") {
      co_return ErrorResponse(drogon::k409Conflict, "the game is not active");
    }
    if (game.current_turn != SideName(*side)) {
      co_return ErrorResponse(drogon::k409Conflict, "it is not your turn");
    }
    if (const auto error = ValidateMove(ToPosition(game), *from, *to)) {
      co_return ErrorResponse(drogon::k422UnprocessableEntity, *error);
    }

    mover = SideName(*side);
    const auto outcome = ApplyMove(ToPosition(game), *from, *to);
    const auto& position = outcome.position;
    const std::string next_turn(SideName(position.turn));
    const std::string status = outcome.winner ? "finished" : "active";
    const std::string winner = outcome.winner ? std::string(SideName(*outcome.winner)) : "";
    Json::Value captured(Json::arrayValue);
    for (int index : position.captured) captured.append(index);
    Json::Value data;
    data["captured_cell"] = CellJson(outcome.captured_cell);
    data["promoted"] = outcome.promoted;
    data["capture_from"] = CellJson(position.capture_from);
    auto moved = co_await db_->execSqlCoro(
        kMakeMove, *game_id, position.board, next_turn, game.version,
        mover, CellIndex(*from), CellIndex(*to), request_id,
        position.capture_from, Serialize(captured), status, winner,
        Serialize(data), outcome.captured_cell, outcome.promoted);
    if (moved.empty()) {
      co_return ErrorResponse(drogon::k409Conflict,
                              "the game was changed by another request, "
                              "read it again and retry");
    }
    move_no = moved[0]["move_no"].as<int>();
    game.board = position.board;
    game.current_turn = next_turn;
    game.capture_from = position.capture_from;
    game.captured = position.captured;
    game.status = status;
    game.winner = winner;
    game.version += 1;
  } catch (const drogon::orm::DrogonDbException& error) {
    co_return ServiceUnavailable(error.base().what());
  }

  co_await InvalidateCache(*game_id);
  LOG_INFO << "move made game_id=" << *game_id << " move_no=" << move_no
           << " request_id=" << request_id;
  co_return JsonResponse(drogon::k200OK, ToJson(game));
}

Task<GameApi::Response> GameApi::FinishGame(Request request,
                                            std::string raw_game_id) {
  const auto game_id = NormalizeGameId(std::move(raw_game_id));
  if (!game_id) co_return GameNotFound();
  const std::string token = request->getHeader("x-player-token");
  if (token.empty()) {
    co_return ErrorResponse(drogon::k401Unauthorized,
                            "the X-Player-Token header is required");
  }
  const std::string request_id = RequestId(request);

  GameState game;
  try {
    auto result = co_await db_->execSqlCoro(kSelectGame, *game_id);
    if (result.empty()) co_return GameNotFound();
    game = ReadGameState(result[0]);

    const auto side = co_await FindPlayer(*game_id, token);
    if (!side) {
      co_return ErrorResponse(drogon::k403Forbidden, "invalid player token");
    }
    if (game.status == "finished") {
      co_return ErrorResponse(drogon::k409Conflict,
                              "the game is already finished");
    }
    // Finishing an active game means resigning: the opponent wins.
    // A game that nobody has joined yet is simply closed.
    const std::string winner =
        game.status == "active" ? std::string(SideName(Opponent(*side))) : "";
    auto position = ToPosition(game);
    ClearCapturedPieces(position);
    auto finished = co_await db_->execSqlCoro(kFinishGame, *game_id, winner,
                                              game.version, request_id, position.board);
    if (finished.empty()) {
      co_return ErrorResponse(drogon::k409Conflict,
                              "the game was changed by another request, "
                              "read it again and retry");
    }
    game.status = "finished";
    game.winner = winner;
    game.board = position.board;
    game.capture_from = -1;
    game.captured.clear();
    game.version = finished[0]["version"].as<int>();
  } catch (const drogon::orm::DrogonDbException& error) {
    co_return ServiceUnavailable(error.base().what());
  }

  co_await InvalidateCache(*game_id);
  LOG_INFO << "game finished game_id=" << *game_id
           << " request_id=" << request_id;
  co_return JsonResponse(drogon::k200OK, ToJson(game));
}

Task<std::optional<Side>> GameApi::FindPlayer(const std::string& game_id,
                                              const std::string& token) {
  auto result = co_await db_->execSqlCoro(
      "SELECT side FROM game_players WHERE game_id = $1::uuid AND "
      "token_hash = $2",
      game_id, HashToken(token));
  if (result.empty()) co_return std::nullopt;
  co_return ParseSide(result[0]["side"].as<std::string>());
}

Task<> GameApi::InvalidateCache(const std::string& game_id) {
  if (!redis_) co_return;
  try {
    co_await redis_->execCommandCoro("DEL %s", CacheKey(game_id).c_str());
  } catch (const std::exception& error) {
    // The entry expires by TTL anyway; the write itself has succeeded.
    LOG_WARN << "Redis invalidation failed for game_id=" << game_id << ": "
             << error.what();
  }
}

}  // namespace mini_checkers
