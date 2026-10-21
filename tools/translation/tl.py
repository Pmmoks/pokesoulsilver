#!/usr/bin/env python3
"""Work on the text translation in files/msgdata/<code>/ (see docs/TRANSLATION.md).

usage: tl.py [--lang fr] dump BANK [--all]
       tl.py [--lang fr] check [BANK ...]
       tl.py [--lang fr] wrap BANK [--width PIXELS]
       tl.py [--lang fr] stats

BANK is a bank number (`3`, `0003`) or a file stem (`msg_0003_EVERYWHERE`).

dump   prints the English rows still to translate, as TSV lines to fill in.
check  reports characters the game cannot show, {COMMANDS} that differ from the
       English row, lines wider than the bank's widest English line, and rows
       longer than the buffers they are read into can hold.
wrap   re-breaks the lines of translated dialogue that are too wide, keeping its
       text boxes (\\r): the second line of a box follows \\n, later ones scroll
       in after \\f.
stats  shows how much of each bank is translated.
"""
import argparse
import collections
import glob
import os
import re
import signal
import struct
import sys
from xml.sax.saxutils import unescape

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MSG_DIR = os.path.join(ROOT, 'files/msgdata/msg')
FONT = os.path.join(ROOT, 'files/graphic/font/font_00000000.bin')
CHARMAP = os.path.join(ROOT, 'charmap.txt')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from translate_gmm import ROW, read_tsv  # noqa: E402

COMMAND = re.compile(r'\{[^}]*\}')
LINE_BREAK = re.compile(r'\\[nrf]')
# A placeholder's width is not known before the game fills it in: one about the size of a
# Pokemon name is assumed, for the English text and the translation alike.
PLACEHOLDER_WIDTH = 60
NUMBER_WIDTH = 24

# Banks shown in a window wider than their widest English line: the window's text width.
# The battle message window is 27 tiles wide (BattleSystem_InitWindows, battle_main.c).
WIDTH_OVERRIDES = {
    'msg_0003': 216, 'msg_0197': 216,  # battle messages
    # Names: the French release, built from the same code, shows 15-character official
    # names such as "Regard Médusant" (88 px) in the same windows.
    'msg_0750': 88, 'msg_0751': 88,  # moves
    'msg_0222': 88,  # items
    'msg_0223': 116, 'msg_0224': 96,  # items with an article, plural: only within sentences
    'msg_0016': 96,  # Seals in the plural: only within sentences
    'msg_0217': 124,  # Accessories with an article: only within sentences
    'msg_0720': 88, 'msg_0721': 88,  # abilities
    # Trainer classes: the Pokegear phone list leaves 122 px for them, the rest is sentences.
    'msg_0730': 80, 'msg_0731': 100,
}

# The Pokegear radio's scripts: the show prints them a line at a time and scrolls, so they
# may have any number of lines, each read into a 50-character buffer, from a script of at
# most 1350 characters (RadioShow_New).
SCROLLING_BANKS = {f'msg_{n:04d}' for n in range(409, 421)}
RADIO_LINE_LENGTH = 50
RADIO_TEXT_LENGTH = 1350

# Banks whose text the game already shows in French somewhere else, in the same window: the
# Pokedex can show its entries in other languages. Their rows may be as wide and as long as
# that bank's.
REFERENCE_BANKS = {
    'msg_0803': 'msg_0806', 'msg_0804': 'msg_0806', 'msg_0805': 'msg_0806',  # entries
    'msg_0816': 'msg_0824', 'msg_0823': 'msg_0824',  # categories
}

# Banks that stay as they are: the Pokedex's text in the other languages it can show, the
# greetings in each language, and debug menus no retail screen opens.
KEEP_BANKS = {f'msg_{n:04d}' for n in [*range(806, 812), *range(817, 829), *range(767, 773),
                                       *range(151, 179), 297, 299, 437, 438,
                                       27]}  # 27: first names of NPC trainers

