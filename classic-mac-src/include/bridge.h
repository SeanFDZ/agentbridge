/*
 * AgentBridge - Classic Mac OS Agent Communication Bridge
 * bridge.h - Core types,  constants,  and shared declarations
 *
 * Target: Mac OS 7.1 - 9.2.2 (68k and PowerPC)
 * Toolchain: Retro68 (GCC cross-compiler)
 */

#ifndef AGENTBRIDGE_H
#define AGENTBRIDGE_H

#include <Types.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Files.h>
#include <Errors.h>

/* ----------------------------------------------------------------
   Protocol Constants
   ---------------------------------------------------------------- */

#define AB_VERSION          "1.0.1"
#define AB_MAX_MSG_SIZE     32768       /* 32KB max message payload */
#define AB_MAX_LINE_LEN     1024        /* Max single line length */
#define AB_MAX_KEY_LEN      32          /* Max key name length */
#define AB_MAX_VALUE_LEN    960         /* Max value length per line */
#define AB_MAX_SEQ          99999       /* Sequence number wraps after this */
#define AB_POLL_TICKS       30          /* ~500ms between inbox polls */
#define AB_HEARTBEAT_TICKS  120         /* ~2 seconds between heartbeats */
#define AB_MAX_WINDOWS      64          /* Max windows to enumerate */
#define AB_MAX_PROCESSES     32          /* Max processes to enumerate */
#define AB_MAX_MENUITEMS    48          /* Max items in a single menu */
#define AB_MAX_FILES        128         /* Max files in a folder listing */

/* Shared folder filenames (must be <= 31 chars for HFS) */
#define AB_INBOX_DIR        "inbox"
#define AB_OUTBOX_DIR       "outbox"
#define AB_ASSETS_DIR       "assets"
#define AB_HEARTBEAT_FILE   "heartbeat"
#define AB_CONFIG_FILE      "bridge.conf"

/* ----------------------------------------------------------------
   Error Codes (match protocol spec Section 7)
   ---------------------------------------------------------------- */

#define AB_ERR_UNKNOWN_CMD      100
#define AB_ERR_INVALID_PARAMS   200
#define AB_ERR_APP_NOT_FOUND    300
#define AB_ERR_APP_NOT_RUNNING  301
#define AB_ERR_AE_FAILED        302
#define AB_ERR_FILE_NOT_FOUND   400
#define AB_ERR_ACCESS_DENIED    401
#define AB_ERR_DISK_FULL        402
#define AB_ERR_TOOLBOX          500
#define AB_ERR_OUT_OF_MEMORY    501
#define AB_ERR_TIMEOUT          600
#define AB_ERR_INTERNAL         900

/* ----------------------------------------------------------------
   Command IDs (parsed from CMD string)
   ---------------------------------------------------------------- */

typedef enum {
    kCmdUnknown = 0,

    /* Introspection */
    kCmdPing,
    kCmdListWindows,
    kCmdListProcesses,
    kCmdListMenus,
    kCmdGetMenuItems,
    kCmdGetFrontWindow,
    kCmdGetClipboard,
    kCmdGetVolumes,
    kCmdListFolder,
    kCmdGetAbout,

    /* Actions */
    kCmdClick,
    kCmdMouseMove,
    kCmdMouseDrag,
    kCmdKeyPress,
    kCmdTypeText,
    kCmdSetClipboard,
    kCmdMenuSelect,
    kCmdSendAppleEvent,
    kCmdLaunchApp,
    kCmdOpenDocument,
    kCmdActivateApp,
    kCmdQuitApp,

    /* File transfer */
    kCmdStageFile,
    kCmdRetrieveFile,
    kCmdDeleteFile,

    /* Composite */
    kCmdWaitForWindow,
    kCmdWaitForIdle,
    kCmdScreenshotRegion,

    /* Lifecycle */
    kCmdShutdown
} ABCommandID;

/* ----------------------------------------------------------------
   Parsed Message Structures
   ---------------------------------------------------------------- */

/* Key-value pair from a parsed message line */
typedef struct {
    char    key[AB_MAX_KEY_LEN];
    char    value[AB_MAX_VALUE_LEN];
} ABKeyValue;

/* Parsed command message */
#define AB_MAX_FIELDS  64

typedef struct {
    char        version[8];
    long        seq;
    ABCommandID cmdID;
    char        cmdStr[32];
    char        timestamp[24];
    ABKeyValue  fields[AB_MAX_FIELDS];
    short       fieldCount;
    char        textBuf[AB_MAX_MSG_SIZE];   /* Assembled multi-line TEXT */
    short       textLen;
} ABCommand;

/* Response builder */
typedef struct {
    long    seq;
    Boolean isError;
    short   errCode;
    char    errMsg[256];
    Handle  bodyH;          /* Accumulates response lines */
    long    bodyLen;
} ABResponse;

/* ----------------------------------------------------------------
   Global State
   ---------------------------------------------------------------- */

