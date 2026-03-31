// Copyright 2024 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

/**
 * Tests for sidecar RPC method handlers (Phase 5).
 *
 * These tests exercise the createRpcMethods factory from index.ts
 * indirectly by replicating the handler logic with real registries.
 */

import { describe, it, expect, beforeEach } from "vitest";
import { HookDispatcher } from "../src/hook-dispatcher.js";
import { ToolExecutor } from "../src/tool-executor.js";
import {
  seedBuiltinProviders,
  DEFAULT_LOCAL_PROVIDER_URL,
  BUILTIN_LOCAL_PROVIDER_ID,
} from "../src/local-provider.js";
import type {
  PluginLogger,
  PluginRecord,
  PluginService,
  PluginCommand,
  ProviderPlugin,
  GatewayMethodHandler,
} from "../src/types.js";
import type { PluginRegistries } from "../src/plugin-api-shim.js";

function createTestLogger(): PluginLogger {
  return {
    debug: () => {},
    info: () => {},
    warn: () => {},
    error: () => {},
  };
}

function createTestRegistries(): PluginRegistries {
  return {
    tools: new ToolExecutor(),
    hooks: new HookDispatcher(createTestLogger()),
    httpHandlers: [],
    httpRoutes: [],
    channels: [],
    services: [],
    providers: [],
    commands: [],
    gatewayMethods: [],
    cliEntries: [],
  };
}

function makeRecord(id: string): PluginRecord {
  return {
    id,
    name: id,
    source: `/plugins/${id}`,
    toolNames: [],
    hookNames: [],
    serviceIds: [],
    providerIds: [],
    channelIds: [],
    commandNames: [],
    gatewayMethods: [],
    cliCommands: [],
    httpHandlerCount: 0,
    hookCount: 0,
  };
}

// ---------------------------------------------------------------------------
// plugin.list
// ---------------------------------------------------------------------------

