#!/usr/bin/env node

/**
 * classic-mac-mcp - MCP Server
 *
 * Model Context Protocol server that exposes Classic Mac OS control
 * as tools for AI agents. Communicates with AgentBridge exclusively
 * through the BridgeClient (shared folder file I/O).
 *
 * Usage:
 *   classic-mac-mcp --config /path/to/fleet.json
 *   classic-mac-mcp                                  (uses ./config/fleet.json)
 */

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  ListResourcesRequestSchema,
  ReadResourceRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";

import path from "path";
import { FleetRegistry, FleetTarget } from "./fleet.js";
import type { MacWindow, MacProcess, MacVolume, MacFileEntry, MacMenuItem } from "./types.js";

// ----------------------------------------------------------------
//  Config
// ----------------------------------------------------------------

const configPath = process.argv.includes("--config")
  ? process.argv[process.argv.indexOf("--config") + 1]
  : path.join(process.cwd(), "config", "fleet.json");

// ----------------------------------------------------------------
//  Fleet initialization
// ----------------------------------------------------------------

const fleet = new FleetRegistry();

// ----------------------------------------------------------------
//  MCP Server setup
// ----------------------------------------------------------------

const server = new Server(
  {
    name: "classic-mac-mcp",
    version: "0.1.0",
  },
  {
    capabilities: {
      tools: {},
      resources: {},
    },
  }
);

// ----------------------------------------------------------------
//  Helper: resolve target from args, with single-target default
// ----------------------------------------------------------------

function resolveTarget(targetArg: unknown): FleetTarget {
  if (targetArg && typeof targetArg === "string") {
    const target = fleet.getTarget(targetArg);
    if (!target) {
      throw new Error(
        `Unknown target: "${targetArg}". ` +
        `Available: ${fleet.getTargetIds().join(", ")}`
      );
    }
    return target;
  }

  // No target specified — use default if fleet has exactly one
  const def = fleet.getDefaultTarget();
  if (def) return def;

  throw new Error(
    `No target specified and fleet has ${fleet.getTargetIds().length} targets. ` +
    `Please specify one of: ${fleet.getTargetIds().join(", ")}`
  );
}

// Target parameter schema (optional when fleet has one target)
const targetParam = {
  type: "string" as const,
  description: "Target ID or alias. Optional if only one target is configured.",
};

// ----------------------------------------------------------------
//  Tool definitions
// ----------------------------------------------------------------

