from pathlib import Path
import re
import subprocess

from breathe.directives.class_like import DoxygenClassDirective, DoxygenStructDirective
from breathe.renderer.filter import Node
from docutils.nodes import make_id
from exhale import utils
from sphinx.util.nodes import make_refnode

project = "Mini Checkers"
author = "Mini Checkers"
language = "ru"

extensions = ["myst_parser", "sphinx.ext.graphviz", "breathe", "exhale"]
source_suffix = {".md": "markdown", ".rst": "restructuredtext"}
primary_domain = "cpp"
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

repository = Path(__file__).resolve().parents[2]
breathe_projects = {project: str(repository / "build/docs/doxygen/xml")}
breathe_default_project = project


def api_member_options(kind):
    return [":members:", ":private-members:"] if kind in ("class", "struct") else []


exhale_args = {
    "containmentFolder": str(repository / "build/docs/source/docs/guide/api"),
    "rootFileName": "library_root.rst",
    "rootFileTitle": "Документация API",
    "doxygenStripFromPath": str(repository),
    "createTreeView": True,
    "fullToctreeMaxDepth": 1,
    "fullApiSubSectionTitle": "Все сущности",
    "contentsTitle": "Содержание",
    "afterTitleDescription": "HTTP-контракт: :doc:`/development/api`.",
    "customSpecificationsMapping": utils.makeCustomSpecificationsMapping(api_member_options),
}

# External library types have no documentation targets in this project.
nitpick_ignore_regex = [("cpp:identifier", r"(?:std|drogon)(?:::.*)?")]
nitpick_ignore = [("cpp:identifier", "amqp_connection_state_t_")]


def generate_doxygen(app):
    subprocess.run(["doxygen", "docs/Doxyfile"], cwd=repository, check=True)


def normalize_tree_anchors(app):
    # Сырые HTML-ссылки Exhale должны использовать те же якоря, что и Docutils.
    for path in Path(exhale_args["containmentFolder"]).glob("*_view_hierarchy.rst.include"):
        content = re.sub(r'(href="[^"#]+#)([^" ]+)',
                         lambda match: match[1] + make_id(match[2]), path.read_text())
        path.write_text(content)


class SeparateNestedTypes:
    # Exhale даёт вложенным типам собственные страницы и ссылки у родителя.
    def render(self, node_stack, project_info, render_filter, *args):
        node = Node()
        nested_type = (node.node_type == "ref") & (node.node_name == "innerclass")
        return super().render(node_stack, project_info, render_filter & ~nested_type, *args)


class ApiClassDirective(SeparateNestedTypes, DoxygenClassDirective):
    pass


class ApiStructDirective(SeparateNestedTypes, DoxygenStructDirective):
    pass


def link_namespace(app, env, node, content):
    # Exhale регистрирует пространства имён как RST-метки, а не C++-объекты.
    if node.get("refdomain") == "cpp" and node.get("reftype") == "identifier":
        label = "namespace_" + node["reftarget"].replace("::", "__")
        target = env.domaindata["std"]["labels"].get(label)
        if target:
            return make_refnode(app.builder, node["refdoc"], target[0], target[1], content)


def setup(app):
    app.add_directive("doxygenclass", ApiClassDirective, override=True)
    app.add_directive("doxygenstruct", ApiStructDirective, override=True)
    app.connect("builder-inited", generate_doxygen, priority=100)
    app.connect("builder-inited", normalize_tree_anchors, priority=600)
    app.connect("missing-reference", link_namespace)
