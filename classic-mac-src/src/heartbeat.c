/*
 * AgentBridge - heartbeat.c
 * Periodic heartbeat file writer.
 *
 * Writes current status to /SharedVolume/heartbeat every N seconds.
 * The MCP server monitors this file to know if AgentBridge is alive.
 * The heartbeat doubles as a lightweight status beacon.
 */

#include <Files.h>
#include <Processes.h>
#include <Memory.h>
#include <string.h>

#include "bridge.h"

void AB_WriteHeartbeat(void)
{
    OSErr   err;
    short   fRefNum;
    Str255  pFilename;
    long    count;
    char    buf[512];
    char    numBuf[16];
    char    ts[24];

    /* Get front app name for status */
    char frontApp[256];
    {
        ProcessSerialNumber psn;
        ProcessInfoRec      info;
        Str255              procName;
        FSSpec              procSpec;

        AB_StrCopy(frontApp,  "unknown",  sizeof(frontApp));

        err = GetFrontProcess(&psn);
        if (err == noErr) {
            memset(&info,  0,  sizeof(info));
            info.processInfoLength = sizeof(ProcessInfoRec);
            info.processName = procName;
            info.processAppSpec = &procSpec;

            if (GetProcessInformation(&psn,  &info) == noErr) {
                AB_PascalToC(procName,  frontApp,  sizeof(frontApp));
            }
        }
    }

    /* Format heartbeat content */
    AB_FormatTimestamp(ts);

    AB_StrCopy(buf,  "BRIDGE ",  sizeof(buf));
    AB_StrCat(buf,  AB_VERSION,  sizeof(buf));
    AB_StrCat(buf,  "\r",  sizeof(buf));

    AB_StrCat(buf,  "UPTIME ",  sizeof(buf));
    AB_LongToStr((TickCount() - gAB.startTicks) / 60,  numBuf);
    AB_StrCat(buf,  numBuf,  sizeof(buf));
    AB_StrCat(buf,  "\r",  sizeof(buf));

    AB_StrCat(buf,  "TICKS ",  sizeof(buf));
    AB_LongToStr(TickCount(),  numBuf);
    AB_StrCat(buf,  numBuf,  sizeof(buf));
    AB_StrCat(buf,  "\r",  sizeof(buf));

    AB_StrCat(buf,  "FRONTAPP ",  sizeof(buf));
    AB_StrCat(buf,  frontApp,  sizeof(buf));
    AB_StrCat(buf,  "\r",  sizeof(buf));

    AB_StrCat(buf,  "FREEMEM ",  sizeof(buf));
    AB_LongToStr(FreeMem() / 1024,  numBuf);
    AB_StrCat(buf,  numBuf,  sizeof(buf));
    AB_StrCat(buf,  "\r",  sizeof(buf));

    AB_StrCat(buf,  "TS ",  sizeof(buf));
    AB_StrCat(buf,  ts,  sizeof(buf));
    AB_StrCat(buf,  "\r",  sizeof(buf));

    AB_StrCat(buf,  "---\r",  sizeof(buf));

    /* Write to heartbeat file (create or overwrite) */
    AB_CToPascal(AB_HEARTBEAT_FILE,  pFilename);

    /* Delete existing file first */
    HDelete(gAB.sharedVRefNum,  gAB.sharedDirID,  pFilename);

    err = HCreate(gAB.sharedVRefNum,  gAB.sharedDirID,
                  pFilename,  'MACS',  'TEXT');
    if (err != noErr && err != dupFNErr) return;

    err = HOpenDF(gAB.sharedVRefNum,  gAB.sharedDirID,
                  pFilename,  fsWrPerm,  &fRefNum);
    if (err != noErr) return;

    count = strlen(buf);
    FSWrite(fRefNum,  &count,  buf);
    FSClose(fRefNum);
    FlushVol(nil,  gAB.sharedVRefNum);

    gAB.lastHeartbeat = TickCount();
}
