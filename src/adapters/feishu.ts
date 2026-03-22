/**
 * QuantClaw Feishu (Lark) Adapter
 *
 * Bridges Feishu/Lark messages to the QuantClaw agent via the gateway WebSocket RPC.
 * Runs as a subprocess managed by ChannelAdapterManager.
 *
 * Environment variables (set by adapter manager):
 *   QUANTCLAW_GATEWAY_URL    — ws://127.0.0.1:18800
 *   QUANTCLAW_AUTH_TOKEN     — gateway auth token
 *   QUANTCLAW_CHANNEL_NAME   — "feishu"
 *   QUANTCLAW_CHANNEL_CONFIG — JSON: {"appId":"cli_xxx","appSecret":"xxx","dmPolicy":"open",...}
 *
 * Usage:
 *   npm install
 *   npx tsx feishu.ts
 */

import { Client, EventPayload } from "@larksuiteoapi/node-sdk";
import { ChannelAdapter, runAdapter } from "./base.js";

interface FeishuConfig {
  appId: string;
  appSecret: string;
  domain?: string;
  dmPolicy?: string;
  groupPolicy?: string;
  botName?: string;
  requireMention?: boolean;
  [key: string]: unknown;
}

class FeishuAdapter extends ChannelAdapter {
  private client: Client;
  private dmPolicy: string;
  private groupPolicy: string;
  private requireMention: boolean;
  private botName: string = "";
  private server: ReturnType<typeof import("http").createServer> | null = null;

  constructor() {
    super();

    const cfg = this.channelConfig as FeishuConfig;
    this.dmPolicy = cfg.dmPolicy ?? "pairing";
    this.groupPolicy = cfg.groupPolicy ?? "open";
    this.requireMention = cfg.requireMention ?? true;
    this.botName = cfg.botName ?? "AI Assistant";

    console.log(
      `[feishu] Config: dmPolicy=${this.dmPolicy}, groupPolicy=${this.groupPolicy}, requireMention=${this.requireMention}`
    );

    this.client = new Client({
      appId: cfg.appId || process.env.FEISHU_APP_ID || "",
      appSecret: cfg.appSecret || process.env.FEISHU_APP_SECRET || "",
      domain: cfg.domain || "https://open.feishu.cn",
    });
  }

  private async onMessageReceive(event: EventPayload<"im.message.receive_v1">): Promise<void> {
    const message = event.data.message;
    const sender = event.data.sender;
    const chat = event.data.chat;

    // Ignore own messages
    if (message.sender_id?.user_id === this.client.config.appId) return;

    let content = message.content ?? "";

    // Parse message content (Feishu uses JSON format)
    try {
      const parsed = JSON.parse(content);
      if (parsed.text) {
        content = parsed.text;
      } else if (parsed.content) {
        content = parsed.content;
      }
    } catch {
      // Plain text content
    }

    const isDM = chat.chat_mode === "p2p";
    const chatId = chat.chat_id;
    const senderId = sender.sender_id?.user_id || sender.sender_id?.open_id || "unknown";

    console.log(
      `[feishu] Message from ${senderId} in ${isDM ? "DM" : `group#${chatId}`}: "${content.slice(0, 80)}"`
    );

    if (isDM) {
      // DM policy: "open" = respond to all, "closed" = ignore, "pairing" = require approval
      if (this.dmPolicy === "closed") {
        console.log("[feishu] DM ignored (dmPolicy=closed)");
        return;
      }
    } else {
      // Group message
      if (this.groupPolicy === "closed" || this.groupPolicy === "disabled") {
        console.log("[feishu] Group message ignored (groupPolicy=disabled)");
        return;
      }

      // Check if bot is mentioned
      const botMentioned = message.mentions?.some(
        (m) => m.name === this.botName || m.id?.user_id === this.client.config.appId
      );

      if (botMentioned) {
        // Bot mentioned, process the message
        console.log("[feishu] Bot mentioned, processing");
      } else if (this.requireMention) {
        // requireMention=true and not mentioned → ignore
        console.log("[feishu] Group message ignored (requireMention=true, not mentioned)");
        return;
      }
      // groupPolicy="open" + requireMention=false → respond to all messages
    }

    if (!content || content.trim() === "") return;

    console.log(
      `[feishu] Processing: "${content.slice(0, 80)}" (session: channel:feishu:${chatId})`
    );

    // Send typing indicator (if supported)
    try {
      await this.client.im.typing.create({
        params: {
          chat_id: chatId,
        },
        data: {
          action: 1, // Start typing
        },
      });
    } catch (e) {
      console.warn("[feishu] Failed to send typing indicator");
    }

    // Use open_id for session key to ensure proper user identification
    const peerId = sender.sender_id?.open_id || senderId;
    await this.handlePlatformMessage(
      peerId,
      chatId,
      content.trim(),
      message.message_id
    );
  }

