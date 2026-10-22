#include "msgdata.h"

#include "global.h"

#include "msgdata/msg.naix"

#include "heap.h"
#include "message_format.h"

static void CopyEncryptedMessage16(u16 *dest, const u16 *src, MAT_ENTRY *alloc);
static void ReadMsgData_ExistingNarc_ExistingString(NARC *narc, u32 group, u32 num, enum HeapID heapID, String *dest);
static String *ReadMsgData_ExistingNarc_NewString(NARC *narc, u32 group, u32 num, enum HeapID heapID);

#ifdef TRANSLATED_TEXT
// Nonzero while the original text is asked for. A frontend sets it every frame,
// finding it by name in the linked program; the debug button does the same on
// hardware that has one.
u8 gMsgDataShowOriginal;

BOOL MsgData_ShowsOriginal(void) {
    return gMsgDataShowOriginal != 0 || (PAD_Read() & PAD_BUTTON_DEBUG) != 0;
}

// NARC_msgdata_msg holds the translated text and NARC_msgdata_msg_orig the
// original English text, which is read instead while it is asked for.
static NarcId MsgData_GetLanguageNarc(NarcId narcId) {
    if (narcId == NARC_msgdata_msg && MsgData_ShowsOriginal()) {
        return NARC_msgdata_msg_orig;
    }
    return narcId;
}

// A MsgData keeps the language it was loaded in. Once the button changes the
// language, its messages are read from the other archive instead.
static BOOL MsgData_LanguageChanged(MsgData *msgData, NarcId *narcId) {
    *narcId = MsgData_GetLanguageNarc((NarcId)msgData->narc_id);
    return *narcId != msgData->loadedNarcId;
}
#else
#define MsgData_GetLanguageNarc(narcId) (narcId)
#endif

static void *LoadSingleElementFromNarc(NarcId narcId, s32 fileId, enum HeapID heapID) {
    return AllocAndReadWholeNarcMemberByIdPair(narcId, fileId, heapID);
}

static void FreeMsgDataRawData(void *data) {
    Heap_Free(data);
}

inline static void Decrypt1(MAT_ENTRY *entry, u32 num, u32 seed) {
    seed = seed * 765 * (num + 1) & 0xffff;
    seed |= seed << 16;
    entry->offset ^= seed;
    entry->length ^= seed;
}

inline static void Decrypt2(u16 *data, u32 count, u32 num) {
    u16 seed = (u16)((num + 1) * 596947);

    while (count-- > 0) {
        *data ^= seed;
        data++;
        seed += 18749;
    }
}

static void ReadMsgData_ExistingTable_ExistingArray(MAT *table, u32 num, u16 *dest) {
    MAT_ENTRY alloc;

    if (num < table->count) {
        alloc = table->alloc[num];
        Decrypt1(&alloc, num, table->key);

        CopyEncryptedMessage16(dest, (const u16 *)((u8 *)table + alloc.offset), &alloc);
        Decrypt2(dest, alloc.length, num);
    } else {
        GF_ASSERT(FALSE);
    }
}

static void ReadMsgData_NewNarc_ExistingArray(NarcId narc_id, u32 group, u32 num, enum HeapID heapID, u16 *dest) {
    NARC *narc = NARC_New(MsgData_GetLanguageNarc(narc_id), heapID);
    u16 header[2];
    MAT_ENTRY alloc;
    if (narc != NULL) {
        NARC_ReadFromMember(narc, group, 0, 4, header);
        NARC_ReadFromMember(narc, group, 8 * num + 4, 8, &alloc);
        Decrypt1(&alloc, num, header[1]);
        NARC_ReadFromMember(narc, group, alloc.offset, 2 * alloc.length, dest);
        Decrypt2(dest, alloc.length, num);
        NARC_Delete(narc);
    }
}

static void CopyEncryptedMessage16(u16 *dest, const u16 *src, MAT_ENTRY *alloc) {
    MI_CpuCopy16(src, dest, alloc->length * 2);
}

static void ReadMsgData_ExistingTable_ExistingString(MAT *table, u32 num, String *dest) {
    MAT_ENTRY alloc;
    u16 *buf;
    if (num < table->count) {
        alloc = table->alloc[num];
        Decrypt1(&alloc, num, table->key);
        buf = Heap_AllocAtEnd(HEAP_ID_DEFAULT, 2 * alloc.length);
        if (buf != NULL) {
            MI_CpuCopy16((char *)table + alloc.offset, buf, 2 * alloc.length);
            Decrypt2(buf, alloc.length, num);
            CopyU16ArrayToStringN(dest, buf, alloc.length);
            Heap_Free(buf);
        }
    } else {
        GF_ASSERT(FALSE);
        String_SetEmpty(dest);
    }
}