server.setRequestHandler(ListToolsRequestSchema, async () => {
  return {
    tools: [
      // -- Fleet management --
      {
        name: "classic_mac_list_targets",
        description:
          "List all Classic Mac targets in the fleet with their current status, " +
          "including architecture, OS version, and whether AgentBridge is alive.",
        inputSchema: {
          type: "object" as const,
          properties: {},
        },
      },

      // -- Bridge health --
      {
        name: "classic_mac_ping",
        description: "Ping a Classic Mac target to check if AgentBridge is responding.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },
      {
        name: "classic_mac_heartbeat",
        description:
          "Read the heartbeat status of a Classic Mac target. Returns uptime, " +
          "front app, free memory, and whether the bridge is alive.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },

      // -- Introspection --
      {
        name: "classic_mac_list_windows",
        description:
          "List all visible windows on a Classic Mac target. Returns window title, " +
          "owning application, screen bounds, and layer order. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },
      {
        name: "classic_mac_list_processes",
        description:
          "List running applications on a Classic Mac target. Returns app name, " +
          "creator code, PID, and memory partition. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },
      {
        name: "classic_mac_list_menus",
        description:
          "List the menu bar contents for the frontmost application. " +
          "Returns menu titles. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },
      {
        name: "classic_mac_get_menu_items",
        description:
          "Get all items in a specific menu, including enabled/disabled state " +
          "and keyboard shortcuts. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            menu: { type: "string", description: "Menu title (e.g. 'File', 'Edit')" },
          },
          required: ["menu"],
        },
      },
      {
        name: "classic_mac_get_front_window",
        description:
          "Get detailed info about the frontmost window: title, app, bounds. " +
          "Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },
      {
        name: "classic_mac_get_volumes",
        description:
          "List mounted volumes (disks) with free and total space. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },
      {
        name: "classic_mac_list_folder",
        description:
          "List contents of a folder on the Classic Mac. Returns file names, " +
          "types, creator codes, and sizes. Path uses ':' as separator " +
          "(e.g. 'Macintosh HD:Documents'). Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            path: {
              type: "string",
              description: "HFS path using ':' separator (e.g. 'Macintosh HD:System Folder')",
            },
          },
          required: ["path"],
        },
      },
      {
        name: "classic_mac_get_about",
        description:
          "Get system information about a Classic Mac target: OS version, " +
          "machine type, RAM, free memory, AgentBridge uptime. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },

      // -- Clipboard --
      {
        name: "classic_mac_get_clipboard",
        description:
          "Read the clipboard (scrap) contents as text. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: { target: targetParam },
        },
      },
      {
        name: "classic_mac_set_clipboard",
        description:
          "Set the clipboard (scrap) to the given text. Useful for pasting " +
          "content into Classic Mac applications. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            text: { type: "string", description: "Text to place on clipboard" },
          },
          required: ["text"],
        },
      },

      // -- Input --
      {
        name: "classic_mac_click",
        description:
          "Click at screen coordinates on a Classic Mac target. " +
          "Coordinates are relative to the Mac's screen.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            x: { type: "number", description: "X coordinate" },
            y: { type: "number", description: "Y coordinate" },
            clicks: {
              type: "number",
              description: "Number of clicks (1 or 2, default 1)",
              default: 1,
            },
            button: {
              type: "number",
              description: "Mouse button (1=left, default 1)",
              default: 1,
            },
          },
          required: ["x", "y"],
        },
      },
      {
        name: "classic_mac_mouse_move",
        description: "Move the cursor to screen coordinates without clicking.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            x: { type: "number", description: "X coordinate" },
            y: { type: "number", description: "Y coordinate" },
          },
          required: ["x", "y"],
        },
      },
      {
        name: "classic_mac_mouse_drag",
        description: "Click and drag from one point to another.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            x1: { type: "number", description: "Start X" },
            y1: { type: "number", description: "Start Y" },
            x2: { type: "number", description: "End X" },
            y2: { type: "number", description: "End Y" },
          },
          required: ["x1", "y1", "x2", "y2"],
        },
      },
      {
        name: "classic_mac_type_text",
        description:
          "Type text on a Classic Mac target. Characters are typed one by one. " +
          "Make sure the correct text field or application is focused first.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            text: { type: "string", description: "Text to type" },
          },
          required: ["text"],
        },
      },
      {
        name: "classic_mac_key_press",
        description:
          "Press a key with optional modifiers. Use for keyboard shortcuts " +
          "(e.g. Cmd+S to save) and special keys (return, escape, arrows).",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            key: {
              type: "string",
              description:
                "Key name: a-z, 0-9, return, tab, escape, space, delete, " +
                "left, right, up, down, f1-f12, or any single character",
            },
            modifiers: {
              type: "string",
              description: "Comma-separated modifiers: cmd, opt, shift, ctrl",
            },
          },
          required: ["key"],
        },
      },

      // -- Menu selection --
      {
        name: "classic_mac_menu_select",
        description:
          "Select a menu item by menu title and item name " +
          "(e.g. menu='File', item='Save As...'). Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            menu: { type: "string", description: "Menu title (e.g. 'File')" },
            item: { type: "string", description: "Menu item name (e.g. 'Save As...')" },
          },
          required: ["menu", "item"],
        },
      },

      // -- App lifecycle --
      {
        name: "classic_mac_launch_app",
        description:
          "Launch an application on the Classic Mac by path or creator code. " +
          "Creator codes are 4-character identifiers (e.g. 'ttxt' for SimpleText, " +
          "'WILD' for HyperCard). Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            path: {
              type: "string",
              description: "HFS path to application (e.g. 'Macintosh HD:Applications:SimpleText')",
            },
            creator: {
              type: "string",
              description: "4-character creator code (e.g. 'ttxt', 'WILD', 'CWIE')",
            },
          },
        },
      },
      {
        name: "classic_mac_activate_app",
        description: "Bring a running application to the front. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            name: { type: "string", description: "Application name (e.g. 'SimpleText')" },
          },
          required: ["name"],
        },
      },
      {
        name: "classic_mac_quit_app",
        description: "Quit a running application by name. Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            name: { type: "string", description: "Application name (e.g. 'SimpleText')" },
          },
          required: ["name"],
        },
      },

      // -- Apple Events --
      {
        name: "classic_mac_send_appleevent",
        description:
          "Send a generic Apple Event to an application. TARGET is app name or " +
          "creator code, EVENT is the event type (e.g. 'oapp'). Requires AgentBridge.",
        inputSchema: {
          type: "object" as const,
          properties: {
            target: targetParam,
            app: { type: "string", description: "Target app name or creator code" },
            event: { type: "string", description: "Apple Event type (e.g. 'oapp', 'odoc')" },
            params: { type: "string", description: "Optional event parameters" },
          },
          required: ["app", "event"],
        },
      },
    ],
  };
});