  protected async startPlatform(): Promise<void> {
    console.log("[feishu] Starting Feishu bot...");

    const cfg = this.channelConfig as FeishuConfig;
    if (!cfg.appId || !cfg.appSecret) {
      throw new Error(
        "Feishu App ID and App Secret required. Set in channel config or FEISHU_APP_ID/FEISHU_APP_SECRET env."
      );
    }

    // Verify credentials by getting tenant_access_token via POST
    try {
      const res = await this.client.request({
        method: "POST",
        url: "/open-apis/auth/v3/tenant_access_token/internal",
        data: {
          app_id: cfg.appId,
          app_secret: cfg.appSecret,
        },
      });
      if (res.code !== 0) {
        throw new Error(`Feishu auth failed: ${res.msg}`);
      }
      console.log("[feishu] Successfully authenticated with Feishu");
    } catch (e: unknown) {
      const err = e as { msg?: string; message?: string };
      throw new Error(`Feishu connection failed: ${err.msg || err.message}`);
    }

    // Start HTTP server for webhook events
    const port = parseInt(process.env.FEISHU_WEBHOOK_PORT || "9200", 10);
    const webhookPath = process.env.FEISHU_WEBHOOK_PATH || "/webhook/feishu";

    const http = await import("http");
    const server = http.createServer((req, res) => {
      if (req.method === "POST" && req.url === webhookPath) {
        let body = "";
        req.on("data", (chunk) => (body += chunk));
        req.on("end", () => {
          try {
            const data = JSON.parse(body);
            // Handle challenge verification
            if (data.type === "url_verification" && data.challenge) {
              console.log("[feishu] URL verification challenge received");
              res.writeHead(200, { "Content-Type": "application/json" });
              res.end(JSON.stringify({ challenge: data.challenge }));
              return;
            }
            // Handle message events
            if (data.header?.event_type === "im.message.receive_v1") {
              this.onMessageReceive(data as EventPayload<"im.message.receive_v1">).catch((e) => {
                console.error("[feishu] Error handling message:", e);
              });
            }
            res.writeHead(200);
            res.end("{}");
          } catch (e) {
            console.error("[feishu] Error parsing webhook body:", e);
            res.writeHead(400);
            res.end("Bad Request");
          }
        });
      } else {
        res.writeHead(404);
        res.end("Not Found");
      }
    });

    server.listen(port, () => {
      console.log(`[feishu] Webhook server listening on port ${port}, path: ${webhookPath}`);
    });

    this.server = server;
    console.log(`[feishu] Bot ready. Configure webhook URL: http://your-server:${port}${webhookPath}`);
  }

  protected async stopPlatform(): Promise<void> {
    console.log("[feishu] Stopping Feishu bot...");
    if (this.server) {
      this.server.close();
      this.server = null;
    }
  }

  protected async sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    // Feishu limit: 2000 chars per message for text
    const chunks = text.match(/[\s\S]{1,2000}/g) ?? [text];

    for (let i = 0; i < chunks.length; i++) {
      try {
        const payload: Record<string, unknown> = {
          receive_id: channelId,
          msg_type: "text",
          content: JSON.stringify({ text: chunks[i] }),
        };

        if (replyTo && i === 0) {
          payload.reply_id = replyTo;
        }

        await this.client.im.message.create({
          params: {
            receive_id: channelId,
          },
          query: {
            receive_id_type: "chat_id",
          },
          data: payload,
        });
      } catch (e: unknown) {
        const err = e as { msg?: string; message?: string };
        console.error(
          `[feishu] Failed to send message chunk ${i + 1}/${chunks.length}: ${err.msg || err.message}`
        );
      }
    }
  }

  // Handle long-connection events from gateway
  async handleLongConnEvent(eventType: string, eventData: Record<string, unknown>): Promise<void> {
    console.log(`[feishu] Long-conn event: ${eventType}`);

    if (eventType === "im.message.receive_v1") {
      const payload = eventData as EventPayload<"im.message.receive_v1">;
      await this.onMessageReceive(payload);
    }
  }
}

// For Feishu, the gateway manages the WebSocket long-connection
// and forwards events to this adapter
const adapter = new FeishuAdapter();

// Handle the special Feishu mode where gateway pushes events
if (process.env.FEISHU_LONG_CONN === "1") {
  console.log("[feishu] Running in long-connection mode, waiting for gateway events...");

  process.on("message", (msg) => {
    try {
      const { eventType, eventData } = JSON.parse(msg as string);
      adapter.handleLongConnEvent(eventType, eventData).catch(console.error);
    } catch (e) {
      console.error("[feishu] Failed to process gateway message:", e);
    }
  });

  process.on("SIGINT", () => adapter.stop());
  process.on("SIGTERM", () => adapter.stop());

  adapter.run().catch((err) => {
    console.error("[feishu] Fatal:", err);
    process.exit(1);
  });
} else {
  // Standard mode: run as standalone adapter
  runAdapter(FeishuAdapter);
}
