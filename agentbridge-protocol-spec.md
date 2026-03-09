# AgentBridge Protocol Specification

## Classic Mac OS ↔ MCP Server Communication Protocol

**Version:** 0.1.0-draft
**Target Environments:** Mac OS 7.1–9.2.2 (68k and PowerPC)
**Transport:** Shared folder (v1),  TCP/AppleEvent (v2)

---

## 1.  Design Constraints

The protocol must respect the realities of Classic Mac OS:

- **HFS filenames** are limited to 31 characters.  No special characters beyond what HFS allows.
- **Text encoding** is MacRoman on the guest side,  UTF-8 on the host side.  The MCP server owns the translation boundary.
- **Line endings** are CR (`\r`, 0x0D) on Classic Mac,  LF (`\n`, 0x0A) on the host.  Each side writes in its native convention;  each side normalizes on read.
- **No JSON parser** ships with Classic Mac Toolbox.  The wire format must be trivially parseable in C with no external dependencies — a line-oriented key-value format.
- **File system latency** through shared folders is non-trivial.  The protocol must be idempotent and tolerate reordering.
- **68k memory constraints** mean message payloads should stay under 32KB per message.

---

## 2.  Shared Folder Layout (v1 Transport)

The shared folder mounted in the emulator serves as a bidirectional message bus.
The AgentBridge directory lives inside the emulator's shared volume (e.g., the
MacintoshPi "Unix" volume at `/home/pi/Downloads`), keeping bridge files
organized and separate from user files.

```
/SharedVolume/AgentBridge/
├── inbox/          ← MCP server writes commands here (Mac reads)
├── outbox/         ← AgentBridge writes responses here (MCP server reads)
├── assets/         ← File transfer staging area (bidirectional)
├── heartbeat       ← AgentBridge touches this file every N seconds
└── bridge.conf     ← Runtime configuration for AgentBridge
```

**Conventions:**

- Command files:  `inbox/C{seq}.msg`  (e.g.,  `inbox/C00042.msg`)
- Response files:  `outbox/R{seq}.msg`  (e.g.,  `outbox/R00042.msg`)
- Sequence numbers are zero-padded to 5 digits (00001–99999),  wrapping to 00001 after 99999.
- AgentBridge deletes command files from `inbox/` after processing.
- MCP server deletes response files from `outbox/` after reading.
- Filenames stay well under the 31-char HFS limit.

---

## 3.  Message Wire Format

Messages are **line-oriented key-value pairs**,  trivially parseable with `ReadLine` or a simple C tokenizer.

### 3.1  Command Message (MCP Server → AgentBridge)

```
BRIDGE 0.1
SEQ 00042
CMD list_windows
TS 20260307T153022
---
```

```
BRIDGE 0.1
SEQ 00043
CMD send_appleevent
TS 20260307T153024
TARGET SimpleText
EVENT oapp
---
```

```
BRIDGE 0.1
SEQ 00044
CMD click
TS 20260307T153030
X 120
Y 45
BUTTON 1
CLICKS 2
---
```

```
BRIDGE 0.1
SEQ 00045
CMD type_text
TS 20260307T153035
TEXT Hello from the future.
---
```

### 3.2  Response Message (AgentBridge → MCP Server)

```
BRIDGE 0.1
SEQ 00042
STATUS ok
TS 20260307T153022
COUNT 3
WINDOW 1|SimpleText|Untitled|40,40,400,300|front
WINDOW 2|Finder||0,0,640,480|back
WINDOW 3|Scrapbook|Scrapbook|100,100,350,250|back
---
```

```
BRIDGE 0.1
SEQ 00043
STATUS ok
TS 20260307T153024
RESULT app_launched
PID 42
---
```

Error response:

```
BRIDGE 0.1
SEQ 00099
STATUS error
TS 20260307T154000
ERRCODE 404
ERRMSG Application not found: MacWrite
---
```

### 3.3  Format Rules

