#!/usr/bin/env python3
"""gen_css_props.py - Lift Blink CSS data tables into cupid-os headers.

Reads Blink .in files (CSSProperties.in, CSSValueKeywords.in,
MediaFeatureNames.in) and emits matching gen_*.h headers consumed by
the cupid-os browser parser/style code.

Currently emits property, keyword, and media-feature name enums plus
reverse lookup tables. Future work: StyleBuilder applyValue dispatch
table keyed off the same property metadata.

Usage:
    gen_css_props.py <blink_root> <out_dir>

Where blink_root is the directory containing Source/core/css/*.in and
out_dir receives gen_css_properties.h, gen_css_keywords.h, and
gen_media_features.h.
"""

from __future__ import annotations

import os
import re
import sys


def parse_in_lines(path: str):
    """Yield (name, flags_text) pairs from a Blink-style .in file.

    Blink .in syntax: lines starting with '//' or blank are skipped.
    Each remaining line begins with the entry name; whitespace then
    optional comma-separated flags. We return the raw flag tail so
    callers can pull out anything they need (e.g. inherited, font).
    """
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.split("//", 1)[0].rstrip()
            if not line.strip():
                continue
            # Some files set namespace="..." metadata at the top; skip.
            if "=" in line and " " not in line.split("=", 1)[0]:
                continue
            parts = line.split(None, 1)
            name = parts[0].strip()
            tail = parts[1].strip() if len(parts) > 1 else ""
            if not name or name.startswith("("):
                continue
            yield name, tail


def ident_for(name: str) -> str:
    """Map 'font-family' / '-webkit-locale' to a C identifier suffix."""
    s = re.sub(r"[^A-Za-z0-9]+", "_", name)
    s = s.strip("_")
    if not s:
        s = "Unknown"
    if s[0].isdigit():
        s = "_" + s
    return s


def emit_properties(in_path: str, out_path: str) -> None:
    entries = list(parse_in_lines(in_path))
    with open(out_path, "w", encoding="utf-8") as out:
        out.write("/* Auto-generated from CSSProperties.in - do not edit. */\n")
        out.write("#ifndef GEN_CSS_PROPERTIES_H\n")
        out.write("#define GEN_CSS_PROPERTIES_H\n\n")
        out.write("enum CSSPropertyID {\n")
        out.write("    CSSProperty_invalid = 0,\n")
        for name, _ in entries:
            out.write(f"    CSSProperty_{ident_for(name)},\n")
        out.write("    CSSProperty_count\n")
        out.write("};\n\n")

        out.write("static const char *css_property_names[] = {\n")
        out.write('    "",\n')
        for name, _ in entries:
            out.write(f'    "{name}",\n')
        out.write("};\n\n")

        out.write("static const unsigned char css_property_inherited[] = {\n")
        out.write("    0,\n")
        for _, tail in entries:
            inherited = 1 if "inherited" in tail else 0
            out.write(f"    {inherited},\n")
        out.write("};\n\n")

        out.write("#endif\n")


def emit_keywords(in_path: str, out_path: str) -> None:
    entries = list(parse_in_lines(in_path))
    with open(out_path, "w", encoding="utf-8") as out:
        out.write("/* Auto-generated from CSSValueKeywords.in - do not edit. */\n")
        out.write("#ifndef GEN_CSS_KEYWORDS_H\n")
        out.write("#define GEN_CSS_KEYWORDS_H\n\n")
        out.write("enum CSSValueID {\n")
        out.write("    CSSValue_invalid = 0,\n")
        for name, _ in entries:
            out.write(f"    CSSValue_{ident_for(name)},\n")
        out.write("    CSSValue_count\n")
        out.write("};\n\n")

        out.write("static const char *css_value_names[] = {\n")
        out.write('    "",\n')
        for name, _ in entries:
            out.write(f'    "{name}",\n')
        out.write("};\n\n")
        out.write("#endif\n")


def emit_media_features(in_path: str, out_path: str) -> None:
    entries = list(parse_in_lines(in_path))
    with open(out_path, "w", encoding="utf-8") as out:
        out.write("/* Auto-generated from MediaFeatureNames.in - do not edit. */\n")
        out.write("#ifndef GEN_MEDIA_FEATURES_H\n")
        out.write("#define GEN_MEDIA_FEATURES_H\n\n")
        out.write("static const char *css_media_feature_names[] = {\n")
        for name, _ in entries:
            out.write(f'    "{name}",\n')
        out.write("    0\n")
        out.write("};\n\n")
        out.write("#endif\n")


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: gen_css_props.py <blink_root> <out_dir>\n")
        return 2
    blink_root, out_dir = sys.argv[1], sys.argv[2]
    css_dir = os.path.join(blink_root, "Source", "core", "css")
    os.makedirs(out_dir, exist_ok=True)
    emit_properties(os.path.join(css_dir, "CSSProperties.in"),
                    os.path.join(out_dir, "gen_css_properties.h"))
    emit_keywords(os.path.join(css_dir, "CSSValueKeywords.in"),
                  os.path.join(out_dir, "gen_css_keywords.h"))
    emit_media_features(os.path.join(css_dir, "MediaFeatureNames.in"),
                        os.path.join(out_dir, "gen_media_features.h"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
