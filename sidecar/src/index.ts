// Copyright 2024 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

// ---------------------------------------------------------------------------
// QuantClaw Sidecar — entry point.
//
// This Node.js process is spawned by the C++ QuantClaw main process.  It
// connects to the parent's Unix domain socket, loads OpenClaw-compatible
// TypeScript plugins via jiti, and serves JSON-RPC 2.0 requests.
//
// Environment variables (set by C++ SidecarManager):
//   QUANTCLAW_PORT           — TCP port that the C++ parent listens on
//   QUANTCLAW_PLUGIN_CONFIG  — JSON string with plugin configuration
// ---------------------------------------------------------------------------

import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { RpcServer } from "./rpc-server.js";
import { HookDispatcher } from "./hook-dispatcher.js";
import { ToolExecutor } from "./tool-executor.js";
import { createPluginRuntime } from "./plugin-runtime-shim.js";
import { loadPlugins } from "./plugin-loader.js";
import type {
  HttpHandlerEntry,
  HttpRequest,
  HttpResponse,
  HttpRouteEntry,
  ChannelEntry,
  ServiceEntry,
  ProviderEntry,
  CommandEntry,
  GatewayMethodEntry,
  CliEntry,
  PluginLogger,
  PluginRecord,
  SidecarStartupConfig,
} from "./types.js";
import type { PluginRegistries } from "./plugin-api-shim.js";

// ---------------------------------------------------------------------------
// Logger
// ---------------------------------------------------------------------------

const logger: PluginLogger = {
  debug: (msg: string) => {
    if (process.env.QUANTCLAW_VERBOSE) {
      process.stderr.write(`[sidecar:debug] ${msg}\n`);
    }
  },
  info: (msg: string) => process.stderr.write(`[sidecar:info] ${msg}\n`),
  warn: (msg: string) => process.stderr.write(`[sidecar:warn] ${msg}\n`),
  error: (msg: string) => process.stderr.write(`[sidecar:error] ${msg}\n`),
};

// ---------------------------------------------------------------------------
// Parse startup configuration
// ---------------------------------------------------------------------------

