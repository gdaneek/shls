# Границы и сетевые связи

Сегменты `public`, `data` и `messaging`, адреса и bridge-firewall описаны в
`docker-compose.yml` и `infra/firewall/mini-checkers.nft`. Game API подключён
ко всем трём сетям; worker находится только в `messaging`, а frontend — в
`public`. Сети `data` и `messaging` внутренние.

| Сегмент | Подсеть / мост | Компоненты |
|---|---|---|
| `public` | `10.10.1.0/24`, `mc-public` | frontend `.3`, Game API `.2` |
| `data` | `10.10.2.0/24`, `mc-data` | API `.2`, PostgreSQL `.3` и `.5`, Redis `.4`, maintenance `.6` |
| `messaging` | `10.10.3.0/24`, `mc-msg` | API `.2`, RabbitMQ `.3` / `.5` / `.6`, worker `.4` |

## Разрешения

| Откуда → куда | Порт | Решение |
|---|---|---|
| Хост → frontend | `127.0.0.1:8000` → 8080 | ALLOW, единственный опубликованный порт по умолчанию |
| frontend → API | TCP 8000 | ALLOW |
| API → оба узла PostgreSQL | TCP 5432 | ALLOW; приложение использует выбранный primary |
| PostgreSQL ↔ PostgreSQL | TCP 5432 | ALLOW для репликации и возврата узла |
| maintenance → PostgreSQL | TCP 5432 | ALLOW при запуске tools-job |
| API → Redis | TCP 6379 | ALLOW |
| API / worker → RabbitMQ | TCP 5672 | ALLOW |
| RabbitMQ ↔ RabbitMQ | TCP 4369, 25672, 35672–35682 | ALLOW для cluster и CLI |
| worker → API, Redis ↔ PostgreSQL | соответствующие порты | DENY для новых соединений |
| worker → PostgreSQL / Redis | 5432 / 6379 | Нет прямого маршрута |

Для установленного разрешённого соединения обратный трафик допускается
через conntrack. Другие новые соединения внутри проектных мостов не проходят
явную разрешающую политику и отбрасываются. Вне проектных мостов эта таблица
не предназначена управлять чужими правилами хоста.

Правила применяются командой `infra/firewall/firewall.sh apply`, состояние
и счётчики доступны через `infra/firewall/firewall.sh status`, откат — через
`infra/firewall/firewall.sh rollback`. TCP-пробы выполняются командой
`python3 -B tests/integration/test_network.py -v`: ожидаются `OPEN` для
разрешённых связей, `FILTERED` при DROP и `UNREACHABLE` между изолированными
сегментами. Проверка роста DROP-счётчика требует доступного `sudo -n nft`;
если его нет, тест сообщает `INFO` и проверяет только TCP-результаты.

## Секреты и границы

API получает пароли БД и AMQP; worker — пароль RabbitMQ; frontend секретов
инфраструктуры не получает. Секреты хранятся в окружении и локальном `.env`,
не в исходниках. Токен игрока передаётся в `X-Player-Token`, в PostgreSQL
хранится его SHA-256, а не исходное значение.

Текущий стенд ограничивает вход loopback-адресом и сегментами, но не включает
TLS внутри сетей и полноценные учётные записи. Отдельная политика для
доступа по Интернету в проекте не настроена.
