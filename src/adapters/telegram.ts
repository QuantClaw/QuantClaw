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

import "dotenv/config";
import { Telegraf } from "telegraf";
import { ChannelAdapter, runAdapter } from "./base.js";

// Ensure gateway URL defaults correctly for local dev if not set
if (!process.env.QUANTCLAW_GATEWAY_URL) {
  process.env.QUANTCLAW_GATEWAY_URL = "ws://127.0.0.1:18800";
}

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
    this.channelName = "telegram";

    this.bot.on("text", async (ctx) => {
      const msg = ctx.message;
      console.log(`[telegram] Received message from ${msg.from.id}: ${msg.text}`);

      // Show typing
      try {
        await ctx.sendChatAction("typing");
      } catch (e) {
        console.error("[telegram] Failed to send typing action:", e);
      }

      await this.handlePlatformMessage(
        String(msg.from.id),
        String(msg.chat.id),
        msg.text,
        String(msg.message_id)
      );
    });
  }

  protected async handlePlatformMessage(
    senderId: string,
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    const sessionKey = `agent:main:telegram:direct:${senderId}`;
    console.log(`[telegram] Handling message: sender=${senderId}, text="${text}"`);

    try {
      const response = await this.agentRequest(text, sessionKey);
      if (response) {
        await this.sendToPlatform(channelId, response, replyTo);
      }
    } catch (err) {
      console.error("[telegram] Error handling message:", err);
      await this.sendToPlatform(channelId, "Sorry, I encountered an error processing your request.", replyTo);
    }
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
