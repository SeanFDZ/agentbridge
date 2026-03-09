/**
 * classic-mac-mcp - Bridge Client
 *
 * Reads and writes AgentBridge protocol messages via the shared folder.
 * Handles sequence numbering,  file lifecycle,  response polling,
 * MacRoman ↔ UTF-8 encoding,  and heartbeat monitoring.
 */

import { readFile,  writeFile,  readdir,  unlink,  stat } from "fs/promises";
import { existsSync } from "fs";
import path from "path";
import {
  BridgeCommand,
  BridgeResponse,
  Heartbeat,
  MACROMAN_TO_UTF8,
} from "../types.js";

const BRIDGE_VERSION = "0.1";
const RESPONSE_POLL_MS = 200;
const RESPONSE_TIMEOUT_MS = 15000;
const HEARTBEAT_STALE_MS = 10000;

export class BridgeClient {
  private sharedFolder: string;
  private seq: number = 0;

  constructor(sharedFolder: string) {
    this.sharedFolder = sharedFolder;
  }

  get inboxDir(): string {
    return path.join(this.sharedFolder,  "inbox");
  }

  get outboxDir(): string {
    return path.join(this.sharedFolder,  "outbox");
  }

  get assetsDir(): string {
    return path.join(this.sharedFolder,  "assets");
  }

  get heartbeatPath(): string {
    return path.join(this.sharedFolder,  "heartbeat");
  }

  // ----------------------------------------------------------------
  //  Sequence number management
  // ----------------------------------------------------------------

  private nextSeq(): number {
    this.seq++;
    if (this.seq > 99999) this.seq = 1;
    return this.seq;
  }

  private padSeq(n: number): string {
    return n.toString().padStart(5,  "0");
  }

  // ----------------------------------------------------------------
  //  Encoding: UTF-8 → MacRoman for command files
  // ----------------------------------------------------------------

  private utf8ToMacRoman(text: string): Buffer {
    const bytes: number[] = [];

    // Build reverse lookup
    const utf8ToMR: Map<string, number> = new Map();
    for (const [byte,  char] of Object.entries(MACROMAN_TO_UTF8) as [string, string][]) {
      utf8ToMR.set(char,  Number(byte));
    }

    for (const char of text) {
      const code = char.codePointAt(0)!;
      if (code < 0x80) {
        bytes.push(code);
      } else {
        const mrByte = utf8ToMR.get(char);
        bytes.push(mrByte ?? 0x3F);   // '?' for unmappable
      }
    }

    return Buffer.from(bytes);
  }

  // ----------------------------------------------------------------
  //  Encoding: MacRoman → UTF-8 for response files
  // ----------------------------------------------------------------

  private macRomanToUtf8(buf: Buffer): string {
    const chars: string[] = [];

    for (const byte of buf) {
      if (byte < 0x80) {
        chars.push(String.fromCharCode(byte));
      } else {
        chars.push(MACROMAN_TO_UTF8[byte] ?? "?");
      }
    }

    return chars.join("");
  }

  // ----------------------------------------------------------------
  //  Format a command message
  // ----------------------------------------------------------------

  private formatCommand(seq: number,  cmd: BridgeCommand): string {
    const lines: string[] = [];

    lines.push(`BRIDGE ${BRIDGE_VERSION}`);
    lines.push(`SEQ ${this.padSeq(seq)}`);
    lines.push(`CMD ${cmd.cmd}`);
    lines.push(`TS ${this.formatTimestamp()}`);

    // Add extra fields
    for (const [key,  value] of Object.entries(cmd.fields)) {
      lines.push(`${key.toUpperCase()} ${value}`);
    }

    // Handle multi-line text
    if (cmd.text != null) {
      const textLines = cmd.text.split("\n");
      lines.push(`TEXT ${textLines[0]}`);
      for (let i = 1; i < textLines.length; i++) {
        lines.push(`+${textLines[i]}`);
      }
    }

    lines.push("---");

    // Use CR line endings for Classic Mac
    return lines.join("\r") + "\r";
  }

  private formatTimestamp(): string {
    const d = new Date();
    const pad = (n: number) => n.toString().padStart(2,  "0");
    return (
      d.getFullYear().toString() +
      pad(d.getMonth() + 1) +
      pad(d.getDate()) +
      "T" +
      pad(d.getHours()) +
      pad(d.getMinutes()) +
      pad(d.getSeconds())
    );
  }

  // ----------------------------------------------------------------
  //  Parse a response message
  // ----------------------------------------------------------------

