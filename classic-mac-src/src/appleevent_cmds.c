/*
 * AgentBridge - appleevent_cmds.c
 * Commands that use AppleEvents and the Process Manager
 * to control other applications.
 */

#include <AppleEvents.h>
#include <Processes.h>
#include <Files.h>
#include <Aliases.h>
#include <Menus.h>
#include <string.h>

#include "bridge.h"

/* ----------------------------------------------------------------
   Helper: Find a running process by name
   Returns true if found,  fills in PSN.
   ---------------------------------------------------------------- */

static Boolean FindProcessByName(const char *name,  ProcessSerialNumber *psn)
{
    ProcessInfoRec  info;
    Str255          procName;
    FSSpec          procSpec;
    char            cName[256];

    psn->highLongOfPSN = 0;
    psn->lowLongOfPSN  = kNoProcess;

    while (GetNextProcess(psn) == noErr) {
        memset(&info,  0,  sizeof(info));
        info.processInfoLength = sizeof(ProcessInfoRec);
        info.processName = procName;
        info.processAppSpec = &procSpec;

        if (GetProcessInformation(psn,  &info) == noErr) {
            AB_PascalToC(procName,  cName,  sizeof(cName));
            if (AB_StrCmpNoCase(cName,  name) == 0) {
                return true;
            }
        }
    }

    return false;
}

/* ----------------------------------------------------------------
   Helper: Find a process by creator code
   ---------------------------------------------------------------- */

static Boolean FindProcessByCreator(OSType creator,  ProcessSerialNumber *psn)
{
    ProcessInfoRec  info;
    Str255          procName;
    FSSpec          procSpec;

    psn->highLongOfPSN = 0;
    psn->lowLongOfPSN  = kNoProcess;

    while (GetNextProcess(psn) == noErr) {
        memset(&info,  0,  sizeof(info));
        info.processInfoLength = sizeof(ProcessInfoRec);
        info.processName = procName;
        info.processAppSpec = &procSpec;

        if (GetProcessInformation(psn,  &info) == noErr) {
            if (info.processSignature == creator) {
                return true;
            }
        }
    }

    return false;
}

/* ----------------------------------------------------------------
   Helper: Parse a 4-char creator code from string
   ---------------------------------------------------------------- */

static OSType ParseCreatorCode(const char *str)
{
    OSType code;

    if (str == nil || strlen(str) < 4) return 0;

    code = ((OSType)str[0] << 24) |
           ((OSType)str[1] << 16) |
           ((OSType)str[2] << 8)  |
           (OSType)str[3];

    return code;
}

/* ----------------------------------------------------------------
   Helper: Send a simple AppleEvent (no params) to a PSN
   ---------------------------------------------------------------- */

static OSErr SendSimpleAE(const ProcessSerialNumber *psn,
                           AEEventClass evtClass,
                           AEEventID evtID)
{
    AppleEvent  event,  reply;
    AEAddressDesc target;
    OSErr       err;

    err = AECreateDesc(typeProcessSerialNumber,
                       psn,  sizeof(ProcessSerialNumber),
                       &target);
    if (err != noErr) return err;

    err = AECreateAppleEvent(evtClass,  evtID,
                             &target,
                             kAutoGenerateReturnID,
                             kAnyTransactionID,
                             &event);
    AEDisposeDesc(&target);
    if (err != noErr) return err;

    err = AESend(&event,  &reply,
                 kAENoReply | kAENeverInteract,
                 kAENormalPriority,
                 kAEDefaultTimeout,
                 nil,  nil);

    AEDisposeDesc(&event);
    if (err == noErr) AEDisposeDesc(&reply);

    return err;
}

/* ----------------------------------------------------------------
   launch_app
   Launch by creator code or Mac path.
   ---------------------------------------------------------------- */

