#include "text.h"

#include "global.h"

#include "font.h"
#include "gf_gfx_loader.h"
#include "pm_string.h"
#include "string_control_code.h"
#include "sys_task.h"
#ifdef TRANSLATED_TEXT
#include "msgdata/msg.naix"

#include "msgdata.h"
#include "text_lang.h"
#endif

static const struct FontInfo *sFonts;

static u8 _021D1F6C;
static u16 sFgColor, sShadowColor, sBgColor;
static SysTask *sTextPrinterTasks[MAX_TEXT_PRINTERS];
static u16 sFontHalfRowLookupTable[4 * 4 * 4 * 4];

static u8 CreateTextPrinterSysTask(SysTaskFunc taskFunc, TextPrinter *printer, u32 priority);
static BOOL TextPrinterSysTaskIsActive(u8 printerId);
static u8 AddTextPrinter(TextPrinterTemplate *template, u32 speed, PrinterCallback_t callback);
static void RunTextPrinter(SysTask *task, TextPrinter *printer);
static RenderResult RenderFont(TextPrinter *printer);
static void TextPrinter_InitFocusIndicatorGfx(TextPrinter *printer);
static u16 *LoadScreenFocusIndicatorGraphics(void);
static void TextPrinter_FreeFocusIndicatorGfx(TextPrinter *printer);
#ifdef TRANSLATED_TEXT
static void TextLang_NotePrint(const TextPrinterTemplate *template, u8 printerId);
static void TextLang_NotePrinterEnd(TextPrinter *printer);
#endif

void SetFontsPointer(const struct FontInfo *fonts) {
    sFonts = fonts;
}

static u8 CreateTextPrinterSysTask(SysTaskFunc taskFunc, TextPrinter *printer, u32 priority) {
    int i;
    for (i = 0; i < MAX_TEXT_PRINTERS; i++) {
        if (sTextPrinterTasks[i] != NULL) {
            continue;
        }

        sTextPrinterTasks[i] = SysTask_CreateOnPrintQueue(taskFunc, printer, priority);
        if (sTextPrinterTasks[i] == NULL) {
            i = MAX_TEXT_PRINTERS;
        }
        break;
    }
    return i;
}

static void DestroyTextPrinterSysTask(u8 printerId) {
    GF_ASSERT(printerId < MAX_TEXT_PRINTERS);
    GF_ASSERT(sTextPrinterTasks[printerId] != NULL);
    if (printerId >= MAX_TEXT_PRINTERS || sTextPrinterTasks[printerId] == NULL) {
        return;
    }

    TextPrinter *printer = SysTask_GetData(sTextPrinterTasks[printerId]);
    if (printer != NULL) {
#ifdef TRANSLATED_TEXT
        TextLang_NotePrinterEnd(printer);
#endif
        TextPrinter_FreeFocusIndicatorGfx(printer);
        Heap_Free(printer);
    }

    SysTask_Destroy(sTextPrinterTasks[printerId]);
    sTextPrinterTasks[printerId] = NULL;
}

static BOOL TextPrinterSysTaskIsActive(u8 printerId) {
    return sTextPrinterTasks[printerId] != NULL;
}

void ResetAllTextPrinters(void) {
    for (int i = 0; i < MAX_TEXT_PRINTERS; i++) {
        sTextPrinterTasks[i] = NULL;
    }
}

u8 TextPrinterCheckActive(u8 printerId) {
    return TextPrinterSysTaskIsActive(printerId);
}

void RemoveTextPrinter(u8 printerId) {
    DestroyTextPrinterSysTask(printerId);
}

u8 AddTextPrinterParameterized(Window *window, FontID fontId, String *string, u32 x, u32 y, u32 textSpeed, PrinterCallback_t callback) {
    TextPrinterTemplate template;

    template.currentChar.wrapped = string;
    template.window = window;
    template.fontId = fontId;
    template.x = x;
    template.y = y;
    template.currentX = x;
    template.currentY = y;
    template.letterSpacing = sFonts[fontId].letterSpacing;
    template.lineSpacing = sFonts[fontId].lineSpacing;
    template.unk14 = sFonts[fontId].unk;
    template.fgColor = sFonts[fontId].fgColor;
    template.bgColor = sFonts[fontId].bgColor;
    template.shadowColor = sFonts[fontId].shadowColor;
    template.glyphTable = 0;
    template.unk1A = 0;
    template.savedColor = 0xFF;

    return AddTextPrinter(&template, textSpeed, callback);
}

u8 AddTextPrinterParameterizedWithColor(Window *window, FontID fontId, String *string, u32 x, u32 y, u32 textSpeed, u32 color, PrinterCallback_t callback) {
    TextPrinterTemplate template;

    template.currentChar.wrapped = string;
    template.window = window;
    template.fontId = fontId;
    template.x = x;
    template.y = y;
    template.currentX = x;
    template.currentY = y;
    template.letterSpacing = sFonts[fontId].letterSpacing;
    template.lineSpacing = sFonts[fontId].lineSpacing;
    template.unk14 = sFonts[fontId].unk;
    template.fgColor = (color >> 16) & 0xFF;
    template.shadowColor = (color >> 8) & 0xFF;
    template.bgColor = (color >> 0) & 0xFF;
    template.glyphTable = 0;
    template.unk1A = 0;
    template.savedColor = 0xFF;

    return AddTextPrinter(&template, textSpeed, callback);
}

u8 AddTextPrinterParameterizedWithColorAndSpacing(Window *window, int fontId, String *string, u32 x, u32 y, u32 textSpeed, u32 color, u32 letterSpacing, u32 lineSpacing, PrinterCallback_t callback) {
    TextPrinterTemplate template;

    template.currentChar.wrapped = string;
    template.window = window;
    template.fontId = fontId;
    template.x = x;
    template.y = y;
    template.currentX = x;
    template.currentY = y;
    template.letterSpacing = letterSpacing;
    template.lineSpacing = lineSpacing;
    template.unk14 = sFonts[fontId].unk;
    template.fgColor = (color >> 16) & 0xFF;
    template.shadowColor = (color >> 8) & 0xFF;
    template.bgColor = (color >> 0) & 0xFF;
    template.glyphTable = 0;
    template.unk1A = 0;
    template.savedColor = 0xFF;

    return AddTextPrinter(&template, textSpeed, callback);
}

