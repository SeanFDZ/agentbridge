/**
 * classic-mac-mcp - Core Types
 *
 * Shared type definitions for the fleet configuration,
 * bridge protocol messages, and target registry.
 */

// ----------------------------------------------------------------
//  Fleet Configuration (loaded from fleet.json)
// ----------------------------------------------------------------

export interface FleetConfig {
  fleet: TargetConfig[];
}

export interface TargetConfig {
  id: string;
  alias?: string;
  arch?: "68k" | "ppc";
  os_version?: string;
  shared_folder: string;           // Local path to the shared folder (or mount point)
  auto_start?: boolean;
}

// ----------------------------------------------------------------
//  Target Status (runtime state)
// ----------------------------------------------------------------

export type BridgeStatus = "alive" | "stale" | "dead" | "unknown";

export interface TargetState {
  config: TargetConfig;
  bridgeStatus: BridgeStatus;
  lastHeartbeat?: Heartbeat;
  lastProbeTime?: number;
  error?: string;
}

// ----------------------------------------------------------------
//  AgentBridge Protocol Types
// ----------------------------------------------------------------

export interface BridgeCommand {
  cmd: string;
  fields: Record<string, string>;
  text?: string;
}

export interface BridgeResponse {
  seq: number;
  status: "ok" | "error";
  timestamp?: string;
  errCode?: number;
  errMsg?: string;
  fields: Record<string, string>;
  multiValues: Record<string, string[]>;    // WINDOW, PROCESS, etc.
  result?: string;
  count?: number;
}

export interface Heartbeat {
  version: string;
  uptime: number;
  ticks: number;
  frontApp: string;
  freeMemKB: number;
  timestamp: string;
  fileModTime: number;      // Host-side mtime of heartbeat file
}

// ----------------------------------------------------------------
//  Parsed Introspection Data
// ----------------------------------------------------------------

export interface MacWindow {
  index: number;
  app: string;
  title: string;
  bounds: { left: number; top: number; right: number; bottom: number };
  layer: "front" | "back";
}

export interface MacProcess {
  index: number;
  name: string;
  creator: string;
  pid: number;
  memPartitionKB: number;
}

export interface MacVolume {
  index: number;
  name: string;
  freeKB: number;
  totalKB: number;
}

export interface MacFileEntry {
  index: number;
  name: string;
  type: string;
  creator: string;
  sizeBytes: number;
  isFolder: boolean;
}

export interface MacMenuItem {
  index: number;
  name: string;
  enabled: boolean;
  shortcut?: string;
}

export interface MacAbout {
  agent: string;
  protocol: string;
  osVersion: string;
  machine: number;
  ramKB: number;
  freeMemKB: number;
  uptimeSeconds: number;
}

// ----------------------------------------------------------------
//  Encoding helpers
// ----------------------------------------------------------------

/**
 * MacRoman to UTF-8 lookup table for bytes 0x80-0xFF.
 * Characters below 0x80 are identical in both encodings.
 */
export const MACROMAN_TO_UTF8: Record<number, string> = {
  0x80: "\u00C4", 0x81: "\u00C5", 0x82: "\u00C7", 0x83: "\u00C9",
  0x84: "\u00D1", 0x85: "\u00D6", 0x86: "\u00DC", 0x87: "\u00E1",
  0x88: "\u00E0", 0x89: "\u00E2", 0x8A: "\u00E4", 0x8B: "\u00E3",
  0x8C: "\u00E5", 0x8D: "\u00E7", 0x8E: "\u00E9", 0x8F: "\u00E8",
  0x90: "\u00EA", 0x91: "\u00EB", 0x92: "\u00ED", 0x93: "\u00EC",
  0x94: "\u00EE", 0x95: "\u00EF", 0x96: "\u00F1", 0x97: "\u00F3",
  0x98: "\u00F2", 0x99: "\u00F4", 0x9A: "\u00F6", 0x9B: "\u00F5",
  0x9C: "\u00FA", 0x9D: "\u00F9", 0x9E: "\u00FB", 0x9F: "\u00FC",
  0xA0: "\u2020", 0xA1: "\u00B0", 0xA2: "\u00A2", 0xA3: "\u00A3",
  0xA4: "\u00A7", 0xA5: "\u2022", 0xA6: "\u00B6", 0xA7: "\u00DF",
  0xA8: "\u00AE", 0xA9: "\u00A9", 0xAA: "\u2122", 0xAB: "\u00B4",
  0xAC: "\u00A8", 0xAD: "\u2260", 0xAE: "\u00C6", 0xAF: "\u00D8",
  0xB0: "\u221E", 0xB1: "\u00B1", 0xB2: "\u2264", 0xB3: "\u2265",
  0xB4: "\u00A5", 0xB5: "\u00B5", 0xB6: "\u2202", 0xB7: "\u2211",
  0xB8: "\u220F", 0xB9: "\u03C0", 0xBA: "\u222B", 0xBB: "\u00AA",
  0xBC: "\u00BA", 0xBD: "\u03A9", 0xBE: "\u00E6", 0xBF: "\u00F8",
  0xC0: "\u00BF", 0xC1: "\u00A1", 0xC2: "\u00AC", 0xC3: "\u221A",
  0xC4: "\u0192", 0xC5: "\u2248", 0xC6: "\u2206", 0xC7: "\u00AB",
  0xC8: "\u00BB", 0xC9: "\u2026", 0xCA: "\u00A0", 0xCB: "\u00C0",
  0xCC: "\u00C3", 0xCD: "\u00D5", 0xCE: "\u0152", 0xCF: "\u0153",
  0xD0: "\u2013", 0xD1: "\u2014", 0xD2: "\u201C", 0xD3: "\u201D",
  0xD4: "\u2018", 0xD5: "\u2019", 0xD6: "\u00F7", 0xD7: "\u25CA",
  0xD8: "\u00FF", 0xD9: "\u0178", 0xDA: "\u2044", 0xDB: "\u20AC",
  0xDC: "\u2039", 0xDD: "\u203A", 0xDE: "\uFB01", 0xDF: "\uFB02",
  0xE0: "\u2021", 0xE1: "\u00B7", 0xE2: "\u201A", 0xE3: "\u201E",
  0xE4: "\u2030", 0xE5: "\u00C2", 0xE6: "\u00CA", 0xE7: "\u00C1",
  0xE8: "\u00CB", 0xE9: "\u00C8", 0xEA: "\u00CD", 0xEB: "\u00CE",
  0xEC: "\u00CF", 0xED: "\u00CC", 0xEE: "\u00D3", 0xEF: "\u00D4",
  0xF0: "\uF8FF", 0xF1: "\u00D2", 0xF2: "\u00DA", 0xF3: "\u00DB",
  0xF4: "\u00D9", 0xF5: "\u0131", 0xF6: "\u02C6", 0xF7: "\u02DC",
  0xF8: "\u00AF", 0xF9: "\u02D8", 0xFA: "\u02D9", 0xFB: "\u02DA",
  0xFC: "\u00B8", 0xFD: "\u02DD", 0xFE: "\u02DB", 0xFF: "\u02C7",
};
