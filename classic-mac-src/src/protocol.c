/*
 * AgentBridge - protocol.c
 * Message parsing (command files) and response writing
 *
 * Wire format:  line-oriented KEY value pairs
 * Terminated by "---" on its own line
 * Multi-line TEXT uses "+" continuation prefix
 */

#include <Files.h>
#include <Memory.h>
#include <TextUtils.h>
#include <string.h>

#include "bridge.h"

/* ----------------------------------------------------------------
   Command Lookup Table
   ---------------------------------------------------------------- */

typedef struct {
    const char  *name;
    ABCommandID  id;
} ABCmdEntry;

static const ABCmdEntry sCmdTable[] = {
    { "ping",               kCmdPing },
    { "list_windows",       kCmdListWindows },
    { "list_processes",     kCmdListProcesses },
    { "list_menus",         kCmdListMenus },
    { "get_menu_items",     kCmdGetMenuItems },
    { "get_front_window",   kCmdGetFrontWindow },
    { "get_clipboard",      kCmdGetClipboard },
    { "get_volumes",        kCmdGetVolumes },
    { "list_folder",        kCmdListFolder },
    { "get_about",          kCmdGetAbout },
    { "click",              kCmdClick },
    { "mouse_move",         kCmdMouseMove },
    { "mouse_drag",         kCmdMouseDrag },
    { "key_press",          kCmdKeyPress },
    { "type_text",          kCmdTypeText },
    { "set_clipboard",      kCmdSetClipboard },
    { "menu_select",        kCmdMenuSelect },
    { "send_appleevent",    kCmdSendAppleEvent },
    { "launch_app",         kCmdLaunchApp },
    { "open_document",      kCmdOpenDocument },
    { "activate_app",       kCmdActivateApp },
    { "quit_app",           kCmdQuitApp },
    { "stage_file",         kCmdStageFile },
    { "retrieve_file",      kCmdRetrieveFile },
    { "delete_file",        kCmdDeleteFile },
    { "wait_for_window",    kCmdWaitForWindow },
    { "wait_for_idle",      kCmdWaitForIdle },
    { "screenshot_region",  kCmdScreenshotRegion },
    { "shutdown",           kCmdShutdown },
    { nil,                  kCmdUnknown }
};

ABCommandID AB_LookupCommand(const char *cmdStr)
{
    const ABCmdEntry *entry = sCmdTable;

    while (entry->name != nil) {
        if (AB_StrCmpNoCase(cmdStr,  entry->name) == 0) {
            return entry->id;
        }
        entry++;
    }
    return kCmdUnknown;
}

/* ----------------------------------------------------------------
   Line Reader
   Read one CR or LF terminated line from an open file.
   Returns line length,  or -1 on EOF/error.
   ---------------------------------------------------------------- */

static short ReadLine(short fRefNum,  char *buf,  short maxLen)
{
    short   pos = 0;
    long    count;
    char    ch;
    OSErr   err;

    while (pos < maxLen - 1) {
        count = 1;
        err = FSRead(fRefNum,  &count,  &ch);

        if (err == eofErr || count == 0) {
            if (pos > 0) break;     /* Return what we have */
            return -1;              /* True EOF */
        }
        if (err != noErr) return -1;

        /* Normalize line endings: CR,  LF,  or CRLF */
        if (ch == '\r') {
            /* Peek for LF after CR */
            count = 1;
            err = FSRead(fRefNum,  &count,  &ch);
            if (err == noErr && ch != '\n') {
                /* Not LF -- back up one byte */
                long offset = -1;
                SetFPos(fRefNum,  fsFromMark,  offset);
            }
            break;
        }
        if (ch == '\n') break;

        buf[pos++] = ch;
    }

    buf[pos] = '\0';
    return pos;
}

/* ----------------------------------------------------------------
   Split a line into key and value at first space
   ---------------------------------------------------------------- */

static Boolean SplitKeyValue(const char *line,  char *key,  short keyMax,
                              char *value,  short valMax)
{
    short i = 0;

    /* Extract key (up to first space) */
    while (line[i] && line[i] != ' ' && i < keyMax - 1) {
        key[i] = line[i];
        i++;
    }
    key[i] = '\0';

    if (key[0] == '\0') return false;

    /* Skip space */
    if (line[i] == ' ') i++;

    /* Rest is value */
    AB_StrCopy(value,  &line[i],  valMax);

    return true;
}

/* ----------------------------------------------------------------
   Parse a Command File
   ---------------------------------------------------------------- */

