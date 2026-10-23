#ifndef POKEHEARTGOLD_TEXT_LANG_H
#define POKEHEARTGOLD_TEXT_LANG_H

#ifdef TRANSLATED_TEXT
// Printing text again in the other language when a translated build's original text is
// asked for, or no longer (see text.c). The rest of the game tells it where text comes
// from and what happens to the windows it was printed in.

struct String;
struct Window;
struct TextOBJ;

u32 Text_Checksum(struct String *string);
BOOL TextLang_IsBusy(void);

void TextLang_NoteRead(struct String *string, u16 fileId, u16 msgNum);
BOOL TextLang_FindRead(struct String *string, u16 *fileId, u16 *msgNum, u8 *lang);
BOOL TextLang_BeginExpand(struct String *src);
void TextLang_ExpandField(u32 fieldno, u16 fileId, u16 msgNum, u8 lang, BOOL upperFirstChar, const u16 *text, u32 length);
void TextLang_EndExpand(struct String *dest);
void TextLang_NoteCopy(struct String *dest, const struct String *src);

void TextLang_NoteFill(struct Window *window, u8 fillValue, u32 x, u32 y, u32 width, u32 height, BOOL whole);
void TextLang_NoteFree(void *start, u32 size);
void TextLang_NoteTextOBJ(struct Window *window, struct TextOBJ *object);
void TextLang_ForgetTextOBJ(struct TextOBJ *object);

void TextLang_Update(void);
#endif

#endif // POKEHEARTGOLD_TEXT_LANG_H
