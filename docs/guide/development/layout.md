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
tests/game-api/         C++ unit-тесты правил и публичных интерфейсов
tests/CMakeLists.txt    сборка GoogleTest и регистрация в CTest
tests/integration/      интеграционные и браузерные проверки
docs/guide/             исходники Sphinx-сайта и Graphviz-схем
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

## Сборка и проверки

После установки системных библиотек из `third-party/README.md`, из корня
репозитория:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --no-tests=error
```

C++-тесты работают без Compose через публичные заголовки `shls/`: проверяют
правила, загрузку схемы, регистрацию маршрутов и publisher при ошибке БД.
Dockerfile также запускает их при сборке сервисов. В GitHub Actions workflow
`Tests` выполняет CTest и публикует HTML-отчёт `coverage`; для локального
сбора покрытия нужен GCC и параметр CMake `-DENABLE_COVERAGE=ON`.

Интеграционные проверки требуют запущенного Compose-стенда:

```bash
python3 -B -m unittest discover -s tests/integration -p 'test_*.py' -v
python3 -B tests/integration/test_ui.py --docker-bridge
```

Первая команда запускает `unittest`, включая backup/restore и SLO;
отказы по умолчанию пропускаются. Вторая отдельно запускает настоящий Firefox
через geckodriver. Условия запуска и соответствие прежним shell-сценариям —
в `tests/integration/README.md`. Общие функции находятся в `support.py`,
а команды обслуживания — в `infra/`. Корневой `scripts/` больше нет.
Python-интеграция и браузер не запускаются текущим workflow `Tests`.
