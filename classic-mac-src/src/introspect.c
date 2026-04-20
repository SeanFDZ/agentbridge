/*
 * AgentBridge - introspect.c
 * Introspection commands:  windows,  processes,  menus,  volumes,  files
 *
 * These give the AI agent structured data about the Mac's state,
 * supplementing (and often replacing) pixel-level screen reading.
 */

#include <Windows.h>
#include <Menus.h>
#include <Processes.h>
#include <Files.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <string.h>

#include "bridge.h"

/* ----------------------------------------------------------------
   ping
   ---------------------------------------------------------------- */

void AB_CmdPing(const ABCommand *cmd)
{
    ABResponse resp;
    AB_InitResponse(&resp,  cmd->seq);
    AB_AddResponseLine(&resp,  "RESULT",  "pong");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   list_windows
   Enumerate all visible windows across all applications.
   Uses the Window Manager's window list (linked via nextWindow).
   ---------------------------------------------------------------- */

void AB_CmdListWindows(const ABCommand *cmd)
{
    ABResponse  resp;
    WindowPtr   win;
    short       count = 0;
    short       index = 0;

    AB_InitResponse(&resp,  cmd->seq);

    /*
     * Walk the window list from front to back.
     * FrontWindow() gives us the frontmost,  then we follow
     * the linked list via GetNextWindow / ((WindowPeek)win)->nextWindow.
     */
    win = FrontWindow();

    while (win != nil && index < AB_MAX_WINDOWS) {
        Rect        bounds;
        Str255      title;
        char        cTitle[256];
        char        line[AB_MAX_LINE_LEN];
        char        indexStr[8];
        char        boundsStr[48];
        const char  *layer;

        /* Get window title */
        GetWTitle(win,  title);
        AB_PascalToC(title,  cTitle,  sizeof(cTitle));

        /* Get window bounds via WindowPeek direct struct access (requires full WindowRecord definition from Universal Interfaces) */
        bounds = (*((WindowPeek)win)->contRgn)->rgnBBox;

        /* Determine layer */
        layer = (index == 0) ? "front" : "back";

        /* Format: index|app_name|title|left,top,right,bottom|layer */
        /* TODO: resolve owning app from windowKind or process list */
        index++;
        count++;

        AB_LongToStr(index,  indexStr);

        /* Build bounds string */
        {
            char numBuf[8];
            AB_StrCopy(boundsStr,  "",  sizeof(boundsStr));
            AB_LongToStr(bounds.left,  numBuf);
            AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));
            AB_StrCat(boundsStr,  ",",  sizeof(boundsStr));
            AB_LongToStr(bounds.top,  numBuf);
            AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));
            AB_StrCat(boundsStr,  ",",  sizeof(boundsStr));
            AB_LongToStr(bounds.right,  numBuf);
            AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));
            AB_StrCat(boundsStr,  ",",  sizeof(boundsStr));
            AB_LongToStr(bounds.bottom,  numBuf);
            AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));
        }

        /* Assemble WINDOW line */
        AB_StrCopy(line,  indexStr,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  "?",  sizeof(line));          /* app name placeholder */
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  cTitle,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  boundsStr,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  layer,  sizeof(line));

        AB_AddResponseLine(&resp,  "WINDOW",  line);

        win = GetNextWindow(win);
    }

    AB_AddResponseLineFmt(&resp,  "COUNT",  count);
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   list_processes
   Walk the Process Manager's process list.
   Requires System 7+.
   ---------------------------------------------------------------- */