OSErr AB_ParseCommandFile(short fRefNum,  ABCommand *cmd)
{
    char    line[AB_MAX_LINE_LEN];
    char    key[AB_MAX_KEY_LEN];
    char    value[AB_MAX_VALUE_LEN];
    short   len;
    Boolean inText = false;

    /* Zero out the command structure */
    memset(cmd,  0,  sizeof(ABCommand));
    cmd->cmdID = kCmdUnknown;

    while ((len = ReadLine(fRefNum,  line,  AB_MAX_LINE_LEN)) >= 0) {

        /* Check for message terminator */
        if (len == 3 && line[0] == '-' && line[1] == '-' && line[2] == '-') {
            break;
        }

        /* Handle TEXT continuation lines */
        if (line[0] == '+' && inText) {
            if (cmd->textLen > 0 && cmd->textLen < AB_MAX_MSG_SIZE - 2) {
                cmd->textBuf[cmd->textLen++] = '\r';   /* CR for Classic Mac */
            }
            /* Append everything after the '+' */
            AB_StrCopy(&cmd->textBuf[cmd->textLen],  &line[1],
                       AB_MAX_MSG_SIZE - cmd->textLen);
            cmd->textLen += strlen(&line[1]);
            continue;
        }

        inText = false;

        /* Split into key-value */
        if (!SplitKeyValue(line,  key,  AB_MAX_KEY_LEN,  value,  AB_MAX_VALUE_LEN)) {
            continue;
        }

        /* Handle known header fields */
        if (AB_StrCmpNoCase(key,  "BRIDGE") == 0) {
            AB_StrCopy(cmd->version,  value,  sizeof(cmd->version));
        }
        else if (AB_StrCmpNoCase(key,  "SEQ") == 0) {
            cmd->seq = AB_StrToLong(value);
        }
        else if (AB_StrCmpNoCase(key,  "CMD") == 0) {
            AB_StrCopy(cmd->cmdStr,  value,  sizeof(cmd->cmdStr));
            cmd->cmdID = AB_LookupCommand(value);
        }
        else if (AB_StrCmpNoCase(key,  "TS") == 0) {
            AB_StrCopy(cmd->timestamp,  value,  sizeof(cmd->timestamp));
        }
        else if (AB_StrCmpNoCase(key,  "TEXT") == 0) {
            /* Start of text payload -- may have continuation lines */
            AB_StrCopy(cmd->textBuf,  value,  AB_MAX_MSG_SIZE);
            cmd->textLen = strlen(value);
            inText = true;
        }
        else {
            /* Store as a generic field for command handlers to access */
            if (cmd->fieldCount < AB_MAX_FIELDS) {
                AB_StrCopy(cmd->fields[cmd->fieldCount].key,
                           key,  AB_MAX_KEY_LEN);
                AB_StrCopy(cmd->fields[cmd->fieldCount].value,
                           value,  AB_MAX_VALUE_LEN);
                cmd->fieldCount++;
            }
        }
    }

    /* Validate minimum required fields */
    if (cmd->seq == 0 || cmd->cmdID == kCmdUnknown) {
        if (cmd->seq == 0 && cmd->cmdStr[0] != '\0') {
            /* Valid command but missing SEQ -- log warning */
            AB_Log("Warning: command missing SEQ number");
        }
    }

    return noErr;
}

/* ----------------------------------------------------------------
   Field Accessor Helpers
   ---------------------------------------------------------------- */

const char* AB_GetField(const ABCommand *cmd,  const char *key)
{
    short i;
    for (i = 0; i < cmd->fieldCount; i++) {
        if (AB_StrCmpNoCase(cmd->fields[i].key,  key) == 0) {
            return cmd->fields[i].value;
        }
    }
    return nil;
}

long AB_GetFieldLong(const ABCommand *cmd,  const char *key,  long defaultVal)
{
    const char *val = AB_GetField(cmd,  key);
    if (val == nil || val[0] == '\0') return defaultVal;
    return AB_StrToLong(val);
}

/* ----------------------------------------------------------------
   Response Builder
   ---------------------------------------------------------------- */

OSErr AB_InitResponse(ABResponse *resp,  long seq)
{
    memset(resp,  0,  sizeof(ABResponse));
    resp->seq = seq;
    resp->isError = false;

    /* Allocate a handle for accumulating response body lines */
    resp->bodyH = NewHandle(0);
    if (resp->bodyH == nil) return memFullErr;

    resp->bodyLen = 0;

    /* Write protocol header lines */
    AB_AddResponseLine(resp,  "BRIDGE",  AB_VERSION);

    {
        char seqBuf[12];
        AB_LongToStr(seq,  seqBuf);
        AB_AddResponseLine(resp,  "SEQ",  seqBuf);
    }

    return noErr;
}

OSErr AB_AddResponseLine(ABResponse *resp,  const char *key,  const char *value)
{
    char    line[AB_MAX_LINE_LEN];
    long    lineLen;
    OSErr   err;

    /* Format: "KEY value\r" */
    AB_StrCopy(line,  key,  AB_MAX_LINE_LEN);
    if (value && value[0]) {
        AB_StrCat(line,  " ",  AB_MAX_LINE_LEN);
        AB_StrCat(line,  value,  AB_MAX_LINE_LEN);
    }
    AB_StrCat(line,  "\r",  AB_MAX_LINE_LEN);

    lineLen = strlen(line);

    /* Grow the handle and append */
    SetHandleSize(resp->bodyH,  resp->bodyLen + lineLen);
    err = MemError();
    if (err != noErr) return err;

    BlockMoveData(line,  *resp->bodyH + resp->bodyLen,  lineLen);
    resp->bodyLen += lineLen;

    return noErr;
}

