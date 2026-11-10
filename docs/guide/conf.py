project = "Mini Checkers"
author = "Mini Checkers"
language = "ru"

extensions = ["myst_parser", "sphinx.ext.graphviz"]
source_suffix = {".md": "markdown"}
root_doc = "index"
exclude_patterns = ["_build", "user/**", "operations/**", "development/testing.md", "development/documentation.md"]
myst_heading_anchors = 3
graphviz_output_format = "svg"

html_theme = "alabaster"
html_title = "Mini Checkers — документация"
html_theme_options = {
    "description": "Возможности и архитектура",
    "page_width": "1100px",
    "sidebar_width": "245px",
    "font_family": "system-ui, sans-serif",
    "font_size": "16px",
}