void AB_CmdListProcesses(const ABCommand *cmd)
{
    ABResponse              resp;
    ProcessSerialNumber     psn;
    ProcessInfoRec          info;
    Str255                  procName;
    FSSpec                  procSpec;
    short                   count = 0;
    short                   index = 0;

    AB_InitResponse(&resp,  cmd->seq);

    psn.highLongOfPSN = 0;
    psn.lowLongOfPSN  = kNoProcess;

    while (GetNextProcess(&psn) == noErr && index < AB_MAX_PROCESSES) {
        char    cName[256];
        char    line[AB_MAX_LINE_LEN];
        char    creatorStr[8];
        char    numBuf[12];

        memset(&info,  0,  sizeof(info));
        info.processInfoLength = sizeof(ProcessInfoRec);
        info.processName = procName;
        info.processAppSpec = &procSpec;

        if (GetProcessInformation(&psn,  &info) != noErr) {
            continue;
        }

        AB_PascalToC(procName,  cName,  sizeof(cName));

        /* Format creator code as 4-char string */
        creatorStr[0] = (char)((info.processSignature >> 24) & 0xFF);
        creatorStr[1] = (char)((info.processSignature >> 16) & 0xFF);
        creatorStr[2] = (char)((info.processSignature >> 8) & 0xFF);
        creatorStr[3] = (char)(info.processSignature & 0xFF);
        creatorStr[4] = '\0';

        index++;
        count++;

        /* Format: index|name|creator|pid|mem_partition_kb */
        AB_LongToStr(index,  numBuf);
        AB_StrCopy(line,  numBuf,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  cName,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  creatorStr,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_LongToStr(psn.lowLongOfPSN,  numBuf);
        AB_StrCat(line,  numBuf,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_LongToStr(info.processSize / 1024,  numBuf);
        AB_StrCat(line,  numBuf,  sizeof(line));

        AB_AddResponseLine(&resp,  "PROCESS",  line);
    }

    AB_AddResponseLineFmt(&resp,  "COUNT",  count);
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   list_menus
   Return the menu bar entries for the frontmost application.
   ---------------------------------------------------------------- */

void AB_CmdListMenus(const ABCommand *cmd)
{
    ABResponse  resp;
    Handle      mbarH;
    short       count = 0;
    short       i;

    AB_InitResponse(&resp,  cmd->seq);

    /*
     * The menu bar is stored as a list of MenuHandle resources.
     * We can walk it by iterating menu IDs.
     * Menu IDs 1-255 are standard app menus.
     * We try each and see which exist.
     */
    for (i = 1; i <= 64; i++) {
        MenuHandle menu = GetMenuHandle(i);
        if (menu != nil) {
            Str255  menuTitle;
            char    cTitle[256];
            char    line[AB_MAX_LINE_LEN];
            char    numBuf[8];

            /* Direct MenuInfo struct access — requires full MenuInfo definition from Universal Interfaces.
               menuData[0] is a Pascal string (length-prefixed) containing the menu title. */
            memcpy(menuTitle,  (*menu)->menuData,  (*menu)->menuData[0] + 1);

            AB_PascalToC(menuTitle,  cTitle,  sizeof(cTitle));

            count++;
            AB_LongToStr(count,  numBuf);
            AB_StrCopy(line,  numBuf,  sizeof(line));
            AB_StrCat(line,  "|",  sizeof(line));
            AB_StrCat(line,  cTitle,  sizeof(line));

            AB_AddResponseLine(&resp,  "MENUBAR",  line);
        }
    }

    AB_AddResponseLineFmt(&resp,  "COUNT",  count);
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   get_menu_items
   Return items in a specific menu by title.
   ---------------------------------------------------------------- */

void AB_CmdGetMenuItems(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *menuName;
    short       i;

    AB_InitResponse(&resp,  cmd->seq);

    menuName = AB_GetField(cmd,  "MENU");
    if (menuName == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "MENU parameter required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* Find the menu by title */
    for (i = 1; i <= 64; i++) {
        MenuHandle menu = GetMenuHandle(i);
        if (menu != nil) {
            Str255  menuTitle;
            char    cTitle[256];

            /* Direct MenuInfo struct access — requires full struct definition from Universal Interfaces */
            memcpy(menuTitle,  (*menu)->menuData,  (*menu)->menuData[0] + 1);
            AB_PascalToC(menuTitle,  cTitle,  sizeof(cTitle));

            if (AB_StrCmpNoCase(cTitle,  menuName) == 0) {
                /* Found the menu -- enumerate items */
                short   itemCount = CountMItems(menu);
                short   j;
                short   count = 0;

                for (j = 1; j <= itemCount && j <= AB_MAX_MENUITEMS; j++) {
                    Str255  itemText;
                    char    cItem[256];
                    char    line[AB_MAX_LINE_LEN];
                    char    numBuf[8];
                    short   cmdChar;
                    Boolean enabled;

                    GetMenuItemText(menu,  j,  itemText);
                    AB_PascalToC(itemText,  cItem,  sizeof(cItem));

                    /* Check if item is enabled */
                    /* Menu enable flags are in (*menu)->enableFlags bit field */
                    /* Bit 0 controls the whole menu,  bit N controls item N */
                    enabled = ((*menu)->enableFlags & (1L << j)) != 0;

                    /* Get keyboard shortcut if any */
                    GetItemCmd(menu,  j,  &cmdChar);

                    count++;
                    AB_LongToStr(count,  numBuf);
                    AB_StrCopy(line,  numBuf,  sizeof(line));
                    AB_StrCat(line,  "|",  sizeof(line));
                    AB_StrCat(line,  cItem,  sizeof(line));
                    AB_StrCat(line,  "|",  sizeof(line));
                    AB_StrCat(line,  enabled ? "enabled" : "disabled",  sizeof(line));
                    AB_StrCat(line,  "|",  sizeof(line));
                    if (cmdChar > 0) {
                        char shortcut[4];
                        shortcut[0] = (char)cmdChar;
                        shortcut[1] = '\0';
                        AB_StrCat(line,  "Cmd-",  sizeof(line));
                        AB_StrCat(line,  shortcut,  sizeof(line));
                    }

                    AB_AddResponseLine(&resp,  "MENUITEM",  line);
                }

                AB_AddResponseLineFmt(&resp,  "COUNT",  count);
                AB_WriteResponseFile(&resp);
                AB_DisposeResponse(&resp);
                return;
            }
        }
    }

    /* Menu not found */
    AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,  "Menu not found");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   get_front_window
   Detailed info about the frontmost window.
   ---------------------------------------------------------------- */

void AB_CmdGetFrontWindow(const ABCommand *cmd)
{
    ABResponse  resp;
    WindowPtr   win;

    AB_InitResponse(&resp,  cmd->seq);

    win = FrontWindow();
    if (win == nil) {
        AB_AddResponseLine(&resp,  "RESULT",  "no_windows");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    {
        Str255  title;
        char    cTitle[256];
        Rect    bounds;
        char    boundsStr[48];
        char    numBuf[8];

        GetWTitle(win,  title);
        AB_PascalToC(title,  cTitle,  sizeof(cTitle));
        AB_AddResponseLine(&resp,  "TITLE",  cTitle);

        /* WindowPeek direct struct access — requires full WindowRecord from Universal Interfaces */
        bounds = (*((WindowPeek)win)->contRgn)->rgnBBox;

        AB_StrCopy(boundsStr,  "",  sizeof(boundsStr));
        AB_LongToStr(bounds.left,  numBuf);
        AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));
        AB_StrCat(boundsStr,  ",",  sizeof(boundsStr));
        AB_LongToStr(bounds.top,  numBuf);
        AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));
        AB_StrCat(boundsStr,  ",",  sizeof(boundsStr));
        AB_LongToStr(bounds.right,  numBuf);
        AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));
        AB_StrCat(boundsStr,  ",",  sizeof(boundsStr));
        AB_LongToStr(bounds.bottom,  numBuf);
        AB_StrCat(boundsStr,  numBuf,  sizeof(boundsStr));

        AB_AddResponseLine(&resp,  "BOUNDS",  boundsStr);
        AB_AddResponseLine(&resp,  "RESULT",  "window_info");
    }

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   get_volumes
   List mounted volumes via PBHGetVInfo.
   ---------------------------------------------------------------- */

void AB_CmdGetVolumes(const ABCommand *cmd)
{
    ABResponse      resp;
    HParamBlockRec  pb;
    Str255          volName;
    short           index = 1;
    short           count = 0;
    OSErr           err;

    AB_InitResponse(&resp,  cmd->seq);

    while (1) {
        char    cName[256];
        char    line[AB_MAX_LINE_LEN];
        char    numBuf[16];

        memset(&pb,  0,  sizeof(pb));
        pb.volumeParam.ioNamePtr = volName;
        pb.volumeParam.ioVolIndex = index;

        err = PBHGetVInfoSync(&pb);
        if (err != noErr) break;    /* No more volumes */

        AB_PascalToC(volName,  cName,  sizeof(cName));

        count++;
        AB_LongToStr(count,  numBuf);
        AB_StrCopy(line,  numBuf,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  cName,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));

        /* Free bytes in KB */
        AB_LongToStr(pb.volumeParam.ioVFrBlk *
                     (pb.volumeParam.ioVAlBlkSiz / 1024),  numBuf);
        AB_StrCat(line,  numBuf,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));

        /* Total bytes in KB */
        AB_LongToStr(pb.volumeParam.ioVNmAlBlks *
                     (pb.volumeParam.ioVAlBlkSiz / 1024),  numBuf);
        AB_StrCat(line,  numBuf,  sizeof(line));

        AB_AddResponseLine(&resp,  "VOLUME",  line);

        index++;
    }

    AB_AddResponseLineFmt(&resp,  "COUNT",  count);
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   list_folder
   List contents of a Mac folder by HFS path.
   ---------------------------------------------------------------- */

