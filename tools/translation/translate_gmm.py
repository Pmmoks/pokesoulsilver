#!/usr/bin/env python3
"""Make a translated message bank from an English one.

usage: translate_gmm.py ENGLISH_GMM TRANSLATION_TSV OUT_GMM

The TSV holds one translated row per line, `<row index><TAB><text>`, with the
text written as in a .gmm file but without XML escaping (`\\n`, `\\r`, `\\f`
and `{COMMAND ...}` as usual). Lines starting with `#` are comments. Rows that
are not listed, and a missing TSV, keep their English text; garbage rows are
never changed. The output keeps the English bank's rows, ids and attributes, so
msgenc encodes it exactly like the original.
"""
import re
import sys
from xml.sax.saxutils import escape

ROW = re.compile(r'(<row id="[^"]*" index="(\d+)">\s*'
                 r'<attribute name="window_context_name">([^<]*)</attribute>\s*'
                 r'<language name="English">)(.*?)(</language>)', re.S)


def read_tsv(path):
    rows = {}
    try:
        f = open(path, encoding='utf-8')
    except FileNotFoundError:
        return rows
    with f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            index, sep, text = line.partition('\t')
            if not sep or not index.isdigit():
                sys.exit(f'{path}:{lineno}: expected "<index><TAB><text>"')
            if int(index) in rows:
                sys.exit(f'{path}:{lineno}: row {index} is translated twice')
            rows[int(index)] = text
    return rows


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    english_path, tsv_path, out_path = sys.argv[1:]
    english = open(english_path, encoding='utf-8').read()
    translations = read_tsv(tsv_path)
    seen = set()

    def replace(m):
        index, context = int(m.group(2)), m.group(3)
        if index not in translations or context == 'garbage':
            return m.group(0)
        seen.add(index)
        return m.group(1) + escape(translations[index]) + m.group(5)

    out = ROW.sub(replace, english)
    unknown = sorted(set(translations) - seen)
    if unknown:
        sys.exit(f'{tsv_path}: no such (non-garbage) rows in {english_path}: {unknown}')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(out)


if __name__ == '__main__':
    main()
