# Проверки

## Сценарии

Проверки выполняются на запущенном стенде с PostgreSQL, Redis и RabbitMQ.

| Область | Что проверяется | Где проверить |
|---|---|---|
| Запуск | Отдельные front/back/worker, Compose, БД/кеш/брокер, hello-world | Главная страница; `test_startup.py` |
| Данные | SQL-схема; INSERT/SELECT/UPDATE/DELETE; запись БД → кеш → событие; cache-aside; consumer | [Данные](../architecture/storage.md), [события](../architecture/events.md); `test_schema.py`, `test_flows.py` |
| Сеть | Сегменты, все правила, ALLOW/DENY и ошибки при запрете необходимой связи | [Firewall](../architecture/security.md); `test_network.py`, `test_firewall_failure.py` |
| Восстановление | Авария primary и promotion standby; отказ лидера брокера; backup/restore; MTBF/MTTR/SLO | [Надёжность](../architecture/reliability.md); `test_postgres_failover.py`, `test_rabbitmq_failover.py`, `test_backup.py`, `test_slo.py` |

Имена тестов в таблице относятся к `tests/integration/`.

## Запуск проверок

После запуска Compose и штатного firewall, из корня репозитория
(Python 3.9+, стандартная библиотека):

```bash
python3 -B -m unittest discover -s tests/integration -p 'test_*.py' -v
python3 -B tests/integration/test_ui.py --docker-bridge
```

Первый запуск проверяет SQL, кеш, события, правила, сеть, backup и SLO.
Созданные партии остаются в БД; схема и restore используют временные базы.
Отказные сценарии требуют `CHECKERS_DISRUPTIVE=1` и запускаются отдельно
командами из разделов firewall/надёжности, без параллельной игры.

Вторая команда требует Firefox/geckodriver: проверяет две вкладки, polling,
зрителя, ходы, взятия и дамки; скриншоты — `build/test-artifacts/`.
Docker-мост обходит опубликованный порт. Проверка самого порта:

```bash
CHECKERS_HOST_TEST=1 python3 -B tests/integration/test_startup.py Startup.test_frontend_from_host -v
```

C++-тесты в `tests/game-api/` проверяют публичные интерфейсы без работающего
стенда; запускаются через CTest в Docker-сборке и CI. Локальная сборка описана
в `third-party/README.md`. CI публикует `coverage`, но не запускает Python-интеграцию.
Подробности и аварийный cleanup — в `tests/integration/README.md`.

## Зафиксированные результаты

Прогон **13.11.2026** после переноса проверок в Python:

| Проверка | Результат |
|---|---|
| `unittest` | 25 PASS, 11 SKIP: десять отказных проверок и запрос через порт хоста пропущены. |
| Чтение / события | 100/100 корректных ответов, p95 0,001193 с; 5/5 событий за ≤15 с. |
| Backup + restore | Данные совпали, 2,81 с. |
| Сеть | Ожидаемые OPEN/FILTERED/UNREACHABLE, DROP 330 → 396. |
| Firefox через Docker-мост | PASS. |

Новые Python-сценарии отказов, опубликованный порт и режим `FIREWALL=off`
реально ещё не проверены. Исторические shell-замеры переключений от
10.11.2026 (PostgreSQL 13 с, RabbitMQ 8 с) не подтверждают новые тесты.