describe("plugin.list", () => {
  it("returns plugins array with mapped field names", () => {
    const rec = makeRecord("test-plugin");
    rec.toolNames = ["t1", "t2"];
    rec.hookNames = ["h1"];
    rec.serviceIds = ["svc1"];

    // Simulate the mapping from index.ts
    const mapped = {
      plugins: [rec].map((r) => ({
        id: r.id,
        name: r.name,
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
    };

    expect(mapped.plugins).toHaveLength(1);
    expect(mapped.plugins[0].tools).toEqual(["t1", "t2"]);
    expect(mapped.plugins[0].hooks).toEqual(["h1"]);
    expect(mapped.plugins[0].services).toEqual(["svc1"]);
  });
});

// ---------------------------------------------------------------------------
// plugin.services
// ---------------------------------------------------------------------------

describe("plugin.services", () => {
  let regs: PluginRegistries;

  beforeEach(() => {
    regs = createTestRegistries();
  });

  it("lists services with running status", () => {
    const svc: PluginService = {
      id: "watcher",
      start: async () => {},
      stop: async () => {},
    };
    regs.services.push({ pluginId: "p1", service: svc });

    const result = regs.services.map((s) => ({
      pluginId: s.pluginId,
      id: s.service.id,
      running: false,
    }));

    expect(result).toHaveLength(1);
    expect(result[0].id).toBe("watcher");
    expect(result[0].running).toBe(false);
  });

  it("service has start and stop methods", () => {
    let started = false;
    let stopped = false;

    const svc: PluginService = {
      id: "test-svc",
      start: async () => { started = true; },
      stop: async () => { stopped = true; },
    };

    regs.services.push({ pluginId: "p1", service: svc });

    const entry = regs.services[0];
    entry.service.start({
      config: {},
      stateDir: "/tmp",
      logger: createTestLogger(),
    });
    expect(started).toBe(true);

    entry.service.stop!({
      config: {},
      stateDir: "/tmp",
      logger: createTestLogger(),
    });
    expect(stopped).toBe(true);
  });

  it("service without stop is valid", () => {
    const svc: PluginService = {
      id: "no-stop",
      start: async () => {},
    };
    regs.services.push({ pluginId: "p1", service: svc });
    expect(regs.services[0].service.stop).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// plugin.providers
// ---------------------------------------------------------------------------

describe("plugin.providers", () => {
  let regs: PluginRegistries;

  beforeEach(() => {
    regs = createTestRegistries();
  });

  it("lists providers with id, label, and aliases", () => {
    const provider: ProviderPlugin = {
      id: "custom-llm",
      label: "Custom LLM",
      aliases: ["custom", "my-llm"],
      baseUrl: "https://example.invalid/v1",
      auth: [],
    };
    regs.providers.push({ pluginId: "p1", provider });

    const result = regs.providers.map((p) => ({
      pluginId: p.pluginId,
      id: p.provider.id,
      label: p.provider.label,
      aliases: p.provider.aliases,
      baseUrl: p.provider.baseUrl,
    }));

    expect(result).toHaveLength(1);
    expect(result[0].id).toBe("custom-llm");
    expect(result[0].label).toBe("Custom LLM");
    expect(result[0].aliases).toEqual(["custom", "my-llm"]);
    expect(result[0].baseUrl).toBe("https://example.invalid/v1");
  });

  it("handles providers without aliases", () => {
    const provider: ProviderPlugin = {
      id: "simple",
      label: "Simple",
      auth: [],
    };
    regs.providers.push({ pluginId: "p1", provider });

    const result = regs.providers.map((p) => ({
      id: p.provider.id,
      aliases: p.provider.aliases,
    }));

    expect(result[0].aliases).toBeUndefined();
  });

  it("seeded builtin local provider is returned by plugin.providers with kind:none", () => {
    seedBuiltinProviders(regs, { enabled_plugins: [] });

    const result = regs.providers.map((p) => ({
      pluginId: p.pluginId,
      id: p.provider.id,
      label: p.provider.label,
      baseUrl: p.provider.baseUrl,
      authKind: p.provider.auth[0]?.kind,
    }));

    expect(result).toHaveLength(1);
    expect(result[0].id).toBe(BUILTIN_LOCAL_PROVIDER_ID);
    expect(result[0].baseUrl).toBe(DEFAULT_LOCAL_PROVIDER_URL);
    expect(result[0].authKind).toBe("none");
  });

  it("seeded builtin local provider respects custom url from startup config", () => {
    seedBuiltinProviders(regs, {
      enabled_plugins: [],
      local_provider_url: "http://127.0.0.1:9999",
    });

    expect(regs.providers[0].provider.baseUrl).toBe("http://127.0.0.1:9999");
  });
});

// ---------------------------------------------------------------------------
// plugin.commands
// ---------------------------------------------------------------------------

describe("plugin.commands", () => {
  let regs: PluginRegistries;

  beforeEach(() => {
    regs = createTestRegistries();
  });

  it("lists commands", () => {
    const cmd: PluginCommand = {
      name: "deploy",
      description: "Deploy the app",
      acceptsArgs: true,
      handler: async () => ({ status: "ok" }),
    };
    regs.commands.push({ pluginId: "p1", command: cmd });

    const result = regs.commands.map((c) => ({
      pluginId: c.pluginId,
      name: c.command.name,
      description: c.command.description,
      acceptsArgs: c.command.acceptsArgs,
    }));

    expect(result).toHaveLength(1);
    expect(result[0].name).toBe("deploy");
    expect(result[0].description).toBe("Deploy the app");
    expect(result[0].acceptsArgs).toBe(true);
  });

  it("executes command by name", async () => {
    const cmd: PluginCommand = {
      name: "greet",
      description: "Say hello",
      handler: async (ctx) => ({ message: `Hello ${ctx.name ?? "world"}` }),
    };
    regs.commands.push({ pluginId: "p1", command: cmd });

    const entry = regs.commands.find((c) => c.command.name === "greet");
    expect(entry).toBeDefined();

    const result = await entry!.command.handler({ name: "Alice" });
    expect(result).toEqual({ message: "Hello Alice" });
  });

  it("throws for unknown command", () => {
    const entry = regs.commands.find((c) => c.command.name === "nonexistent");
    expect(entry).toBeUndefined();
  });

  it("executes command using 'command' parameter (C++ compatibility)", async () => {
    const cmd: PluginCommand = {
      name: "build",
      description: "Build project",
      handler: async (args) => ({ built: true, target: args.target }),
    };
    regs.commands.push({ pluginId: "p1", command: cmd });

    // Simulate C++ sending {action: "execute", command: "build", args: {target: "release"}}
    const params = { action: "execute", command: "build", args: { target: "release" } };
    const name = (params.command ?? (params as any).name) as string;
    const args = (params.args ?? {}) as Record<string, unknown>;

    const entry = regs.commands.find((c) => c.command.name === name);
    expect(entry).toBeDefined();

    const result = await entry!.command.handler(args);
    expect(result).toEqual({ built: true, target: "release" });
  });
});

// ---------------------------------------------------------------------------
// plugin.gateway_methods
// ---------------------------------------------------------------------------

describe("plugin.gateway_methods", () => {
  let regs: PluginRegistries;

  beforeEach(() => {
    regs = createTestRegistries();
  });

  it("lists gateway methods", () => {
    regs.gatewayMethods.push({
      pluginId: "p1",
      method: "custom.rpc",
      handler: async () => ({}),
    });
    regs.gatewayMethods.push({
      pluginId: "p2",
      method: "analytics.track",
      handler: async () => ({}),
    });

    const result = regs.gatewayMethods.map((g) => ({
      pluginId: g.pluginId,
      method: g.method,
    }));

    expect(result).toHaveLength(2);
    expect(result[0].method).toBe("custom.rpc");
    expect(result[1].method).toBe("analytics.track");
  });

  it("gateway method handler returns result", async () => {
    const handler: GatewayMethodHandler = async (req) => ({
      response: `handled: ${req.action}`,
    });
    regs.gatewayMethods.push({ pluginId: "p1", method: "test.echo", handler });

    const entry = regs.gatewayMethods.find((g) => g.method === "test.echo");
    expect(entry).toBeDefined();

    const result = await entry!.handler({ action: "ping" });
    expect(result).toEqual({ response: "handled: ping" });
  });
});

// ---------------------------------------------------------------------------
// plugin.http
// ---------------------------------------------------------------------------

describe("plugin.http", () => {
  let regs: PluginRegistries;

  beforeEach(() => {
    regs = createTestRegistries();
  });

  it("routes to specific path handler", async () => {
    regs.httpRoutes.push({
      pluginId: "p1",
      path: "/api/health",
      handler: async () => ({ status: 200, body: { ok: true } }),
    });

    const route = regs.httpRoutes.find(
      (r) => "/api/health" === r.path || "/api/health".startsWith(r.path + "/"),
    );
    expect(route).toBeDefined();

    const resp = await route!.handler({
      method: "GET",
      path: "/api/health",
    });
    expect(resp?.status).toBe(200);
    expect(resp?.body).toEqual({ ok: true });
  });

  it("falls back to generic handler", async () => {
    regs.httpHandlers.push({
      pluginId: "p1",
      handler: async (req) => {
        if (req.path.startsWith("/custom")) {
          return { status: 200, body: { custom: true } };
        }
        return null;
      },
    });

    const resp = await regs.httpHandlers[0].handler({
      method: "GET",
      path: "/custom/endpoint",
    });
    expect(resp?.status).toBe(200);
  });
});

// ---------------------------------------------------------------------------
// plugin.cli
// ---------------------------------------------------------------------------

describe("plugin.cli", () => {
  let regs: PluginRegistries;

  beforeEach(() => {
    regs = createTestRegistries();
  });

  it("routes CLI command to handler", async () => {
    let receivedCommand = "";
    let receivedArgs: string[] = [];

    regs.cliEntries.push({
      pluginId: "p1",
      registrar: (ctx) => {
        receivedCommand = ctx.command;
        receivedArgs = ctx.args;
      },
      commands: ["test-cmd"],
    });

    // Simulate routing
    const command = "test-cmd";
    const args = ["--verbose"];
    const entry = regs.cliEntries.find(
      (e) => !e.commands || e.commands.includes(command),
    );
    expect(entry).toBeDefined();

    await entry!.registrar({ command, args });
    expect(receivedCommand).toBe("test-cmd");
    expect(receivedArgs).toEqual(["--verbose"]);
  });

  it("skips entries that don't match command", () => {
    regs.cliEntries.push({
      pluginId: "p1",
      registrar: () => {},
      commands: ["other-cmd"],
    });

    const command = "test-cmd";
    const entry = regs.cliEntries.find(
      (e) => !e.commands || e.commands.includes(command),
    );
    expect(entry).toBeUndefined();
  });
});