static u8 AddTextPrinter(TextPrinterTemplate *template, u32 speed, PrinterCallback_t callback) {
    if (sFonts == NULL) {
        return 0xFF;
    }

    TextPrinter *printer = Heap_Alloc(HEAP_ID_DEFAULT, sizeof(TextPrinter));
    printer->active = TRUE;
    printer->state = 0;
    printer->textSpeedBottom = speed;
    printer->delayCounter = 0;
    printer->scrollDistance = 0;
    printer->callbackResult = 0;
    for (int i = 0; i < 7; i++) {
        printer->subStructFields[i] = 0;
    }
    printer->template = *template;
    printer->template.currentChar.raw = String_cstr(printer->template.currentChar.wrapped);
    printer->callback = callback;
    _021D1F6C = 0;

    TextPrinter_InitFocusIndicatorGfx(printer);

    if (speed != TEXT_SPEED_NOTRANSFER && speed != TEXT_SPEED_INSTANT) {
        printer->textSpeedBottom--;
        printer->textSpeedTop = 1;
        printer->id = CreateTextPrinterSysTask((SysTaskFunc)RunTextPrinter, printer, 1);
#ifdef TRANSLATED_TEXT
        TextLang_NotePrint(template, printer->id);
#endif
        return printer->id;
    }
#ifdef TRANSLATED_TEXT
    TextLang_NotePrint(template, MAX_TEXT_PRINTERS);
#endif

    u32 i = 0;

    printer->textSpeedBottom = 0;
    printer->textSpeedTop = 0;

    GenerateFontHalfRowLookupTable(template->fgColor, template->bgColor, template->shadowColor);

    for (; i < 0x400; i++) {
        if (RenderFont(printer) == RENDER_FINISH) {
            break;
        }
    }

    if (speed != TEXT_SPEED_NOTRANSFER) {
        CopyWindowToVram(printer->template.window);
    }

    TextPrinter_FreeFocusIndicatorGfx(printer);
    Heap_Free(printer);

    return MAX_TEXT_PRINTERS;
}

static void RunTextPrinter(SysTask *task, TextPrinter *printer) {
    if (_021D1F6C != 0) {
        return;
    }

    if (printer->callbackResult == 0) {
        printer->callbackParam = 0;

        GenerateFontHalfRowLookupTable(printer->template.fgColor, printer->template.bgColor, printer->template.shadowColor);

        switch (RenderFont(printer)) {
        case RENDER_PRINT:
            CopyWindowToVram(printer->template.window);
            // fallthrough
        case RENDER_UPDATE:
            if (printer->callback != NULL) {
                printer->callbackResult = printer->callback(&printer->template, printer->callbackParam);
            }
            return;
        case RENDER_FINISH:
            DestroyTextPrinterSysTask(printer->id);
            return;
        }
    } else {
        printer->callbackResult = printer->callback(&printer->template, printer->callbackParam);
    }
}

static RenderResult RenderFont(TextPrinter *printer) {
    RenderResult result;
    do {
        result = FontID_RenderText(printer->template.fontId, printer);
    } while (result == RENDER_REPEAT);
    return result;
}

void GenerateFontHalfRowLookupTable(u8 fgColor, u8 bgColor, u8 shadowColor) {
    u32 colors[4];
    colors[0] = 0;
    colors[1] = fgColor;
    colors[2] = shadowColor;
    colors[3] = bgColor;

    sBgColor = bgColor;
    sFgColor = fgColor;
    sShadowColor = shadowColor;

    u32 index = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                for (int l = 0; l < 4; l++) {
                    sFontHalfRowLookupTable[index++] = (colors[l] << 12) | (colors[k] << 8) | (colors[j] << 4) | colors[i];
                }
            }
        }
    }
}

void DecompressGlyphTile(const u8 *src, u8 *dest) {
    const u16 *src16 = (u16 *)src;
    u16 *dest16 = (u16 *)dest;
    dest16[0] = sFontHalfRowLookupTable[(u32)src16[0] >> 8];
    dest16[1] = sFontHalfRowLookupTable[(u32)src16[0] & 0xFF];
    dest16[2] = sFontHalfRowLookupTable[(u32)src16[1] >> 8];
    dest16[3] = sFontHalfRowLookupTable[(u32)src16[1] & 0xFF];
    dest16[4] = sFontHalfRowLookupTable[(u32)src16[2] >> 8];
    dest16[5] = sFontHalfRowLookupTable[(u32)src16[2] & 0xFF];
    dest16[6] = sFontHalfRowLookupTable[(u32)src16[3] >> 8];
    dest16[7] = sFontHalfRowLookupTable[(u32)src16[3] & 0xFF];
    dest16[8] = sFontHalfRowLookupTable[(u32)src16[4] >> 8];
    dest16[9] = sFontHalfRowLookupTable[(u32)src16[4] & 0xFF];
    dest16[10] = sFontHalfRowLookupTable[(u32)src16[5] >> 8];
    dest16[11] = sFontHalfRowLookupTable[(u32)src16[5] & 0xFF];
    dest16[12] = sFontHalfRowLookupTable[(u32)src16[6] >> 8];
    dest16[13] = sFontHalfRowLookupTable[(u32)src16[6] & 0xFF];
    dest16[14] = sFontHalfRowLookupTable[(u32)src16[7] >> 8];
    dest16[15] = sFontHalfRowLookupTable[(u32)src16[7] & 0xFF];
}

static void TextPrinter_InitFocusIndicatorGfx(TextPrinter *printer) {
    printer->focusIndicatorGfx = NULL;
}

static u16 *LoadScreenFocusIndicatorGraphics(void) {
    u16 *ret = Heap_Alloc(HEAP_ID_DEFAULT, 32 * 24 * sizeof(u16));

    NNSG2dCharacterData *g2dCharData;
    void *charData = GfGfxLoader_GetCharData(NARC_graphic_font, 6, FALSE, &g2dCharData, HEAP_ID_DEFAULT);
    MI_CpuCopy32(g2dCharData->pRawData, ret, 32 * 24 * sizeof(u16));
    Heap_Free(charData);

    return ret;
}

