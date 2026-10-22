#include "text.h"

#include "global.h"

#include "font.h"
#include "gf_gfx_loader.h"
#include "pm_string.h"
#include "string_control_code.h"
#include "sys_task.h"

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
        return printer->id;
    }

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
// Printing a message again in the other language, when a translated build's original
// text is asked for or no longer, from the page the player is on.

// The start of the page after the one text is on (where a printer waits for a button
// before going on), or NULL on the last page.
static const u16 *Text_NextPage(const u16 *text) {
    while (*text != EOS) {
        u16 c = *text;
        if (c == 0x25BC || c == 0x25BD) {
            return text + 1;
        }
        if (c == EXT_CTRL_CODE_BEGIN) {
            int code = (u16)MsgArray_GetControlCode(text);
            BOOL pageEnd = code == 0x207 || code == 0x208;
            if (code == 0xFE06) {
                int field = MsgArray_ControlCodeGetField(text, 0);
                pageEnd = field == 0xFE00 || field == 0xFE01;
            }
            text = MsgArray_SkipControlCode(text);
            if (pageEnd) {
                if ((code == 0x207 || code == 0x208) && *text == CHAR_LF) {
                    text++;
                }
                return text;
            }
        } else if (c == 0xF0FD) {
            text += 2;
        } else {
            text++;
        }
    }
    return NULL;
}

// The page of string that the printer is printing (from 0), or -1 if it has finished or
// prints other text.
int TextPrinter_GetPage(u8 printerId, String *string) {
    if (printerId >= MAX_TEXT_PRINTERS || sTextPrinterTasks[printerId] == NULL) {
        return -1;
    }
    TextPrinter *printer = SysTask_GetData(sTextPrinterTasks[printerId]);
    const u16 *start = String_cstr(string);
    const u16 *position = printer->template.currentChar.raw;
    if (position < start || position > start + String_GetLength(string)) {
        return -1;
    }
    const u16 *next;
    int page = 0;
    for (next = Text_NextPage(start); next != NULL && next <= position; next = Text_NextPage(next)) {
        page++;
    }
    // Waiting for a button at the end of a page, it is past the code that ends the page.
    if (printer->state == 2 || printer->state == 3 || printer->state == 7 || printer->state == 8) {
        page--;
    }
    return page;
}

// Prints string from its start in place of what the printer was printing, keeping its
// number, window, speed and callback: the first page at once, the rest as before. For
// printers that AddTextPrinterParameterized started (in the font's own colours).
void TextPrinter_Reprint(u8 printerId, String *string) {
    TextPrinter *printer = SysTask_GetData(sTextPrinterTasks[printerId]);
    const struct FontInfo *font = &sFonts[printer->template.fontId];
    u8 speed = printer->textSpeedBottom;
    int i;

    printer->template.currentChar.raw = String_cstr(string);
    printer->template.currentX = printer->template.x;
    printer->template.currentY = printer->template.y;
    printer->template.fgColor = font->fgColor;
    printer->template.bgColor = font->bgColor;
    printer->template.shadowColor = font->shadowColor;
    printer->template.glyphTable = 0;
    printer->template.unk1A = 0;
    printer->template.savedColor = 0xFF;
    printer->state = 0;
    printer->delayCounter = 0;
    printer->scrollDistance = 0;
    printer->callbackResult = 0;
    printer->callbackParam = 0;

    // Up to the end of the page (or a pause), without the delay between letters and without
    // the sounds the text asks for, which were played the first time.
    printer->textSpeedBottom = 0;
    GenerateFontHalfRowLookupTable(printer->template.fgColor, printer->template.bgColor, printer->template.shadowColor);
    for (i = 0; i < 0x400 && printer->state == 0; i++) {
        if (RenderFont(printer) == RENDER_FINISH) {
            CopyWindowToVram(printer->template.window);
            DestroyTextPrinterSysTask(printerId);
            return;
        }
    }
    printer->textSpeedBottom = speed;
    printer->callbackParam = 0;
    CopyWindowToVram(printer->template.window);
}

// Leaves out the first pages of string (all but the last if it has fewer); returns how
// many were left out.
int Text_DropPages(String *string, int pages) {
    const u16 *start = String_cstr(string);
    int dropped;
    for (dropped = 0; dropped < pages; dropped++) {
        const u16 *next = Text_NextPage(start);
        if (next == NULL) {
            break;
        }
        start = next;
    }
    if (dropped > 0) {
        CopyU16ArrayToString(string, start);
    }
    return dropped;
}

// To tell whether a message buffer still holds the message that was printed from it.
u32 Text_Checksum(String *string) {
    const u16 *c = String_cstr(string);
    u32 sum = 0;
    while (*c != EOS) {
        sum = sum * 31 + *c++;
    }
    return sum;
}
#endif