// ----------------------------------------------------------------
//  Tool execution
// ----------------------------------------------------------------

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args = {} } = request.params;

  try {
    switch (name) {
      // -- Fleet management --

      case "classic_mac_list_targets": {
        const states = await fleet.getAllStatus();
        if (states.length === 0) {
          return {
            content: [{
              type: "text",
              text: "No targets configured. Add targets to config/fleet.json.",
            }],
          };
        }

        const lines = states.map((s) => {
          const alias = s.config.alias ? ` ("${s.config.alias}")` : "";
          const os = s.config.os_version ?? "unknown";
          const arch = s.config.arch ?? "unknown";
          const hb = s.lastHeartbeat;
          const frontApp = hb?.frontApp ? `, front app: ${hb.frontApp}` : "";
          const freeMem = hb?.freeMemKB ? `, free mem: ${hb.freeMemKB}KB` : "";

          return (
            `${s.config.id}${alias}: ` +
            `arch=${arch}, ` +
            `os=${os}, ` +
            `bridge=${s.bridgeStatus}` +
            `${frontApp}${freeMem}`
          );
        });

        return {
          content: [{
            type: "text",
            text: `Fleet status (${states.length} target${states.length !== 1 ? "s" : ""}):\n\n${lines.join("\n")}`,
          }],
        };
      }

      // -- Bridge health --

      case "classic_mac_ping": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "ping", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Ping failed: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `Pong! (seq ${resp.seq})` }] };
      }

      case "classic_mac_heartbeat": {
        const { bridge } = resolveTarget(args.target);
        const hb = await bridge.readHeartbeat();

        if (!hb) {
          return { content: [{ type: "text", text: "No heartbeat found. AgentBridge may not be running." }] };
        }

        const alive = await bridge.isBridgeAlive();
        const info = [
          `Status: ${alive ? "alive" : "stale"}`,
          `Version: ${hb.version}`,
          `Uptime: ${hb.uptime} seconds`,
          `Front app: ${hb.frontApp}`,
          `Free memory: ${hb.freeMemKB}KB`,
          `Last update: ${new Date(hb.fileModTime).toISOString()}`,
        ].join("\n");

        return { content: [{ type: "text", text: info }] };
      }

      // -- Introspection --

      case "classic_mac_list_windows": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "list_windows", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const windows = parseWindows(resp.multiValues["WINDOW"] ?? []);
        const text = windows.length === 0
          ? "No windows found."
          : windows.map(w =>
              `${w.index}. [${w.layer}] "${w.title}" (${w.app}) ` +
              `bounds: ${w.bounds.left},${w.bounds.top},${w.bounds.right},${w.bounds.bottom}`
            ).join("\n");

        return { content: [{ type: "text", text }] };
      }

      case "classic_mac_list_processes": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "list_processes", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const procs = parseProcesses(resp.multiValues["PROCESS"] ?? []);
        const text = procs.map(p =>
          `${p.index}. ${p.name} (creator: '${p.creator}', pid: ${p.pid}, mem: ${p.memPartitionKB}KB)`
        ).join("\n");

        return { content: [{ type: "text", text: text || "No processes found." }] };
      }

      case "classic_mac_list_menus": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "list_menus", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const menus = (resp.multiValues["MENUBAR"] ?? []).map(line => {
          const parts = line.split("|");
          return `${parts[0]}. ${parts[1] ?? "?"}`;
        });

        return { content: [{ type: "text", text: menus.length > 0 ? menus.join("\n") : "No menus found." }] };
      }

      case "classic_mac_get_menu_items": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({
          cmd: "get_menu_items",
          fields: { MENU: args.menu as string },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const items = parseMenuItems(resp.multiValues["MENUITEM"] ?? []);
        const text = items.map(i => {
          const shortcut = i.shortcut ? ` (${i.shortcut})` : "";
          const enabled = i.enabled ? "" : " [disabled]";
          return `${i.index}. ${i.name}${shortcut}${enabled}`;
        }).join("\n");

        return { content: [{ type: "text", text: text || "No menu items found." }] };
      }

      case "classic_mac_get_front_window": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "get_front_window", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const windows = parseWindows(resp.multiValues["WINDOW"] ?? []);
        if (windows.length === 0) {
          return { content: [{ type: "text", text: "No front window." }] };
        }

        const w = windows[0];
        const text = [
          `Title: "${w.title}"`,
          `Application: ${w.app}`,
          `Bounds: ${w.bounds.left},${w.bounds.top},${w.bounds.right},${w.bounds.bottom}`,
        ].join("\n");

        return { content: [{ type: "text", text }] };
      }

      case "classic_mac_get_volumes": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "get_volumes", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const vols = parseVolumes(resp.multiValues["VOLUME"] ?? []);
        const text = vols.map(v =>
          `${v.index}. ${v.name} — ${v.freeKB}KB free / ${v.totalKB}KB total`
        ).join("\n");

        return { content: [{ type: "text", text: text || "No volumes found." }] };
      }

      case "classic_mac_list_folder": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({
          cmd: "list_folder",
          fields: { PATH: args.path as string },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const entries = parseFileEntries(resp.multiValues["FILEENTRY"] ?? []);
        const text = entries.map(e =>
          e.isFolder
            ? `[folder] ${e.name}`
            : `${e.name} (${e.type}/${e.creator}, ${e.sizeBytes} bytes)`
        ).join("\n");

        return { content: [{ type: "text", text: text || "Empty folder." }] };
      }

      case "classic_mac_get_about": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "get_about", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const info = [
          `AgentBridge: ${resp.fields["AGENT"] ?? "?"} v${resp.fields["PROTOCOL"] ?? "?"}`,
          `Mac OS: ${resp.fields["OSVERSION"] ?? "?"}`,
          `Machine type: ${resp.fields["MACHINE"] ?? "?"}`,
          `RAM: ${resp.fields["RAMKB"] ?? "?"}KB`,
          `Free memory: ${resp.fields["FREEMEM"] ?? "?"}KB`,
          `Uptime: ${resp.fields["UPTIME"] ?? "?"} seconds`,
        ].join("\n");

        return { content: [{ type: "text", text: info }] };
      }

      // -- Clipboard --

      case "classic_mac_get_clipboard": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({ cmd: "get_clipboard", fields: {} });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const clipText = resp.fields["TEXT"] ?? "";
        return {
          content: [{
            type: "text",
            text: resp.result === "empty"
              ? "Clipboard is empty."
              : `Clipboard contents:\n${clipText}`,
          }],
        };
      }

      case "classic_mac_set_clipboard": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({
          cmd: "set_clipboard",
          fields: {},
          text: args.text as string,
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: "Clipboard set." }] };
      }

      // -- Input --

      case "classic_mac_click": {
        const { bridge } = resolveTarget(args.target);
        const x = args.x as number;
        const y = args.y as number;
        const clicks = (args.clicks as number) ?? 1;
        const button = (args.button as number) ?? 1;

        const resp = await bridge.sendCommand({
          cmd: "click",
          fields: {
            X: String(x),
            Y: String(y),
            CLICKS: String(clicks),
            BUTTON: String(button),
          },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `Clicked at (${x}, ${y})${clicks > 1 ? " double-click" : ""}` }] };
      }

      case "classic_mac_mouse_move": {
        const { bridge } = resolveTarget(args.target);
        const x = args.x as number;
        const y = args.y as number;

        const resp = await bridge.sendCommand({
          cmd: "mouse_move",
          fields: { X: String(x), Y: String(y) },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `Moved cursor to (${x}, ${y})` }] };
      }

      case "classic_mac_mouse_drag": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({
          cmd: "mouse_drag",
          fields: {
            X1: String(args.x1 as number),
            Y1: String(args.y1 as number),
            X2: String(args.x2 as number),
            Y2: String(args.y2 as number),
          },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return {
          content: [{
            type: "text",
            text: `Dragged from (${args.x1},${args.y1}) to (${args.x2},${args.y2})`,
          }],
        };
      }

      case "classic_mac_type_text": {
        const { bridge } = resolveTarget(args.target);
        const text = args.text as string;

        const resp = await bridge.sendCommand({
          cmd: "type_text",
          fields: {},
          text,
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `Typed ${text.length} characters` }] };
      }

      case "classic_mac_key_press": {
        const { bridge } = resolveTarget(args.target);
        const fields: Record<string, string> = { KEY: args.key as string };
        if (args.modifiers) {
          fields["MODIFIERS"] = args.modifiers as string;
        }

        const resp = await bridge.sendCommand({ cmd: "key_press", fields });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        const modStr = args.modifiers ? `${args.modifiers}+` : "";
        return { content: [{ type: "text", text: `Pressed ${modStr}${args.key}` }] };
      }

      // -- Menu selection --

      case "classic_mac_menu_select": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({
          cmd: "menu_select",
          fields: {
            MENU: args.menu as string,
            ITEM: args.item as string,
          },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `Selected ${args.menu} > ${args.item}` }] };
      }

      // -- App lifecycle --

      case "classic_mac_launch_app": {
        const { bridge } = resolveTarget(args.target);
        const fields: Record<string, string> = {};
        if (args.path) fields["PATH"] = args.path as string;
        if (args.creator) fields["CREATOR"] = args.creator as string;

        const resp = await bridge.sendCommand({ cmd: "launch_app", fields });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `App launched: ${resp.result ?? "ok"}` }] };
      }

      case "classic_mac_activate_app": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({
          cmd: "activate_app",
          fields: { NAME: args.name as string },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `Activated ${args.name}` }] };
      }

      case "classic_mac_quit_app": {
        const { bridge } = resolveTarget(args.target);
        const resp = await bridge.sendCommand({
          cmd: "quit_app",
          fields: { NAME: args.name as string },
        });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `Quit sent to ${args.name}` }] };
      }

      // -- Apple Events --

      case "classic_mac_send_appleevent": {
        const { bridge } = resolveTarget(args.target);
        const fields: Record<string, string> = {
          TARGET: args.app as string,
          EVENT: args.event as string,
        };
        if (args.params) fields["PARAMS"] = args.params as string;

        const resp = await bridge.sendCommand({ cmd: "send_appleevent", fields });

        if (resp.status === "error") {
          return { content: [{ type: "text", text: `Error: ${resp.errMsg}` }], isError: true };
        }

        return { content: [{ type: "text", text: `AppleEvent sent: ${resp.result ?? "ok"}` }] };
      }

      default:
        return {
          content: [{ type: "text", text: `Unknown tool: ${name}` }],
          isError: true,
        };
    }
  } catch (err: any) {
    return {
      content: [{ type: "text", text: `Error: ${err.message}` }],
      isError: true,
    };
  }
});

