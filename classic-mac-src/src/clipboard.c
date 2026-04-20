/*
 * AgentBridge - clipboard.c
 * Clipboard (Scrap Manager) operations
 *
 * The Classic Mac clipboard uses the Scrap Manager.
 * TEXT scrap type is what we care about for agent interaction.
 */

#include <Scrap.h>
#include <Memory.h>
#include <string.h>

#include "bridge.h"

/* ----------------------------------------------------------------
   get_clipboard
   ---------------------------------------------------------------- */

void AB_CmdGetClipboard(const ABCommand *cmd)
{
    ABResponse  resp;
    Handle      textH;
    long        offset;
    long        len;

    AB_InitResponse(&resp,  cmd->seq);

    textH = NewHandle(0);
    if (textH == nil) {
        AB_SetResponseError(&resp,  AB_ERR_OUT_OF_MEMORY,
            "Cannot allocate handle for clipboard");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    len = GetScrap(textH,  'TEXT',  &offset);

    if (len <= 0) {
        AB_AddResponseLine(&resp,  "RESULT",  "empty");
        AB_AddResponseLine(&resp,  "TEXT",  "");
    }
    else {
        /*
         * Clipboard text may be large.  Truncate to our max
         * and return what fits in a single response.
         */
        if (len > AB_MAX_MSG_SIZE - 256) {
            len = AB_MAX_MSG_SIZE - 256;
        }

        HLock(textH);
        {
            char *text = *textH;
            /* Null-terminate for safety (may not be terminated in scrap) */
            char saveCh = text[len];
            text[len] = '\0';

            AB_AddResponseLine(&resp,  "RESULT",  "text");
            AB_AddResponseLine(&resp,  "TEXT",  text);
            AB_AddResponseLineFmt(&resp,  "LENGTH",  len);

            text[len] = saveCh;
        }
        HUnlock(textH);
    }

    DisposeHandle(textH);

    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   set_clipboard
   ---------------------------------------------------------------- */

void AB_CmdSetClipboard(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *text;
    short       len;
    long        llen;
    OSErr       err;

    AB_InitResponse(&resp,  cmd->seq);

    text = cmd->textBuf;
    len = cmd->textLen;

    if (len == 0) {
        text = AB_GetField(cmd,  "TEXT");
        if (text != nil) len = strlen(text);
    }

    /* Clear existing scrap */
    ZeroScrap();

    if (len > 0) {
        llen = (long)len;
        err = PutScrap(llen,  'TEXT',  (Ptr)text);
        if (err != noErr) {
            AB_SetResponseError(&resp,  AB_ERR_TOOLBOX,
                "PutScrap failed");
            AB_WriteResponseFile(&resp);
            AB_DisposeResponse(&resp);
            return;
        }
    }

    AB_AddResponseLine(&resp,  "RESULT",  "clipboard_set");
    AB_AddResponseLineFmt(&resp,  "LENGTH",  (long)len);
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}
