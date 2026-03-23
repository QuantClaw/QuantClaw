/**
 * QuantClaw Telegram Adapter
 *
 * Bridges Telegram messages to the QuantClaw agent via the gateway WebSocket RPC.
 *
 * Environment variables (set by adapter manager):
 *   QUANTCLAW_GATEWAY_URL    — ws://127.0.0.1:18789
 *   QUANTCLAW_AUTH_TOKEN     — gateway auth token
 *   QUANTCLAW_CHANNEL_NAME   — "telegram"
 *   QUANTCLAW_CHANNEL_CONFIG — JSON: {"token":"...","allowedUsers":[...]}
 *
 * Usage:
 *   npm install
 *   npx tsx telegram.ts
 */

import { Telegraf } from "telegraf";
import { ChannelAdapter, runAdapter } from "./base.js";

class TelegramAdapter extends ChannelAdapter {
  private bot: Telegraf;

  constructor() {
    super();

    const token =
      this.channelConfig.token || process.env.TELEGRAM_BOT_TOKEN || "";
    if (!token) {
      throw new Error(
        "Telegram bot token required. Set in channel config or TELEGRAM_BOT_TOKEN env."
      );
    }

    this.bot = new Telegraf(token);

    this.bot.on("text", async (ctx) => {
      const msg = ctx.message;

      // Show typing
      await ctx.sendChatAction("typing");

      await this.handlePlatformMessage(
        String(msg.from.id),
        String(msg.chat.id),
        msg.text,
        String(msg.message_id)
      );
    });
  }

  protected async startPlatform(): Promise<void> {
    await this.bot.launch();
    console.log("[telegram] Bot started");
  }

  protected async stopPlatform(): Promise<void> {
    this.bot.stop("adapter shutdown");
  }

  protected async sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    const chatId = Number(channelId);

    // Telegram limit: 4096 chars
    const chunks = text.match(/[\s\S]{1,4096}/g) ?? [text];

    for (let i = 0; i < chunks.length; i++) {
      const opts: Record<string, unknown> = {};
      if (i === 0 && replyTo) {
        opts.reply_to_message_id = Number(replyTo);
      }
      await this.bot.telegram.sendMessage(chatId, chunks[i], opts);
    }
  }
}

runAdapter(TelegramAdapter);
