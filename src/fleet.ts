/**
 * classic-mac-mcp - Fleet Registry
 *
 * Manages the collection of Classic Mac targets.
 * Loads fleet configuration, creates BridgeClients,
 * provides target lookup by ID or alias.
 */

import { readFile } from "fs/promises";
import {
  FleetConfig,
  TargetConfig,
  TargetState,
  BridgeStatus,
} from "./types.js";
import { BridgeClient } from "./bridge/client.js";

export interface FleetTarget {
  config: TargetConfig;
  bridge: BridgeClient;
}

export class FleetRegistry {
  private targets: Map<string, FleetTarget> = new Map();
  private aliases: Map<string, string> = new Map();       // alias -> id

  /**
   * Load fleet config from JSON file and create BridgeClients.
   */
  async loadConfig(configPath: string): Promise<void> {
    const raw = await readFile(configPath, "utf-8");
    const config = JSON.parse(raw) as FleetConfig;

    for (const target of config.fleet) {
      const bridge = new BridgeClient(target.shared_folder);
      this.targets.set(target.id, { config: target, bridge });

      if (target.alias) {
        this.aliases.set(target.alias.toLowerCase(), target.id);
      }
    }

    console.error(`[fleet] Loaded ${this.targets.size} target(s) from config`);
  }

  /**
   * Resolve a target ID or alias to a FleetTarget.
   */
  getTarget(idOrAlias: string): FleetTarget | undefined {
    // Try direct ID lookup
    let target = this.targets.get(idOrAlias);
    if (target) return target;

    // Try alias lookup
    const resolved = this.aliases.get(idOrAlias.toLowerCase());
    if (resolved) return this.targets.get(resolved);

    // Try case-insensitive ID match
    for (const [id, t] of this.targets) {
      if (id.toLowerCase() === idOrAlias.toLowerCase()) return t;
    }

    return undefined;
  }

  /**
   * Get the default target (if fleet has exactly one target).
   */
  getDefaultTarget(): FleetTarget | undefined {
    if (this.targets.size === 1) {
      return this.targets.values().next().value as FleetTarget;
    }
    return undefined;
  }

  /**
   * Get all target IDs.
   */
  getTargetIds(): string[] {
    return [...this.targets.keys()];
  }

  /**
   * Get status of all targets.
   */
  async getAllStatus(): Promise<TargetState[]> {
    const states: TargetState[] = [];

    for (const [, target] of this.targets) {
      try {
        const hb = await target.bridge.readHeartbeat();
        const alive = await target.bridge.isBridgeAlive();

        let bridgeStatus: BridgeStatus;
        if (!hb) {
          bridgeStatus = "unknown";
        } else if (alive) {
          bridgeStatus = "alive";
        } else {
          bridgeStatus = "stale";
        }

        states.push({
          config: target.config,
          bridgeStatus,
          lastHeartbeat: hb ?? undefined,
          lastProbeTime: Date.now(),
        });
      } catch (err: any) {
        states.push({
          config: target.config,
          bridgeStatus: "unknown",
          error: err.message,
          lastProbeTime: Date.now(),
        });
      }
    }

    return states;
  }
}
