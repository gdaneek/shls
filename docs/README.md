# Документация Mini Checkers

Исходники сайта возможностей и архитектуры находятся в `docs/guide/`.
Стартовая страница включает текст корневого `README.md` непосредственно, без
второй копии. Стартовая страница сборки — `build/docs/html/index.html`.
Сайт содержит девять основных разделов: запуск (README), архитектура, HTTP API, данные и
кеш, события, firewall, надёжность, уровень обслуживания с наблюдаемостью и
«Документация API» — справочник C++-интерфейсов из заголовков `shls/*.h`.
Сайт собирается Sphinx с темой Read the Docs. Doxygen создаёт XML из комментариев
в заголовках, Breathe отображает описания, а Exhale формирует деревья классов и
файлов с отдельными страницами типов, функций и констант.
При push тега или ручном запуске workflow `Docs` собирает HTML, сохраняет artifact `documentation`
и публикует сайт в ветку `gh-pages`. GitHub Pages размещает содержимое корня
этой ветки. Создание релиза отдельно workflow не запускает.

## GitHub Pages

### Однократная настройка репозитория

Используется режим **Deploy from a branch**. Для push в ветку публикации
нужен отдельный SSH deploy key: push с обычным `GITHUB_TOKEN` не запускает
сборку Pages в этом режиме
([ограничение GitHub](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site#troubleshooting-publishing-from-a-branch)).

1. Создайте отдельную пару ключей вне репозитория. Если такой файл уже есть,
   используйте другое имя, не перезаписывая существующий ключ:

   ```bash
   mkdir -p "$HOME/.ssh"
   ssh-keygen -t ed25519 -C "shls-project Pages" -f "$HOME/.ssh/shls-pages" -N ''
   ```

2. **Settings → Deploy keys → Add deploy key**: добавьте содержимое
   `~/.ssh/shls-pages.pub`, включите **Allow write access**.
3. **Settings → Secrets and variables → Actions → New repository secret**:
   имя **`GH_PAGES_DEPLOY_KEY`**, значение - полное содержимое приватного файла
   `~/.ssh/shls-pages`, включая строки BEGIN/END. Приватный ключ не добавляется
   в Git и не публикуется в документации.
4. **Settings → Pages → Build and deployment**:
   - Source: **Deploy from a branch**;
   - Branch: **gh-pages**;
   - Folder: **/(root)**;
   - **Save**.

Если ветки `gh-pages` ещё нет, первый успешный workflow создаст её.
После этого выберите её в Pages и сохраните настройки. Ветка предназначена
только для результата сборки: при публикации её файлы заменяются содержимым
нового сайта, история коммитов сохраняется. Правила защиты этой ветки должны
допускать прямой push deploy key.

### Публикация версии

Тег должен указывать на коммит с обновлённым `.github/workflows/docs.yml`.
После включения изменений в нужную ветку создайте новый тег, например:

```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

Выберите ещё не занятое имя версии. Workflow собирает именно коммит тега;
отдельно создавать GitHub Release не требуется. Публикации сериализованы,
но версии по SemVer не сравниваются: повторный запуск старого тега может
заменить сайт старой версией.

### Ручная публикация

Откройте **Actions → Docs → Run workflow**, выберите ветку с нужной версией
документации и нажмите **Run workflow**. Выполняются та же сборка и публикация
в `gh-pages`, что и при push тега; deploy key также обязателен.
Чтобы кнопка появилась, файл workflow с `workflow_dispatch` должен находиться
в default branch репозитория. В выбранной ветке также должна быть версия
workflow с этим триггером. Ветку результата `gh-pages` для запуска не выбирайте.

Путь публикации:

```text
push тега / Run workflow → Docs → Sphinx → gh-pages → Pages build and deployment → сайт
```

`sphinx.ext.githubpages` создаёт `.nojekyll`: Jekyll не обрабатывает готовый
HTML и не отбрасывает каталоги `_static`, `_images` и `_sources`.
Кеш Sphinx хранится отдельно от HTML; в ветку попадает только сайт.
Ссылки и ресурсы Sphinx относительные и работают под префиксом репозитория.

Для `gdaneek/shls-project` стандартный адрес:
<https://gdaneek.github.io/shls-project/>. Фактический адрес показывает
**Settings → Pages**. После push проверьте оба запуска в **Actions**:
сначала `Docs`, затем `pages build and deployment`. На сайте должны
открываться главная страница, C1/C2 и раздел параметров обслуживания;
стили, схемы и поиск должны загружаться.

При ошибке авторизации проверьте имя секрета, соответствие пары ключей и
право записи deploy key. Если `Docs` завершился успешно, но сайт не появился,
проверьте источник `gh-pages / (root)` и результат Pages deployment.

## Сборка через Docker

Из корня репозитория:

```bash
docker build -t mini-checkers-docs:local docs/guide
mkdir -p build/docs/html
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$PWD:/project:ro" \
  -v "$PWD/build:/project/build" \
  mini-checkers-docs:local
```

Для создания образа нужен доступ к Docker Registry и PyPI; сама сборка HTML
работает без сети. Откройте `build/docs/html/index.html` в браузере.

## Сборка через Python

Нужны Python 3.10–3.13 с `venv`, Graphviz (`dot`) и Doxygen (`doxygen`);
из корня репозитория:

```bash
python3 -m venv build/docs-venv
build/docs-venv/bin/python -m pip install -r docs/guide/requirements.txt
build/docs-venv/bin/python -B docs/build.py
```

Если окружение `build/docs-venv` уже создано, пропустите команду `venv`.
Исходники схем лежат в `docs/guide/_static/*.dot`; Sphinx строит SVG при сборке и сохраняет
их в `build/docs/html/`. Docker-образ включает Graphviz и Doxygen.

`docs/build.py` подготавливает копию исходников в `build/docs/source/` и запускает
строгую сборку Sphinx. Exhale создаёт страницы RST внутри этой копии; исходные
документы в `docs/guide/` остаются только входными данными сборки. Каталоги
`build/docs/source/` и `build/docs/html/` пересоздаются при каждом запуске.

Sphinx автоматически запускает `doxygen docs/Doxyfile` перед генерацией справочника.
XML сохраняется в `build/docs/doxygen/xml/`, главная страница справочника —
`build/docs/html/api/library_root.html`. Отдельная генерация XML из корня
репозитория: `doxygen docs/Doxyfile`. Недокументированные объявления в заголовках
и ошибки Doxygen останавливают сборку.

Состав сайта задают `docs/guide/index.md` и исключения в `docs/guide/conf.py`.
Результат сборки находится в игнорируемом Git каталоге `build/`.
