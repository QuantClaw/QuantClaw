/**
 * QuantClaw Feishu (Lark) Adapter
 *
 * Bridges Feishu/Lark messages to the QuantClaw agent via the gateway WebSocket RPC.
 * Uses Feishu SDK's long connection (WebSocket) mode for event subscription.
 */

import {
  Client,
  EventDispatcher,
  LoggerLevel,
  WSClient,
} from "@larksuiteoapi/node-sdk";

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

// Event structure from Feishu long connection
type FeishuMessageEvent = {
  sender: {
    sender_id: { open_id?: string; user_id?: string; union_id?: string };
    display_name?: string;
    simple_name?: string;
    tenant_key?: string;
  };
  message: {
    message_id: string;
    root_id?: string;
    parent_id?: string;
    chat_id: string;
    chat_type: "p2p" | "group";
    message_type: string;
    content: string;
    mentions?: Array<{
      key: string;
      id: { open_id?: string; user_id?: string; union_id?: string };
      name: string;
      tenant_key?: string;
    }>;
  };
};

class FeishuAdapter extends ChannelAdapter {
  private client: Client;
  private wsClient: WSClient | null = null;
  private dmPolicy: string;
  private groupPolicy: string;
  private requireMention: boolean;
  private botName: string = "";
  private appId: string = "";

  constructor() {
    super();

    const cfg = this.channelConfig as FeishuConfig;
    this.dmPolicy = cfg.dmPolicy ?? "pairing";
    this.groupPolicy = cfg.groupPolicy ?? "open";
    this.requireMention = cfg.requireMention ?? true;
    this.botName = cfg.botName ?? "AI Assistant";
    this.appId = cfg.appId || process.env.FEISHU_APP_ID || "";

    console.log(
      `[feishu] Config: dmPolicy=${this.dmPolicy}, groupPolicy=${
        this.groupPolicy
      }, requireMention=${this.requireMention}`,
    );

    this.client = new Client({
      appId: this.appId,
      appSecret: cfg.appSecret || process.env.FEISHU_APP_SECRET || "",
      domain: cfg.domain || "https://open.feishu.cn",
    });
  }

  private async onMessageReceive(data: FeishuMessageEvent): Promise<void> {
    console.log("[feishu] Received message event via long connection");

    const message = data.message;
    const sender = data.sender;

    if (!message) {
      console.error("[feishu] Invalid event structure, missing message");
      return;
    }

    // Extract chat info from message object
    const chatId = message.chat_id;
    const chatType = message.chat_type;
    const isDM = chatType === "p2p";

    // Extract sender info - we need open_id for sending replies
    const senderOpenId = sender?.sender_id?.open_id || "";
    const senderUserId = sender?.sender_id?.user_id || "";
    const senderName =
      sender?.display_name || sender?.simple_name || senderUserId;

    // Ignore own messages (bot messages have sender_type === "app")
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const senderType = (sender as any)?.sender_type;
    const senderId = (sender as any)?.sender_id?.id;
    if (senderType === "app" && senderId === this.appId) {
      return;
    }

    // Parse message content
    let content = message.content ?? "";
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

    console.log(
      `[feishu] Message from ${senderName} (open_id=${senderOpenId}, user_id=${
        senderUserId
      }) in ${isDM ? "DM" : `group#${chatId}`}: "${content.slice(0, 80)}"`,
    );

    if (isDM) {
      // DM policy: "open" = respond to all, "closed" = ignore
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
      const botMentioned = message.mentions?.some((m) => {
        // Check by name or by ID (for app users, id.id contains the app_id)
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const mentionId = (m.id as any)?.id;
        const mentioned =
          m.name === this.botName ||
          mentionId === this.appId ||
          m.id?.user_id === this.appId ||
          m.id?.open_id === this.appId;
        return mentioned;
      });

      if (botMentioned) {
        console.log("[feishu] Bot mentioned, processing");
      } else if (this.requireMention) {
        console.log(
          "[feishu] Group message ignored (requireMention=true, not mentioned)",
        );
        return;
      }
    }

    if (!content || content.trim() === "") return;

    console.log(
      `[feishu] Processing: "${content.slice(0, 80)}" (session: channel:feishu:${chatId})`,
    );

    // Send typing indicator
    try {
      await this.client.im.typing.create({
        params: { chat_id: chatId },
        data: { action: 1 },
      });
    } catch (e) {
      console.warn("[feishu] Failed to send typing indicator");
    }

    // For DM, use sender's open_id as the reply target
    // For group, use chat_id
    const replyTargetId = isDM ? senderOpenId : chatId;

    // Store open_id in session for later use
    const peerId = senderOpenId || senderUserId;
    await this.handlePlatformMessage(
      peerId,
      replyTargetId,
      content.trim(),
      message.message_id,
    );
  }

