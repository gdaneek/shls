"""Собирает Sphinx и справочник API, сохраняя генерируемые файлы в build/docs/."""

from pathlib import Path
import shutil

from sphinx.cmd.build import build_main


def main():
    root = Path(__file__).resolve().parents[1]
    guide = root / "docs/guide"
    output = root / "build/docs"
    source = output / "source"
    html = output / "html"

    # Exhale создаёт RST внутри дерева исходников Sphinx, поэтому работаем с копией.
    for directory in (source, html):
        if directory.exists():
            shutil.rmtree(directory)
    staged_guide = source / "docs/guide"
    shutil.copytree(guide, staged_guide, ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copy2(root / "README.md", source / "README.md")

    return build_main([
        "-b", "html", "-n", "-W", "--keep-going", "-E", "-a",
        "-d", str(output / "doctrees"),
        "-c", str(guide), str(staged_guide), str(html),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