void RenderScreenFocusIndicatorTile(TextPrinter *printer, u32 unusedX, u32 unusedY, u16 fieldNum) {
    Window *window = printer->template.window;

    if (printer->focusIndicatorGfx == NULL) {
        printer->focusIndicatorGfx = LoadScreenFocusIndicatorGraphics();
    }

    u16 *startAddr = (void *)printer->focusIndicatorGfx + (fieldNum * (24 * 8 * sizeof(u16)));
    u16 destX = (GetWindowWidth(window) - 3) * 8;
    BlitBitmapRectToWindow(window, startAddr, 0, 0, 24, 32, destX, 0, 24, 32);
}

static void TextPrinter_FreeFocusIndicatorGfx(TextPrinter *printer) {
    if (printer->focusIndicatorGfx != NULL) {
        Heap_Free(printer->focusIndicatorGfx);
        printer->focusIndicatorGfx = NULL;
    }
}

#ifdef TRANSLATED_TEXT
// Printing text again in the other language, when a translated build's original text is
// asked for or no longer.
//
// Text read from the message archive is noted with where it came from, and so is text made
// from it by putting names into a message (for names read from the archive, which names),
// and copies of either. A print of such text is noted with its window and how it was
// printed. Once the language changes, each noted print still on screen is read again in
// the language now shown and printed over the old one: a message being printed goes on in
// the other language from the same page. The pages of the last message can also be turned
// back and forth (gMsgDataPageTurn).

#define TEXTLANG_NUM_READS    64
#define TEXTLANG_NUM_EXPANDED 16
#define TEXTLANG_NUM_PRINTS   24
#define TEXTLANG_NUM_FILLS    16
#define TEXTLANG_FIELDS       8
#define TEXTLANG_LITERAL      32
#define TEXTLANG_TEXT_LENGTH  1024
#define TEXTLANG_NAME_LENGTH  256
#define TEXTLANG_NO_FILE      0xFFFF
#define TEXTLANG_MIXED        0xFF

#define TEXTLANG_STRING_MAGIC 0xB6F8D2EC // as in pm_string.c

#define TEXTLANG_FIELD_USED   (1 << 0)
#define TEXTLANG_FIELD_SOURCE (1 << 1) // read again from fileId and msgNum
#define TEXTLANG_FIELD_UPPER  (1 << 2) // with the first letter in capitals

// What was put into a placeholder of a message.
typedef struct TextLangField {
    u16 fileId;
    u16 msgNum;
    u8 offset; // in literal, when it is kept as it is
    u8 length;
    u8 flags;
    u8 lang;
} TextLangField;

// Where text came from: a message, and for a message with placeholders, what was put in.
typedef struct TextLangSource {
    u16 fileId;
    u16 msgNum;
    u8 lang; // 0 translated, 1 original, TEXTLANG_MIXED for both
    u8 expanded;
    u8 literalLength;
    u8 padding;
    TextLangField fields[TEXTLANG_FIELDS];
    u16 literal[TEXTLANG_LITERAL];
} TextLangSource;

typedef struct TextLangRead {
    String *string;
    u32 checksum;
    u16 fileId;
    u16 msgNum;
    u8 lang;
} TextLangRead;

typedef struct TextLangExpanded {
    String *string;
    u32 checksum;
    TextLangSource source;
} TextLangExpanded;

typedef struct TextLangFill {
    Window *window;
    void *pixelBuffer;
    u16 x, y, width, height;
    u8 value;
    u8 used;
} TextLangFill;

typedef struct TextLangPrint {
    Window *window;
    void *pixelBuffer; // to tell when the window is no longer the same
    BgConfig *bgConfig;
    String *string; // printed from, while it is still there
    TextLangSource source;
    u32 age;
    u16 letterSpacing, lineSpacing;
    u16 width, height; // drawn; for paged text, to the end of the window
    s16 fill;          // the window's background around the text, or -1 if not known
    u16 baseTile;
    u8 used;
    u8 bgId, windowWidth, windowHeight;
    u8 fontId, x, y;
    u8 fgColor, bgColor, shadowColor, unk14;
    u8 printerId; // MAX_TEXT_PRINTERS once the printer has finished
    u8 paged;     // printed letter by letter, as messages are
    u8 centered;
    u8 lang;
    u8 screen, scroll; // the page shown: screens are cleared before, scrolls scroll up
} TextLangPrint;

static struct {
    TextLangRead reads[TEXTLANG_NUM_READS];
    TextLangExpanded expanded[TEXTLANG_NUM_EXPANDED];
    TextLangPrint prints[TEXTLANG_NUM_PRINTS];
    TextLangFill fills[TEXTLANG_NUM_FILLS];
    TextLangExpanded expanding;
    u32 age;
    u8 nextRead, nextExpanded, nextFill;
    u8 expandingValid;
    u8 busy;
} sTextLang;

static BOOL TextLang_InRam(const void *ptr) {
    return (u32)ptr >= HW_MAIN_MEM && (u32)ptr < HW_MAIN_MEM + 0x400000 && ((u32)ptr & 3) == 0;
}

static BOOL TextLang_StringIsValid(const String *string) {
    return TextLang_InRam(string) && string->magic == TEXTLANG_STRING_MAGIC && string->size < string->maxsize;
}

static u8 TextLang_Current(void) {
    return MsgData_ShowsOriginal() ? 1 : 0;
}

BOOL TextLang_IsBusy(void) {
    return sTextLang.busy;
}

// To tell whether a string still holds what was noted of it.
u32 Text_Checksum(String *string) {
    u32 sum = 0;
    int i;
    for (i = 0; i < string->size; i++) {
        sum = sum * 31 + string->data[i];
    }
    return sum + string->size;
}

static BOOL TextLang_StringMatches(String *string, String *noted, u32 checksum) {
    return string == noted && TextLang_StringIsValid(string) && Text_Checksum(string) == checksum;
}

static TextLangRead *TextLang_FindReadEntry(String *string) {
    int i;
    if (string == NULL) {
        return NULL;
    }
    for (i = 0; i < TEXTLANG_NUM_READS; i++) {
        TextLangRead *read = &sTextLang.reads[i];
        if (TextLang_StringMatches(string, read->string, read->checksum)) {
            return read;
        }
    }
    return NULL;
}

