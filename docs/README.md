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

## Сборка через Python

Нужны Python 3.10–3.13 с `venv`, Graphviz (`dot`) и Doxygen (`doxygen`);
из корня репозитория:

```bash
python3 -m venv build/docs-venv
build/docs-venv/bin/python -m pip install -r docs/guide/requirements.txt
build/docs-venv/bin/python -B docs/build.py
```

Sphinx автоматически запускает `doxygen docs/Doxyfile` перед генерацией справочника.
XML сохраняется в `build/docs/doxygen/xml/`, главная страница справочника —
`build/docs/html/api/library_root.html`. Отдельная генерация XML из корня
репозитория: `doxygen docs/Doxyfile`. Недокументированные объявления в заголовках
и ошибки Doxygen останавливают сборку.

Состав сайта задают `docs/guide/index.md` и исключения в `docs/guide/conf.py`.
Результат сборки находится в игнорируемом Git каталоге `build/`.