// ----------------------------------------------------------------
//  Resources: expose heartbeat data
// ----------------------------------------------------------------

server.setRequestHandler(ListResourcesRequestSchema, async () => {
  const ids = fleet.getTargetIds();
  return {
    resources: ids.map((id) => ({
      uri: `classic-mac://${id}/heartbeat`,
      name: `${id} heartbeat`,
      description: `Live status beacon from ${id}`,
      mimeType: "application/json",
    })),
  };
});

server.setRequestHandler(ReadResourceRequestSchema, async (request) => {
  const uri = request.params.uri;
  const match = uri.match(/^classic-mac:\/\/([^/]+)\/heartbeat$/);

  if (!match) {
    throw new Error(`Unknown resource: ${uri}`);
  }

  const target = fleet.getTarget(match[1]);
  if (!target) {
    throw new Error(`Unknown target: ${match[1]}`);
  }

  const hb = await target.bridge.readHeartbeat();
  return {
    contents: [{
      uri,
      mimeType: "application/json",
      text: JSON.stringify(hb, null, 2),
    }],
  };
});

// ----------------------------------------------------------------
//  Parsers
// ----------------------------------------------------------------

function parseWindows(lines: string[]): MacWindow[] {
  return lines.map((line) => {
    const parts = line.split("|");
    const bounds = (parts[3] ?? "0,0,0,0").split(",").map(Number);
    return {
      index: parseInt(parts[0], 10),
      app: parts[1] ?? "?",
      title: parts[2] ?? "",
      bounds: { left: bounds[0], top: bounds[1], right: bounds[2], bottom: bounds[3] },
      layer: (parts[4] ?? "back") as "front" | "back",
    };
  });
}

