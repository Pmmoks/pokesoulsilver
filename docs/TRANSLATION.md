# Translating the game's text

`make TRANSLATION=FRENCH` (or `./scripts/build.sh GAME_VERSION=SOULSILVER TRANSLATION=FRENCH`)
builds `build/<game>.us.fr/poke<game>.us.fr.nds`. Its message archive (`a/0/2/7`) holds
French text, and the English original is kept as a second archive (`a/2/6/5`). While the
DS debug button is held, or while a frontend sets the byte `gMsgDataShowOriginal`, `src/msgdata.c`
reads from the original instead; `src/system.c` hides that button from the rest of the game.
Text already on screen is printed again in the other language when either changes
(`TextLang_Update` in `src/text.c`, called once a frame from `src/main.c`): text read
from the archive is noted with where it came from, so is text made from it by
`StringExpandPlaceholders` (with which names went into it) and copies of either, and
each print of such text is noted with its window. A message being printed carries on
from the same page, matched by the number of times the box was cleared and then
scrolled. A frontend can turn the pages of the last message through the byte
`gMsgDataPageTurn` (-1 back, 1 on); back from its first page it shows the messages
printed before it in the same box (the conversation, kept while the box is open or
opens again within three seconds), until the language changes or A or B is pressed.
Text that centres or moves its own lines (as the answer buttons do) is cleared across
the whole width of its lines before it is printed again. Text that the game shows as
sprites (`src/font_sprite.c`, such as the moves of a battle) is copied to its sprites again
once it is printed again. `src/heap.c` and the window fills in
`src/bg_window.c` tell it when text is gone. Without `TRANSLATION` the build is the
retail ROM as before.

## Where the text is

Each English bank `files/msgdata/msg/<bank>.gmm` can have a translation
`files/msgdata/fr/<bank>.tsv`:

```
# comment
0	SORTIE
6	Que voulez-vous faire de\nla photo choisie ?
```

One row per line: the row's index, a tab, and the text written as in a `.gmm` file
but without XML escaping (`&`, not `&amp;`). `\n` starts the second line of a box,
`\f` waits and scrolls up a line, `\r` waits and starts a new box; `{COMMANDS}`
(placeholders, colours, `{YESNO 0}`) must all be kept. Rows that are not listed
stay in English. At build time `tools/translation/translate_gmm.py` writes the
French banks to `files/msgdata/msg_fr/` and msgenc encodes them.

The Easy Chat word picker lists the words by initial in its ABC mode, from tables
in `src/easy_chat_menu.c` that follow the English spelling. A translation build
replaces them with `files/msgdata/easy_chat_initials.h`, which
`tools/translation/easy_chat_initials.py` writes from the translated words.

Some banks come from the game itself: the US Pokédex can show its entries in other
languages, so species names, entries and categories are copied from its French
banks (`msg_0818`, `msg_0806`, `msg_0824`), and heights and weights are written in
metric from `zukan_data.json`.

## Tools

```sh
python3 tools/translation/tl.py dump 3        # English rows of msg_0003 still to translate
python3 tools/translation/tl.py check         # problems in every translated bank
python3 tools/translation/tl.py wrap 96       # re-break lines of msg_0096 that are too wide
python3 tools/translation/tl.py wrap 96 --one-line   # also rows that are one line in English
python3 tools/translation/tl.py stats         # progress
```

`check` measures lines with the game's font (`font_00000000.bin`) and compares
them with the widest English line of the bank, since the game does not wrap
text itself; placeholders count as a 60-pixel name. It also reports characters
missing from `charmap.txt`, commands that differ from the English row, and names
longer than the buffers they are read into (species names: 10 characters, other
names: 15).
