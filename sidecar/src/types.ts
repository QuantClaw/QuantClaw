// Copyright 2024 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

// ---------------------------------------------------------------------------
// Shared type definitions for the QuantClaw sidecar.
// ---------------------------------------------------------------------------

/** JSON-RPC 2.0 request. */
export interface JsonRpcRequest {
  jsonrpc: "2.0";
  method: string;
  params?: Record<string, unknown>;
  id: number | string;
}

/** JSON-RPC 2.0 success response. */
export interface JsonRpcSuccessResponse {
  jsonrpc: "2.0";
  result: unknown;
  id: number | string;
}

/** JSON-RPC 2.0 error response. */
export interface JsonRpcErrorResponse {
  jsonrpc: "2.0";
  error: { code: number; message: string; data?: unknown } | string;
  id: number | string | null;
}

export type JsonRpcResponse = JsonRpcSuccessResponse | JsonRpcErrorResponse;

// ---------------------------------------------------------------------------
// Plugin types — compatible with OpenClaw's plugin-sdk
// ---------------------------------------------------------------------------

/** Minimal tool schema exposed to the LLM. */
export interface ToolSchema {
  name: string;
  description: string;
  inputSchema?: Record<string, unknown>;
  parameters?: Record<string, unknown>;
}

/** A callable agent tool registered by a plugin. */
export interface AgentTool {
  name: string;
  description: string;
  inputSchema?: Record<string, unknown>;
  parameters?: Record<string, unknown>;
  run: (params: Record<string, unknown>) => Promise<unknown> | unknown;
}

/** Factory that lazily creates tools given context. */
export type ToolFactory = (ctx: ToolFactoryContext) => AgentTool | AgentTool[] | null | undefined;

export interface ToolFactoryContext {
  config?: Record<string, unknown>;
  workspaceDir?: string;
  agentId?: string;
}

export interface ToolRegistrationOptions {
  name?: string;
  names?: string[];
  optional?: boolean;
}

/** A registered tool entry with its owning plugin. */
export interface ToolEntry {
  pluginId: string;
  tool: AgentTool;
}

// ---------------------------------------------------------------------------
// Hook types
// ---------------------------------------------------------------------------

export type HookHandler = (
  event: Record<string, unknown>,
  ctx?: Record<string, unknown>,
) => Promise<Record<string, unknown> | void> | Record<string, unknown> | void;

export interface HookRegistration {
  pluginId: string;
  hookName: string;
  handler: HookHandler;
  priority: number;
}

export interface HookOptions {
  priority?: number;
}

/**
 * Hook execution mode — must match OpenClaw semantics exactly.
 *
 * - void:      fire-and-forget, all handlers run in parallel
 * - modifying: sequential, results merged via Object.assign
 * - sync:      synchronous only, Promise return is rejected
 */
export type HookMode = "void" | "modifying" | "sync";

/** Classification of every OpenClaw hook. */
export const HOOK_MODES: Record<string, HookMode> = {
  before_model_resolve: "modifying",
  before_prompt_build: "modifying",
  before_agent_start: "modifying",
  llm_input: "void",
  llm_output: "void",
  agent_end: "void",
  before_compaction: "void",
  after_compaction: "void",
  before_reset: "void",
  message_received: "void",
  message_sending: "modifying",
  message_sent: "void",
  before_tool_call: "modifying",
  after_tool_call: "void",
  tool_result_persist: "sync",
  before_message_write: "sync",
  session_start: "void",
  session_end: "void",
  subagent_spawning: "modifying",
  subagent_delivery_target: "modifying",
  subagent_spawned: "void",
  subagent_ended: "void",
  gateway_start: "void",
  gateway_stop: "void",
};

// ---------------------------------------------------------------------------
// HTTP handler types
// ---------------------------------------------------------------------------

export interface HttpRequest {
  method: string;
  path: string;
  body?: unknown;
  headers?: Record<string, string>;
}

export interface HttpResponse {
  status: number;
  body?: unknown;
  headers?: Record<string, string>;
}

export type HttpHandler = (req: HttpRequest) => Promise<HttpResponse | null> | HttpResponse | null;

export interface HttpRouteEntry {
  pluginId: string;
  path: string;
  handler: HttpHandler;
}

export interface HttpHandlerEntry {
  pluginId: string;
  handler: HttpHandler;
}

// ---------------------------------------------------------------------------
// CLI types
// ---------------------------------------------------------------------------

export type CliRegistrar = (ctx: CliContext) => void | Promise<void>;

export interface CliContext {
  command: string;
  args: string[];
}

export interface CliEntry {
  pluginId: string;
  registrar: CliRegistrar;
  commands?: string[];
}

// ---------------------------------------------------------------------------
// Service types
// ---------------------------------------------------------------------------

export interface PluginService {
  id: string;
  start: (ctx: ServiceContext) => void | Promise<void>;
  stop?: (ctx: ServiceContext) => void | Promise<void>;
}

export interface ServiceContext {
  config: Record<string, unknown>;
  workspaceDir?: string;
  stateDir: string;
  logger: PluginLogger;
}

export interface ServiceEntry {
  pluginId: string;
  service: PluginService;
}

// ---------------------------------------------------------------------------
// Provider types
// ---------------------------------------------------------------------------

export interface ProviderPlugin {
  id: string;
  label: string;
  docsPath?: string;
  aliases?: string[];
  envVars?: string[];
  baseUrl?: string;
  models?: Record<string, unknown>;
  auth: ProviderAuthMethod[];
  formatApiKey?: (cred: Record<string, unknown>) => string;
}

export interface ProviderAuthMethod {
  id: string;
  label: string;
  hint?: string;
  kind: "oauth" | "api_key" | "token" | "device_code" | "custom" | "none";
  run: (ctx: Record<string, unknown>) => Promise<Record<string, unknown>>;
}