static TextLangExpanded *TextLang_FindExpandedEntry(String *string) {
    int i;
    if (string == NULL) {
        return NULL;
    }
    for (i = 0; i < TEXTLANG_NUM_EXPANDED; i++) {
        TextLangExpanded *expanded = &sTextLang.expanded[i];
        if (TextLang_StringMatches(string, expanded->string, expanded->checksum)) {
            return expanded;
        }
    }
    return NULL;
}

// Forgets what was noted of string, which now holds something else.
static void TextLang_ForgetString(String *string) {
    int i;
    for (i = 0; i < TEXTLANG_NUM_READS; i++) {
        if (sTextLang.reads[i].string == string) {
            sTextLang.reads[i].string = NULL;
        }
    }
    for (i = 0; i < TEXTLANG_NUM_EXPANDED; i++) {
        if (sTextLang.expanded[i].string == string) {
            sTextLang.expanded[i].string = NULL;
        }
    }
}

static void TextLang_AddRead(String *string, u16 fileId, u16 msgNum, u8 lang) {
    TextLangRead *read;
    TextLang_ForgetString(string);
    read = &sTextLang.reads[sTextLang.nextRead];
    sTextLang.nextRead = (sTextLang.nextRead + 1) % TEXTLANG_NUM_READS;
    read->string = string;
    read->checksum = Text_Checksum(string);
    read->fileId = fileId;
    read->msgNum = msgNum;
    read->lang = lang;
}

static void TextLang_AddExpanded(String *string, const TextLangSource *source) {
    TextLangExpanded *expanded;
    TextLang_ForgetString(string);
    expanded = &sTextLang.expanded[sTextLang.nextExpanded];
    sTextLang.nextExpanded = (sTextLang.nextExpanded + 1) % TEXTLANG_NUM_EXPANDED;
    expanded->string = string;
    expanded->checksum = Text_Checksum(string);
    expanded->source = *source;
}

// string was just read from message msgNum of file fileId of the message archive.
void TextLang_NoteRead(String *string, u16 fileId, u16 msgNum) {
    if (sTextLang.busy || !TextLang_StringIsValid(string)) {
        return;
    }
    TextLang_AddRead(string, fileId, msgNum, TextLang_Current());
}

BOOL TextLang_FindRead(String *string, u16 *fileId, u16 *msgNum, u8 *lang) {
    TextLangRead *read = TextLang_FindReadEntry(string);
    if (read == NULL) {
        return FALSE;
    }
    *fileId = read->fileId;
    *msgNum = read->msgNum;
    *lang = read->lang;
    return TRUE;
}

// Placeholders of src, a message read from the archive, are about to be filled in
// (StringExpandPlaceholders); returns whether to say what goes in them.
BOOL TextLang_BeginExpand(String *src) {
    TextLangSource *source = &sTextLang.expanding.source;
    TextLangRead *read;
    int i;
    sTextLang.expandingValid = FALSE;
    if (sTextLang.busy) {
        return FALSE;
    }
    read = TextLang_FindReadEntry(src);
    if (read == NULL) {
        return FALSE;
    }
    source->fileId = read->fileId;
    source->msgNum = read->msgNum;
    source->lang = read->lang;
    source->expanded = TRUE;
    source->literalLength = 0;
    for (i = 0; i < TEXTLANG_FIELDS; i++) {
        source->fields[i].flags = 0;
    }
    sTextLang.expandingValid = TRUE;
    return TRUE;
}

// text (length long) went into placeholder fieldno: the name in message msgNum of file
// fileId, or something to keep as it is when fileId is TEXTLANG_NO_FILE.
void TextLang_ExpandField(u32 fieldno, u16 fileId, u16 msgNum, u8 lang, BOOL upperFirstChar, const u16 *text, u32 length) {
    TextLangSource *source = &sTextLang.expanding.source;
    TextLangField *field;
    if (!sTextLang.expandingValid) {
        return;
    }
    if (fieldno >= TEXTLANG_FIELDS) {
        sTextLang.expandingValid = FALSE;
        return;
    }
    field = &source->fields[fieldno];
    if (field->flags & TEXTLANG_FIELD_USED) {
        return;
    }
    field->flags = TEXTLANG_FIELD_USED;
    if (fileId != TEXTLANG_NO_FILE) {
        field->flags |= TEXTLANG_FIELD_SOURCE;
        if (upperFirstChar) {
            field->flags |= TEXTLANG_FIELD_UPPER;
        }
        field->fileId = fileId;
        field->msgNum = msgNum;
        field->lang = lang;
        if (lang != source->lang) {
            source->lang = TEXTLANG_MIXED;
        }
        return;
    }
    if (source->literalLength + length > TEXTLANG_LITERAL) {
        sTextLang.expandingValid = FALSE;
        return;
    }
    field->offset = source->literalLength;
    field->length = length;
    MI_CpuCopy16(text, &source->literal[source->literalLength], length * sizeof(u16));
    source->literalLength += length;
}

void TextLang_EndExpand(String *dest) {
    if (sTextLang.expandingValid && TextLang_StringIsValid(dest)) {
        TextLang_AddExpanded(dest, &sTextLang.expanding.source);
    }
    sTextLang.expandingValid = FALSE;
}

// dest was just copied from src.
void TextLang_NoteCopy(String *dest, const String *src) {
    TextLangRead *read;
    TextLangExpanded *expanded;
    if (sTextLang.busy || dest == src) {
        return;
    }
    expanded = TextLang_FindExpandedEntry((String *)src);
    if (expanded != NULL) {
        TextLang_AddExpanded(dest, &expanded->source);
        return;
    }
    read = TextLang_FindReadEntry((String *)src);
    if (read != NULL) {
        TextLang_AddRead(dest, read->fileId, read->msgNum, read->lang);
    }
}

static BOOL TextLang_RectsMeet(u32 x1, u32 y1, u32 w1, u32 h1, u32 x2, u32 y2, u32 w2, u32 h2) {
    return x1 < x2 + w2 && x2 < x1 + w1 && y1 < y2 + h2 && y2 < y1 + h1;
}

