# Интеграционные проверки

Python 3 + стандартный `unittest`, Docker Compose и запущенный локальный стенд.
Из корня проекта:

```bash
python3 -B -m unittest discover -s tests/integration -p 'test_*.py' -v
python3 -B tests/integration/test_captures.py -v
```

Тесты создают новые партии, проверяют БД/кеш/события, схему, правила игры и
сетевую матрицу. Схема проверяется в отдельной временной БД. Остановки сервисов
и переключения по умолчанию пропускаются. Проверка опубликованного порта:

```bash
CHECKERS_HOST_TEST=1 python3 -B tests/integration/test_startup.py Startup.test_frontend_from_host -v
```

Отказные проверки запускайте отдельно, без параллельной игры:

```bash
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_flows.py -v
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_scenarios.py FaultScenarios.test_postgres_failover -v
```

Они останавливают сервисы, меняют роли PostgreSQL и `.env`, создают бэкапы.
После прерванного PostgreSQL failover сначала проверьте роли и выполните
rejoin; старый primary нельзя запускать вслепую. Firewall требует sudo/nft.
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