typedef struct {
    /* Shared folder location */
    short   sharedVRefNum;          /* Volume reference for shared folder */
    long    sharedDirID;            /* Directory ID of shared folder root */
    long    inboxDirID;
    long    outboxDirID;
    long    assetsDirID;

    /* Runtime state */
    Boolean running;
    long    nextSeq;                /* For duplicate detection */
    long    startTicks;             /* TickCount at launch */
    long    lastHeartbeat;          /* TickCount of last heartbeat write */
    long    pollInterval;           /* Ticks between inbox polls */
    long    heartbeatInterval;      /* Ticks between heartbeats */

    /* Configuration */
    Boolean verbose;                /* Verbose logging to file */
} ABGlobals;

extern ABGlobals gAB;

/* ----------------------------------------------------------------
   Function Prototypes - protocol.c
   ---------------------------------------------------------------- */

OSErr       AB_ParseCommandFile(short fRefNum,  ABCommand *cmd);
OSErr       AB_InitResponse(ABResponse *resp,  long seq);
OSErr       AB_AddResponseLine(ABResponse *resp,  const char *key,  const char *value);
OSErr       AB_AddResponseLineFmt(ABResponse *resp,  const char *key,  long numValue);
OSErr       AB_SetResponseError(ABResponse *resp,  short errCode,  const char *errMsg);
OSErr       AB_WriteResponseFile(ABResponse *resp);
void        AB_DisposeResponse(ABResponse *resp);
ABCommandID AB_LookupCommand(const char *cmdStr);
const char* AB_GetField(const ABCommand *cmd,  const char *key);
long        AB_GetFieldLong(const ABCommand *cmd,  const char *key,  long defaultVal);

/* ----------------------------------------------------------------
   Function Prototypes - commands.c
   ---------------------------------------------------------------- */

void        AB_DispatchCommand(const ABCommand *cmd);

/* ----------------------------------------------------------------
   Function Prototypes - introspect.c
   ---------------------------------------------------------------- */

void        AB_CmdPing(const ABCommand *cmd);
void        AB_CmdListWindows(const ABCommand *cmd);
void        AB_CmdListProcesses(const ABCommand *cmd);
void        AB_CmdListMenus(const ABCommand *cmd);
void        AB_CmdGetMenuItems(const ABCommand *cmd);
void        AB_CmdGetFrontWindow(const ABCommand *cmd);
void        AB_CmdGetVolumes(const ABCommand *cmd);
void        AB_CmdListFolder(const ABCommand *cmd);
void        AB_CmdGetAbout(const ABCommand *cmd);

/* ----------------------------------------------------------------
   Function Prototypes - input.c
   ---------------------------------------------------------------- */

void        AB_CmdClick(const ABCommand *cmd);
void        AB_CmdMouseMove(const ABCommand *cmd);
void        AB_CmdMouseDrag(const ABCommand *cmd);
void        AB_CmdKeyPress(const ABCommand *cmd);
void        AB_CmdTypeText(const ABCommand *cmd);

/* Event injection queue (async posting from main loop) */
Boolean     AB_HasPendingEvents(void);
void        AB_ProcessInjectionQueue(void);
short       AB_GetInjectionMask(void);

/* ----------------------------------------------------------------
   Function Prototypes - clipboard.c
   ---------------------------------------------------------------- */

void        AB_CmdGetClipboard(const ABCommand *cmd);
void        AB_CmdSetClipboard(const ABCommand *cmd);

/* ----------------------------------------------------------------
   Function Prototypes - appleevent_cmds.c
   ---------------------------------------------------------------- */

void        AB_CmdMenuSelect(const ABCommand *cmd);
void        AB_CmdSendAppleEvent(const ABCommand *cmd);
void        AB_CmdLaunchApp(const ABCommand *cmd);
void        AB_CmdOpenDocument(const ABCommand *cmd);
void        AB_CmdActivateApp(const ABCommand *cmd);
void        AB_CmdQuitApp(const ABCommand *cmd);

/* ----------------------------------------------------------------
   Function Prototypes - files.c
   ---------------------------------------------------------------- */

void        AB_CmdStageFile(const ABCommand *cmd);
void        AB_CmdRetrieveFile(const ABCommand *cmd);
void        AB_CmdDeleteFile(const ABCommand *cmd);
OSErr       AB_ResolveSharedFolder(void);
OSErr       AB_ScanInbox(void);

/* ----------------------------------------------------------------
   Function Prototypes - heartbeat.c
   ---------------------------------------------------------------- */

void        AB_WriteHeartbeat(void);

/* ----------------------------------------------------------------
   Function Prototypes - bridge_utils.c
   ---------------------------------------------------------------- */

void        AB_StrCopy(char *dst,  const char *src,  long maxLen);
void        AB_StrCat(char *dst,  const char *src,  long maxLen);
short       AB_StrCmp(const char *a,  const char *b);
short       AB_StrCmpNoCase(const char *a,  const char *b);
void        AB_LongToStr(long val,  char *buf);
long        AB_StrToLong(const char *str);
void        AB_PascalToC(const unsigned char *pstr,  char *cstr,  short maxLen);
void        AB_CToPascal(const char *cstr,  unsigned char *pstr);
void        AB_FormatTimestamp(char *buf);
void        AB_Log(const char *msg);

#endif /* AGENTBRIDGE_H */