// Whether window is another window drawn with the same tiles as print's.
static BOOL TextLang_SharesTiles(TextLangPrint *print, Window *window) {
    return print->window != window && print->bgConfig == window->bgConfig && print->bgId == window->bgId && print->bgId < GF_BG_LYR_MAX
        && print->baseTile < window->baseTile + window->width * window->height
        && window->baseTile < print->baseTile + print->windowWidth * print->windowHeight;
}

static BOOL TextLang_PrinterIsAlive(TextLangPrint *print) {
    return print->printerId < MAX_TEXT_PRINTERS && sTextPrinterTasks[print->printerId] != NULL;
}

// Part of window was filled with fillValue (all of it if whole): text printed there is gone,
// and text printed there later is on fillValue.
void TextLang_NoteFill(Window *window, u8 fillValue, u32 x, u32 y, u32 width, u32 height, BOOL whole) {
    TextLangFill *fill;
    int i;
    if (sTextLang.busy) {
        return;
    }
    if (whole) {
        x = 0;
        y = 0;
        width = window->width * 8;
        height = window->height * 8;
    }
    for (i = 0; i < TEXTLANG_NUM_PRINTS; i++) {
        TextLangPrint *print = &sTextLang.prints[i];
        // A message box is cleared between pages by its own printer.
        if (print->used && print->window == window && !TextLang_PrinterIsAlive(print)
            && TextLang_RectsMeet(x, y, width, height, print->x, print->y, print->width, print->height)) {
            print->used = FALSE;
        }
        if (print->used && TextLang_SharesTiles(print, window)) {
            print->used = FALSE;
        }
    }
    fill = &sTextLang.fills[sTextLang.nextFill];
    sTextLang.nextFill = (sTextLang.nextFill + 1) % TEXTLANG_NUM_FILLS;
    fill->window = window;
    fill->pixelBuffer = window->pixelBuffer;
    fill->x = x;
    fill->y = y;
    fill->width = width;
    fill->height = height;
    fill->value = fillValue;
    fill->used = TRUE;
}

static BOOL TextLang_InBlock(const void *ptr, const u8 *start, u32 size) {
    return (const u8 *)ptr >= start && (const u8 *)ptr < start + size;
}

// The memory from start (size bytes) was freed: windows, pictures and text in it are gone.
void TextLang_NoteFree(void *start, u32 size) {
    int i;
    for (i = 0; i < TEXTLANG_NUM_PRINTS; i++) {
        TextLangPrint *print = &sTextLang.prints[i];
        if (!print->used) {
            continue;
        }
        if (TextLang_InBlock(print->window, start, size) || TextLang_InBlock(print->pixelBuffer, start, size) || TextLang_InBlock(print->bgConfig, start, size)) {
            print->used = FALSE;
        } else if (TextLang_InBlock(print->string, start, size)) {
            print->string = NULL;
        }
    }
    for (i = 0; i < TEXTLANG_NUM_FILLS; i++) {
        TextLangFill *fill = &sTextLang.fills[i];
        if (TextLang_InBlock(fill->window, start, size) || TextLang_InBlock(fill->pixelBuffer, start, size)) {
            fill->used = FALSE;
        }
    }
    for (i = 0; i < TEXTLANG_NUM_READS; i++) {
        if (TextLang_InBlock(sTextLang.reads[i].string, start, size)) {
            sTextLang.reads[i].string = NULL;
        }
    }
    for (i = 0; i < TEXTLANG_NUM_EXPANDED; i++) {
        if (TextLang_InBlock(sTextLang.expanded[i].string, start, size)) {
            sTextLang.expanded[i].string = NULL;
        }
    }
}

static int TextLang_FindFill(Window *window, u32 x, u32 y) {
    int i, j;
    for (j = 1; j <= TEXTLANG_NUM_FILLS; j++) {
        TextLangFill *fill;
        i = (sTextLang.nextFill + TEXTLANG_NUM_FILLS - j) % TEXTLANG_NUM_FILLS;
        fill = &sTextLang.fills[i];
        if (fill->used && fill->window == window && fill->pixelBuffer == window->pixelBuffer
            && x >= fill->x && x < fill->x + fill->width && y >= fill->y && y < fill->y + fill->height) {
            return fill->value;
        }
    }
    return -1;
}

static u32 TextLang_LineHeight(u8 fontId, u16 lineSpacing) {
    return GetFontAttribute(fontId, 1) + lineSpacing;
}

static u32 TextLang_TextHeight(const u16 *text, u8 fontId, u16 lineSpacing) {
    u32 lines = 0;
    while (*text != EOS) {
        if (*text++ == CHAR_LF) {
            lines++;
        }
    }
    return lines * TextLang_LineHeight(fontId, lineSpacing) + GetFontAttribute(fontId, 1);
}

static BOOL TextLang_WindowIsSame(TextLangPrint *print) {
    Window *window = print->window;
    return TextLang_InRam(window) && window->pixelBuffer == print->pixelBuffer && window->pixelBuffer != NULL
        && window->bgConfig == print->bgConfig && window->bgId == print->bgId
        && window->width == print->windowWidth && window->height == print->windowHeight;
}

