/*
 * AgentBridge - main.c
 * Application entry point,  event loop,  initialization
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Events.h>
#include <Dialogs.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Processes.h>
#include <AppleEvents.h>
#include <Gestalt.h>
#include <Sound.h>

#include "bridge.h"

/* Global state */
ABGlobals gAB;

/* ----------------------------------------------------------------
   Toolbox Initialization (Classic Mac boilerplate)
   ---------------------------------------------------------------- */

static void InitToolbox(void)
{
    /* Standard Classic Mac Toolbox init sequence */
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(nil);
    InitCursor();

    /* Ensure we get enough master pointers */
    MoreMasters();
    MoreMasters();
    MoreMasters();
    MoreMasters();
}

/* ----------------------------------------------------------------
   AppleEvent Handlers (required for System 7+ compliance)
   ---------------------------------------------------------------- */

static pascal OSErr HandleOApp(const AppleEvent *evt,  AppleEvent *reply,  long refcon)
{
#pragma unused(evt, reply, refcon)
    return noErr;
}

static pascal OSErr HandleODoc(const AppleEvent *evt,  AppleEvent *reply,  long refcon)
{
#pragma unused(evt, reply, refcon)
    return errAEEventNotHandled;
}

static pascal OSErr HandlePDoc(const AppleEvent *evt,  AppleEvent *reply,  long refcon)
{
#pragma unused(evt, reply, refcon)
    return errAEEventNotHandled;
}

static pascal OSErr HandleQuit(const AppleEvent *evt,  AppleEvent *reply,  long refcon)
{
#pragma unused(evt, reply, refcon)
    gAB.running = false;
    return noErr;
}

static void InstallAEHandlers(void)
{
    AEInstallEventHandler(kCoreEventClass,  kAEOpenApplication,
        NewAEEventHandlerUPP(HandleOApp),  0,  false);
    AEInstallEventHandler(kCoreEventClass,  kAEOpenDocuments,
        NewAEEventHandlerUPP(HandleODoc),  0,  false);
    AEInstallEventHandler(kCoreEventClass,  kAEPrintDocuments,
        NewAEEventHandlerUPP(HandlePDoc),  0,  false);
    AEInstallEventHandler(kCoreEventClass,  kAEQuitApplication,
        NewAEEventHandlerUPP(HandleQuit),  0,  false);
}

/* ----------------------------------------------------------------
   Splash Screen (shown for ~3 seconds on launch)
   ---------------------------------------------------------------- */

static void ShowSplash(void)
{
    Rect        wRect;
    WindowPtr   splashWin;
    Rect        screenRect;
    short       screenW, screenH;
    unsigned long endTicks;
    EventRecord evt;
    short       wWidth = 340;
    short       wHeight = 100;

    /* Center on screen */
    screenRect = qd.screenBits.bounds;
    screenW = screenRect.right - screenRect.left;
    screenH = screenRect.bottom - screenRect.top;

    wRect.left   = screenRect.left + (screenW - wWidth) / 2;
    wRect.top    = screenRect.top  + (screenH - wHeight) / 2;
    wRect.right  = wRect.left + wWidth;
    wRect.bottom = wRect.top + wHeight;

    /* plainDBox = no title bar, no close box, just a plain rectangle */
    splashWin = NewWindow(nil,  &wRect,  "\p",  true,
                          plainDBox,  (WindowPtr)-1,  false,  0);
    if (splashWin == nil) return;

    SetPort(splashWin);
    EraseRect(&splashWin->portRect);

    /* Draw "AgentBridge" centered, bold */
    TextSize(12);
    TextFace(bold);
    {
        const char *title = "AgentBridge";
        short tw = TextWidth(title,  0,  11);
        MoveTo((wWidth - tw) / 2,  30);
        DrawText(title,  0,  11);
    }

    /* Draw "Sean Lavigne" centered, plain */
    TextFace(0);
    TextSize(0);    /* system default (12pt Chicago) */
    {
        const char *author = "Sean Lavigne";
        short tw = TextWidth(author,  0,  12);
        MoveTo((wWidth - tw) / 2,  50);
        DrawText(author,  0,  12);
    }

    /* Bottom line: copyright left, version right */
    {
        /* \xA9 is the copyright symbol in MacRoman */
        const char *copy = "\xA9 Falling Data Zone, LLC 2026";
        const char *ver  = "Version 1.0.1";
        short copyLen = 30;
        short verLen  = 13;

        MoveTo(10,  85);
        DrawText(copy,  0,  copyLen);

        {
            short vw = TextWidth(ver,  0,  verLen);
            MoveTo(wWidth - 10 - vw,  85);
            DrawText(ver,  0,  verLen);
        }
    }

    /* Wait ~3 seconds (180 ticks) cooperatively */
    endTicks = TickCount() + 180;
    while (TickCount() < endTicks) {
        WaitNextEvent(everyEvent,  &evt,  6,  nil);
    }

    DisposeWindow(splashWin);
}

