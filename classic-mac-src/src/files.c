/*
 * AgentBridge - files.c
 * Shared folder management:  locating the shared volume,
 * scanning the inbox for commands,  file transfer staging.
 */

#include <Files.h>
#include <Processes.h>
#include <Memory.h>
#include <string.h>

#include "bridge.h"

/* ----------------------------------------------------------------
   Helper: resolve a subdirectory by name within a parent dir.
   Returns the directory ID of the child, or an error.
   ---------------------------------------------------------------- */

static OSErr ResolveSubdir(short vRefNum,  long parentDirID,
                           const char *name,  long *outDirID)
{
    CInfoPBRec  cpb;
    Str255      dirName;

    AB_CToPascal(name,  dirName);
    memset(&cpb,  0,  sizeof(cpb));
    cpb.dirInfo.ioNamePtr = dirName;
    cpb.dirInfo.ioVRefNum = vRefNum;
    cpb.dirInfo.ioDrDirID = parentDirID;
    cpb.dirInfo.ioFDirIndex = 0;

    {
        OSErr err = PBGetCatInfoSync(&cpb);
        if (err != noErr) return err;
        if (!(cpb.dirInfo.ioFlAttrib & 0x10)) return fnfErr;
        *outDirID = cpb.dirInfo.ioDrDirID;
    }
    return noErr;
}

/* ----------------------------------------------------------------
   Helper: given a working directory (vRefNum + dirID), resolve
   the inbox/, outbox/, assets/ subdirectories into globals.
   ---------------------------------------------------------------- */

static OSErr ResolveSubdirs(short vRefNum,  long dirID)
{
    OSErr err;

    gAB.sharedVRefNum = vRefNum;
    gAB.sharedDirID   = dirID;

    err = ResolveSubdir(vRefNum,  dirID,  AB_INBOX_DIR,  &gAB.inboxDirID);
    if (err != noErr) return err;

    err = ResolveSubdir(vRefNum,  dirID,  AB_OUTBOX_DIR,  &gAB.outboxDirID);
    if (err != noErr) return err;

    err = ResolveSubdir(vRefNum,  dirID,  AB_ASSETS_DIR,  &gAB.assetsDirID);
    if (err != noErr) return err;

    return noErr;
}

/* ----------------------------------------------------------------
   Primary strategy: use the app's own launch location.

   GetCurrentProcess + GetProcessInformation gives us an FSSpec
   for the application file.  The FSSpec's vRefNum and parID
   tell us exactly which folder the app lives in.  We use that
   folder as the working directory (inbox/, outbox/, assets/
   are siblings of the app).
   ---------------------------------------------------------------- */

static OSErr ResolveFromAppLocation(void)
{
    ProcessSerialNumber psn;
    ProcessInfoRec      info;
    Str255              procName;
    FSSpec              appSpec;
    OSErr               err;

    err = GetCurrentProcess(&psn);
    if (err != noErr) return err;

    memset(&info,  0,  sizeof(info));
    info.processInfoLength = sizeof(ProcessInfoRec);
    info.processName = procName;
    info.processAppSpec = &appSpec;

    err = GetProcessInformation(&psn,  &info);
    if (err != noErr) return err;

    /* appSpec.vRefNum = volume,  appSpec.parID = parent directory */
    err = ResolveSubdirs(appSpec.vRefNum,  appSpec.parID);
    if (err != noErr) return err;

    AB_Log("Using app launch folder as working directory");
    return noErr;
}

/* ----------------------------------------------------------------
   Fallback strategy: scan volumes for an "AgentBridge" folder.
   ---------------------------------------------------------------- */

#define BRIDGE_DIR_NAME     "AgentBridge"

static OSErr ResolveByVolumeScan(void)
{
    HParamBlockRec  pb;
    Str255          volName;
    short           index = 1;
    OSErr           err;

    while (1) {
        long bridgeDirID;

        memset(&pb,  0,  sizeof(pb));
        pb.volumeParam.ioNamePtr = volName;
        pb.volumeParam.ioVolIndex = index;

        err = PBHGetVInfoSync(&pb);
        if (err != noErr) break;

        /* Check if this volume has an "AgentBridge" directory at root */
        err = ResolveSubdir(pb.volumeParam.ioVRefNum,  2,
                            BRIDGE_DIR_NAME,  &bridgeDirID);
        if (err == noErr) {
            char cVolName[256];
            AB_PascalToC(volName,  cVolName,  sizeof(cVolName));
            AB_Log("Found AgentBridge on volume: ");
            AB_Log(cVolName);

            return ResolveSubdirs(pb.volumeParam.ioVRefNum,  bridgeDirID);
        }

        index++;
    }

    return fnfErr;
}