// A printer is about to print the text of template (as printerId, or MAX_TEXT_PRINTERS if
// all at once).
static void TextLang_NotePrint(const TextPrinterTemplate *template, u8 printerId) {
    String *string = template->currentChar.wrapped;
    Window *window = template->window;
    TextLangExpanded *expanded;
    TextLangRead *read;
    TextLangPrint *print;
    u32 width, height, windowWidth;
    BOOL paged = printerId < MAX_TEXT_PRINTERS;
    int i;

    if (sTextLang.busy || !TextLang_InRam(window) || window->pixelBuffer == NULL || !TextLang_StringIsValid(string)) {
        return;
    }
    windowWidth = window->width * 8;
    if (paged) {
        width = windowWidth > template->x ? windowWidth - template->x : 0;
        height = window->height * 8 > template->y ? window->height * 8 - template->y : 0;
    } else {
        width = FontID_String_GetWidthMultiline(template->fontId, string, template->letterSpacing);
        height = TextLang_TextHeight(String_cstr(string), template->fontId, template->lineSpacing);
    }

    // What this covers is no longer the text printed there before.
    for (i = 0; i < TEXTLANG_NUM_PRINTS; i++) {
        print = &sTextLang.prints[i];
        if (print->used && print->window == window && TextLang_RectsMeet(template->x, template->y, width, height, print->x, print->y, print->width, print->height)) {
            print->used = FALSE;
        }
        if (print->used && TextLang_SharesTiles(print, window)) {
            print->used = FALSE;
        }
    }

    expanded = TextLang_FindExpandedEntry(string);
    read = expanded == NULL ? TextLang_FindReadEntry(string) : NULL;
    if (expanded == NULL && read == NULL) {
        return;
    }

    print = &sTextLang.prints[0];
    for (i = 0; i < TEXTLANG_NUM_PRINTS; i++) {
        if (!sTextLang.prints[i].used) {
            print = &sTextLang.prints[i];
            break;
        }
        if (sTextLang.prints[i].age < print->age) {
            print = &sTextLang.prints[i];
        }
    }

    if (expanded != NULL) {
        print->source = expanded->source;
    } else {
        print->source.fileId = read->fileId;
        print->source.msgNum = read->msgNum;
        print->source.lang = read->lang;
        print->source.expanded = FALSE;
    }
    print->window = window;
    print->pixelBuffer = window->pixelBuffer;
    print->bgConfig = window->bgConfig;
    print->bgId = window->bgId;
    print->baseTile = window->baseTile;
    print->windowWidth = window->width;
    print->windowHeight = window->height;
    print->string = string;
    print->age = ++sTextLang.age;
    print->fontId = template->fontId;
    print->x = template->x;
    print->y = template->y;
    print->letterSpacing = template->letterSpacing;
    print->lineSpacing = template->lineSpacing;
    print->unk14 = template->unk14;
    print->fgColor = template->fgColor;
    print->bgColor = template->bgColor;
    print->shadowColor = template->shadowColor;
    print->width = width;
    print->height = height;
    print->printerId = printerId;
    print->paged = paged;
    print->centered = !paged && template->x > 0 && width < windowWidth && (int)(2 * template->x + width) - (int)windowWidth <= 2 && (int)windowWidth - (int)(2 * template->x + width) <= 2;
    if (paged || template->bgColor != 0) {
        print->fill = template->bgColor;
    } else {
        print->fill = TextLang_FindFill(window, template->x, template->y);
    }
    print->lang = print->source.lang;
    print->screen = 0;
    print->scroll = 0;
    print->used = TRUE;
}

// Where a printer next waits for a button, from text: returns where the page after it
// starts, or NULL if text has no more pages. *lineStart is where the line with the wait
// starts and *scroll whether the page after it scrolls up (else the box is cleared first).
static const u16 *TextLang_NextBreak(const u16 *text, const u16 **lineStart, BOOL *scroll) {
    *lineStart = text;
    while (*text != EOS) {
        u16 c = *text;
        if (c == CHAR_LF) {
            text++;
            *lineStart = text;
        } else if (c == 0x25BC || c == 0x25BD) {
            *scroll = c == 0x25BD;
            return text + 1;
        } else if (c == EXT_CTRL_CODE_BEGIN) {
            int code = (u16)MsgArray_GetControlCode(text);
            const u16 *next = MsgArray_SkipControlCode(text);
            if (code == 0x207 || code == 0x208) {
                *scroll = code == 0x208;
                if (*next == CHAR_LF) {
                    next++;
                }
                return next;
            }
            if (code == 0xFE06) {
                int field = MsgArray_ControlCodeGetField(text, 0);
                if (field == 0xFE00 || field == 0xFE01) {
                    *scroll = field == 0xFE00;
                    return next;
                }
            }
            text = next;
        } else if (c == 0xF0FD) {
            text += 2;
        } else {
            text++;
        }
    }
    return NULL;
}

static int TextLang_PageCount(const u16 *text) {
    const u16 *lineStart;
    BOOL scroll;
    int count = 1;
    while ((text = TextLang_NextBreak(text, &lineStart, &scroll)) != NULL) {
        count++;
    }
    return count;
}

// The page that a printer at position is on (waiting at its end if waiting).
static int TextLang_PageAt(const u16 *text, const u16 *position, BOOL waiting) {
    const u16 *lineStart;
    BOOL scroll;
    int page = 0;
    while ((text = TextLang_NextBreak(text, &lineStart, &scroll)) != NULL && text <= position) {
        page++;
    }
    if (waiting && page > 0) {
        page--;
    }
    return page;
}

static void TextLang_PageToScreen(const u16 *text, int page, u8 *screen, u8 *scrollNum) {
    const u16 *lineStart;
    BOOL scroll;
    int i;
    *screen = 0;
    *scrollNum = 0;
    for (i = 0; i < page && (text = TextLang_NextBreak(text, &lineStart, &scroll)) != NULL; i++) {
        if (scroll) {
            (*scrollNum)++;
        } else {
            (*screen)++;
            *scrollNum = 0;
        }
    }
}

// The page that is scrollNum scrolls into screen (cleared box) screen, or the nearest one
// that text has: messages in the two languages are cleared as often, but not always
// scrolled as often.
static int TextLang_ScreenToPage(const u16 *text, int screen, int scrollNum) {
    const u16 *start = text;
    const u16 *lineStart;
    BOOL scroll;
    int screens = 1, page = 0, found = 0, curScreen = 0, curScroll = 0;
    while ((text = TextLang_NextBreak(text, &lineStart, &scroll)) != NULL) {
        if (!scroll) {
            screens++;
        }
    }
    if (screen >= screens) {
        screen = screens - 1;
    }
    text = start;
    while ((text = TextLang_NextBreak(text, &lineStart, &scroll)) != NULL) {
        page++;
        if (scroll) {
            curScroll++;
        } else {
            curScreen++;
            curScroll = 0;
        }
        if (curScreen == screen && curScroll <= scrollNum) {
            found = page;
        }
    }
    return found;
}

