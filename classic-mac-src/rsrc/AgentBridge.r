/*
 * AgentBridge.r - Rez resource file
 * Defines the application's Finder metadata and SIZE resource.
 *
 * The SIZE resource is critical on Classic Mac OS --
 * it tells the system how much memory to allocate for the app.
 */

#include "MacTypes.r"
#include "Processes.r"
#include "Finder.r"

/*
 * SIZE resource: memory requirements
 * AgentBridge is a lightweight background helper.
 * Preferred:  512KB,  Minimum: 256KB
 */
resource 'SIZE' (-1) {
    dontSaveScreen,
    acceptSuspendResumeEvents,
    enableOptionSwitch,
    canBackground,              /* We MUST be able to run in background */
    multiFinderAware,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,      /* Required for AppleEvents */
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    notDisplayManagerAware,
    reserved,
    reserved,
    524288,                     /* preferred memory size: 512KB */
    262144                      /* minimum memory size: 256KB */
};

/*
 * Finder bundle info
 * Creator code: 'AgBr'  (AgentBridge)
 * This lets the Finder identify our application.
 */
type 'AgBr' as 'STR ';
resource 'AgBr' (0,  purgeable) {
    "AgentBridge - Classic Mac OS Agent Communication Bridge"
};

/*
 * Version resource
 */
resource 'vers' (1) {
    0x01,               /* major */
    0x00,               /* minor */
    release,            /* stage */
    0x01,               /* non-release (patch) */
    0,                  /* region: US */
    "1.0.1",            /* short version string */
    "AgentBridge 1.0.1 - AI Agent Communication Bridge"
};

resource 'vers' (2) {
    0x01,
    0x00,
    release,
    0x01,
    0,
    "1.0.1",
    "Falling Data Zone LLC"
};