function parseStartupConfig(): SidecarStartupConfig {
  const raw = process.env.QUANTCLAW_PLUGIN_CONFIG;
  if (!raw) {
    logger.warn("QUANTCLAW_PLUGIN_CONFIG not set, no plugins will be loaded");
    return { enabled_plugins: [] };
  }
  try {
    return JSON.parse(raw) as SidecarStartupConfig;
  } catch (err) {
    logger.error(`Failed to parse QUANTCLAW_PLUGIN_CONFIG: ${String(err)}`);
    return { enabled_plugins: [] };
  }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let pluginRecords: PluginRecord[] = [];
let registries: PluginRegistries;
let runningServices: Array<{ id: string; stop?: () => Promise<void> | void }> = [];

// ---------------------------------------------------------------------------
// RPC method handlers
// ---------------------------------------------------------------------------

function createRpcMethods(opts: {
  tools: ToolExecutor;
  hooks: HookDispatcher;
  getRecords: () => PluginRecord[];
  getRegistries: () => PluginRegistries;
}): Record<string, (params: Record<string, unknown>) => Promise<unknown> | unknown> {
  const { tools, hooks, getRecords, getRegistries } = opts;

  return {
    // ----- P0: Heartbeat -----
    ping: () => ({}),

    // ----- P0: Plugin listing -----
    "plugin.list": () => ({
      plugins: getRecords().map((r) => ({
        id: r.id,
        name: r.name,
        version: r.version,
        description: r.description,
        tools: r.toolNames,
        hooks: r.hookNames,
        services: r.serviceIds,
        providers: r.providerIds,
        channels: r.channelIds,
        commands: r.commandNames,
        gatewayMethods: r.gatewayMethods,
        cliEntries: r.cliCommands,
        httpHandlers: r.httpHandlerCount,
      })),
    }),

    // ----- P0: Tool schemas -----
    "plugin.tools": () => tools.getSchemas(),

    // ----- P0: Tool execution -----
    "plugin.call_tool": async (params) => {
      const toolName = params.toolName as string;
      const args = (params.args ?? {}) as Record<string, unknown>;
      if (!toolName) throw new Error("toolName is required");
      return tools.execute(toolName, args);
    },

    // ----- P0: Hook firing -----
    "plugin.hooks": async (params) => {
      const hookName = params.hookName as string;
      const event = (params.event ?? {}) as Record<string, unknown>;
      if (!hookName) throw new Error("hookName is required");
      const result = await hooks.fire(hookName, event);
      return result ?? {};
    },

    // ----- P1: HTTP request handling -----
    "plugin.http": async (params) => {
      const req: HttpRequest = {
        method: (params.method as string) ?? "GET",
        path: (params.path as string) ?? "/",
        body: params.body,
        headers: (params.headers as Record<string, string>) ?? {},
      };

      const regs = getRegistries();

      // Try specific routes first.
      for (const route of regs.httpRoutes) {
        if (req.path === route.path || req.path.startsWith(route.path + "/")) {
          const response = await route.handler(req);
          if (response) return response;
        }
      }

      // Fall back to generic handlers.
      for (const entry of regs.httpHandlers) {
        const response = await entry.handler(req);
        if (response) return response;
      }

      return { status: 404, body: { error: "Not found" } };
    },

    // ----- P1: CLI command routing -----
    "plugin.cli": async (params) => {
      const command = (params.command as string) ?? "";
      const args = (params.args as string[]) ?? [];
      const regs = getRegistries();

      for (const entry of regs.cliEntries) {
        if (entry.commands && !entry.commands.includes(command)) continue;
        try {
          await entry.registrar({ command, args });
          return { handled: true };
        } catch (err) {
          return { handled: false, error: String(err) };
        }
      }

      return { handled: false, error: `No CLI handler for command: ${command}` };
    },

    // ----- P2: Service management -----
    "plugin.services": async (params) => {
      const regs = getRegistries();
      const action = (params.action as string) ?? "list";

      if (action === "start") {
        const serviceId = params.serviceId as string;
        if (!serviceId) throw new Error("serviceId is required");
        const entry = regs.services.find((s) => s.service.id === serviceId);
        if (!entry) throw new Error(`Service not found: ${serviceId}`);
        // Check if already running
        if (runningServices.some((r) => r.id === serviceId)) {
          return { status: "already_running", id: serviceId };
        }
        const homeDir = process.env.HOME ?? process.env.USERPROFILE ?? "/tmp";
        const stateDir = path.join(homeDir, ".quantclaw", "plugins", entry.pluginId);
        const startupCfg = parseStartupConfig();
        await entry.service.start({
          config: {},
          workspaceDir: startupCfg.workspace_dir,
          stateDir,
          logger,
        });
        runningServices.push({
          id: serviceId,
          stop: entry.service.stop
            ? () => entry.service.stop!({ config: {}, workspaceDir: startupCfg.workspace_dir, stateDir, logger })
            : undefined,
        });
        return { status: "started", id: serviceId };
      }

      if (action === "stop") {
        const serviceId = params.serviceId as string;
        if (!serviceId) throw new Error("serviceId is required");
        const idx = runningServices.findIndex((r) => r.id === serviceId);
        if (idx === -1) return { status: "not_running", id: serviceId };
        const svc = runningServices[idx];
        if (svc.stop) await svc.stop();
        runningServices.splice(idx, 1);
        return { status: "stopped", id: serviceId };
      }

      // Default: list services
      return regs.services.map((s) => ({
        pluginId: s.pluginId,
        id: s.service.id,
        running: runningServices.some((r) => r.id === s.service.id),
      }));
    },

    // ----- P2: Provider listing -----
    "plugin.providers": () => {
      const regs = getRegistries();
      return regs.providers.map((p) => ({
        pluginId: p.pluginId,
        id: p.provider.id,
        label: p.provider.label,
        aliases: p.provider.aliases,
      }));
    },

    // ----- P2: Command listing & execution -----
    "plugin.commands": async (params) => {
      const regs = getRegistries();
      const action = (params.action as string) ?? "list";

      if (action === "execute") {
        // Accept both "command" (C++ side) and "name" for backwards compat
        const name = (params.command ?? params.name) as string;
        const args = (params.args ?? params.ctx ?? {}) as Record<string, unknown>;
        if (!name) throw new Error("command name is required");
        const entry = regs.commands.find((c) => c.command.name === name);
        if (!entry) throw new Error(`Command not found: ${name}`);
        return entry.command.handler(args);
      }

      // Default: list commands.
      return regs.commands.map((c) => ({
        pluginId: c.pluginId,
        name: c.command.name,
        description: c.command.description,
        acceptsArgs: c.command.acceptsArgs,
      }));
    },

    // ----- P2: Gateway method listing -----
    "plugin.gateway_methods": () => {
      const regs = getRegistries();
      return regs.gatewayMethods.map((g) => ({
        pluginId: g.pluginId,
        method: g.method,
      }));
    },
  };
}

// ---------------------------------------------------------------------------
// Service lifecycle
// ---------------------------------------------------------------------------

async function startServices(
  serviceEntries: ServiceEntry[],
  config: Record<string, unknown>,
  workspaceDir?: string,
): Promise<void> {
  const homeDir = process.env.HOME ?? process.env.USERPROFILE ?? "/tmp";

  for (const entry of serviceEntries) {
    try {
      const stateDir = path.join(homeDir, ".quantclaw", "plugins", entry.pluginId);
      await entry.service.start({
        config,
        workspaceDir,
        stateDir,
        logger,
      });
      runningServices.push({
        id: entry.service.id,
        stop: entry.service.stop
          ? () => entry.service.stop!({
              config,
              workspaceDir,
              stateDir,
              logger,
            })
          : undefined,
      });
      logger.info(`[services] started: ${entry.service.id}`);
    } catch (err) {
      logger.error(`[services] failed to start ${entry.service.id}: ${String(err)}`);
    }
  }
}

async function stopServices(): Promise<void> {
  for (const svc of runningServices.toReversed()) {
    if (svc.stop) {
      try {
        await svc.stop();
        logger.info(`[services] stopped: ${svc.id}`);
      } catch (err) {
        logger.warn(`[services] failed to stop ${svc.id}: ${String(err)}`);
      }
    }
  }
  runningServices = [];
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main(): Promise<void> {
  const portStr = process.env.QUANTCLAW_PORT;
  if (!portStr) {
    logger.error("QUANTCLAW_PORT environment variable not set");
    process.exit(1);
  }

  const ipcPort = parseInt(portStr, 10);
  logger.info(`sidecar starting, TCP port: ${ipcPort}`);

  // Parse startup config.
  const startupConfig = parseStartupConfig();

  // Create shared registries.
  const tools = new ToolExecutor();
  const hooks = new HookDispatcher(logger);
  registries = {
    tools,
    hooks,
    httpHandlers: [],
    httpRoutes: [],
    channels: [],
    services: [],
    providers: [],
    commands: [],
    gatewayMethods: [],
    cliEntries: [],
  };

  // Create runtime shim.
  const config: Record<string, unknown> = {
    workspace_dir: startupConfig.workspace_dir,
    plugins: startupConfig.plugins,
  };
  const runtime = createPluginRuntime({ config, workspaceDir: startupConfig.workspace_dir, logger });

  // Resolve SDK shim path (adjacent to this file).
  const thisDir = path.dirname(fileURLToPath(import.meta.url));
  const sdkShimPath = path.join(thisDir, "plugin-sdk-shim.js");

  // Load plugins.
  const loadResult = await loadPlugins({
    startupConfig,
    runtime,
    logger,
    registries,
    sdkShimPath,
  });
  pluginRecords = loadResult.records;

  // Start plugin services.
  await startServices(registries.services, config, startupConfig.workspace_dir);

  // Create RPC server.
  const rpcMethods = createRpcMethods({
    tools,
    hooks,
    getRecords: () => pluginRecords,
    getRegistries: () => registries,
  });

  const rpc = new RpcServer({
    port: ipcPort,
    methods: rpcMethods,
    onError: (err) => logger.error(`[rpc] ${err.message}`),
    onConnected: () => logger.info("[rpc] connected to parent process"),
    onDisconnected: () => {
      logger.info("[rpc] disconnected from parent process");
      // Parent disconnected — shut down gracefully.
      void shutdown(rpc);
    },
  });

  // Connect to the C++ parent's IPC socket.
  try {
    await rpc.connect();
  } catch (err) {
    logger.error(`[rpc] failed to connect to 127.0.0.1:${ipcPort}: ${String(err)}`);
    process.exit(1);
  }

  // Handle signals.
  process.on("SIGTERM", () => {
    logger.info("received SIGTERM, shutting down");
    void shutdown(rpc);
  });

  process.on("SIGHUP", () => {
    logger.info("received SIGHUP, reload not yet implemented");
    // TODO: reload plugins without stopping.
  });

  process.on("uncaughtException", (err) => {
    logger.error(`uncaught exception: ${String(err)}`);
  });

  process.on("unhandledRejection", (reason) => {
    logger.error(`unhandled rejection: ${String(reason)}`);
  });

  logger.info(
    `sidecar ready: ${pluginRecords.length} plugins, ` +
    `${tools.toolNames().length} tools, ` +
    `${hooks.registeredHooks().length} hooks`,
  );
}

async function shutdown(rpc: RpcServer): Promise<void> {
  try {
    await stopServices();
    rpc.disconnect();
  } catch (err) {
    logger.error(`[shutdown] ${String(err)}`);
  }
  process.exit(0);
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------

main().catch((err) => {
  logger.error(`fatal: ${String(err)}`);
  process.exit(1);
});