// Where page starts in text, and where to start printing to show it: the line before, for
// a page that scrolled up into the box.
static const u16 *TextLang_PageStart(const u16 *text, int page, const u16 **displayStart) {
    const u16 *lineStart, *next;
    BOOL scroll;
    int i;
    *displayStart = text;
    for (i = 0; i < page; i++) {
        next = TextLang_NextBreak(text, &lineStart, &scroll);
        if (next == NULL) {
            break;
        }
        *displayStart = scroll ? lineStart : next;
        text = next;
    }
    return text;
}

static void TextLang_FillPage(TextLangPrint *print, u32 x, u32 width) {
    if (print->fill >= 0) {
        FillWindowPixelRect(print->window, print->fill, x, print->y, width, print->height);
    }
}

// Prints page of text with printer as print was printed (at x), all at once: up to where
// the page waits for a button (where printer is left, waiting) or the end of text (TRUE).
static BOOL TextLang_ShowPage(TextPrinter *printer, TextLangPrint *print, const u16 *text, int page, u32 x) {
    const u16 *displayStart;
    const u16 *pageStart = TextLang_PageStart(text, page, &displayStart);
    u8 speed = printer->textSpeedBottom;
    BOOL finished = FALSE;
    BOOL pageEnd = FALSE;
    int i;

    printer->template.currentChar.raw = displayStart;
    printer->template.window = print->window;
    printer->template.fontId = print->fontId;
    printer->template.x = x;
    printer->template.y = print->y;
    printer->template.currentX = x;
    printer->template.currentY = print->y;
    printer->template.letterSpacing = print->letterSpacing;
    printer->template.lineSpacing = print->lineSpacing;
    printer->template.unk14 = print->unk14;
    printer->template.fgColor = print->fgColor;
    printer->template.bgColor = print->bgColor;
    printer->template.shadowColor = print->shadowColor;
    printer->template.glyphTable = 0;
    printer->template.unk1A = 0;
    printer->template.savedColor = 0xFF;
    printer->state = 0;
    printer->delayCounter = 0;
    printer->scrollDistance = 0;
    printer->callbackResult = 0;
    printer->callbackParam = 0;

    // Without the delay between letters, and without the sounds and pauses the text asks
    // for, which were played the first time.
    printer->textSpeedBottom = 0;
    GenerateFontHalfRowLookupTable(printer->template.fgColor, printer->template.bgColor, printer->template.shadowColor);
    for (i = 0; i < 0x800 && !pageEnd; i++) {
        if (RenderFont(printer) == RENDER_FINISH) {
            finished = TRUE;
            break;
        }
        switch (printer->state) {
        case 1:
        case 5:
        case 6:
            printer->state = 0;
            printer->delayCounter = 0;
            break;
        case 2:
        case 7:
        case 3:
        case 8:
            if (printer->template.currentChar.raw > pageStart) {
                pageEnd = TRUE;
                break;
            }
            // The page before, of which the line that scrolls up into the box is shown.
            printer->template.currentX = x;
            if (printer->state == 3 || printer->state == 8) {
                printer->template.currentY += TextLang_LineHeight(print->fontId, print->lineSpacing);
            } else {
                TextLang_FillPage(print, print->x, print->width);
                printer->template.currentY = print->y;
            }
            printer->state = 0;
            break;
        }
    }
    printer->textSpeedBottom = speed;
    printer->callbackParam = 0;
    return finished;
}

static void TextLang_CopyToVram(TextLangPrint *print) {
    BgConfig *bgConfig = print->bgConfig;
    if (TextLang_InRam(bgConfig) && print->bgId < GF_BG_LYR_MAX && bgConfig->bgs[print->bgId].mode == GF_BG_TYPE_TEXT && bgConfig->bgs[print->bgId].tileSize != 0) {
        CopyWindowPixelsToVram_TextMode(print->window);
    }
}

static BOOL TextLang_ReadMessage(u16 fileId, u16 msgNum, String *dest) {
    MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, fileId, HEAP_ID_DEFAULT);
    String_SetEmpty(dest);
    if (msgData == NULL) {
        return FALSE;
    }
    ReadMsgDataIntoString(msgData, msgNum, dest);
    DestroyMsgData(msgData);
    return TRUE;
}

// Reads the text of source again, in the language now shown, into dest.
static BOOL TextLang_ReadAgain(const TextLangSource *source, String *dest) {
    String *message, *name;
    const u16 *c;
    if (!source->expanded) {
        return TextLang_ReadMessage(source->fileId, source->msgNum, dest);
    }
    message = String_New(TEXTLANG_TEXT_LENGTH, HEAP_ID_DEFAULT);
    name = String_New(TEXTLANG_NAME_LENGTH, HEAP_ID_DEFAULT);
    if (message == NULL || name == NULL || !TextLang_ReadMessage(source->fileId, source->msgNum, message)) {
        if (message != NULL) {
            String_Delete(message);
        }
        if (name != NULL) {
            String_Delete(name);
        }
        return FALSE;
    }
    String_SetEmpty(dest);
    c = String_cstr(message);
    while (*c != EOS) {
        if (*c == EXT_CTRL_CODE_BEGIN && MsgArray_ControlCodeIsStrVar(c)) {
            u32 fieldno = MsgArray_ControlCodeGetField(c, 0);
            const TextLangField *field = &source->fields[fieldno];
            int i;
            c = MsgArray_SkipControlCode(c);
            if (fieldno >= TEXTLANG_FIELDS || !(field->flags & TEXTLANG_FIELD_USED)) {
                continue; // not in the message as first printed
            }
            if (field->flags & TEXTLANG_FIELD_SOURCE) {
                TextLang_ReadMessage(field->fileId, field->msgNum, name);
                if (field->flags & TEXTLANG_FIELD_UPPER) {
                    String_UpperCharN(name, 0);
                }
                String_Cat_HandleTrainerName(dest, name);
            } else {
                for (i = 0; i < field->length; i++) {
                    String_AddChar(dest, source->literal[field->offset + i]);
                }
            }
        } else if (*c == EXT_CTRL_CODE_BEGIN) {
            const u16 *end = MsgArray_SkipControlCode(c);
            while (c < end) {
                String_AddChar(dest, *c++);
            }
        } else {
            String_AddChar(dest, *c++);
        }
    }
    String_Delete(message);
    String_Delete(name);
    return TRUE;
}

