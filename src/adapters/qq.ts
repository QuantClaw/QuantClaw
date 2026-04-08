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

import { Bot, ReceiverMode, type Intent } from "qq-official-bot";
import { ChannelAdapter, runAdapter } from "./base.js";

const DEFAULT_INTENTS: Intent[] = [
  "GROUP_AT_MESSAGE_CREATE",
  "C2C_MESSAGE_CREATE",
  "GUILD_MESSAGES",
  "DIRECT_MESSAGE",
];

const VALID_INTENTS = new Set<string>([
  "GUILDS",
  "GUILD_MEMBERS",
  "GUILD_MESSAGES",
  "GUILD_MESSAGE_REACTIONS",
  "DIRECT_MESSAGE",
  "AUDIO_OR_LIVE_CHANNEL_MEMBERS",
  "GROUP_MESSAGE_CREATE",
  "C2C_MESSAGE_CREATE",
  "GROUP_AT_MESSAGE_CREATE",
  "INTERACTION",
  "MESSAGE_AUDIT",
  "FORUMS_EVENTS",
  "OPEN_FORUMS_EVENTS",
  "AUDIO_ACTIONS",
  "PUBLIC_GUILD_MESSAGES",
]);

const CHUNK_INTERVAL_MS = 300;

interface QQConfig {
  appId: string;
  appSecret: string;
  sandbox?: boolean;
  dmPolicy?: string;
  groupPolicy?: string;
  intents?: string[];
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

    let intents: Intent[];
    if (this.cfg.intents?.length) {
      const invalid = this.cfg.intents.filter((i) => !VALID_INTENTS.has(i));
      if (invalid.length) {
        console.warn(`[qq] Unknown intents ignored: ${invalid.join(", ")}`);
      }
      intents = this.cfg.intents.filter((i) =>
        VALID_INTENTS.has(i)
      ) as Intent[];
      if (!intents.length) {
        console.warn("[qq] No valid intents after filtering, using defaults");
        intents = DEFAULT_INTENTS;
      }
    } else {
      intents = DEFAULT_INTENTS;
    }

    this.bot = new Bot({
      appid: appId,
      secret: appSecret,
      sandbox: this.cfg.sandbox ?? false,
      removeAt: true,
      logLevel: "info",
      maxRetry: 10,
      intents,
      mode: ReceiverMode.WEBSOCKET,
    });

    console.log(
      `[qq] Config: sandbox=${this.cfg.sandbox ?? false}, ` +
        `groupPolicy=${this.cfg.groupPolicy ?? "mention"}, ` +
        `dmPolicy=${this.cfg.dmPolicy ?? "open"}, ` +
        `intents=[${intents.join(",")}]`
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

  private _msgCounter = 0;

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
        return;
      }
      if (groupPolicy === "mention") {
        const botId = (this.bot as any).self_id;
        const mentionPattern = botId
          ? new RegExp(`<@!?${botId}>`)
          : null;
        const isMentioned =
          event.mentions?.some((m: any) => m.id === botId) ||
          (mentionPattern && mentionPattern.test(event.content ?? ""));
        if (!isMentioned) {
          return;
        }
      }
    } else if (messageType === "private") {
      channelId = `dm-${senderId}`;

      const dmPolicy = this.cfg.dmPolicy ?? "open";
      if (dmPolicy === "closed") {
        return;
      }
    } else if (messageType === "guild") {
      channelId = event.channel_id ?? "guild-unknown";
    } else {
      channelId = event.channel_id ?? event.group_id ?? "unknown";
    }

    const msgId: string =
      event.message_id ?? event.id ?? `_fallback_${Date.now()}_${++this._msgCounter}`;

    console.log(
      `[qq] ${messageType} from ${senderId} in ${channelId}: "${content.slice(0, 80)}"`
    );

    this._replyMap.set(msgId, event);
    try {
      await this.handlePlatformMessage(senderId, channelId, content, msgId);
    } finally {
      this._replyMap.delete(msgId);
    }
  }

  private _replyMap = new Map<string, any>();

  private sleep(ms: number): Promise<void> {
    return new Promise((r) => setTimeout(r, ms));
  }

  protected async sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    const chunks = text.match(/[\s\S]{1,2000}/g) ?? [text];

    const event = replyTo ? this._replyMap.get(replyTo) : undefined;

    if (!event) {
      console.error(
        `[qq] No event context for replyTo=${replyTo ?? "(none)"}, channel=${channelId}; message dropped`
      );
      return;
    }

    if (event.reply) {
      for (let i = 0; i < chunks.length; i++) {
        if (i > 0) await this.sleep(CHUNK_INTERVAL_MS);
        try {
          await event.reply(chunks[i]);
        } catch (err) {
          console.error("[qq] Failed to reply via event.reply():", err);
          await this.sendViaService(channelId, chunks[i], event);
        }
      }
      return;
    }

    for (let i = 0; i < chunks.length; i++) {
      if (i > 0) await this.sleep(CHUNK_INTERVAL_MS);
      await this.sendViaService(channelId, chunks[i], event);
    }
  }

  private async sendViaService(
    channelId: string,
    text: string,
    event: any
  ): Promise<void> {
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
      throw new Error(
        `Cannot determine send target: messageType=${messageType}, channel=${channelId}`
      );
    }
  }
}

runAdapter(QQAdapter);