function parseProcesses(lines: string[]): MacProcess[] {
  return lines.map((line) => {
    const parts = line.split("|");
    return {
      index: parseInt(parts[0], 10),
      name: parts[1] ?? "?",
      creator: parts[2] ?? "????",
      pid: parseInt(parts[3], 10),
      memPartitionKB: parseInt(parts[4], 10),
    };
  });
}

function parseVolumes(lines: string[]): MacVolume[] {
  return lines.map((line) => {
    const parts = line.split("|");
    return {
      index: parseInt(parts[0], 10),
      name: parts[1] ?? "?",
      freeKB: parseInt(parts[2], 10),
      totalKB: parseInt(parts[3], 10),
    };
  });
}

function parseFileEntries(lines: string[]): MacFileEntry[] {
  return lines.map((line) => {
    const parts = line.split("|");
    return {
      index: parseInt(parts[0], 10),
      name: parts[1] ?? "?",
      type: parts[2] ?? "????",
      creator: parts[3] ?? "????",
      sizeBytes: parseInt(parts[4], 10),
      isFolder: parts[5] === "1",
    };
  });
}

function parseMenuItems(lines: string[]): MacMenuItem[] {
  return lines.map((line) => {
    const parts = line.split("|");
    return {
      index: parseInt(parts[0], 10),
      name: parts[1] ?? "?",
      enabled: parts[2] !== "0",
      shortcut: parts[3] || undefined,
    };
  });
}

// ----------------------------------------------------------------
//  Startup
// ----------------------------------------------------------------

async function main() {
  console.error("[classic-mac-mcp] Starting...");
  console.error(`[classic-mac-mcp] Config: ${configPath}`);

  try {
    await fleet.loadConfig(configPath);
  } catch (err: any) {
    console.error(`[classic-mac-mcp] Fleet config error: ${err.message}`);
    console.error("[classic-mac-mcp] Starting with no targets configured.");
  }

  const transport = new StdioServerTransport();
  await server.connect(transport);

  console.error("[classic-mac-mcp] MCP server running on stdio");
}

main().catch((err) => {
  console.error(`[classic-mac-mcp] Fatal: ${err.message}`);
  process.exit(1);
});
