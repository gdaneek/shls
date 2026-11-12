# Структура исходников

## Где искать код

```text
frontend/               HTML, CSS, JavaScript, Nginx и его Dockerfile
shls/                   заголовки C++: API, правила, publisher
src/game-api/           HTTP-приложение, правила и публикация outbox
src/event-worker/       отдельный consumer
infra/postgres/         схема, репликация, promotion и rejoin
infra/rabbitmq/         образ, настройка и обновление кластера
infra/firewall/         nftables и управление таблицей
infra/backups/          backup/restore, установка systemd unit/timer
tests/integration/      интеграционные и браузерные проверки
third-party/            настройки получения C++ зависимостей
docker-compose.yml      сервисы, сети, тома и конфигурация
Dockerfile              общая многостадийная сборка C++ сервисов
build/                  локальные CMake и документационные артефакты
```

Заголовки включаются как `shls/game_rules.h` и т. п.; компилятору передаётся
корень проекта. API и worker — разные executable и разные контейнеры, хотя
используют общий многостадийный Dockerfile. `frontend/` собирается отдельно.

Локальные результаты сборки размещаются в `build/`; Sphinx-сайт и его схемы —
в `build/docs/html/`. Именованные тома Docker и `backups/` содержат рабочие
данные, а не артефакты сборки. Docker хранит образы и BuildKit-кеш отдельно
от файлов рабочего каталога.

## Основные зависимости

- C++20 и CMake; HTTP, PostgreSQL и Redis-клиенты предоставляет Drogon.
- `rabbitmq-c` — AMQP-клиент API и worker.
- PostgreSQL, Redis и RabbitMQ запускаются в отдельных контейнерах.

CMake загружает закреплённые версии Drogon и rabbitmq-c через FetchContent;
при первой конфигурации нужны исходники этих зависимостей и их системные
библиотеки. В runtime-образ не копируется компилятор.

`src/game-api/main.cc` читает схему из `POSTGRES_SCHEMA_FILE`; Docker задаёт
файл внутри образа. При локальном запуске default —
`infra/postgres/schema.sql` относительно текущего рабочего каталога.
