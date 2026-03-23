/**
 * QuantClaw Channel Adapter Base
 *
 * Generic bridge between any messaging platform and the QuantClaw gateway.
 * Each platform adapter extends ChannelAdapter and implements:
 *   - startPlatform()    — connect to the platform, start listening
 *   - stopPlatform()     — disconnect from the platform
 *   - sendToPlatform()   — send a message back to the platform
 *
 * The base class handles:
 *   - WebSocket connection to QuantClaw gateway
 *   - Authentication handshake (connect.hello)
 *   - Sending chat.send RPC and collecting the response
 */

import WebSocket from "ws";

// ---- Types ----

export interface ChannelConfig {
  token: string;
  allowedChannels?: string[];
  allowedUsers?: string[];
  [key: string]: unknown;
}

interface RpcRequest {
  type: "req";
  id: string;
  method: string;
  params: Record<string, unknown>;
}

interface RpcResponse {
  type: "res";
  id: string;
  ok: boolean;
  payload?: unknown;
  error?: string;
}

interface RpcEvent {
  type: "event";
  event: string;
  payload: Record<string, unknown>;
}

type GatewayFrame = RpcResponse | RpcEvent;

// ---- Base Adapter ----

export abstract class ChannelAdapter {
  protected gatewayUrl: string;
  protected authToken: string;
  protected channelConfig: ChannelConfig;
  protected channelName: string;

  private ws: WebSocket | null = null;
  private requestCounter = 0;
  private pending = new Map<
    string,
    { resolve: (v: unknown) => void; reject: (e: Error) => void }
  >();
  private running = false;

  constructor() {
    this.gatewayUrl =
      process.env.QUANTCLAW_GATEWAY_URL ?? "ws://127.0.0.1:18800";
    this.authToken = process.env.QUANTCLAW_AUTH_TOKEN ?? "";
    this.channelName = process.env.QUANTCLAW_CHANNEL_NAME ?? "unknown";

    try {
      this.channelConfig = JSON.parse(
        process.env.QUANTCLAW_CHANNEL_CONFIG ?? "{}"
      );
    } catch {
      this.channelConfig = { token: "" };
    }
  }

  // ---- Abstract: platform side ----

  protected abstract startPlatform(): Promise<void>;
  protected abstract stopPlatform(): Promise<void>;
  protected abstract sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void>;

  // ---- Gateway communication ----

  private nextId(): string {
    return String(++this.requestCounter);
  }

  private connectGateway(): Promise<void> {
    return new Promise((resolve, reject) => {
      console.log(`[adapter] Connecting to gateway at ${this.gatewayUrl}`);
      this.ws = new WebSocket(this.gatewayUrl);

      let handshakeDone = false;

      this.ws.on("open", () => {
        console.log("[adapter] WebSocket connected, waiting for challenge...");
      });

      this.ws.on("message", (data: WebSocket.Data) => {
        const frame: GatewayFrame = JSON.parse(data.toString());

        if (frame.type === "event") {
          const evt = frame as RpcEvent;

          // Handle challenge → send hello
          if (evt.event === "connect.challenge" && !handshakeDone) {
            const helloId = this.nextId();
            const hello: RpcRequest = {
              type: "req",
              id: helloId,
              method: "connect.hello",
              params: {
                minProtocol: 1,
                maxProtocol: 1,
                clientName: `quantclaw-adapter-${this.channelName}`,
                clientVersion: "0.2.0",
                role: "operator",
                scopes: ["operator.read", "operator.write"],
                authToken: this.authToken,
              },
            };
            this.ws!.send(JSON.stringify(hello));
            return;
          }
        }

        if (frame.type === "res") {
          const resp = frame as RpcResponse;

          // Hello-ok detection
          if (
            !handshakeDone &&
            resp.ok &&
            (resp.payload as Record<string, unknown>)?.protocol
          ) {
            handshakeDone = true;
            console.log("[adapter] Authenticated with gateway");
            resolve();
            return;
          }

          // Resolve pending RPC calls
          const p = this.pending.get(resp.id);
          if (p) {
            this.pending.delete(resp.id);
            if (resp.ok) {
              p.resolve(resp.payload);
            } else {
              p.reject(new Error(resp.error ?? "RPC error"));
            }
          }
        }
      });

      this.ws.on("error", (err) => {
        console.error("[adapter] WebSocket error:", err.message);
        if (!handshakeDone) reject(err);
      });

      this.ws.on("close", () => {
        console.log("[adapter] WebSocket closed");
        // Reject all pending calls
        for (const [, p] of this.pending) {
          p.reject(new Error("Connection closed"));
        }
        this.pending.clear();
      });
    });
  }