- First line is always `BRIDGE {version}` — allows future protocol negotiation.
- `SEQ` ties responses to commands.  Response SEQ matches command SEQ.
- `---` is the message terminator (three hyphens,  then line ending).
- Keys are uppercase ASCII.  Values are everything after the first space to end of line.
- Multi-value fields use a numeric suffix or pipe-delimited fields within a line (see `WINDOW` above).
- `TEXT` values may contain spaces.  Value is everything after `TEXT `.
- Binary data is never inline — use the `assets/` folder and reference by filename.
- Lines are processed in order but parsers must tolerate unknown keys (forward compatibility).

---

## 4.  Command Reference

### 4.1  Introspection Commands

| Command | Parameters | Description |
|---|---|---|
| `ping` | (none) | Health check.  Returns `STATUS ok` with `RESULT pong`. |
| `list_windows` | (none) | Enumerate all visible windows with title,  owner app,  bounds,  layer. |
| `list_processes` | (none) | List running applications with name,  creator code,  PID,  memory partition. |
| `list_menus` | (none) | Return the current menu bar contents for the frontmost application. |
| `get_menu_items` | `MENU {menu_title}` | Return all items in a specific menu,  including enabled/disabled state and shortcut keys. |
| `get_front_window` | (none) | Return detailed info about the frontmost window:  title,  app,  bounds,  controls,  text fields. |
| `get_clipboard` | (none) | Return clipboard contents as text (TEXT type from Scrap Manager). |
| `get_volumes` | (none) | List mounted volumes with name,  free space,  total space. |
| `list_folder` | `PATH {mac_path}` | List contents of a folder.  Path uses `:` separator (e.g.,  `Macintosh HD:Documents`). |
| `get_about` | (none) | Return AgentBridge version,  Mac OS version,  machine type,  available memory. |

### 4.2  Action Commands

| Command | Parameters | Description |
|---|---|---|
| `click` | `X`, `Y`, `BUTTON` (1=left), `CLICKS` (1 or 2) | Post a synthetic mouse click event at screen coordinates. |
| `mouse_move` | `X`, `Y` | Move the cursor without clicking. |
| `mouse_drag` | `X1`, `Y1`, `X2`, `Y2` | Click-drag from point to point. |
| `key_press` | `KEY`, `MODIFIERS` (comma-separated: cmd,opt,shift,ctrl) | Post a synthetic key event. |
| `type_text` | `TEXT` | Type a string character by character,  with appropriate inter-key delay. |
| `set_clipboard` | `TEXT` | Place text on the clipboard via the Scrap Manager. |
| `menu_select` | `MENU`, `ITEM` | Activate a menu item by menu title and item name (e.g.,  `MENU File` / `ITEM Save As...`). |
| `send_appleevent` | `TARGET`, `EVENT`, optional `PARAMS` | Send an AppleEvent to a running or launchable application.  TARGET is app name or creator code. |
| `launch_app` | `PATH` or `CREATOR` | Launch an application by Mac path or four-character creator code. |
| `open_document` | `PATH` | Open a document with its associated application (uses Finder AppleEvent). |
| `activate_app` | `NAME` | Bring an application to the front. |
| `quit_app` | `NAME` | Send Quit AppleEvent to an application. |

### 4.3  File Transfer Commands

| Command | Parameters | Description |
|---|---|---|
| `stage_file` | `FILENAME` | Signal that the MCP server has placed a file in `assets/` for the Mac to pick up.  AgentBridge copies it to a Mac-local destination. |
| `retrieve_file` | `PATH` | AgentBridge copies a Mac-local file to `assets/` and responds with the filename. |
| `delete_file` | `PATH` | Delete a file on the Mac side. |

### 4.4  Composite / Convenience Commands

