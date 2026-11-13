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

Отказные тесты останавливают сервисы, меняют роли PostgreSQL и `.env`, создают бэкапы.
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
python3 -B tests/integration/test_ui.py
```

Браузерный сценарий запускается отдельно и не выполняется через `unittest discover`.
По умолчанию он обращается к `http://127.0.0.1:8000`; другой адрес задаётся
переменной `FRONTEND_URL`. `--http-only` выполняет только HTTP-проверки.
Скриншоты сохраняются в `build/test-artifacts/`.

Дополнительный режим `--docker-bridge` направляет запросы через тестовый
прокси и `docker compose exec` во frontend. Он проверяет frontend/API,
но не доступность опубликованного порта хоста.

Команды обслуживания находятся в `infra/backups/`, `infra/postgres/` и
`infra/rabbitmq/`.

## Сценарии

| Файл | Что проверяет |
|---|---|
| `test_startup.py` | Health, hello, БД/кеш/quorum, опубликованные порты |
| `test_flows.py` | Запись/чтение, cache-aside, авторизация, конкурентные ходы и события |
| `test_schema.py` | Пустая БД, повторное применение и обновление старой схемы |
| `test_network.py` | Разрешённые/запрещённые TCP-связи, изоляция сегментов, DROP-счётчик |
| `test_backup.py` | Восстановленные данные, права архива, запрет перезаписи и ротация |
| `test_slo.py` | 100 чтений, p95, пять событий за ≤15 с |
| `test_failure.py` | Авторестарт, деградация, сохранность событий, потеря quorum, полный отказ брокера и outbox |
| `test_firewall_failure.py` | Ошибка SQL-связи и откат без изменения основной политики |
| `test_postgres_failover.py` | SIGKILL, promotion, чтение/запись, кеш, rejoin и backup |
| `test_rabbitmq_failover.py` | Отказ лидера, backlog, reconnect consumer, непрерывная работа клиентов и догоняние реплик |
| `test_captures.py` | Взятия, цепочки, дамки, победа и сохранность продолжения боя |
| `test_ui.py` | HTTP, две вкладки, polling, зритель, взятия/дамки и восстановление после reload |
| `test_harness.py` | Аварийный cleanup на моках без остановки стенда |
