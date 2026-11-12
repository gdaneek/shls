# Сегментация и firewall

## Сети и правила

IP-план задан в `docker-compose.yml`, правила — в
`infra/firewall/mini-checkers.nft` (таблица `bridge mini_checkers`).

| Сеть / мост | Подсеть | Узлы: последний октет |
|---|---|---|
| `public` / `mc-public` | `10.10.1.0/24` | API `.2`, frontend `.3` |
| `data` / `mc-data` | `10.10.2.0/24` | API `.2`, PostgreSQL `.3` и `.5`, Redis `.4`, maintenance `.6` |
| `messaging` / `mc-msg` | `10.10.3.0/24` | API `.2`, RabbitMQ `.3` / `.5` / `.6`, worker `.4` |

`data` и `messaging` — внутренние Docker-сети. На хост опубликован только
frontend: `127.0.0.1:8000` по умолчанию.

| Новое соединение | TCP-порт назначения | Решение |
|---|---|---|
| frontend → API | 8000 | ALLOW |
| API → оба PostgreSQL | 5432 | ALLOW |
| PostgreSQL ↔ PostgreSQL | 5432 | ALLOW: репликация/rejoin |
| maintenance → оба PostgreSQL | 5432 | ALLOW: обслуживание |
| API → Redis | 6379 | ALLOW |
| API / worker → все RabbitMQ | 5672 | ALLOW |
| RabbitMQ ↔ RabbitMQ | 4369, 25672, 35672–35682 | ALLOW: cluster и CLI |
| Остальные связи внутри проектных мостов | любые | DROP |

ARP и ответы установленных соединений разрешены. Чужие мосты пропускаются
без фильтрации этой таблицей. Остальной трафик проектных мостов учитывается
DROP-счётчиком и логируется с ограничением частоты. Worker → PostgreSQL/Redis
не имеет прямого маршрута: это сегментация, а не доказательство срабатывания DROP.

## Проверка разрешений и запретов

```bash
infra/firewall/firewall.sh apply
infra/firewall/firewall.sh status
python3 -B tests/integration/test_network.py -v
```

`status` показывает все проектные правила и счётчики. Тест делает TCP-пробы
по IP: `OPEN` — разрешено, `FILTERED` — таймаут при DROP, `UNREACHABLE` —
изоляция сегментов. Рост DROP проверяется при доступном `sudo -n nft`;
иначе выводится `INFO`. Один лишь сбой DNS не считается сетевой проверкой.
Откат: `infra/firewall/firewall.sh rollback`. После перезагрузки хоста правила
нужно применить заново.

## Запрет необходимой связи

Проверка поведения API при потере доступа к БД:

```bash
CHECKERS_DISRUPTIVE=1 python3 -B tests/integration/test_firewall_failure.py -v
```

Тест создаёт временную таблицу с DROP API → текущий PostgreSQL primary.
Readiness и чтение после очистки кеша должны дать **503**, liveness — **200**,
а локальный SQL-запрос в контейнере БД остаётся успешным. После удаления
таблицы ответы возвращаются к **200**; основная политика не меняется.
Нужны права на nftables; тест прерывает SQL-связь на время проверки.

## Прикладные ограничения доступа

Токен игрока — 32 случайных байта; в БД хранится SHA-256, в запросе —
`X-Player-Token`. Размер тела ограничен 1 КиБ. Frontend/API/worker работают
без root и лишних capabilities; секреты передаются через локальный `.env`.
У приложения пока административный пользователь БД. TLS и удалённый доступ
не настроены: это локальный стенд, а не публичное развёртывание.
