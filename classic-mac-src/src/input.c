/*
 * AgentBridge - input.c
 * Synthetic mouse and keyboard event injection
 *
 * Classic Mac OS input injection strategy:
 *
 * PostEvent() posts events to the OS event queue,  but under System 7+
 * with cooperative multitasking,  AgentBridge's own WaitNextEvent calls
 * can consume those events before the frontmost app gets them.
 *
 * Solution:  Asynchronous event injection queue.
 *   1. Command handlers add events to a global queue and return.
 *   2. The main event loop posts one event per iteration.
 *   3. While draining,  the main loop uses a restricted event mask
 *      (excluding key/mouse events) so AgentBridge doesn't consume them.
 *   4. The frontmost app's WaitNextEvent picks up the injected events.
 */

#include <Events.h>
#include <LowMem.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <string.h>

#include "bridge.h"

/* Timing (ticks) */
#define AB_INJECT_DELAY     3   /* ~50ms between injected events */
#define AB_CURSOR_SETTLE    4   /* ~67ms after cursor move before click */

/* ----------------------------------------------------------------
   Virtual key code mapping (US keyboard layout)
   ---------------------------------------------------------------- */

static short CharToKeyCode(char ch)
{
    /* a-z (and A-Z share same physical keys) */
    static const short kAlpha[] = {
        0x00, 0x0B, 0x08, 0x02, 0x0E,  /* a b c d e */
        0x03, 0x05, 0x04, 0x22, 0x26,  /* f g h i j */
        0x28, 0x25, 0x2E, 0x2D, 0x1F,  /* k l m n o */
        0x23, 0x0C, 0x0F, 0x01, 0x11,  /* p q r s t */
        0x20, 0x09, 0x0D, 0x07, 0x10,  /* u v w x y */
        0x06                            /* z */
    };
    static const short kDigit[] = {
        0x1D, 0x12, 0x13, 0x14, 0x15,  /* 0 1 2 3 4 */
        0x17, 0x16, 0x1A, 0x1C, 0x19   /* 5 6 7 8 9 */
    };

    if (ch >= 'a' && ch <= 'z') return kAlpha[ch - 'a'];
    if (ch >= 'A' && ch <= 'Z') return kAlpha[ch - 'A'];
    if (ch >= '0' && ch <= '9') return kDigit[ch - '0'];

    switch (ch) {
        case ' ':  return 0x31;
        case '\r': return 0x24;
        case '\n': return 0x24;
        case '\t': return 0x30;
        case '\b': return 0x33;
        case '-':  case '_':  return 0x1B;
        case '=':  case '+':  return 0x18;
        case '[':  case '{':  return 0x21;
        case ']':  case '}':  return 0x1E;
        case '\\': case '|':  return 0x2A;
        case ';':  case ':':  return 0x29;
        case '\'': case '"':  return 0x27;
        case '`':  case '~':  return 0x32;
        case ',':  case '<':  return 0x2B;
        case '.':  case '>':  return 0x2F;
        case '/':  case '?':  return 0x2C;
        case '!':  return 0x12;
        case '@':  return 0x13;
        case '#':  return 0x14;
        case '$':  return 0x15;
        case '%':  return 0x17;
        case '^':  return 0x16;
        case '&':  return 0x1A;
        case '*':  return 0x1C;
        case '(':  return 0x19;
        case ')':  return 0x1D;
    }

    return 0x00;   /* fallback */
}

/* Build event message: charCode in bits 0-7,  keyCode in bits 8-15 */
static long MakeKeyMessage(char ch)
{
    short kc = CharToKeyCode(ch);
    return ((long)kc << 8) | ((long)ch & 0xFF);
}

/* ----------------------------------------------------------------
   Asynchronous Event Injection Queue
   ---------------------------------------------------------------- */

#define AB_MAX_INJECT   512

/* Special event type: just move cursor,  don't post an event */
#define kInjectCursorMove  (-1)

typedef struct {
    short   what;           /* keyDown / keyUp / mouseDown / mouseUp / kInjectCursorMove */
    long    message;        /* event message */
    short   cursorX;        /* cursor position for mouse events */
    short   cursorY;
    Boolean setCursor;      /* move cursor before posting? */
} ABInjectEvent;

static ABInjectEvent sQueue[AB_MAX_INJECT];
static short sHead  = 0;
static short sTail  = 0;
static short sCount = 0;
static unsigned long sNextTick = 0;

static Boolean AB_EnqueueEvent(short what,  long message,
                       short cx,  short cy,  Boolean setCursor)
{
    ABInjectEvent *ev;

    if (sCount >= AB_MAX_INJECT) return false;

    ev = &sQueue[sTail];
    ev->what      = what;
    ev->message   = message;
    ev->cursorX   = cx;
    ev->cursorY   = cy;
    ev->setCursor = setCursor;

    sTail = (sTail + 1) % AB_MAX_INJECT;
    sCount++;
    return true;
}