  private parseResponse(content: string): BridgeResponse {
    const resp: BridgeResponse = {
      seq: 0,
      status: "error",
      fields: {},
      multiValues: {},
    };

    // Normalize line endings
    const lines = content.replace(/\r\n/g,  "\n").replace(/\r/g,  "\n").split("\n");

    for (const line of lines) {
      if (line === "---") break;
      if (line.trim() === "") continue;

      const spaceIdx = line.indexOf(" ");
      if (spaceIdx < 0) continue;

      const key = line.substring(0,  spaceIdx);
      const value = line.substring(spaceIdx + 1);

      switch (key) {
        case "BRIDGE":
          // Protocol version - could validate
          break;
        case "SEQ":
          resp.seq = parseInt(value,  10);
          break;
        case "STATUS":
          resp.status = value as "ok" | "error";
          break;
        case "TS":
          resp.timestamp = value;
          break;
        case "ERRCODE":
          resp.errCode = parseInt(value,  10);
          break;
        case "ERRMSG":
          resp.errMsg = value;
          break;
        case "RESULT":
          resp.result = value;
          break;
        case "COUNT":
          resp.count = parseInt(value,  10);
          break;

        // Multi-value fields (WINDOW,  PROCESS,  MENUBAR,  MENUITEM,  VOLUME,  FILEENTRY)
        case "WINDOW":
        case "PROCESS":
        case "MENUBAR":
        case "MENUITEM":
        case "VOLUME":
        case "FILEENTRY":
          if (!resp.multiValues[key]) resp.multiValues[key] = [];
          resp.multiValues[key].push(value);
          break;

        default:
          resp.fields[key] = value;
          break;
      }
    }

    return resp;
  }

  // ----------------------------------------------------------------
  //  Send a command and wait for response
  // ----------------------------------------------------------------

  async sendCommand(cmd: BridgeCommand): Promise<BridgeResponse> {
    const seq = this.nextSeq();
    const seqStr = this.padSeq(seq);

    // Write command file
    const cmdFilename = `C${seqStr}.msg`;
    const cmdPath = path.join(this.inboxDir,  cmdFilename);
    const content = this.formatCommand(seq,  cmd);
    const encoded = this.utf8ToMacRoman(content);

    await writeFile(cmdPath,  encoded);

    // Poll for response file
    const respFilename = `R${seqStr}.msg`;
    const respPath = path.join(this.outboxDir,  respFilename);

    const startTime = Date.now();

    while (Date.now() - startTime < RESPONSE_TIMEOUT_MS) {
      if (existsSync(respPath)) {
        // Small delay to ensure file is fully written
        await sleep(50);

        try {
          const rawBuf = await readFile(respPath);
          const respContent = this.macRomanToUtf8(rawBuf);
          const resp = this.parseResponse(respContent);

          // Clean up response file
          await unlink(respPath).catch(() => {});

          return resp;
        } catch (e) {
          // File might still be getting written,  retry
          await sleep(100);
          continue;
        }
      }

      await sleep(RESPONSE_POLL_MS);
    }

    // Timeout - clean up command file if still there
    await unlink(cmdPath).catch(() => {});

    return {
      seq,
      status: "error",
      errCode: 600,
      errMsg: `Timeout waiting for response to ${cmd.cmd} (seq ${seq})`,
      fields: {},
      multiValues: {},
    };
  }

  // ----------------------------------------------------------------
  //  Read heartbeat file
  // ----------------------------------------------------------------

  async readHeartbeat(): Promise<Heartbeat | null> {
    try {
      if (!existsSync(this.heartbeatPath)) return null;

      const fileStat = await stat(this.heartbeatPath);
      const rawBuf = await readFile(this.heartbeatPath);
      const content = this.macRomanToUtf8(rawBuf);

      // Parse heartbeat (same key-value format)
      const lines = content.replace(/\r\n/g,  "\n").replace(/\r/g,  "\n").split("\n");
      const hb: Heartbeat = {
        version: "",
        uptime: 0,
        ticks: 0,
        frontApp: "unknown",
        freeMemKB: 0,
        timestamp: "",
        fileModTime: fileStat.mtimeMs,
      };

      for (const line of lines) {
        if (line === "---") break;
        const spaceIdx = line.indexOf(" ");
        if (spaceIdx < 0) continue;

        const key = line.substring(0,  spaceIdx);
        const value = line.substring(spaceIdx + 1);

        switch (key) {
          case "BRIDGE":   hb.version = value; break;
          case "UPTIME":   hb.uptime = parseInt(value,  10); break;
          case "TICKS":    hb.ticks = parseInt(value,  10); break;
          case "FRONTAPP": hb.frontApp = value; break;
          case "FREEMEM":  hb.freeMemKB = parseInt(value,  10); break;
          case "TS":       hb.timestamp = value; break;
        }
      }

      return hb;
    } catch {
      return null;
    }
  }

  async isBridgeAlive(): Promise<boolean> {
    const hb = await this.readHeartbeat();
    if (!hb) return false;
    return (Date.now() - hb.fileModTime) < HEARTBEAT_STALE_MS;
  }
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve,  ms));
}
