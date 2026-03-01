// Copyright 2024 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 server over TCP loopback.
//
// Protocol: line-delimited JSON — each message is one JSON object followed
// by a single '\n'.  The C++ parent process (SidecarManager) acts as the
// TCP *server* and the sidecar connects as a *client*.
//
// The C++ parent passes the bound port via QUANTCLAW_PORT.  The sidecar
// connects to 127.0.0.1:PORT using Node.js's standard net module — no
// additional npm dependencies required, and works identically on Linux,
// macOS, and Windows.
// ---------------------------------------------------------------------------

import * as net from "node:net";
import type { JsonRpcRequest, JsonRpcResponse } from "./types.js";

export type RpcMethodHandler = (
  params: Record<string, unknown>,
) => Promise<unknown> | unknown;

export interface RpcServerOptions {
  port: number;
  host?: string;
  methods: Record<string, RpcMethodHandler>;
  onError?: (err: Error) => void;
  onConnected?: () => void;
  onDisconnected?: () => void;
}

/**
 * A JSON-RPC 2.0 client that connects to the C++ parent's TCP loopback port
 * and responds to incoming requests.
 */
export class RpcServer {
  private socket: net.Socket | null = null;
  private buffer = "";
  private connected = false;
  private readonly methods: Record<string, RpcMethodHandler>;
  private readonly port: number;
  private readonly host: string;
  private readonly onError: (err: Error) => void;
  private readonly onConnected: () => void;
  private readonly onDisconnected: () => void;

  constructor(opts: RpcServerOptions) {
    this.port = opts.port;
    this.host = opts.host ?? "127.0.0.1";
    this.methods = opts.methods;
    this.onError = opts.onError ?? ((err) => console.error("[rpc]", err.message));
    this.onConnected = opts.onConnected ?? (() => {});
    this.onDisconnected = opts.onDisconnected ?? (() => {});
  }

  /** Connect to the C++ parent's TCP IPC port. */
  connect(): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      const socket = net.createConnection(this.port, this.host, () => {
        this.connected = true;
        this.onConnected();
        resolve();
      });

      socket.setEncoding("utf-8");

      socket.on("data", (chunk: string) => {
        this.buffer += chunk;
        this.processBuffer();
      });

      socket.on("error", (err) => {
        if (!this.connected) {
          reject(err);
        } else {
          this.onError(err);
        }
      });

      socket.on("close", () => {
        this.connected = false;
        this.onDisconnected();
      });

      this.socket = socket;
    });
  }

  /** Disconnect from the socket. */
  disconnect(): void {
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
      this.connected = false;
    }
  }

  isConnected(): boolean {
    return this.connected;
  }

  // -----------------------------------------------------------------------
  // Internal
  // -----------------------------------------------------------------------

  /** Process accumulated buffer for complete JSON lines. */
  private processBuffer(): void {
    let newlineIdx: number;
    while ((newlineIdx = this.buffer.indexOf("\n")) !== -1) {
      const line = this.buffer.slice(0, newlineIdx).trim();
      this.buffer = this.buffer.slice(newlineIdx + 1);
      if (line.length > 0) {
        this.handleLine(line);
      }
    }
  }

  /** Parse and dispatch a single JSON-RPC line. */
  private handleLine(line: string): void {
    let request: JsonRpcRequest;
    try {
      request = JSON.parse(line) as JsonRpcRequest;
    } catch {
      this.sendResponse({
        jsonrpc: "2.0",
        error: { code: -32700, message: "Parse error" },
        id: null,
      });
      return;
    }

    if (!request.method || request.id === undefined) {
      this.sendResponse({
        jsonrpc: "2.0",
        error: { code: -32600, message: "Invalid Request" },
        id: request.id ?? null,
      });
      return;
    }

    const handler = this.methods[request.method];
    if (!handler) {
      this.sendResponse({
        jsonrpc: "2.0",
        error: { code: -32601, message: `Method not found: ${request.method}` },
        id: request.id,
      });
      return;
    }

    // Execute — may be sync or async.
    void this.executeHandler(handler, request);
  }

  private async executeHandler(
    handler: RpcMethodHandler,
    request: JsonRpcRequest,
  ): Promise<void> {
    try {
      const result = await handler(request.params ?? {});
      this.sendResponse({
        jsonrpc: "2.0",
        result: result ?? {},
        id: request.id,
      });
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      this.sendResponse({
        jsonrpc: "2.0",
        error: { code: -32000, message },
        id: request.id,
      });
    }
  }

  private sendResponse(response: JsonRpcResponse): void {
    if (!this.socket || !this.connected) return;
    const line = JSON.stringify(response) + "\n";
    this.socket.write(line);
  }
}
