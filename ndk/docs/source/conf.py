from pathlib import Path


NDK_ROOT = Path(__file__).resolve().parents[2]

project = "Astra 68 NDK"
author = "Astra 68 Project"
copyright = "2026, Astra 68 Project"
release = "0.1.0"
version = "0.1"

extensions = [
    "breathe",
    "myst_parser",
]

breathe_projects = {
    "astra-ndk": str(NDK_ROOT / "build/docs/doxygen/xml"),
}
breathe_default_project = "astra-ndk"
breathe_domain_by_extension = {"h": "c"}
breathe_show_include = False

source_suffix = {
    ".md": "markdown",
    ".rst": "restructuredtext",
}
primary_domain = "c"
nitpicky = True
nitpick_ignore = [
    ("c:identifier", "uint8_t"),
    ("c:identifier", "uint16_t"),
    ("c:identifier", "uint32_t"),
    ("c:identifier", "uint64_t"),
    ("c:identifier", "int32_t"),
]

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "fieldlist",
]

templates_path = []
exclude_patterns = []

html_theme = "furo"
html_title = "Astra 68 NDK"
html_static_path = ["_static"]
html_css_files = ["astra.css"]

latex_engine = "pdflatex"
latex_documents = [
    (
        "index",
        "astra68-ndk.tex",
        "Astra 68 Native Developer Kit",
        "Astra 68 Project",
        "manual",
    ),
]
latex_elements = {
    "papersize": "letterpaper",
    "pointsize": "10pt",
}