  protected rpcCall(
    method: string,
    params: Record<string, unknown> = {},
    timeoutMs = 120_000
  ): Promise<unknown> {
    return new Promise((resolve, reject) => {
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        reject(new Error("Not connected to gateway"));
        return;
      }

      const id = this.nextId();
      const request: RpcRequest = { type: "req", id, method, params };

      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`RPC timeout: ${method}`));
      }, timeoutMs);

      this.pending.set(id, {
        resolve: (v) => {
          clearTimeout(timer);
          resolve(v);
        },
        reject: (e) => {
          clearTimeout(timer);
          reject(e);
        },
      });

      this.ws.send(JSON.stringify(request));
    });
  }

  // ---- High-level: send user message, get agent reply ----

  async agentRequest(message: string, sessionKey?: string): Promise<string> {
    sessionKey ??= `channel:${this.channelName}:default`;

    console.log(`[adapter] Sending chat.send: session=${sessionKey}, message="${message.slice(0, 80)}"`);

    const result = (await this.rpcCall("chat.send", {
      message,
      sessionKey,
    })) as { response?: string };

    console.log(`[adapter] chat.send response: "${(result?.response ?? "").slice(0, 100)}"`);
    return result?.response ?? "";
  }

  // ---- Message handler: called by platform subclass ----

  async handlePlatformMessage(
    senderId: string,
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    // Check allowlists
    const { allowedUsers, allowedChannels } = this.channelConfig;
    if (allowedUsers?.length && !allowedUsers.includes(senderId)) return;
    if (allowedChannels?.length && !allowedChannels.includes(channelId)) return;

    const sessionKey = `channel:${this.channelName}:${channelId}`;
    console.log(
      `[adapter] Message from ${senderId} in ${channelId}: ${text.slice(0, 80)}`
    );

    try {
      const response = await this.agentRequest(text, sessionKey);
      await this.sendToPlatform(channelId, response || "(no response)", replyTo);
    } catch (err) {
      console.error("[adapter] Failed to process message:", err);
      await this.sendToPlatform(
        channelId,
        "Error processing your message.",
        replyTo
      );
    }
  }

  // ---- Lifecycle ----

  async run(): Promise<void> {
    this.running = true;

    await this.connectGateway();
    await this.startPlatform();

    console.log(`[adapter] '${this.channelName}' running`);

    // Keep alive until stopped
    await new Promise<void>((resolve) => {
      const check = setInterval(() => {
        if (!this.running) {
          clearInterval(check);
          resolve();
        }
      }, 1000);
    });

    await this.stopPlatform();
    this.ws?.close();
    console.log(`[adapter] '${this.channelName}' stopped`);
  }

  stop(): void {
    this.running = false;
  }
}

/**
 * Helper to run an adapter from the command line.
 * Handles SIGINT/SIGTERM for graceful shutdown.
 */
export function runAdapter(AdapterClass: new () => ChannelAdapter): void {
  const adapter = new AdapterClass();

  process.on("SIGINT", () => adapter.stop());
  process.on("SIGTERM", () => adapter.stop());

  adapter.run().catch((err) => {
    console.error("[adapter] Fatal:", err);
    process.exit(1);
  });
}
