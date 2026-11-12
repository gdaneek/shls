# Интеграционные проверки

Python 3 + стандартный `unittest`, Docker Compose и запущенный локальный стенд.
Из корня проекта:

```bash
python3 -B -m unittest discover -s tests/integration -p 'test_*.py' -v
python3 -B tests/integration/test_captures.py -v
```

Тесты создают новые партии, проверяют БД/кеш/события, схему, правила игры,
сетевую матрицу, backup/restore и SLO. Схема и restore проверяются в отдельных
временных БД; архивы и ротация — в своём временном каталоге. Остановки сервисов
и переключения по умолчанию пропускаются. Проверка опубликованного порта:

```bash
CHECKERS_HOST_TEST=1 python3 -B tests/integration/test_startup.py Startup.test_frontend_from_host -v
```

Отказные проверки запускайте отдельно, без параллельной игры:

```bash
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_failure.py -v
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_postgres_failover.py -v
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_rabbitmq_failover.py -v
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_firewall_failure.py -v
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_captures.py Captures.test_chain_survives_api_restart -v
```

Это полноценные Python-тесты, прежних тестовых `.sh` и обёрток больше нет.
Они останавливают сервисы, меняют роли PostgreSQL и `.env`, создают бэкапы.
После прерванного PostgreSQL failover сначала проверьте роли и выполните
rejoin; старый primary нельзя запускать вслепую. Firewall требует sudo/nft.
В остальных отказных сценариях `finally` возвращает остановленные контейнеры
и временные правила при ошибке, Ctrl-C и SIGTERM; SIGKILL не позволяет откат.
При отключённом проектном firewall сетевой тест принимает `FIREWALL=off`.

Браузерный сценарий (Firefox и geckodriver):

```bash
python3 -B tests/integration/test_ui.py --docker-bridge
```

`--http-only` выполняет только HTTP-проверки. Docker-мост проверяет реальные
frontend/API, но не опубликованный порт хоста. Скриншоты — `build/test-artifacts/`.

Команды обслуживания находятся в `infra/backups/`, `infra/postgres/` и
`infra/rabbitmq/`. После обновления существующей установки переустановите
таймер командой `infra/backups/backup-timer.sh install`: путь backup изменился.

## Соответствие прежним shell-проверкам

| Прежняя команда | Python-тест |
|---|---|
| `test-startup.sh` | `test_startup.py`: health, hello, БД/кеш/quorum, опубликованные порты |
| `test-integration.sh` | `test_flows.py`: запись/чтение, cache-aside, авторизация, гонка и события; остановки worker/Redis — `test_failure.py` |
| `test-schema.sh` | `test_schema.py`: пустая БД, повторное применение, upgrade старой схемы |
| `test-network.sh` | `test_network.py`: все прежние TCP-пары, DROP-счётчик, `FIREWALL=off` |
| `test-backup.sh` | `test_backup.py`: данные, права, запрет перезаписи и ротация |
| `test-slo.sh` | `test_slo.py`: 100 чтений, p95, пять событий за ≤15 с |
| `test-failure.sh` | `test_failure.py`: авторестарт, деградация, persistent-событие, quorum loss, полный outage + outbox |
| `test-firewall-failure.sh` | `test_firewall_failure.py`: ошибка SQL-связи и откат без изменения основной политики |
| `test-postgres-failover.sh` | `test_postgres_failover.py`: SIGKILL, promotion, чтение/запись, кеш, rejoin и backup |
| `test-rabbitmq-failover.sh` | `test_rabbitmq_failover.py`: отказ лидера, backlog, reconnect consumer, проверка неизменности клиентов и догоняния реплик |

`test_captures.py` сохраняет все десять игровых кейсов, `test_ui.py` — HTTP,
две вкладки, polling, spectator, взятия/дамки и восстановление после reload.
`test_harness.py` проверяет аварийный cleanup на моках без остановки стенда.