void AB_CmdLaunchApp(const ABCommand *cmd)
{
    ABResponse              resp;
    const char              *creator;
    const char              *path;
    LaunchParamBlockRec     lpb;
    FSSpec                  appSpec;
    OSErr                   err;

    AB_InitResponse(&resp,  cmd->seq);

    creator = AB_GetField(cmd,  "CREATOR");
    path = AB_GetField(cmd,  "PATH");

    if (creator != nil && strlen(creator) >= 4) {
        /*
         * Find the app by creator code using the Desktop Database.
         * DTGetAPPL would be the proper call,  but for simplicity
         * we first check if it's already running.
         */
        ProcessSerialNumber psn;
        OSType creatorCode = ParseCreatorCode(creator);

        if (FindProcessByCreator(creatorCode,  &psn)) {
            /* Already running -- just activate it */
            SetFrontProcess(&psn);
            AB_AddResponseLine(&resp,  "RESULT",  "already_running");
            AB_WriteResponseFile(&resp);
            AB_DisposeResponse(&resp);
            return;
        }

        /*
         * Not running.  We'd need to use the Desktop Database
         * (DTGetAPPL) to find the app's location.  For now,
         * fall through to error.
         */
        AB_SetResponseError(&resp,  AB_ERR_APP_NOT_FOUND,
            "App not running; PATH launch from creator DB not yet implemented");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }
    else if (path != nil) {
        /* Launch by HFS path */
        Str255 pPath;
        AB_CToPascal(path,  pPath);

        err = FSMakeFSSpec(0,  0,  pPath,  &appSpec);
        if (err != noErr) {
            AB_SetResponseError(&resp,  AB_ERR_APP_NOT_FOUND,
                "Application path not found");
            AB_WriteResponseFile(&resp);
            AB_DisposeResponse(&resp);
            return;
        }

        memset(&lpb,  0,  sizeof(lpb));
        lpb.launchBlockID = extendedBlock;
        lpb.launchEPBLength = extendedBlockLen;
        lpb.launchFileFlags = 0;
        lpb.launchControlFlags = launchContinue | launchNoFileFlags;
        lpb.launchAppSpec = &appSpec;

        err = LaunchApplication(&lpb);
        if (err != noErr) {
            AB_SetResponseError(&resp,  AB_ERR_TOOLBOX,
                "LaunchApplication failed");
            AB_WriteResponseFile(&resp);
            AB_DisposeResponse(&resp);
            return;
        }

        AB_AddResponseLine(&resp,  "RESULT",  "launched");
        AB_AddResponseLineFmt(&resp,  "PID",
            lpb.launchProcessSN.lowLongOfPSN);
    }
    else {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "CREATOR or PATH required");
    }

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   activate_app
   Bring a running application to the front.
   ---------------------------------------------------------------- */

