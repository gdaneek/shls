# Интеграционные проверки

Проверки написаны на Python `unittest` из стандартной библиотеки. Нужны Python 3,
Docker Compose и запущенный локальный стенд; Python-зависимости не устанавливаются.
Запускать из корня репозитория:

```bash
python3 -B -m unittest discover -s scripts/integration -p 'test_*.py' -v
```

Запуск проверяет работоспособность контейнеров, PostgreSQL, Redis, RabbitMQ,
схему, сетевые правила, интеграционные потоки и правила игры. Тесты создают
**новые тестовые партии** в рабочей БД; схема проверяется в отдельной временной
БД, которая затем удаляется. Сетевые пробы увеличивают счётчики firewall.
Проверка портов из контейнеров не подтверждает доступность сайта с хоста.

Отдельно проверить опубликованный порт с ноутбука, без HTTP-прокси:

```bash
CHECKERS_HOST_TEST=1 python3 -B scripts/integration/test_startup.py Startup.test_frontend_from_host -v
```

## Опасные сценарии — только на демонстрационном стенде

Обычный запуск **пропускает** остановку Redis/worker, рестарт API и проверки
отказов. Не выполняйте их во время игры. Подготовьте бэкап и проверьте
состояние стенда до и после теста. Выбирайте нужный тест явно:

```bash
CHECKERS_DISRUPTIVE=1 python3 -B scripts/integration/test_flows.py -v
CHECKERS_DISRUPTIVE=1 python3 -B scripts/integration/test_captures.py Captures.test_chain_survives_api_restart -v
CHECKERS_DISRUPTIVE=1 python3 -B scripts/integration/test_scenarios.py FaultScenarios.test_postgres_failover -v
```

`test_flows.py` временно останавливает Redis и worker и запускает их в `finally`.
`test_scenarios.py` запускает существующие shell-сценарии firewall, отказов,
backup/restore и SLO как отдельные тесты. Они по-прежнему могут останавливать
сервисы, менять роли PostgreSQL и `.env`, создавать партии и архивы. Проверка
бэкапа запускает политику хранения файлов; внимательно изучите скрипт перед
запуском. После прерванного PostgreSQL failover старый primary нельзя запускать
вслепую: сначала проверьте роли и выполните процедуру восстановления. Для этих
shell-сценариев также нужны `jq` и соответствующие права; для firewall — sudo
с доступом к nftables.

## Старые команды и браузер

`scripts/test-startup.sh`, `test-schema.sh`, `test-network.sh` и
`test-integration.sh` оставлены как короткие совместимые точки входа в Python.
Последняя, как и прежняя команда, останавливает Redis и worker. Для проверки
сети без применённого firewall задайте `FIREWALL=off`.

Браузерный сценарий остаётся отдельной программой (Firefox и geckodriver):

```bash
python3 -B scripts/integration/test_ui.py --docker-bridge
```

`--http-only` проверяет только HTTP. Тестовый Docker-мост не проверяет
опубликованный порт хоста. Скриншоты по умолчанию — в `build/test-artifacts/`.
Команды резервного копирования и управления PostgreSQL/RabbitMQ остались в
`scripts/`: это инструменты эксплуатации, а не тесты.