static TextPrinter *TextLang_GetPrinter(TextLangPrint *print) {
    TextPrinter *printer;
    const u16 *start;
    if (!TextLang_PrinterIsAlive(print)) {
        print->printerId = MAX_TEXT_PRINTERS;
        return NULL;
    }
    printer = SysTask_GetData(sTextPrinterTasks[print->printerId]);
    if (printer == NULL || printer->template.window != print->window || print->string == NULL || !TextLang_StringIsValid(print->string)) {
        return NULL;
    }
    start = String_cstr(print->string);
    if (printer->template.currentChar.raw < start || printer->template.currentChar.raw > start + print->string->size) {
        return NULL;
    }
    return printer;
}

static BOOL TextLang_IsWaiting(TextPrinter *printer) {
    return printer->state == 2 || printer->state == 3 || printer->state == 7 || printer->state == 8;
}

// Prints print again in the language now shown (lang), from the same page or pageTurn
// pages on.
static void TextLang_Reprint(TextLangPrint *print, u8 lang, int pageTurn) {
    TextPrinter *printer = TextLang_GetPrinter(print);
    String *text = NULL;
    const u16 *chars;
    int page, pageCount;
    BOOL finished;

    sTextLang.busy = TRUE;
    if (!FontID_IsLoaded(print->fontId) || (!print->paged && print->fill < 0)) {
        goto done;
    }
    if (printer == NULL && TextLang_PrinterIsAlive(print)) {
        goto done; // its printer prints something else now
    }
    if (printer != NULL) {
        const u16 *shown = String_cstr(print->string);
        TextLang_PageToScreen(shown, TextLang_PageAt(shown, printer->template.currentChar.raw, TextLang_IsWaiting(printer)), &print->screen, &print->scroll);
    }
    if (printer == NULL || print->lang != lang) {
        text = String_New(TEXTLANG_TEXT_LENGTH, HEAP_ID_DEFAULT);
        if (text == NULL || !TextLang_ReadAgain(&print->source, text) || text->size == 0) {
            goto done;
        }
        if (printer != NULL && text->size >= print->string->maxsize) {
            goto done; // it does not fit where the game keeps the message
        }
    }
    chars = String_cstr(text != NULL ? text : print->string);

    page = TextLang_ScreenToPage(chars, print->screen, print->scroll);
    if (pageTurn != 0) {
        pageCount = TextLang_PageCount(chars);
        if (page + pageTurn < 0 || page + pageTurn >= pageCount) {
            goto done;
        }
        page += pageTurn;
        TextLang_PageToScreen(chars, page, &print->screen, &print->scroll);
    }

    if (printer != NULL) {
        if (text != NULL) {
            String_Copy(print->string, text);
        }
        TextLang_FillPage(print, print->x, print->width);
        finished = TextLang_ShowPage(printer, print, String_cstr(print->string), page, print->x);
        TextLang_CopyToVram(print);
        if (finished) {
            DestroyTextPrinterSysTask(print->printerId);
            print->printerId = MAX_TEXT_PRINTERS;
        }
    } else {
        TextPrinter *tempPrinter = Heap_Alloc(HEAP_ID_DEFAULT, sizeof(TextPrinter));
        u32 x = print->x;
        u32 width = print->width;
        u32 left = print->x;
        u32 right = print->x + print->width;
        if (tempPrinter == NULL) {
            goto done;
        }
        MI_CpuClear8(tempPrinter, sizeof(TextPrinter));
        tempPrinter->id = MAX_TEXT_PRINTERS;
        if (!print->paged) {
            u32 windowWidth = print->windowWidth * 8;
            u32 height = TextLang_TextHeight(chars, print->fontId, print->lineSpacing);
            width = FontID_String_GetWidthMultiline(print->fontId, text, print->letterSpacing);
            if (print->centered && width < windowWidth) {
                x = (windowWidth - width) / 2;
            }
            if (x < left) {
                left = x;
            }
            if (x + width > right) {
                right = x + width;
            }
            if (height > print->height) {
                print->height = height;
            }
        }
        TextLang_FillPage(print, left, right - left);
        TextLang_ShowPage(tempPrinter, print, chars, page, x);
        TextPrinter_FreeFocusIndicatorGfx(tempPrinter);
        Heap_Free(tempPrinter);
        TextLang_CopyToVram(print);
        if (!print->paged) {
            print->x = x;
            print->width = width;
        }
    }

done:
    print->lang = lang;
    if (text != NULL) {
        String_Delete(text);
    }
    sTextLang.busy = FALSE;
}

// A printer is about to go: its message stays on the page it was on.
static void TextLang_NotePrinterEnd(TextPrinter *printer) {
    int i;
    for (i = 0; i < TEXTLANG_NUM_PRINTS; i++) {
        TextLangPrint *print = &sTextLang.prints[i];
        if (print->used && print->printerId == printer->id) {
            if (!sTextLang.busy && TextLang_GetPrinter(print) == printer) {
                const u16 *shown = String_cstr(print->string);
                TextLang_PageToScreen(shown, TextLang_PageAt(shown, printer->template.currentChar.raw, TextLang_IsWaiting(printer)), &print->screen, &print->scroll);
            }
            print->printerId = MAX_TEXT_PRINTERS;
        }
    }
}

// Once a frame: prints text again where it is not in the language now shown, and turns
// the pages of the last message as asked (gMsgDataPageTurn).
void TextLang_Update(void) {
    u8 lang = TextLang_Current();
    int pageTurn = gMsgDataPageTurn;
    TextLangPrint *last = NULL;
    int i;

    gMsgDataPageTurn = 0;
    if (sFonts == NULL) {
        return;
    }
    for (i = 0; i < TEXTLANG_NUM_PRINTS; i++) {
        TextLangPrint *print = &sTextLang.prints[i];
        if (!print->used) {
            continue;
        }
        if (!TextLang_WindowIsSame(print)) {
            print->used = FALSE;
            continue;
        }
        if (print->lang != lang) {
            TextLang_Reprint(print, lang, 0);
        }
        if (print->paged && (last == NULL || print->age > last->age)) {
            last = print;
        }
    }
    if (pageTurn != 0 && last != NULL) {
        TextLang_Reprint(last, lang, pageTurn);
    }
}
#endif
