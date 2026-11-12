# Интеграционные проверки

Нужны Python 3.9+ (стандартная библиотека), Docker Compose и запущенный
локальный стенд со штатным firewall. Из корня проекта:

```bash
python3 -B -m unittest discover -s tests/integration -p 'test_*.py' -v
python3 -B tests/integration/test_captures.py -v
```

Тесты создают новые партии, проверяют БД/кеш/события, схему, правила игры,
сетевую матрицу, backup/restore и SLO. Схема и restore проверяются в отдельных
временных БД; архивы и ротация — в своём временном каталоге. Созданные партии
остаются в рабочей БД. Остановки сервисов и переключения по умолчанию
пропускаются. HTTP-запросы обычных проверок идут внутри контейнеров;
проверка опубликованного порта включается отдельно:

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
Контекстные менеджеры `stopped`/`crashed` запускают остановленные ими
контейнеры и восстанавливают restart policy при ошибке, Ctrl-C и SIGTERM;
firewall-тест удаляет свою временную таблицу. Проверка авторестарта процессов
полагается на Docker restart policy. Ошибка запуска контейнера или SIGKILL
тестового процесса могут потребовать ручного восстановления.
При отключённом проектном firewall сетевой тест принимает `FIREWALL=off`;
эта переменная меняет ожидания теста, но сама правила не отключает.
Проверка DROP-счётчика выполняется при доступном `sudo -n nft`, иначе
выводится `INFO`; TCP-пробы продолжают проверяться.

Браузерный сценарий (Firefox и geckodriver):

```bash
python3 -B tests/integration/test_ui.py --docker-bridge
```

Браузерный сценарий запускается отдельно и не выполняется через `unittest discover`.
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

## Последняя проверка переноса

13.11.2026: общий прогон — **25 PASS, 11 SKIP** (36 тестов), полный Firefox
через Docker-мост — PASS. Пропущены десять отказных проверок и запрос через
порт хоста. Новые Python-сценарии отказов и режим `FIREWALL=off` в этом прогоне
не проверялись; прежние результаты shell-сценариев их не подтверждают.