/* ---- Public API (called from main.c) ---- */

Boolean AB_HasPendingEvents(void)
{
    return (sCount > 0);
}

void AB_ProcessInjectionQueue(void)
{
    ABInjectEvent *ev;
    unsigned long now;

    if (sCount <= 0) return;

    now = TickCount();
    if (now < sNextTick) return;   /* not yet */

    ev = &sQueue[sHead];

    /* Move cursor if requested */
    if (ev->setCursor) {
        Point pt;
        pt.h = ev->cursorX;
        pt.v = ev->cursorY;
        LMSetMouseTemp(pt);
        LMSetRawMouseLocation(pt);
    }

    /* Post the event (unless it's a cursor-only move) */
    if (ev->what != kInjectCursorMove) {
        PostEvent(ev->what,  ev->message);
    }

    /* Advance */
    sHead = (sHead + 1) % AB_MAX_INJECT;
    sCount--;

    /* Schedule next event */
    sNextTick = now + AB_INJECT_DELAY;
}

short AB_GetInjectionMask(void)
{
    if (sCount > 0) {
        /*
         * Exclude key and mouse events so AgentBridge's WaitNextEvent
         * does NOT dequeue the events we just posted.
         * The frontmost app will pick them up instead.
         */
        return everyEvent & ~(keyDownMask | keyUpMask | autoKeyMask
                              | mDownMask | mUpMask);
    }
    return everyEvent;
}

/* ----------------------------------------------------------------
   click  --  enqueue mouse down/up at (x,y)
   ---------------------------------------------------------------- */

