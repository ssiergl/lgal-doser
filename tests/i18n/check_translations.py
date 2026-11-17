#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks the web UI translations against the English strings.

English is defined in the JavaScript sources (add({ en: { 'key': 'text', … } })), every other
language in components/webui/www/lang/<code>.json. Fails when
  - a language file misses a key or has a key English does not know,
  - a translation uses other placeholders ({n}, {v} …) than the English text,
  - the code calls t('key') with a literal key English does not define.

    python3 tests/i18n/check_translations.py
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WWW = os.path.join(ROOT, "components", "webui", "www")
PLACEHOLDER = re.compile(r"\{(\w+)\}")


def skip_string(src, i):
    """Index after the JS string literal that starts at src[i]."""
    quote = src[i]
    i += 1
    while src[i] != quote:
        i += 2 if src[i] == "\\" else 1
    return i + 1


def add_blocks(src):
    """Source text of every add({ … }) call."""
    for m in re.finditer(r"\badd\(\{", src):
        i, depth = m.end() - 1, 0
        while True:
            c = src[i]
            if c in "'\"`":
                i = skip_string(src, i)
                continue
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    yield src[m.end() - 1:i + 1]
                    break
            i += 1


def js_string(lit):
    """Value of a simple JS string literal ('…' or "…")."""
    body = lit[1:-1]
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), body)


STR = r"""'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*\""""
PAIR = re.compile(r"""(['"])([\w.\-]+)\1\s*:\s*(""" + STR + r"|\[\s*(?:(?:" + STR + r")\s*,?\s*)*\])")


def english():
    strings, sources = {}, []
    for path in sorted(glob.glob(os.path.join(WWW, "**", "*.js"), recursive=True)):
        if os.sep + "vendor" + os.sep in path:
            continue
        src = open(path, encoding="utf-8").read()
        sources.append((path, src))
        for block in add_blocks(src):
            for m in PAIR.finditer(block):
                if m.group(2) in strings:
                    sys.exit(f"{os.path.relpath(path, ROOT)}: key {m.group(2)} defined twice")
                v = m.group(3)
                strings[m.group(2)] = ([js_string(s) for s in re.findall(STR, v)] if v.startswith("[")
                                       else js_string(v))
    return strings, sources


def main():
    en, sources = english()
    errors = []
    if len(en) < 100:
        errors.append(f"only {len(en)} English strings found: the parser no longer matches the sources")
    for path in sorted(glob.glob(os.path.join(WWW, "lang", "*.json"))):
        code = os.path.basename(path)[:-5]
        tr = json.load(open(path, encoding="utf-8"))
        for k in sorted(set(en) - set(tr)):
            errors.append(f"{code}: missing {k}")
        for k in sorted(set(tr) - set(en)):
            errors.append(f"{code}: unknown key {k}")
        for k in sorted(set(en) & set(tr)):
            if isinstance(en[k], list) or isinstance(tr[k], list):
                if not (isinstance(tr[k], list) and isinstance(en[k], list) and len(tr[k]) == len(en[k])):
                    errors.append(f"{code}: {k} must be a list of {len(en[k]) if isinstance(en[k], list) else 0}")
                continue
            a, b = set(PLACEHOLDER.findall(en[k])), set(PLACEHOLDER.findall(tr[k]))
            if a != b:
                errors.append(f"{code}: {k} has placeholders {sorted(b)}, English {sorted(a)}")
    for path, src in sources:
        for m in re.finditer(r"\bt\((['\"])([\w.\-]+)\1\s*[,)]", src):
            if m.group(2) not in en:
                line = src.count("\n", 0, m.start()) + 1
                errors.append(f"{os.path.relpath(path, ROOT)}:{line}: t('{m.group(2)}') has no English text")
    for e in errors:
        print(e)
    print(f"{len(en)} strings, {len(glob.glob(os.path.join(WWW, 'lang', '*.json')))} languages, "
          f"{len(errors)} problems")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