# Banks whose rows are read into fixed-size buffers, names mostly: the longest a row may be (the
# count includes the terminator, as in the encoded bank). Species names are also kept in
# the save, ten characters at most.
NAME_LIMITS = {
    'msg_0237': 11,  # species
    'msg_0750': 16,  # moves
    'msg_0751': 16,  # moves, capitals
    'msg_0222': 16,  # items
    'msg_0223': 32, 'msg_0224': 32,  # items with an article, plural: read into MessageFormat's buffer
    'msg_0016': 32,  # Seals in the plural, likewise
    'msg_0217': 32,  # Accessories with an article, likewise
    'msg_0720': 16, 'msg_0721': 16,  # abilities
    'msg_0735': 16,  # types
    'msg_0730': 16,  # trainer classes
    'msg_0731': 32,  # trainer classes with an article: read into MessageFormat's buffer
    # Trainer texts: the battle copies them into its message buffer (String_New(320), battle_main.c).
    'msg_0723': 320, 'msg_0724': 320, 'msg_0728': 320,
}


def load_charmap():
    chars = {}
    with open(CHARMAP, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\r\n')
            if not line or line.lstrip().startswith('//') or '=' not in line:
                continue
            code, text = line.split('=', 1)
            chars.setdefault(text, int(code.strip(), 16))
    return chars


def load_widths():
    data = open(FONT, 'rb').read()
    _, width_offset, count = struct.unpack_from('<III', data, 0)
    return data[width_offset:width_offset + count]


CHARS = load_charmap()
WIDTHS = load_widths()


def glyph_width(c):
    code = CHARS.get(c)
    if code is None or not 1 <= code <= len(WIDTHS):
        return 6
    return WIDTHS[code - 1]


def bad_chars(text):
    return sorted({c for c in COMMAND.sub('', LINE_BREAK.sub('', text)) if c not in CHARS})


def line_width(line):
    width = 0
    for part in re.split(r'(\{[^}]*\})', line):
        if part.startswith('{'):
            m = re.match(r'\{STRVAR_\d+ (\d+),', part)
            if m:
                # Kinds 50 and up are numbers: about four digits.
                width += NUMBER_WIDTH if int(m.group(1)) >= 50 else PLACEHOLDER_WIDTH
        else:
            width += sum(glyph_width(c) for c in part)
    return width


def lines(text):
    return LINE_BREAK.split(text)


def encoded_length(text):
    """Characters in the encoded row, terminator included."""
    plain = COMMAND.sub('', text)
    n = len(LINE_BREAK.sub('x', plain))
    # A command takes its code and its arguments.
    for cmd in COMMAND.findall(text):
        n += 2 + len([a for a in cmd[1:-1].split(' ', 1)[1:] for a in a.split(',')])
    return n + 1


class Bank:
    def __init__(self, path, lang):
        self.path = path
        self.stem = os.path.basename(path)[:-4]
        self.rows = {}
        self.garbage = set()
        for m in ROW.finditer(open(path, encoding='utf-8').read()):
            index = int(m.group(2))
            if m.group(3) == 'garbage':
                self.garbage.add(index)
            self.rows[index] = unescape(m.group(4))
        self.lang = lang
        self.tsv = os.path.join(ROOT, 'files/msgdata', lang, self.stem + '.tsv')
        self.translated = read_tsv(self.tsv)

    def needs_translation(self, index):
        text = self.rows[index]
        if index in self.garbage or self.stem[:8] in KEEP_BANKS:
            return False
        # Placeholders, numbers and punctuation alone stay as they are.
        return re.search(r'[A-Za-z]', COMMAND.sub('', LINE_BREAK.sub('', text))) is not None

    def english_line_width(self):
        widest = max([line_width(l) for i, t in self.rows.items() if i not in self.garbage for l in lines(t)] or [0])
        if self.stem[:8] in REFERENCE_BANKS:
            widest = max(widest, find_bank(REFERENCE_BANKS[self.stem[:8]], self.lang).english_line_width())
        return max(widest, WIDTH_OVERRIDES.get(self.stem[:8], 0))

    def fixed_box(self):
        """Whether the bank's text never pages (no \\r or \\f in any English row)."""
        return not any(('\\r' in t or '\\f' in t) for t in self.rows.values())

    def english_lines(self):
        return max([len(lines(t)) for i, t in self.rows.items() if i not in self.garbage] or [0])

    def english_length(self):
        longest = max([encoded_length(t) for i, t in self.rows.items() if i not in self.garbage] or [0])
        if self.stem[:8] in REFERENCE_BANKS:
            longest = max(longest, find_bank(REFERENCE_BANKS[self.stem[:8]], self.lang).english_length())
        return longest


def find_bank(spec, lang):
    if spec.isdigit():
        spec = 'msg_%04d' % int(spec)
    hits = sorted(glob.glob(os.path.join(MSG_DIR, spec + '.gmm')) + glob.glob(os.path.join(MSG_DIR, spec + '_*.gmm')))
    if not hits:
        sys.exit(f'no bank {spec}')
    return Bank(hits[0], lang)


def all_banks(lang):
    return [Bank(p, lang) for p in sorted(glob.glob(os.path.join(MSG_DIR, '*.gmm')))]


def cmd_dump(args):
    bank = find_bank(args.bank, args.lang)
    print(f'# {bank.stem}: {len(bank.rows)} rows, widest line {bank.english_line_width()} px, '
          f'longest row {bank.english_length()} characters')
    for index in sorted(bank.rows):
        if not bank.needs_translation(index):
            continue
        if index in bank.translated and not args.all:
            continue
        print(f'{index}\t{bank.rows[index]}')


def check_bank(bank):
    problems = []
    if not bank.translated:
        return problems
    max_width = bank.english_line_width()
    max_length = bank.english_length()
    max_lines = bank.english_lines()
    fixed_box = bank.fixed_box()
    limit = NAME_LIMITS.get(bank.stem[:8])
    for index, text in sorted(bank.translated.items()):
        english = bank.rows.get(index)
        where = f'{bank.stem}:{index}'
        if english is None:
            problems.append(f'{where}: no such row')
            continue
        chars = bad_chars(text)
        if chars:
            problems.append(f'{where}: characters the game cannot show: {" ".join(chars)}')
        # The same commands, each at least once: a name may be said once where English says it twice.
        if set(COMMAND.findall(text)) != set(COMMAND.findall(english)):
            problems.append(f'{where}: commands differ: {COMMAND.findall(english)} -> {COMMAND.findall(text)}')
        if text.count('{YESNO') and not text.rstrip().endswith('}'):
            problems.append(f'{where}: {{YESNO}} must end the row')
        for line in lines(text):
            w = line_width(line)
            if w > max_width:
                problems.append(f'{where}: {w} px wide, the bank allows {max_width}: {line}')
        if fixed_box and '\\f' in text:
            problems.append(f'{where}: \\f scrolls, but the bank is drawn in a fixed box')
        if bank.stem[:8] in SCROLLING_BANKS:
            if encoded_length(text) - 1 > RADIO_TEXT_LENGTH:
                problems.append(f'{where}: {encoded_length(text) - 1} characters, a radio script holds {RADIO_TEXT_LENGTH}')
            for line in lines(text):
                if encoded_length(line) - 1 > RADIO_LINE_LENGTH:
                    problems.append(f'{where}: a radio line holds {RADIO_LINE_LENGTH} characters: {line}')
        elif fixed_box and len(lines(text)) > max_lines:
            problems.append(f'{where}: {len(lines(text))} lines, the bank has at most {max_lines}')
        length = encoded_length(text)
        if limit and length > limit:
            problems.append(f'{where}: {length - 1} characters, at most {limit - 1} fit: {text}')
        elif not limit and max_length <= 32 and length > max_length:
            # Labels: the buffers they are read into may be no bigger than the longest one.
            problems.append(f'{where}: longer than every English row ({length} > {max_length}); check its buffer')
    return problems


def cmd_check(args):
    banks = [find_bank(b, args.lang) for b in args.banks] if args.banks else all_banks(args.lang)
    total = 0
    for bank in banks:
        problems = check_bank(bank)
        total += len(problems)
        # A few of each kind, so that one systematic mistake does not bury the rest.
        kinds = collections.defaultdict(list)
        for p in problems:
            kinds[re.sub(r'[0-9]+', '#', p.split(': ', 1)[1].split(':')[0])].append(p)
        for kind, items in kinds.items():
            for p in items[:args.max]:
                print(p)
            if len(items) > args.max:
                print(f'{bank.stem}: ... and {len(items) - args.max} more like that')
    print(f'{total} problem(s)', file=sys.stderr)
    return 1 if total else 0


def wrap_text(text, width, scroll=True):
    """Re-breaks each text box (between \\r) of `text` into lines at most `width` wide. The
    lines after the second scroll in (\\f) unless `scroll` is false (a fixed box)."""
    out = []
    for box in re.split(r'(\\r)', text):
        if box == '\\r' or all(line_width(l) <= width for l in lines(box)):
            out.append(box)
            continue
        # Words, each {COMMAND ...} whole.
        words = re.findall(r'(?:\{[^}]*\}|[^ {])+', re.sub(r'\\[nf]', ' ', box))
        # French puts a space before these; they stay with the word before.
        glued = []
        for w in words:
            if glued and w in ('!', '?', ':', ';', '!?', '?!', '...', '…', '”'):
                glued[-1] += ' ' + w
            elif w:
                glued.append(w)
        rows, current = [], ''
        for w in glued:
            candidate = (current + ' ' + w) if current else w
            if current and line_width(candidate) > width:
                rows.append(current)
                current = w
            else:
                current = candidate
        if current:
            rows.append(current)
        joined = rows[0] if rows else ''
        for i, row in enumerate(rows[1:], 1):
            joined += ('\\n' if i == 1 or not scroll else '\\f') + row
        out.append(joined)
    return ''.join(out)


def write_tsv(bank, rows):
    header = []
    if os.path.exists(bank.tsv):
        with open(bank.tsv, encoding='utf-8') as f:
            header = [l.rstrip('\n') for l in f if l.startswith('#')]
    os.makedirs(os.path.dirname(bank.tsv), exist_ok=True)
    with open(bank.tsv, 'w', encoding='utf-8', newline='\n') as f:
        for line in header:
            f.write(line + '\n')
        for index in sorted(rows):
            f.write(f'{index}\t{rows[index]}\n')


def cmd_wrap(args):
    bank = find_bank(args.bank, args.lang)
    width = args.width or bank.english_line_width()
    scroll = not bank.fixed_box()
    rows = dict(bank.translated)
    changed = 0
    for index, text in rows.items():
        if not args.one_line and '\\n' not in bank.rows.get(index, '') \
                and '\\r' not in bank.rows.get(index, '') and '\\f' not in bank.rows.get(index, ''):
            continue  # one-line rows (labels, names) are not dialogue
        if '\\n\\n' in bank.rows.get(index, ''):
            continue  # laid out with blank lines (help panels): break those by hand
        wrapped = wrap_text(text, width, scroll)
        if wrapped != text:
            rows[index] = wrapped
            changed += 1
    write_tsv(bank, rows)
    print(f'{bank.stem}: {changed} row(s) re-wrapped to {width} px')


def cmd_stats(args):
    total = done = 0
    for bank in all_banks(args.lang):
        todo = [i for i in bank.rows if bank.needs_translation(i)]
        n = len([i for i in todo if i in bank.translated])
        total += len(todo)
        done += n
        if args.verbose or (todo and n < len(todo)):
            print(f'{bank.stem:28} {n:5}/{len(todo):<5}')
    print(f'{done}/{total} rows translated ({100 * done / max(total, 1):.1f}%)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--lang', default='fr')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('dump')
    p.add_argument('bank')
    p.add_argument('--all', action='store_true', help='also rows already translated')
    p = sub.add_parser('check')
    p.add_argument('banks', nargs='*')
    p.add_argument('--max', type=int, default=5, help='problems of each kind shown per bank')
    p = sub.add_parser('wrap')
    p.add_argument('bank')
    p.add_argument('--width', type=int)
    p.add_argument('--one-line', action='store_true',
                   help='also break rows that are one line in English (message boxes only)')
    p = sub.add_parser('stats')
    p.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    return {'dump': cmd_dump, 'check': cmd_check, 'wrap': cmd_wrap, 'stats': cmd_stats}[args.cmd](args) or 0


if __name__ == '__main__':
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # for `tl.py dump ... | head`
    sys.exit(main())