static String *ReadMsgData_ExistingTable_NewString(MAT *table, u32 num, enum HeapID heapID) {
    MAT_ENTRY alloc;
    u16 *buf;
    String *dest;
    if (num < table->count) {
        alloc = table->alloc[num];
        Decrypt1(&alloc, num, table->key);
        buf = Heap_AllocAtEnd(heapID, 2 * alloc.length);
        if (buf != NULL) {
            MI_CpuCopy16((char *)table + alloc.offset, buf, 2 * alloc.length);
            Decrypt2(buf, alloc.length, num);
            dest = String_New(alloc.length, heapID);
            if (dest != NULL) {
                CopyU16ArrayToStringN(dest, buf, alloc.length);
            }
            Heap_Free(buf);
            return dest;
        } else {
            return NULL;
        }
    } else {
        GF_ASSERT(FALSE);
        return String_New(4, heapID);
    }
}

void ReadMsgData_NewNarc_ExistingString(NarcId narc_id, s32 group, u32 num, enum HeapID heapID, String *dest) {
    NARC *narc = NARC_New(MsgData_GetLanguageNarc(narc_id), heapID);
    if (narc != NULL) {
        ReadMsgData_ExistingNarc_ExistingString(narc, group, num, heapID, dest);
        NARC_Delete(narc);
    }
}

static void ReadMsgData_ExistingNarc_ExistingString(NARC *narc, u32 group, u32 num, enum HeapID heapID, String *dest) {
    u16 *buf;
    u32 size;
    u16 header[2];
    MAT_ENTRY alloc;

    NARC_ReadFromMember(narc, group, 0, 4, header);
    if (num < header[0]) {
        NARC_ReadFromMember(narc, group, 8 * num + 4, 8, &alloc);
        Decrypt1(&alloc, num, header[1]);
        size = alloc.length * 2;
        buf = Heap_AllocAtEnd(heapID, size);
        if (buf != NULL) {
            NARC_ReadFromMember(narc, group, alloc.offset, size, buf);
            Decrypt2(buf, alloc.length, num);
            CopyU16ArrayToStringN(dest, buf, alloc.length);
            Heap_Free(buf);
            return;
        }
    } else {
        GF_ASSERT(FALSE);
        String_SetEmpty(dest);
    }
}

String *ReadMsgData_NewNarc_NewString(NarcId narc_id, u32 group, u32 num, enum HeapID heapID) {
    NARC *narc = NARC_New(MsgData_GetLanguageNarc(narc_id), heapID);
    String *string;
    if (narc != NULL) {
        string = ReadMsgData_ExistingNarc_NewString(narc, group, num, heapID);
        NARC_Delete(narc);
    } else {
        string = String_New(4, heapID);
    }
    return string;
}

static String *ReadMsgData_ExistingNarc_NewString(NARC *narc, u32 group, u32 num, enum HeapID heapID) {
    String *dest;
    u16 *buf;
    u32 size;
    u16 header[2];
    MAT_ENTRY alloc;

    NARC_ReadFromMember(narc, group, 0, 4, header);
    if (num < header[0]) {
        NARC_ReadFromMember(narc, group, 8 * num + 4, 8, &alloc);
        Decrypt1(&alloc, num, header[1]);
        dest = String_New(alloc.length, heapID);
        if (dest != NULL) {
            size = alloc.length * 2;
            buf = Heap_AllocAtEnd(heapID, size);
            if (buf != NULL) {
                NARC_ReadFromMember(narc, group, alloc.offset, size, buf);
                Decrypt2(buf, alloc.length, num);
                CopyU16ArrayToStringN(dest, buf, alloc.length);
                Heap_Free(buf);
            }
        }
        return dest;
    } else {
        GF_ASSERT(FALSE);
        return String_New(4, heapID);
    }
}

static u16 GetMsgCount_ExistingTable(MAT *tbl) {
    return tbl->count;
}

static u16 GetMsgCount_TableFromNarc(NarcId narc_id, s32 file_id) {
    u16 n[2];
    ReadFromNarcMemberByIdPair(&n, narc_id, file_id, 0, 4);
    return n[0];
}

MsgData *NewMsgDataFromNarc(MsgDataLoadType type, NarcId narc_id, s32 file_id, enum HeapID heapID) {
    MsgData *msgData = Heap_AllocAtEnd(heapID, sizeof(MsgData));
#ifdef TRANSLATED_TEXT
    NarcId requestedNarcId = narc_id;
    narc_id = MsgData_GetLanguageNarc(narc_id);
#endif
    if (msgData != NULL) {
        if (type == MSGDATA_LOAD_DIRECT) {
            msgData->direct = LoadSingleElementFromNarc(narc_id, file_id, heapID);
            if (msgData->direct == NULL) {
                Heap_Free(msgData);
                return NULL;
            }
        } else {
            msgData->lazy = NARC_New(narc_id, heapID);
        }
        msgData->type = (u16)type;
        msgData->narc_id = (u16)narc_id;
        msgData->file_id = (u16)file_id;
        msgData->heapID = (u16)heapID;
#ifdef TRANSLATED_TEXT
        msgData->loadedNarcId = (u16)narc_id;
        msgData->narc_id = (u16)requestedNarcId;
#endif
    }
    return msgData;
}