| Command | Parameters | Description |
|---|---|---|
| `wait_for_window` | `TITLE`, `TIMEOUT` (seconds) | Block until a window with the given title appears,  or timeout. |
| `wait_for_idle` | `TIMEOUT` | Block until the system event queue settles (no mouse/key events pending). |
| `screenshot_region` | `X`, `Y`, `W`, `H`, `FILENAME` | Capture a region of the screen,  save as PICT in `assets/`.  (Supplements the MCP server's external screen capture.) |

---

## 5.  Response Field Reference

### 5.1  Standard Fields (all responses)

| Field | Description |
|---|---|
| `BRIDGE` | Protocol version. |
| `SEQ` | Sequence number matching the command. |
| `STATUS` | `ok` or `error`. |
| `TS` | Timestamp (ISO-ish,  no timezone — Classic Mac has no TZ concept). |

### 5.2  Error Fields

| Field | Description |
|---|---|
| `ERRCODE` | Numeric error code (see Section 7). |
| `ERRMSG` | Human-readable error description. |

### 5.3  Common Data Fields

| Field | Format | Description |
|---|---|---|
| `WINDOW` | `index\|app_name\|title\|left,top,right,bottom\|layer` | Window descriptor. |
| `PROCESS` | `index\|name\|creator\|pid\|mem_partition_kb` | Process descriptor. |
| `MENUBAR` | `index\|menu_title` | Menu bar entry. |
| `MENUITEM` | `index\|item_name\|enabled\|shortcut` | Menu item within a menu. |
| `VOLUME` | `index\|name\|free_kb\|total_kb` | Mounted volume. |
| `FILEENTRY` | `index\|name\|type\|creator\|size_bytes\|is_folder` | Folder listing entry. |
| `RESULT` | (string) | Generic result value for simple commands. |
| `COUNT` | (integer) | Number of multi-value items that follow. |

---

## 6.  Heartbeat and Lifecycle

### 6.1  Heartbeat

AgentBridge writes the current TickCount (or a simple incrementing counter) to `/SharedVolume/heartbeat` every 2 seconds.  The MCP server monitors this file's modification time to determine if AgentBridge is alive.

```
BRIDGE 0.1
UPTIME 3842
TICKS 482910234
FRONTAPP Finder
FREEMEM 4821
---
```

The heartbeat doubles as a lightweight status beacon:  current front app,  free memory,  uptime in seconds.

### 6.2  Startup Sequence

1. AgentBridge launches (via Startup Items or manually).
2. Reads `bridge.conf` for configuration (poll interval,  log level,  etc.).
3. Clears any stale files from `inbox/` and `outbox/`.
4. Writes initial heartbeat.
5. Begins polling `inbox/` for command files.

### 6.3  Shutdown

- On receiving `CMD shutdown` or on application Quit:
  - Writes a final response `STATUS ok` / `RESULT shutting_down`.
  - Stops heartbeat.
  - Cleans up `inbox/` and `outbox/`.
  - Exits.

---

## 7.  Error Codes

| Code | Meaning |
|---|---|
| 100 | Unknown command |
| 200 | Invalid parameters |
| 300 | Target application not found |
| 301 | Target application not running (and cannot be launched) |
| 302 | AppleEvent delivery failed |
| 400 | File not found |
| 401 | File access denied |
| 402 | Disk full |
| 500 | Toolbox error (ERRMSG contains OSErr code) |
| 501 | Out of memory |
| 600 | Timeout (for wait_* commands) |
| 900 | Internal AgentBridge error |

---

## 8.  Encoding Boundary

The MCP server is the **encoding translation boundary**.

- When writing command files:  MCP server converts UTF-8 text values to MacRoman.
- When reading response files:  MCP server converts MacRoman text values to UTF-8.
- Characters that cannot be mapped produce a `?` replacement and a warning in the MCP server log.
- Filenames in `assets/` use ASCII-safe names only (the MCP server generates them).

A lookup table for MacRoman ↔ UTF-8 is ~128 entries and well-documented.  The MCP server carries this table;  AgentBridge never needs to think about UTF-8.

---

## 9.  v2 Transport: TCP/AppleEvent Bridge

For real hardware (v2),  the shared folder transport is replaced:

- **Host → Mac:**  Remote AppleEvents via PPC Toolbox / Program Linking over TCP/IP.  The MCP server's Network Adapter sends AppleEvents directly to AgentBridge's handler.  The command vocabulary maps 1:1 — each command becomes an AppleEvent with the same parameters encoded as AEDescriptors.
- **Mac → Host:**  AgentBridge sends reply AppleEvents back,  or posts HTTP requests to a lightweight HTTP listener in the MCP server (Mac OS 9 has Open Transport TCP).
- **Same protocol semantics:**  SEQ,  STATUS,  ERRCODE all carry over.  The wire format changes from files to AppleEvent descriptors,  but the command/response model is identical.

This means the MCP server's tool layer never changes.  Only the transport adapter swaps.

---

## 10.  Security Considerations

- The shared folder is a trust boundary.  In v1 (local emulator),  this is acceptable — same user,  same machine.
- In v2 (network),  Program Linking has rudimentary authentication (Mac OS Users & Groups).  The MCP server should authenticate as a configured guest or user.
- AgentBridge should validate all incoming SEQ numbers and reject duplicates.
- AgentBridge should enforce a maximum message size (32KB) to prevent memory exhaustion.
- File transfer via `assets/` should validate filenames to prevent path traversal (no `:` or `::` sequences in staged filenames).

---

## 11.  Implementation Notes

### 11.1  Building AgentBridge for 68k/PPC

**Recommended toolchain:**  Retro68 (GCC cross-compiler targeting classic Mac OS).  Builds on modern Linux/macOS,  produces 68k and PPC binaries,  links against original Toolbox headers.

**Key Toolbox APIs:**

- `WaitNextEvent` — main event loop and polling timer
- `AEInstallEventHandler` — register AppleEvent handlers
- `AESend` — send AppleEvents to other apps
- `GetWindowList` / `FrontWindow` — window enumeration
- `GetProcessInformation` / `GetNextProcess` — process list
- `GetMenuBar` / `GetMenuHandle` / `GetMenuItemText` — menu introspection
- `PostEvent` / `CGPostKeyboardEvent` equivalent — synthetic input (note: Classic Mac uses `PostEvent` or `SystemEvent` for low-level input injection;  this requires careful implementation)
- `ZeroScrap` / `PutScrap` / `GetScrap` — clipboard
- `PBHGetVInfo` / `PBGetCatInfo` — volume and file enumeration
- `FSOpen` / `FSRead` / `FSWrite` — file I/O for message channel

### 11.2  Polling Implementation

```
main loop:
    WaitNextEvent(everyEvent, &event, sleepTicks=30)  // ~500ms
    HandleSystemEvents(&event)
    CheckInboxForCommands()
    WriteHeartbeatIfDue()
```

The 30-tick sleep (~500ms) balances responsiveness against CPU load.  The emulated Mac has limited cycles.

### 11.3  Multi-line TEXT Handling

For `type_text` and `set_clipboard` commands that may contain multi-line text,  use continuation lines prefixed with `+`:

```
BRIDGE 0.1
SEQ 00050
CMD set_clipboard
TS 20260307T160000
TEXT First line of text
+Second line of text
+Third line of text
---
```

AgentBridge concatenates all `+` prefixed lines following a `TEXT` field,  inserting CR between them.

---

## 12.  Example Full Exchange

**Agent wants to open SimpleText,  type a greeting,  and save the file.**

Command 1:
```
BRIDGE 0.1
SEQ 00001
CMD launch_app
TS 20260307T170000
CREATOR ttxt
---
```

Response 1:
```
BRIDGE 0.1
SEQ 00001
STATUS ok
TS 20260307T170001
RESULT launched
PID 15
---
```

Command 2:
```
BRIDGE 0.1
SEQ 00002
CMD wait_for_window
TS 20260307T170001
TITLE Untitled
TIMEOUT 10
---
```

Response 2:
```
BRIDGE 0.1
SEQ 00002
STATUS ok
TS 20260307T170003
RESULT window_found
WINDOW 1|SimpleText|Untitled|40,40,400,300|front
---
```

Command 3:
```
BRIDGE 0.1
SEQ 00003
CMD type_text
TS 20260307T170003
TEXT Hello from 2026.  An AI wrote this on your Mac.
---
```

Response 3:
```
BRIDGE 0.1
SEQ 00003
STATUS ok
TS 20260307T170005
RESULT text_typed
---
```

Command 4:
```
BRIDGE 0.1
SEQ 00004
CMD menu_select
TS 20260307T170005
MENU File
ITEM Save As...
---
```

Response 4:
```
BRIDGE 0.1
SEQ 00004
STATUS ok
TS 20260307T170006
RESULT menu_activated
---
```

(Agent would now use screen_capture via MCP to see the Save dialog,  then type a filename and click Save.)