export interface ProviderEntry {
  pluginId: string;
  provider: ProviderPlugin;
}

// ---------------------------------------------------------------------------
// Command types
// ---------------------------------------------------------------------------

export interface PluginCommand {
  name: string;
  description: string;
  acceptsArgs?: boolean;
  requireAuth?: boolean;
  handler: (ctx: Record<string, unknown>) => Promise<Record<string, unknown>> | Record<string, unknown>;
}

export interface CommandEntry {
  pluginId: string;
  command: PluginCommand;
}

// ---------------------------------------------------------------------------
// Gateway method types
// ---------------------------------------------------------------------------

export type GatewayMethodHandler = (
  req: Record<string, unknown>,
) => Promise<Record<string, unknown>> | Record<string, unknown>;

export interface GatewayMethodEntry {
  pluginId: string;
  method: string;
  handler: GatewayMethodHandler;
}

// ---------------------------------------------------------------------------
// Channel types
// ---------------------------------------------------------------------------

export interface ChannelPlugin {
  id: string;
  meta?: Record<string, unknown>;
  capabilities?: Record<string, unknown>;
  [key: string]: unknown;
}

export interface ChannelEntry {
  pluginId: string;
  channel: ChannelPlugin;
}

// ---------------------------------------------------------------------------
// Logger
// ---------------------------------------------------------------------------

export interface PluginLogger {
  debug?: (message: string) => void;
  info: (message: string) => void;
  warn: (message: string) => void;
  error: (message: string) => void;
}

// ---------------------------------------------------------------------------
// Plugin record — tracks what a plugin has registered
// ---------------------------------------------------------------------------

export interface PluginRecord {
  id: string;
  name: string;
  version?: string;
  description?: string;
  source: string;

  toolNames: string[];
  hookNames: string[];
  serviceIds: string[];
  providerIds: string[];
  channelIds: string[];
  commandNames: string[];
  gatewayMethods: string[];
  cliCommands: string[];
  httpHandlerCount: number;
  hookCount: number;
}

// ---------------------------------------------------------------------------
// Plugin definition — what a plugin module exports
// ---------------------------------------------------------------------------

export interface PluginDefinition {
  id?: string;
  name?: string;
  description?: string;
  version?: string;
  kind?: string;
  configSchema?: Record<string, unknown>;
  register?: (api: PluginApi) => void | Promise<void>;
  activate?: (api: PluginApi) => void | Promise<void>;
}

// ---------------------------------------------------------------------------
// Plugin API — the interface given to each plugin's register() function.
// Compatible with OpenClaw's OpenClawPluginApi.
// ---------------------------------------------------------------------------

export interface PluginApi {
  id: string;
  name: string;
  version?: string;
  description?: string;
  source: string;
  config: Record<string, unknown>;
  pluginConfig?: Record<string, unknown>;
  runtime: PluginRuntime;
  logger: PluginLogger;

  registerTool: (
    tool: AgentTool | ToolFactory,
    opts?: ToolRegistrationOptions,
  ) => void;
  registerHook: (
    events: string | string[],
    handler: HookHandler,
    opts?: HookOptions,
  ) => void;
  registerHttpHandler: (handler: HttpHandler) => void;
  registerHttpRoute: (params: { path: string; handler: HttpHandler }) => void;
  registerChannel: (registration: { plugin: ChannelPlugin } | ChannelPlugin) => void;
  registerGatewayMethod: (method: string, handler: GatewayMethodHandler) => void;
  registerCli: (registrar: CliRegistrar, opts?: { commands?: string[] }) => void;
  registerService: (service: PluginService) => void;
  registerProvider: (provider: ProviderPlugin) => void;
  registerCommand: (command: PluginCommand) => void;
  resolvePath: (input: string) => string;
  on: <K extends string>(
    hookName: K,
    handler: HookHandler,
    opts?: { priority?: number },
  ) => void;
}

// ---------------------------------------------------------------------------
// Plugin runtime — stub-able runtime context.
// OpenClaw's PluginRuntime is enormous; we provide a minimal shim.
// ---------------------------------------------------------------------------

export interface PluginRuntime {
  version: string;
  config: {
    loadConfig: () => Record<string, unknown>;
    writeConfigFile: (patch: Record<string, unknown>) => void;
  };
  system: {
    enqueueSystemEvent: (event: Record<string, unknown>) => void;
    runCommandWithTimeout: (
      cmd: string,
      args: string[],
      opts?: Record<string, unknown>,
    ) => Promise<{ stdout: string; stderr: string; exitCode: number }>;
    formatNativeDependencyHint: (dep: string) => string;
  };
  logging: {
    shouldLogVerbose: () => boolean;
    getChildLogger: (
      bindings?: Record<string, unknown>,
      opts?: { level?: string },
    ) => PluginLogger;
  };
  state: {
    resolveStateDir: (pluginId: string) => string;
  };
  // Stubs for subsystems not yet implemented in QuantClaw sidecar.
  media: Record<string, (...args: unknown[]) => unknown>;
  tts: Record<string, (...args: unknown[]) => unknown>;
  tools: Record<string, (...args: unknown[]) => unknown>;
  channel: Record<string, Record<string, (...args: unknown[]) => unknown>>;
}

// ---------------------------------------------------------------------------
// Startup configuration from QUANTCLAW_PLUGIN_CONFIG env var
// ---------------------------------------------------------------------------

export interface SidecarStartupConfig {
  enabled_plugins: string[];
  workspace_dir?: string;
  local_provider_url?: string;
  plugins?: Record<string, Record<string, unknown>>;
}