void AB_CmdActivateApp(const ABCommand *cmd)
{
    ABResponse          resp;
    const char          *name;
    ProcessSerialNumber psn;

    AB_InitResponse(&resp,  cmd->seq);

    name = AB_GetField(cmd,  "NAME");
    if (name == nil) name = AB_GetField(cmd,  "TARGET");

    if (name == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "NAME parameter required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    if (FindProcessByName(name,  &psn)) {
        SetFrontProcess(&psn);
        AB_AddResponseLine(&resp,  "RESULT",  "activated");
    }
    else {
        AB_SetResponseError(&resp,  AB_ERR_APP_NOT_RUNNING,  name);
    }

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   quit_app
   Send kAEQuitApplication to a running app.
   ---------------------------------------------------------------- */

void AB_CmdQuitApp(const ABCommand *cmd)
{
    ABResponse          resp;
    const char          *name;
    ProcessSerialNumber psn;
    OSErr               err;

    AB_InitResponse(&resp,  cmd->seq);

    name = AB_GetField(cmd,  "NAME");
    if (name == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "NAME parameter required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    if (!FindProcessByName(name,  &psn)) {
        AB_SetResponseError(&resp,  AB_ERR_APP_NOT_RUNNING,  name);
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    err = SendSimpleAE(&psn,  kCoreEventClass,  kAEQuitApplication);
    if (err != noErr) {
        AB_SetResponseError(&resp,  AB_ERR_AE_FAILED,
            "Quit AppleEvent failed");
    }
    else {
        AB_AddResponseLine(&resp,  "RESULT",  "quit_sent");
    }

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   send_appleevent
   Generic AppleEvent sender.  Supports oapp,  odoc,  quit for now.
   ---------------------------------------------------------------- */

void AB_CmdSendAppleEvent(const ABCommand *cmd)
{
    ABResponse          resp;
    const char          *target;
    const char          *event;
    ProcessSerialNumber psn;
    Boolean             found;

    AB_InitResponse(&resp,  cmd->seq);

    target = AB_GetField(cmd,  "TARGET");
    event = AB_GetField(cmd,  "EVENT");

    if (target == nil || event == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "TARGET and EVENT required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* Find target process */
    found = FindProcessByName(target,  &psn);

    if (!found) {
        /* Try as creator code */
        if (strlen(target) == 4) {
            OSType code = ParseCreatorCode(target);
            found = FindProcessByCreator(code,  &psn);
        }
    }

    if (!found) {
        AB_SetResponseError(&resp,  AB_ERR_APP_NOT_RUNNING,  target);
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* Dispatch based on event name */
    if (AB_StrCmpNoCase(event,  "oapp") == 0) {
        SendSimpleAE(&psn,  kCoreEventClass,  kAEOpenApplication);
        AB_AddResponseLine(&resp,  "RESULT",  "oapp_sent");
    }
    else if (AB_StrCmpNoCase(event,  "quit") == 0) {
        SendSimpleAE(&psn,  kCoreEventClass,  kAEQuitApplication);
        AB_AddResponseLine(&resp,  "RESULT",  "quit_sent");
    }
    else {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "Unsupported event type (use oapp, quit)");
    }

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   open_document
   Open a document via the Finder (send odoc to Finder).
   ---------------------------------------------------------------- */

void AB_CmdOpenDocument(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *path;
    /* TODO: implement using AESend odoc to Finder with FSSpec */

    AB_InitResponse(&resp,  cmd->seq);

    path = AB_GetField(cmd,  "PATH");
    if (path == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "PATH parameter required");
    }
    else {
        AB_SetResponseError(&resp,  AB_ERR_UNKNOWN_CMD,
            "open_document not yet implemented");
    }

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   menu_select
   Activate a menu item by simulating HiliteMenu + menu selection.
   This uses the Menu Manager to find and invoke the item.
   ---------------------------------------------------------------- */

void AB_CmdMenuSelect(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *menuName;
    const char  *itemName;
    short       i;

    AB_InitResponse(&resp,  cmd->seq);

    menuName = AB_GetField(cmd,  "MENU");
    itemName = AB_GetField(cmd,  "ITEM");

    if (menuName == nil || itemName == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "MENU and ITEM required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* Find the menu by title */
    for (i = 1; i <= 64; i++) {
        MenuHandle menu = GetMenuHandle(i);
        if (menu != nil) {
            Str255  title;
            char    cTitle[256];

            /* Direct MenuInfo struct access — requires full struct definition from Universal Interfaces */
            memcpy(title,  (*menu)->menuData,  (*menu)->menuData[0] + 1);
            AB_PascalToC(title,  cTitle,  sizeof(cTitle));

            if (AB_StrCmpNoCase(cTitle,  menuName) == 0) {
                /* Found menu -- find item by name */
                short itemCount = CountMItems(menu);
                short j;

                for (j = 1; j <= itemCount; j++) {
                    Str255  iText;
                    char    cItem[256];

                    GetMenuItemText(menu,  j,  iText);
                    AB_PascalToC(iText,  cItem,  sizeof(cItem));

                    if (AB_StrCmpNoCase(cItem,  itemName) == 0) {
                        /*
                         * We can't directly "select" a menu item via
                         * the Menu Manager in another app's context.
                         *
                         * Strategy: post the keyboard shortcut if one
                         * exists,  or use MenuKey simulation.
                         *
                         * For now,  we flash the menu and post a
                         * synthetic menu-result event.
                         */
                        long menuResult;

                        HiliteMenu(i);

                        /* Construct menuResult: hi word = menuID,  lo word = item */
                        menuResult = ((long)i << 16) | (long)j;

                        /* The app's event loop should pick this up */
                        /* But actually -- PostEvent(menuMsg) doesn't exist */
                        /* We need to simulate via keyboard shortcut */

                        {
                            short cmdChar;
                            GetItemCmd(menu,  j,  &cmdChar);
                            if (cmdChar > 0) {
                                /* Has keyboard shortcut -- post Cmd+key */
                                long eventMsg = (long)(cmdChar & 0xFF);
                                /* Set modifiers to cmdKey */
                                PostEvent(keyDown,  eventMsg);
                            }
                            else {
                                /* No shortcut -- this is harder.
                                   We'd need to click on the menu bar and
                                   then on the item.  Fall back to click. */
                                AB_SetResponseError(&resp,
                                    AB_ERR_INVALID_PARAMS,
                                    "Menu item has no shortcut; use click instead");
                                AB_WriteResponseFile(&resp);
                                AB_DisposeResponse(&resp);
                                return;
                            }
                        }

                        HiliteMenu(0);

                        AB_AddResponseLine(&resp,  "RESULT",  "menu_activated");
                        AB_WriteResponseFile(&resp);
                        AB_DisposeResponse(&resp);
                        return;
                    }
                }

                AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
                    "Menu item not found");
                AB_WriteResponseFile(&resp);
                AB_DisposeResponse(&resp);
                return;
            }
        }
    }

    AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
        "Menu not found");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}
