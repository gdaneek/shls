/**
 * @file
 * @brief HTTP-обработчики игры с хранением в PostgreSQL и кешем Redis.
 * @details Объявляет регистрацию маршрутов и асинхронные операции над партиями.
 */
#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include <optional>
#include <string>

#include "shls/game_rules.h"

namespace mini_checkers {

/**
 * @brief Обрабатывает игровые запросы, чтение кеша и транзакционную запись событий в outbox.
 * @details PostgreSQL хранит состояние и события одной атомарной операции.
 * После записи обработчик инвалидирует кеш; публикацию в RabbitMQ выполняет
 * EventPublisher независимо от HTTP-ответа.
 * @note Объект должен существовать всё время обслуживания зарегистрированных
 * маршрутов: обработчики Drogon сохраняют указатель this.
 */
class GameApi {
 public:
  /**
   * @brief Задаёт клиенты хранилищ и время жизни кеша в секундах.
   * @param[in] db Ненулевой клиент PostgreSQL для партий и outbox.
   * @param[in] redis Клиент Redis; nullptr отключает кеширование.
   * @param[in] cache_ttl_seconds Положительное время жизни записи кеша.
   * @note Конструктор не создаёт схему и не регистрирует маршруты.
   */
  GameApi(drogon::orm::DbClientPtr db, drogon::nosql::RedisClientPtr redis,
          int cache_ttl_seconds);

  /**
   * @brief Применяет файл SQL-схемы; передаёт ошибки чтения и БД вызывающему коду.
   * @param[in] path Путь к непустому SQL-файлу; относительный путь считается от рабочего каталога.
   * @throws std::runtime_error Если файл не открывается, не читается или пуст.
   * @throws drogon::orm::DrogonDbException Если выполнение SQL завершилось ошибкой.
   * @note Выполняет блокирующий запрос; вызывается при запуске до обслуживания HTTP.
   */
  void CreateSchema(const std::string& path);

  /**
   * @brief Регистрирует маршруты создания, присоединения, чтения, хода и завершения партии.
   * @note Вызывается один раз до запуска приложения Drogon. Внутренние пути
   * начинаются с /games; внешний префикс /api добавляется обратным прокси.
   */
  void RegisterRoutes();

 private:
  /// @brief Указатель с совместным владением на HTTP-ответ обработчика.
  using Response = drogon::HttpResponsePtr;
  /// @brief Указатель с совместным владением на входящий HTTP-запрос.
  using Request = drogon::HttpRequestPtr;

  /**
   * @brief Создаёт ожидающую партию и выдаёт токен белых.
   * @param[in] request HTTP-запрос; X-Request-Id используется для связи с событием.
   * @return Задача с JSON-ответом: 201 с game_id, side и player_token;
   * 503 при ошибке PostgreSQL.
   * @throws std::runtime_error Если не удалось сгенерировать секретный токен.
   * @note Партия, хеш токена и событие game.created записываются атомарно.
   * Исходный токен возвращается клиенту, в БД сохраняется только его хеш.
   */
  drogon::Task<Response> CreateGame(Request request);

  /**
   * @brief Присоединяет игрока к ожидающей партии и выдаёт токен чёрных.
   * @param[in] request HTTP-запрос с необязательным X-Request-Id.
   * @param[in] game_id UUID партии из маршрута; нормализуется к нижнему регистру.
   * @return Задача с JSON-ответом: 200 с реквизитами игрока, 404 для неверного
   * или отсутствующего UUID, 409 для не ожидающей партии, 503 при ошибке БД.
   * @throws std::runtime_error Если не удалось сгенерировать секретный токен.
   * @note Условное обновление статуса позволяет присоединиться только одному сопернику.
   */
  drogon::Task<Response> JoinGame(Request request, std::string game_id);

  /**
   * @brief Читает партию из кеша, обращаясь к PostgreSQL при промахе или ошибке.
   * @param[in] request HTTP-запрос; его содержимое этим обработчиком не используется.
   * @param[in] game_id UUID партии из маршрута.
   * @return Задача с JSON-ответом: 200 с состоянием партии, 404 для неверного
   * или отсутствующего UUID, 503 при ошибке чтения БД.
   * @note Чтение не требует токена. Успешный ответ содержит X-Cache:
   * HIT, MISS или BYPASS. Ошибка Redis не препятствует чтению из PostgreSQL.
   */
  drogon::Task<Response> GetGame(Request request, std::string game_id);

  /**
   * @brief Проверяет права и сохраняет допустимый ход с оптимистической проверкой версии.
   * @param[in] request Запрос с X-Player-Token и JSON-полями from/to — парами [row, col].
   * @param[in] game_id UUID партии из маршрута.
   * @return Задача с JSON-ответом: 200 с новой позицией; 400 при неверном теле,
   * 401/403 при отсутствии или несовпадении токена, 404 для неизвестной партии,
   * 409 при конфликте состояния или очереди, 422 для недопустимого хода,
   * 503 при ошибке БД.
   * @note Запрос выполняет одно звено боя. Ход, версия партии и события
   * сохраняются атомарно; конкурентное изменение версии даёт 409.
   */
  drogon::Task<Response> MakeMove(Request request, std::string game_id);

  /**
   * @brief Проверяет права на сдачу или закрытие ожидающей партии и сохраняет результат.
   * @param[in] request Запрос с X-Player-Token и необязательным X-Request-Id.
   * @param[in] game_id UUID партии из маршрута.
   * @return Задача с JSON-ответом: 200 с завершённой партией, 401/403 при
   * ошибке авторизации, 404 для неверного или отсутствующего UUID,
   * 409 при повторном завершении или конфликте версии, 503 при ошибке БД.
   * @note В активной партии победа присуждается сопернику сдавшегося игрока;
   * ожидающая партия закрывается без победителя. Остаток цепочки боя очищается.
   */
  drogon::Task<Response> FinishGame(Request request, std::string game_id);

  /**
   * @brief Определяет сторону игрока по секретному токену; при отсутствии возвращает nullopt.
   * @param[in] game_id Проверенный UUID партии.
   * @param[in] token Исходный секретный токен из заголовка запроса.
   * @return Задача со стороной игрока либо std::nullopt, если соответствие не найдено.
   * @throws drogon::orm::DrogonDbException Если запрос к PostgreSQL не выполнен.
   * @note Сопоставление выполняется по SHA-256 токена, а не по открытому секрету.
   */
  drogon::Task<std::optional<Side>> FindPlayer(const std::string& game_id,
                                               const std::string& token);

  /**
   * @brief Удаляет партию из кеша, допуская недоступность Redis.
   * @param[in] game_id Нормализованный UUID партии для ключа game:{id}.
   * @return Задача без значения, завершённая после попытки удаления.
   * @note Без клиента Redis сразу завершается. Ошибки удаления журналируются;
   * запись в PostgreSQL не откатывается, старая запись кеша ограничена TTL.
   */
  drogon::Task<> InvalidateCache(const std::string& game_id);

  drogon::orm::DbClientPtr db_; ///< Основное хранилище партий и событий outbox.
  drogon::nosql::RedisClientPtr redis_; ///< Необязательный клиент кеша.
  int cache_ttl_seconds_; ///< Время жизни записи кеша в секундах.
};

}  // namespace mini_checkers
