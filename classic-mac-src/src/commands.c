/*
 * AgentBridge - commands.c
 * Command dispatcher -- routes parsed commands to handler functions
 */

#include "bridge.h"

void AB_DispatchCommand(const ABCommand *cmd)
{
    if (gAB.verbose) {
        char logBuf[64];
        AB_StrCopy(logBuf,  "Dispatch: ",  sizeof(logBuf));
        AB_StrCat(logBuf,  cmd->cmdStr,  sizeof(logBuf));
        AB_Log(logBuf);
    }

    switch (cmd->cmdID) {

        /* -- Introspection -- */
        case kCmdPing:              AB_CmdPing(cmd);            break;
        case kCmdListWindows:       AB_CmdListWindows(cmd);     break;
        case kCmdListProcesses:     AB_CmdListProcesses(cmd);   break;
        case kCmdListMenus:         AB_CmdListMenus(cmd);       break;
        case kCmdGetMenuItems:      AB_CmdGetMenuItems(cmd);    break;
        case kCmdGetFrontWindow:    AB_CmdGetFrontWindow(cmd);  break;
        case kCmdGetClipboard:      AB_CmdGetClipboard(cmd);    break;
        case kCmdGetVolumes:        AB_CmdGetVolumes(cmd);      break;
        case kCmdListFolder:        AB_CmdListFolder(cmd);      break;
        case kCmdGetAbout:          AB_CmdGetAbout(cmd);        break;

        /* -- Input injection -- */
        case kCmdClick:             AB_CmdClick(cmd);           break;
        case kCmdMouseMove:         AB_CmdMouseMove(cmd);       break;
        case kCmdMouseDrag:         AB_CmdMouseDrag(cmd);       break;
        case kCmdKeyPress:          AB_CmdKeyPress(cmd);        break;
        case kCmdTypeText:          AB_CmdTypeText(cmd);        break;

        /* -- Clipboard -- */
        case kCmdSetClipboard:      AB_CmdSetClipboard(cmd);    break;

        /* -- AppleEvent / App control -- */
        case kCmdMenuSelect:        AB_CmdMenuSelect(cmd);      break;
        case kCmdSendAppleEvent:    AB_CmdSendAppleEvent(cmd);  break;
        case kCmdLaunchApp:         AB_CmdLaunchApp(cmd);       break;
        case kCmdOpenDocument:      AB_CmdOpenDocument(cmd);     break;
        case kCmdActivateApp:       AB_CmdActivateApp(cmd);     break;
        case kCmdQuitApp:           AB_CmdQuitApp(cmd);         break;

        /* -- File transfer -- */
        case kCmdStageFile:         AB_CmdStageFile(cmd);       break;
        case kCmdRetrieveFile:      AB_CmdRetrieveFile(cmd);    break;
        case kCmdDeleteFile:        AB_CmdDeleteFile(cmd);      break;

        /* -- Composite -- */
        case kCmdWaitForWindow:
        case kCmdWaitForIdle:
        case kCmdScreenshotRegion:
            /* TODO: implement composite commands */
            {
                ABResponse resp;
                AB_InitResponse(&resp,  cmd->seq);
                AB_SetResponseError(&resp,  AB_ERR_UNKNOWN_CMD,
                    "Composite command not yet implemented");
                AB_WriteResponseFile(&resp);
                AB_DisposeResponse(&resp);
            }
            break;

        /* -- Lifecycle -- */
        case kCmdShutdown:
            {
                ABResponse resp;
                AB_InitResponse(&resp,  cmd->seq);
                AB_AddResponseLine(&resp,  "RESULT",  "shutting_down");
                AB_WriteResponseFile(&resp);
                AB_DisposeResponse(&resp);
                gAB.running = false;
            }
            break;

        /* -- Unknown -- */
        case kCmdUnknown:
        default:
            {
                ABResponse resp;
                AB_InitResponse(&resp,  cmd->seq);
                AB_SetResponseError(&resp,  AB_ERR_UNKNOWN_CMD,
                    cmd->cmdStr[0] ? cmd->cmdStr : "empty command");
                AB_WriteResponseFile(&resp);
                AB_DisposeResponse(&resp);
            }
            break;
    }
}