void AB_CmdListFolder(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *path;
    Str255      pPath;
    CInfoPBRec  cpb;
    Str255      itemName;
    short       vRefNum;
    long        dirID;
    short       index = 1;
    short       count = 0;
    OSErr       err;

    AB_InitResponse(&resp,  cmd->seq);

    path = AB_GetField(cmd,  "PATH");
    if (path == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "PATH parameter required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* Resolve the path to vRefNum + dirID */
    AB_CToPascal(path,  pPath);

    /* Use PBGetCatInfo to resolve the directory */
    memset(&cpb,  0,  sizeof(cpb));
    cpb.dirInfo.ioNamePtr = pPath;
    cpb.dirInfo.ioVRefNum = 0;
    cpb.dirInfo.ioDrDirID = 0;
    cpb.dirInfo.ioFDirIndex = 0;

    err = PBGetCatInfoSync(&cpb);
    if (err != noErr || !(cpb.dirInfo.ioFlAttrib & 0x10)) {
        AB_SetResponseError(&resp,  AB_ERR_FILE_NOT_FOUND,
            "Path not found or not a folder");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    vRefNum = cpb.dirInfo.ioVRefNum;
    dirID = cpb.dirInfo.ioDrDirID;

    /* Enumerate contents */
    while (index <= AB_MAX_FILES) {
        char    cName[256];
        char    line[AB_MAX_LINE_LEN];
        char    numBuf[16];
        char    typeStr[8];
        char    creatorStr[8];
        Boolean isFolder;

        memset(&cpb,  0,  sizeof(cpb));
        cpb.hFileInfo.ioNamePtr = itemName;
        cpb.hFileInfo.ioVRefNum = vRefNum;
        cpb.hFileInfo.ioDirID = dirID;
        cpb.hFileInfo.ioFDirIndex = index;

        err = PBGetCatInfoSync(&cpb);
        if (err != noErr) break;

        AB_PascalToC(itemName,  cName,  sizeof(cName));
        isFolder = (cpb.hFileInfo.ioFlAttrib & 0x10) != 0;

        /* Type and creator (files only) */
        if (!isFolder) {
            typeStr[0] = (char)((cpb.hFileInfo.ioFlFndrInfo.fdType >> 24) & 0xFF);
            typeStr[1] = (char)((cpb.hFileInfo.ioFlFndrInfo.fdType >> 16) & 0xFF);
            typeStr[2] = (char)((cpb.hFileInfo.ioFlFndrInfo.fdType >> 8) & 0xFF);
            typeStr[3] = (char)(cpb.hFileInfo.ioFlFndrInfo.fdType & 0xFF);
            typeStr[4] = '\0';

            creatorStr[0] = (char)((cpb.hFileInfo.ioFlFndrInfo.fdCreator >> 24) & 0xFF);
            creatorStr[1] = (char)((cpb.hFileInfo.ioFlFndrInfo.fdCreator >> 16) & 0xFF);
            creatorStr[2] = (char)((cpb.hFileInfo.ioFlFndrInfo.fdCreator >> 8) & 0xFF);
            creatorStr[3] = (char)(cpb.hFileInfo.ioFlFndrInfo.fdCreator & 0xFF);
            creatorStr[4] = '\0';
        }
        else {
            AB_StrCopy(typeStr,  "fold",  sizeof(typeStr));
            AB_StrCopy(creatorStr,  "MACS",  sizeof(creatorStr));
        }

        count++;
        AB_LongToStr(count,  numBuf);

        /* Format: index|name|type|creator|size_bytes|is_folder */
        AB_StrCopy(line,  numBuf,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  cName,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  typeStr,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  creatorStr,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        if (!isFolder) {
            AB_LongToStr(cpb.hFileInfo.ioFlLgLen,  numBuf);  /* data fork size */
        } else {
            AB_StrCopy(numBuf,  "0",  sizeof(numBuf));
        }
        AB_StrCat(line,  numBuf,  sizeof(line));
        AB_StrCat(line,  "|",  sizeof(line));
        AB_StrCat(line,  isFolder ? "1" : "0",  sizeof(line));

        AB_AddResponseLine(&resp,  "FILEENTRY",  line);

        index++;
    }

    AB_AddResponseLineFmt(&resp,  "COUNT",  count);
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   get_about
   Return system information.
   ---------------------------------------------------------------- */

void AB_CmdGetAbout(const ABCommand *cmd)
{
    ABResponse  resp;
    long        sysVersion;
    long        machineType;
    long        physMem;
    char        numBuf[16];
    char        versionStr[16];

    AB_InitResponse(&resp,  cmd->seq);

    AB_AddResponseLine(&resp,  "AGENT",  "AgentBridge");
    AB_AddResponseLine(&resp,  "PROTOCOL",  AB_VERSION);

    /* Mac OS version */
    if (Gestalt(gestaltSystemVersion,  &sysVersion) == noErr) {
        /* Format: 0x0904 -> "9.0.4" */
        versionStr[0] = '0' + (char)((sysVersion >> 8) & 0xF);
        versionStr[1] = '.';
        versionStr[2] = '0' + (char)((sysVersion >> 4) & 0xF);
        versionStr[3] = '.';
        versionStr[4] = '0' + (char)(sysVersion & 0xF);
        versionStr[5] = '\0';
        AB_AddResponseLine(&resp,  "OSVERSION",  versionStr);
    }

    /* Machine type */
    if (Gestalt(gestaltMachineType,  &machineType) == noErr) {
        AB_AddResponseLineFmt(&resp,  "MACHINE",  machineType);
    }

    /* Physical RAM in KB */
    if (Gestalt(gestaltPhysicalRAMSize,  &physMem) == noErr) {
        AB_AddResponseLineFmt(&resp,  "RAMKB",  physMem / 1024);
    }

    /* Free memory in current heap */
    AB_AddResponseLineFmt(&resp,  "FREEMEM",  FreeMem() / 1024);

    /* Uptime */
    AB_AddResponseLineFmt(&resp,  "UPTIME",
        (TickCount() - gAB.startTicks) / 60);

    AB_AddResponseLine(&resp,  "RESULT",  "about_info");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}
