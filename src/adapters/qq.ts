/**
 * QuantClaw QQ Adapter
 *
 * Bridges QQ group/private messages to the QuantClaw agent via the gateway
 * WebSocket RPC.  Uses the qq-official-bot SDK (WebSocket mode) which talks
 * to the QQ Bot open-platform API v2.
 *
 * Prerequisites:
 *   1. Register a bot at https://q.qq.com
 *   2. Obtain appId + appSecret
 *   3. Enable "群聊@消息" and/or "C2C私聊消息" intents in the platform console
 *
 * Environment variables (set by adapter manager):
 *   QUANTCLAW_GATEWAY_URL    — ws://127.0.0.1:18800
 *   QUANTCLAW_AUTH_TOKEN     — gateway auth token
 *   QUANTCLAW_CHANNEL_NAME   — "qq"
 *   QUANTCLAW_CHANNEL_CONFIG — JSON with appId, appSecret, etc.
 *
 * Usage:
 *   npm install
 *   npx tsx qq.ts
 */

import { Bot, ReceiverMode } from "qq-official-bot";
import { ChannelAdapter, runAdapter } from "./base.js";

interface QQConfig {
  appId: string;
  appSecret: string;
  sandbox?: boolean;
  dmPolicy?: string;
  groupPolicy?: string;
  requireMention?: boolean;
  [key: string]: unknown;
}

class QQAdapter extends ChannelAdapter {
  private bot: Bot;
  private cfg: QQConfig;

  constructor() {
    super();

    this.cfg = this.channelConfig as unknown as QQConfig;

    const appId =
      this.cfg.appId || (process.env.QQ_APP_ID as string) || "";
    const appSecret =
      this.cfg.appSecret || (process.env.QQ_APP_SECRET as string) || "";

    if (!appId || !appSecret) {
      throw new Error(
        "QQ bot appId and appSecret required. " +
          "Set in channel config or QQ_APP_ID / QQ_APP_SECRET env vars."
      );
    }

    const intents: string[] = [
      "GROUP_AT_MESSAGE_CREATE",
      "C2C_MESSAGE_CREATE",
      "GUILD_MESSAGES",
      "DIRECT_MESSAGE",
    ];

    this.bot = new Bot({
      appid: appId,
      secret: appSecret,
      sandbox: this.cfg.sandbox ?? false,
      removeAt: true,
      logLevel: "info",
      maxRetry: 10,
      intents,
      mode: ReceiverMode.WEBSOCKET,
    } as ConstructorParameters<typeof Bot>[0]);

    console.log(
      `[qq] Config: sandbox=${this.cfg.sandbox ?? false}, ` +
        `groupPolicy=${this.cfg.groupPolicy ?? "mention"}, ` +
        `dmPolicy=${this.cfg.dmPolicy ?? "open"}`
    );
  }

  protected async startPlatform(): Promise<void> {
    this.bot.on("message" as any, async (event: any) => {
      try {
        await this.onMessage(event);
      } catch (err) {
        console.error("[qq] Error handling message:", err);
      }
    });

    await this.bot.start();
    console.log("[qq] Bot started");
  }

  protected async stopPlatform(): Promise<void> {
    try {
      await (this.bot as any).stop?.();
    } catch {
      // SDK may not expose stop()
    }
  }

  private async onMessage(event: any): Promise<void> {
    const messageType: string = event.message_type ?? "";
    const content: string = (event.raw_message ?? event.content ?? "").trim();
    if (!content) return;

    const senderId: string =
      event.sender?.user_openid ??
      event.sender?.user_id ??
      event.user_id ??
      "unknown";

    let channelId: string;

    if (messageType === "group") {
      channelId = event.group_id ?? event.group_openid ?? "group-unknown";

      const groupPolicy = this.cfg.groupPolicy ?? "mention";
      if (groupPolicy === "closed") {
        console.log("[qq] Group message ignored (groupPolicy=closed)");
        return;
      }
    } else if (messageType === "private") {
      channelId = `dm-${senderId}`;

      const dmPolicy = this.cfg.dmPolicy ?? "open";
      if (dmPolicy === "closed") {
        console.log("[qq] DM ignored (dmPolicy=closed)");
        return;
      }
    } else if (messageType === "guild") {
      channelId = event.channel_id ?? "guild-unknown";
    } else {
      channelId = event.channel_id ?? event.group_id ?? "unknown";
    }

    const msgId: string = event.message_id ?? event.id ?? "";

    console.log(
      `[qq] ${messageType} from ${senderId} in ${channelId}: "${content.slice(0, 80)}"`
    );

    // Store the event so sendToPlatform can call event.reply
    this._lastEvent = event;

    await this.handlePlatformMessage(senderId, channelId, content, msgId);
  }

  private _lastEvent: any = null;
  private _replyMap = new Map<string, any>();

  protected async sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    const chunks = text.match(/[\s\S]{1,2000}/g) ?? [text];

    // Prefer event.reply() when available (handles group/private/guild routing)
    const event = this._lastEvent;
    if (event?.reply) {
      for (const chunk of chunks) {
        try {
          await event.reply(chunk);
        } catch (err) {
          console.error("[qq] Failed to reply via event.reply():", err);
          await this.sendViaService(channelId, chunk, event);
        }
      }
      return;
    }

    for (const chunk of chunks) {
      await this.sendViaService(channelId, chunk, event);
    }
  }

  private async sendViaService(
    channelId: string,
    text: string,
    event: any
  ): Promise<void> {
    try {
      const messageType = event?.message_type ?? "";
      if (messageType === "group" && event?.group_id) {
        await this.bot.sendGroupMessage(event.group_id, text, event);
      } else if (messageType === "guild" && event?.channel_id) {
        await this.bot.sendGuildMessage(event.channel_id, text, event);
      } else if (
        messageType === "private" &&
        (event?.user_id || event?.guild_id)
      ) {
        if (event.sub_type === "direct" && event.guild_id) {
          await this.bot.sendDirectMessage(event.guild_id, text, event);
        } else {
          await this.bot.sendPrivateMessage(event.user_id, text, event);
        }
      } else {
        console.warn(
          `[qq] Cannot determine send target for channel=${channelId}`
        );
      }
    } catch (err) {
      console.error("[qq] sendViaService failed:", err);
    }
  }
}

runAdapter(QQAdapter);
