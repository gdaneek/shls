project = "Mini Checkers"
author = "Mini Checkers"
language = "ru"

extensions = ["myst_parser", "sphinx.ext.graphviz"]
source_suffix = {".md": "markdown"}
root_doc = "index"
exclude_patterns = ["_build", "user/**", "operations/**", "development/testing.md", "development/documentation.md"]
myst_heading_anchors = 3
graphviz_output_format = "svg"

html_theme = "sphinx_rtd_theme"
html_title = "Mini Checkers — документация"
html_static_path = ["_theme"]
html_css_files = ["custom.css"]
html_theme_options = {
    "navigation_depth": 3,
    "style_external_links": True,
}