OSErr AB_AddResponseLineFmt(ABResponse *resp,  const char *key,  long numValue)
{
    char buf[16];
    AB_LongToStr(numValue,  buf);
    return AB_AddResponseLine(resp,  key,  buf);
}

OSErr AB_SetResponseError(ABResponse *resp,  short errCode,  const char *errMsg)
{
    resp->isError = true;
    resp->errCode = errCode;
    AB_StrCopy(resp->errMsg,  errMsg,  sizeof(resp->errMsg));
    return noErr;
}

/* ----------------------------------------------------------------
   Write Response File to outbox
   ---------------------------------------------------------------- */

OSErr AB_WriteResponseFile(ABResponse *resp)
{
    OSErr           err;
    short           fRefNum;
    long            count;
    char            filename[32];
    char            statusLine[32];
    char            terminator[4];
    Str255          pFilename;
    char            seqBuf[8];

    /* Build filename: R00042.msg */
    AB_StrCopy(filename,  "R",  sizeof(filename));
    /* Zero-pad sequence to 5 digits */
    {
        long s = resp->seq;
        seqBuf[0] = '0' + (char)((s / 10000) % 10);
        seqBuf[1] = '0' + (char)((s / 1000) % 10);
        seqBuf[2] = '0' + (char)((s / 100) % 10);
        seqBuf[3] = '0' + (char)((s / 10) % 10);
        seqBuf[4] = '0' + (char)(s % 10);
        seqBuf[5] = '\0';
    }
    AB_StrCat(filename,  seqBuf,  sizeof(filename));
    AB_StrCat(filename,  ".msg",  sizeof(filename));

    AB_CToPascal(filename,  pFilename);

    /* Create and open the response file in outbox */
    err = HCreate(gAB.sharedVRefNum,  gAB.outboxDirID,
                  pFilename,  'MACS',  'TEXT');
    if (err != noErr && err != dupFNErr) return err;

    err = HOpenDF(gAB.sharedVRefNum,  gAB.outboxDirID,
                  pFilename,  fsWrPerm,  &fRefNum);
    if (err != noErr) return err;

    /* Write the accumulated header+body (BRIDGE,  SEQ already in bodyH) */
    HLock(resp->bodyH);
    count = resp->bodyLen;
    err = FSWrite(fRefNum,  &count,  *resp->bodyH);
    HUnlock(resp->bodyH);
    if (err != noErr) { FSClose(fRefNum); return err; }

    /* Write STATUS line */
    if (resp->isError) {
        char errLine[AB_MAX_LINE_LEN];

        AB_StrCopy(statusLine,  "STATUS error\r",  sizeof(statusLine));
        count = strlen(statusLine);
        FSWrite(fRefNum,  &count,  statusLine);

        /* ERRCODE */
        AB_StrCopy(errLine,  "ERRCODE ",  sizeof(errLine));
        {
            char codeBuf[8];
            AB_LongToStr(resp->errCode,  codeBuf);
            AB_StrCat(errLine,  codeBuf,  sizeof(errLine));
        }
        AB_StrCat(errLine,  "\r",  sizeof(errLine));
        count = strlen(errLine);
        FSWrite(fRefNum,  &count,  errLine);

        /* ERRMSG */
        AB_StrCopy(errLine,  "ERRMSG ",  sizeof(errLine));
        AB_StrCat(errLine,  resp->errMsg,  sizeof(errLine));
        AB_StrCat(errLine,  "\r",  sizeof(errLine));
        count = strlen(errLine);
        FSWrite(fRefNum,  &count,  errLine);
    }
    else {
        AB_StrCopy(statusLine,  "STATUS ok\r",  sizeof(statusLine));
        count = strlen(statusLine);
        FSWrite(fRefNum,  &count,  statusLine);
    }

    /* Write timestamp */
    {
        char tsLine[48];
        char ts[24];
        AB_FormatTimestamp(ts);
        AB_StrCopy(tsLine,  "TS ",  sizeof(tsLine));
        AB_StrCat(tsLine,  ts,  sizeof(tsLine));
        AB_StrCat(tsLine,  "\r",  sizeof(tsLine));
        count = strlen(tsLine);
        FSWrite(fRefNum,  &count,  tsLine);
    }

    /* Write terminator */
    AB_StrCopy(terminator,  "---\r",  4);
    count = 4;
    FSWrite(fRefNum,  &count,  terminator);

    FSClose(fRefNum);
    FlushVol(nil,  gAB.sharedVRefNum);

    return noErr;
}

void AB_DisposeResponse(ABResponse *resp)
{
    if (resp->bodyH != nil) {
        DisposeHandle(resp->bodyH);
        resp->bodyH = nil;
    }
}