void AB_CmdClick(const ABCommand *cmd)
{
    ABResponse  resp;
    long        x,  y,  clicks;
    short       i;

    AB_InitResponse(&resp,  cmd->seq);

    x = AB_GetFieldLong(cmd,  "X",  -1);
    y = AB_GetFieldLong(cmd,  "Y",  -1);
    clicks = AB_GetFieldLong(cmd,  "CLICKS",  1);

    if (x < 0 || y < 0) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "X and Y coordinates required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    if (clicks < 1) clicks = 1;
    if (clicks > 3) clicks = 3;

    /* First: move cursor to target (cursor-only event) */
    AB_EnqueueEvent(kInjectCursorMove,  0,  (short)x,  (short)y,  true);

    /* Then: down/up pairs */
    for (i = 0; i < clicks; i++) {
        AB_EnqueueEvent(mouseDown,  0,  (short)x,  (short)y,  true);
        AB_EnqueueEvent(mouseUp,    0,  (short)x,  (short)y,  true);
    }

    AB_AddResponseLine(&resp,  "RESULT",  "clicked");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   mouse_move  --  immediate (no event posting needed)
   ---------------------------------------------------------------- */

void AB_CmdMouseMove(const ABCommand *cmd)
{
    ABResponse resp;
    long x,  y;
    Point pt;

    AB_InitResponse(&resp,  cmd->seq);

    x = AB_GetFieldLong(cmd,  "X",  -1);
    y = AB_GetFieldLong(cmd,  "Y",  -1);

    if (x < 0 || y < 0) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "X and Y coordinates required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    pt.h = (short)x;
    pt.v = (short)y;
    LMSetMouseTemp(pt);
    LMSetRawMouseLocation(pt);

    AB_AddResponseLine(&resp,  "RESULT",  "moved");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   mouse_drag  --  enqueue down,  interpolated moves,  up
   ---------------------------------------------------------------- */

void AB_CmdMouseDrag(const ABCommand *cmd)
{
    ABResponse  resp;
    long        x1,  y1,  x2,  y2;
    short       steps,  i;
    long        dx,  dy,  dist;

    AB_InitResponse(&resp,  cmd->seq);

    x1 = AB_GetFieldLong(cmd,  "X1",  -1);
    y1 = AB_GetFieldLong(cmd,  "Y1",  -1);
    x2 = AB_GetFieldLong(cmd,  "X2",  -1);
    y2 = AB_GetFieldLong(cmd,  "Y2",  -1);

    if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "X1, Y1, X2, Y2 required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    dx = x2 - x1;
    dy = y2 - y1;
    dist = dx * dx + dy * dy;
    steps = 1;
    if (dist > 400)   steps = 10;
    if (dist > 10000)  steps = 20;

    /* Move cursor to start */
    AB_EnqueueEvent(kInjectCursorMove,  0,  (short)x1,  (short)y1,  true);

    /* Mouse down at start */
    AB_EnqueueEvent(mouseDown,  0,  (short)x1,  (short)y1,  true);

    /* Interpolated cursor moves */
    for (i = 1; i <= steps; i++) {
        short cx = (short)(x1 + dx * i / steps);
        short cy = (short)(y1 + dy * i / steps);
        AB_EnqueueEvent(kInjectCursorMove,  0,  cx,  cy,  true);
    }

    /* Mouse up at end */
    AB_EnqueueEvent(mouseUp,  0,  (short)x2,  (short)y2,  true);

    AB_AddResponseLine(&resp,  "RESULT",  "dragged");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   Key name lookup table
   ---------------------------------------------------------------- */

typedef struct {
    const char *name;
    short       keyCode;
    short       charCode;
} ABKeyMap;

static const ABKeyMap sKeyMap[] = {
    { "return",     0x24,   0x0D },
    { "enter",      0x4C,   0x03 },
    { "tab",        0x30,   0x09 },
    { "space",      0x31,   0x20 },
    { "delete",     0x33,   0x08 },     /* backspace */
    { "fwd_delete", 0x75,   0x7F },
    { "escape",     0x35,   0x1B },
    { "left",       0x7B,   0x1C },
    { "right",      0x7C,   0x1D },
    { "up",         0x7E,   0x1E },
    { "down",       0x7D,   0x1F },
    { "home",       0x73,   0x01 },
    { "end",        0x77,   0x04 },
    { "pageup",     0x74,   0x0B },
    { "pagedown",   0x79,   0x0C },
    { "f1",         0x7A,   0x00 },
    { "f2",         0x78,   0x00 },
    { "f3",         0x63,   0x00 },
    { "f4",         0x76,   0x00 },
    { "f5",         0x60,   0x00 },
    { nil,          0,      0 }
};

static Boolean LookupKey(const char *name,  short *keyCode,  short *charCode)
{
    const ABKeyMap *entry = sKeyMap;

    /* Single printable character */
    if (name[0] != '\0' && name[1] == '\0') {
        *charCode = (short)name[0];
        *keyCode  = CharToKeyCode(name[0]);
        return true;
    }

    while (entry->name != nil) {
        if (AB_StrCmpNoCase(name,  entry->name) == 0) {
            *keyCode  = entry->keyCode;
            *charCode = entry->charCode;
            return true;
        }
        entry++;
    }

    return false;
}

/* Parse modifier string like "cmd,opt,shift" into modifier bits */
static short ParseModifiers(const char *modStr)
{
    short mods = 0;

    if (modStr == nil) return 0;

    if (strstr(modStr,  "cmd") != nil)   mods |= cmdKey;
    if (strstr(modStr,  "opt") != nil)   mods |= optionKey;
    if (strstr(modStr,  "shift") != nil) mods |= shiftKey;
    if (strstr(modStr,  "ctrl") != nil)  mods |= controlKey;

    return mods;
}

/* ----------------------------------------------------------------
   key_press  --  enqueue single key down/up
   ---------------------------------------------------------------- */

void AB_CmdKeyPress(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *keyName;
    const char  *modStr;
    short       keyCode,  charCode;
    short       mods;
    long        eventMsg;

    AB_InitResponse(&resp,  cmd->seq);

    keyName = AB_GetField(cmd,  "KEY");
    if (keyName == nil) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "KEY parameter required");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    if (!LookupKey(keyName,  &keyCode,  &charCode)) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "Unknown key name");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    modStr = AB_GetField(cmd,  "MODIFIERS");
    mods = ParseModifiers(modStr);
    (void)mods;   /* TODO: set modifier state via low-memory globals */

    eventMsg = ((long)keyCode << 8) | (charCode & 0xFF);

    AB_EnqueueEvent(keyDown,  eventMsg,  0,  0,  false);
    AB_EnqueueEvent(keyUp,    eventMsg,  0,  0,  false);

    AB_AddResponseLine(&resp,  "RESULT",  "key_pressed");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}

/* ----------------------------------------------------------------
   type_text  --  enqueue keyDown/keyUp for each character
   ---------------------------------------------------------------- */

void AB_CmdTypeText(const ABCommand *cmd)
{
    ABResponse  resp;
    const char  *text;
    short       i,  len;

    AB_InitResponse(&resp,  cmd->seq);

    text = cmd->textBuf;
    len = cmd->textLen;

    if (len == 0) {
        text = AB_GetField(cmd,  "TEXT");
        if (text != nil) {
            len = strlen(text);
        }
    }

    if (len == 0) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "No text to type");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    /* Check queue capacity (need 2 events per character) */
    if (len * 2 > AB_MAX_INJECT - sCount) {
        AB_SetResponseError(&resp,  AB_ERR_INVALID_PARAMS,
            "Text too long for injection queue");
        AB_WriteResponseFile(&resp);
        AB_DisposeResponse(&resp);
        return;
    }

    for (i = 0; i < len; i++) {
        char ch = text[i];
        long msg;

        if (ch == '\0') continue;

        msg = MakeKeyMessage(ch);

        AB_EnqueueEvent(keyDown,  msg,  0,  0,  false);
        AB_EnqueueEvent(keyUp,    msg,  0,  0,  false);
    }

    AB_AddResponseLine(&resp,  "RESULT",  "text_typed");
    AB_WriteResponseFile(&resp);
    AB_DisposeResponse(&resp);
}