void DestroyMsgData(MsgData *msgData) {
    if (msgData != NULL) {
        switch (msgData->type) {
        case MSGDATA_LOAD_DIRECT:
            FreeMsgDataRawData(msgData->direct);
            break;
        case MSGDATA_LOAD_LAZY:
            NARC_Delete(msgData->lazy);
            break;
        }
        Heap_Free(msgData);
    }
}

void ReadMsgDataIntoString(MsgData *msgData, s32 msg_no, String *dest) {
#ifdef TRANSLATED_TEXT
    NarcId narcId;
    NARC *narc;
    if (MsgData_LanguageChanged(msgData, &narcId)) {
        narc = NARC_New(narcId, HEAP_ID_DEFAULT);
        if (narc != NULL) {
            ReadMsgData_ExistingNarc_ExistingString(narc, msgData->file_id, msg_no, HEAP_ID_DEFAULT, dest);
            NARC_Delete(narc);
        }
        return;
    }
#endif
    switch (msgData->type) {
    case MSGDATA_LOAD_DIRECT:
        ReadMsgData_ExistingTable_ExistingString(msgData->direct, msg_no, dest);
        break;
    case MSGDATA_LOAD_LAZY:
        ReadMsgData_ExistingNarc_ExistingString(msgData->lazy, msgData->file_id, msg_no, (enum HeapID)msgData->heapID, dest);
        break;
    }
}

String *NewString_ReadMsgData(MsgData *msgData, s32 msg_no) {
#ifdef TRANSLATED_TEXT
    NarcId narcId;
    NARC *narc;
    String *string;
    if (MsgData_LanguageChanged(msgData, &narcId)) {
        narc = NARC_New(narcId, HEAP_ID_DEFAULT);
        if (narc == NULL) {
            return NULL;
        }
        string = ReadMsgData_ExistingNarc_NewString(narc, msgData->file_id, msg_no, (enum HeapID)msgData->heapID);
        NARC_Delete(narc);
        return string;
    }
#endif
    switch (msgData->type) {
    case MSGDATA_LOAD_DIRECT:
        return ReadMsgData_ExistingTable_NewString(msgData->direct, msg_no, (enum HeapID)msgData->heapID);
    case MSGDATA_LOAD_LAZY:
        return ReadMsgData_ExistingNarc_NewString(msgData->lazy, msgData->file_id, msg_no, (enum HeapID)msgData->heapID);
    default:
        return NULL;
    }
}

u32 MsgDataGetCount(MsgData *msgData) {
    switch (msgData->type) {
    case MSGDATA_LOAD_DIRECT:
        return GetMsgCount_ExistingTable(msgData->direct);
    case MSGDATA_LOAD_LAZY:
        return GetMsgCount_TableFromNarc((NarcId)msgData->narc_id, msgData->file_id);
    default:
        return 0;
    }
}

void ReadMsgDataIntoU16Array(MsgData *msgData, u32 msg_no, u16 *dest) {
#ifdef TRANSLATED_TEXT
    NarcId narcId;
    if (MsgData_LanguageChanged(msgData, &narcId)) {
        ReadMsgData_NewNarc_ExistingArray(narcId, msgData->file_id, msg_no, HEAP_ID_DEFAULT, dest);
        return;
    }
#endif
    switch (msgData->type) {
    case MSGDATA_LOAD_DIRECT:
        ReadMsgData_ExistingTable_ExistingArray(msgData->direct, msg_no, dest);
        break;
    case MSGDATA_LOAD_LAZY:
        ReadMsgData_NewNarc_ExistingArray((NarcId)msgData->narc_id, msgData->file_id, msg_no, (enum HeapID)msgData->heapID, dest);
        break;
    }
}

void GetSpeciesNameIntoArray(u16 species, enum HeapID heapID, u16 *dest) {
    MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0237_bin, heapID);
    ReadMsgDataIntoU16Array(msgData, species, dest);
    DestroyMsgData(msgData);
}

String *ReadMsgData_ExpandPlaceholders(MessageFormat *messageFormat, MsgData *msgData, u32 msgno, enum HeapID heapID) {
    String *ret = NULL;
    String *expandedString = String_New(1024, HEAP_ID_DEFAULT);
    String *rawString;
    if (expandedString != NULL) {
        rawString = NewString_ReadMsgData(msgData, msgno);
        if (rawString != NULL) {
            StringExpandPlaceholders(messageFormat, expandedString, rawString);
            ret = String_Dup(expandedString, heapID);
            String_Delete(rawString);
        }
        String_Delete(expandedString);
    }
    return ret;
}

String *GetMoveName(u32 move, enum HeapID heapno) {
    MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0750_bin, heapno);
    String *ret;
    if (msgData != NULL) {
        ret = String_New(16, heapno);
        if (ret != NULL) {
            ReadMsgDataIntoString(msgData, move, ret);
        }
        DestroyMsgData(msgData);
        return ret;
    }
    return NULL;
}

String *GetSpeciesName(u16 species, enum HeapID heapID) {
    String *ret;
    MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0237_bin, heapID);
    if (msgData != NULL) {
        ret = NewString_ReadMsgData(msgData, species);
        DestroyMsgData(msgData);
        return ret;
    }
    return NULL;
}