/* ----------------------------------------------------------------
   System Version Check
   ---------------------------------------------------------------- */

static Boolean CheckSystemVersion(void)
{
    long response;
    OSErr err;

    err = Gestalt(gestaltSystemVersion,  &response);
    if (err != noErr) return false;

    /* Require System 7.0 or later (for AppleEvents) */
    if (response < 0x0700) {
        /* Could show an alert here */
        return false;
    }

    return true;
}

/* ----------------------------------------------------------------
   Initialize Bridge State
   ---------------------------------------------------------------- */

static Boolean InitBridge(void)
{
    OSErr err;

    /* Zero out globals */
    gAB.running             = true;
    gAB.nextSeq             = 0;
    gAB.startTicks          = TickCount();
    gAB.lastHeartbeat       = 0;
    gAB.pollInterval        = AB_POLL_TICKS;
    gAB.heartbeatInterval   = AB_HEARTBEAT_TICKS;
    gAB.verbose             = false;
    gAB.sharedVRefNum       = 0;
    gAB.sharedDirID         = 0;

    /* Locate the shared folder volume */
    err = AB_ResolveSharedFolder();
    if (err != noErr) {
        AB_Log("FATAL: Cannot find shared folder volume");
        return false;
    }

    AB_Log("AgentBridge starting up");
    AB_Log("Shared folder located");

    /* Write initial heartbeat */
    AB_WriteHeartbeat();

    return true;
}

/* ----------------------------------------------------------------
   Process System Events
   ---------------------------------------------------------------- */

static void HandleMouseDown(EventRecord *evt)
{
    WindowPtr window;
    short part;

    part = FindWindow(evt->where,  &window);

    switch (part) {
        case inMenuBar:
            /* We have no menus of our own,  but handle gracefully */
            HiliteMenu(0);
            break;

        case inSysWindow:
            SystemClick(evt,  window);
            break;

        default:
            break;
    }
}

static void HandleHighLevelEvent(EventRecord *evt)
{
    AEProcessAppleEvent(evt);
}

static void ProcessEvent(EventRecord *evt)
{
    switch (evt->what) {
        case mouseDown:
            HandleMouseDown(evt);
            break;

        case keyDown:
        case autoKey:
            /* Check for Cmd-Q */
            if (evt->modifiers & cmdKey) {
                char key = evt->message & charCodeMask;
                if (key == 'q' || key == 'Q') {
                    gAB.running = false;
                }
            }
            break;

        case kHighLevelEvent:
            HandleHighLevelEvent(evt);
            break;

        case updateEvt:
        case activateEvt:
        case osEvt:
            /* AgentBridge is faceless -- minimal event handling */
            break;

        default:
            break;
    }
}

/* ----------------------------------------------------------------
   Main Loop
   ---------------------------------------------------------------- */

static void RunBridge(void)
{
    EventRecord event;
    unsigned long currentTicks;

    while (gAB.running) {
        /*
         * WaitNextEvent yields CPU to other apps.
         *
         * When the injection queue is active,  we use a restricted
         * event mask that excludes key/mouse events.  This prevents
         * AgentBridge from consuming events we posted for the
         * frontmost app.  We also use a shorter sleep time so
         * events are injected promptly.
         */
        {
            short mask    = AB_GetInjectionMask();
            long  sleepT  = AB_HasPendingEvents() ? 1 : gAB.pollInterval;

            WaitNextEvent(mask,  &event,  sleepT,  nil);
        }

        /* Handle any system events first */
        ProcessEvent(&event);

        if (!gAB.running) break;

        /* Post one pending injection event (if any) */
        AB_ProcessInjectionQueue();

        /* Poll the inbox for new command files */
        AB_ScanInbox();

        /* Write heartbeat if due */
        currentTicks = TickCount();
        if (currentTicks - gAB.lastHeartbeat >= gAB.heartbeatInterval) {
            AB_WriteHeartbeat();
        }
    }
}

/* ----------------------------------------------------------------
   Cleanup
   ---------------------------------------------------------------- */

static void ShutdownBridge(void)
{
    AB_Log("AgentBridge shutting down");
    /* Could clean stale files from inbox/outbox here */
}

/* ----------------------------------------------------------------
   Entry Point
   ---------------------------------------------------------------- */

void main(void)
{
    InitToolbox();

    if (!CheckSystemVersion()) {
        SysBeep(10);
        ExitToShell();
    }

    ShowSplash();

    InstallAEHandlers();

    if (!InitBridge()) {
        SysBeep(10);
        ExitToShell();
    }

    RunBridge();
    ShutdownBridge();
    ExitToShell();
}
