# Документация Mini Checkers

Исходники сайта возможностей и архитектуры находятся в `docs/guide/`.
Стартовая страница включает текст корневого `README.md` непосредственно, без
второй копии. Стартовая страница сборки — `build/docs/html/index.html`.
Статьи описывают контекст, контейнеры, компоненты и потоки данных. Дополнительные
локальные заметки, если они есть, для сборки сайта не требуются.
Сайт собирается Sphinx с темой Read the Docs.
При push тега или публикации релиза GitHub Actions собирает HTML и сохраняет
его как artifact `documentation`.

## Сборка через Docker

Из корня репозитория:

```bash
docker build -t mini-checkers-docs:local docs/guide
mkdir -p build/docs/html
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$PWD/docs/guide:/docs:ro" \
  -v "$PWD/README.md:/README.md:ro" \
  -v "$PWD/build:/build" \
  mini-checkers-docs:local
```

Для создания образа нужен доступ к Docker Registry и PyPI; сама сборка HTML
работает без сети. Откройте `build/docs/html/index.html` в браузере. Если в
каталоге результата уже лежат важные файлы, выберите другой каталог вывода.

## Сборка через Python

Нужны Python 3.10–3.13 с `venv` и Graphviz (`dot`); из корня репозитория:

```bash
python3 -m venv build/docs-venv
build/docs-venv/bin/python -m pip install -r docs/guide/requirements.txt
build/docs-venv/bin/sphinx-build -b html -n -W --keep-going -E -a docs/guide build/docs/html
```

Не создавайте окружение поверх уже существующего без проверки. Исходники схем
лежат в `docs/guide/_static/*.dot`; Sphinx строит SVG при сборке и сохраняет
их в `build/docs/html/`. Docker-образ включает Graphviz.

Исходники сайта отслеживаются git; результат сборки находится в игнорируемом
`build/`. Дополнительные локальные заметки не включаются в оглавление сайта.