  protected async startPlatform(): Promise<void> {
    console.log("[feishu] Starting Feishu bot with long connection mode...");

    const cfg = this.channelConfig as FeishuConfig;
    if (!cfg.appId || !cfg.appSecret) {
      throw new Error(
        "Feishu App ID and App Secret required. Set in channel config or FEISHU_APP_ID/FEISHU_APP_SECRET env.",
      );
    }

    // Create event dispatcher and register message handler
    const eventDispatcher = new EventDispatcher({}).register({
      "im.message.receive_v1": async (data) => {
        await this.onMessageReceive(data as unknown as FeishuMessageEvent);
      },
    });

    // Create and start WebSocket client for long connection
    const wsClient = new WSClient({
      appId: cfg.appId,
      appSecret: cfg.appSecret,
      domain: cfg.domain || "https://open.feishu.cn",
      loggerLevel: LoggerLevel.info,
    });

    try {
      await wsClient.start({ eventDispatcher });
      console.log("[feishu] Long connection established successfully");
      this.wsClient = wsClient;
    } catch (e: unknown) {
      const err = e as { message?: string };
      console.error(
        "[feishu] Failed to start long connection:",
        err.message || e,
      );
      throw e;
    }
  }

  protected async stopPlatform(): Promise<void> {
    console.log("[feishu] Stopping Feishu bot...");
    if (this.wsClient) {
      try {
        (this.wsClient as any).stop?.();
      } catch (e) {
        console.warn("[feishu] Error stopping WSClient:", e);
      }
      this.wsClient = null;
    }
  }

  protected async sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string,
  ): Promise<void> {
    const chunks = text.match(/[\s\S]{1,2000}/g) ?? [text];

    for (let i = 0; i < chunks.length; i++) {
      try {
        // Determine receive_id_type based on channel ID prefix
        // oc_ = group chat (chat_id), ou_ = user (open_id)
        const isGroupChat = channelId.startsWith("oc_");
        const receiveIdType = isGroupChat ? "chat_id" : "open_id";

        console.log(
          `[feishu] Sending reply to ${channelId} (type=${receiveIdType}): "${chunks[
            i
          ].slice(0, 50)}"`,
        );

        const payload: Record<string, unknown> = {
          receive_id: channelId,
          msg_type: "text",
          content: JSON.stringify({ text: chunks[i] }),
        };

        if (replyTo && i === 0) {
          payload.reply_id = replyTo;
        }

        const result = await this.client.im.v1.message.create({
          params: { receive_id_type: receiveIdType },
          data: payload,
        });

        console.log(
          `[feishu] Message sent successfully:`,
          result?.data?.message_id || result,
        );
      } catch (e: unknown) {
        const err = e as {
          msg?: string;
          message?: string;
          response?: {
            data?: {
              field_violations?: Array<{ field: string; description: string }>;
            };
          };
        };
        console.error(
          `[feishu] Failed to send message chunk ${i + 1}/${chunks.length}: ${
            err.msg || err.message
          }`,
        );
        if (err.response?.data?.field_violations) {
          console.error(
            `[feishu] Field violations:`,
            JSON.stringify(err.response.data.field_violations, null, 2),
          );
        }
      }
    }
  }
}

// Standard mode: run as standalone adapter
runAdapter(FeishuAdapter);