/* ----------------------------------------------------------------
   Resolve the shared folder.

   Primary: use the app's own launch location (per-Mac subfolder).
   Fallback: scan volumes for an "AgentBridge" folder.
   ---------------------------------------------------------------- */

OSErr AB_ResolveSharedFolder(void)
{
    OSErr err;

    /* Try app launch location first */
    err = ResolveFromAppLocation();
    if (err == noErr) return noErr;

    AB_Log("App location failed, falling back to volume scan");

    /* Fall back to scanning volumes */
    return ResolveByVolumeScan();
}

/* ----------------------------------------------------------------
   Scan inbox for command files.
   Look for files matching C?????.msg pattern,
   process each in sequence order,  delete after processing.
   ---------------------------------------------------------------- */

OSErr AB_ScanInbox(void)
{
    CInfoPBRec  cpb;
    Str255      fileName;
    short       index = 1;
    OSErr       err;

    while (1) {
        char    cName[256];

        memset(&cpb,  0,  sizeof(cpb));
        cpb.hFileInfo.ioNamePtr = fileName;
        cpb.hFileInfo.ioVRefNum = gAB.sharedVRefNum;
        cpb.hFileInfo.ioDirID = gAB.inboxDirID;
        cpb.hFileInfo.ioFDirIndex = index;

        err = PBGetCatInfoSync(&cpb);
        if (err != noErr) break;    /* No more files */

        AB_PascalToC(fileName,  cName,  sizeof(cName));

        /* Check if it's a command file: starts with 'C' and ends with '.msg' */
        if (cName[0] == 'C') {
            short nameLen = strlen(cName);
            if (nameLen > 4 &&
                cName[nameLen-4] == '.' &&
                cName[nameLen-3] == 'm' &&
                cName[nameLen-2] == 's' &&
                cName[nameLen-1] == 'g')
            {
                /* Found a command file -- open and parse it */
                short       fRefNum;
                ABCommand   cmd;

                err = HOpenDF(gAB.sharedVRefNum,  gAB.inboxDirID,
                              fileName,  fsRdPerm,  &fRefNum);
                if (err == noErr) {
                    err = AB_ParseCommandFile(fRefNum,  &cmd);
                    FSClose(fRefNum);

                    if (err == noErr) {
                        /* Dispatch the command */
                        AB_DispatchCommand(&cmd);
                    }

                    /* Delete the command file after processing */
                    HDelete(gAB.sharedVRefNum,  gAB.inboxDirID,  fileName);
                    FlushVol(nil,  gAB.sharedVRefNum);

                    /*
                     * After deleting,  reset index to 1 since the
                     * directory listing shifted.
                     */
                    index = 1;
                    continue;
                }
            }
        }

        index++;
    }

    return noErr;
}

/* ----------------------------------------------------------------
   stage_file
   Copy a file from assets/ to a Mac-local destination.
   ---------------------------------------------------------------- */

void AB_CmdStageFile(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *filename;
    const char  *dest;

    AB_InitResponse(&resp,  cmd->seq);

    filename = AB_GetField(cmd,  "FILENAME");
    dest = AB_GetField(cmd,  "DEST");

    if (filename == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "FILENAME required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* TODO: implement file copy from assets/ to Mac destination */
    /* Would use PBHCopyFile or manual read/write loop */
    AB_SetResponseError(&resp,  AB_ERR_UNKNOWN_CMD,
        "stage_file copy not yet implemented");

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   retrieve_file
   Copy a Mac-local file to assets/ for the host to pick up.
   ---------------------------------------------------------------- */

void AB_CmdRetrieveFile(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *path;

    AB_InitResponse(&resp,  cmd->seq);

    path = AB_GetField(cmd,  "PATH");

    if (path == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "PATH required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* TODO: implement file copy to assets/ */
    AB_SetResponseError(&resp,  AB_ERR_UNKNOWN_CMD,
        "retrieve_file copy not yet implemented");

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   delete_file
   ---------------------------------------------------------------- */

void AB_CmdDeleteFile(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *path;
    Str255      pPath;
    OSErr       err;

    AB_InitResponse(&resp,  cmd->seq);

    path = AB_GetField(cmd,  "PATH");

    if (path == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "PATH required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    AB_CToPascal(path,  pPath);
    err = HDelete(0,  0,  pPath);

    if (err == fnfErr) {
        AB_SetResponseError(&resp,  AB_ERR_FILE_NOT_FOUND,  path);
    }
    else if (err != noErr) {
        AB_SetResponseError(&resp,  AB_ERR_TOOLBOX,
            "HDelete failed");
    }
    else {
        AB_AddResponseLine(&resp,  "RESULT",  "deleted");
    }

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}
