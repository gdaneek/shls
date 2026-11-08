# Mini Checkers

Mini Checkers — небольшой онлайн-сервис для игры в упрощённые шашки вдвоём. Реализованы создание игры, подключение второго игрока, поочерёдные ходы и сохранение состояния партии. Основная цель — не полноценный игровой движок, а демонстрация взаимодействия микросервисов и практик безопасной разработки.

Два сервиса на C++20: REST API на Drogon и отдельный обработчик игровых событий. PostgreSQL хранит состояние игр, Redis кеширует данные, RabbitMQ передаёт события. Запуск — Docker Compose; настроены сегментация и firewall, перезапуск сервисов, отложенная доставка событий и ежедневные резервные копии БД.

## Запуск

Нужны Docker Engine и Docker Compose v2. Из корня репозитория:

```bash
cp .env.example .env    # если .env ещё нет; задайте свои пароли
infrastructure/firewall/firewall.sh apply    # Linux, нужен sudo для nft
docker compose up --build --wait --wait-timeout 180
curl http://127.0.0.1:8000/hello    # {"message":"hello-world"}
bash scripts/test-startup.sh
bash scripts/test-integration.sh    # нужен jq; перезапускает event-worker и redis
bash scripts/test-network.sh        # сегментация и firewall
```

Наружу публикуется только API на `127.0.0.1:${API_PORT:-8000}`. Остановка — `docker compose down`, удаление данных вместе с томами — `docker compose down -v`.

## Сеть и firewall

| Сегмент | Подсеть | Мост | Сервисы |
|---|---|---|---|
| `public` | `10.10.1.0/24` | `mc-public` | game-api `.2` (порт 8000 опубликован на `127.0.0.1`) |
| `data` (internal) | `10.10.2.0/24` | `mc-data` | game-api `.2`, postgres `.3`, redis `.4` |
| `messaging` (internal) | `10.10.3.0/24` | `mc-msg` | game-api `.2`, rabbitmq `.3`, event-worker `.4` |

Между сегментами маршрута нет. Внутри `data` и `messaging` трафик фильтрует таблица nftables `bridge mini_checkers` ([`infrastructure/firewall/mini-checkers.nft`](infrastructure/firewall/mini-checkers.nft)): разрешены только game-api → postgres:5432, game-api → redis:6379, game-api → rabbitmq:5672 и event-worker → rabbitmq:5672, остальное логируется (`mc-fw-drop:`) и отбрасывается. Управление: `infrastructure/firewall/firewall.sh apply|rollback|status`. Правила не сохраняются после перезагрузки хоста — их нужно применить заново, лучше до `docker compose up`.

## API

| Метод и путь | Назначение |
|---|---|
| `POST /games` | Создать игру; в ответе `game_id` и токен белых |
| `POST /games/{id}/join` | Присоединиться вторым игроком; в ответе токен чёрных |
| `GET /games/{id}` | Состояние игры; заголовок `X-Cache: HIT`/`MISS`/`BYPASS` |
| `POST /games/{id}/moves` | Ход `{"from": [5, 0], "to": [4, 1]}` (строка, столбец) |
| `POST /games/{id}/finish` | Завершить игру (сдаться); побеждает соперник |

Ходы и завершение требуют заголовок `X-Player-Token`. Правила упрощены: шашка ходит на одну клетку по диагонали вперёд, белые начинают. После каждого изменения Game API сбрасывает кэш Redis и публикует событие (`game.created`, `player.joined`, `move.made`, `game.finished`) в очередь RabbitMQ `game.events`, которую читает Event Worker:

```bash
docker compose logs -f event-worker
```

## Резервные копии

```bash
scripts/backup.sh                      # создать архив БД в backups/
scripts/restore.sh backups/<имя>.dump   # восстановить в новую тестовую БД
scripts/backup-timer.sh install        # ежедневный бэкап в 02:00, systemd user timer
scripts/backup-timer.sh remove         # отключить расписание
```

Архивы хранятся 7 дней. Восстановление не перезаписывает рабочую БД.
