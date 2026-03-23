/**
 * QuantClaw Discord Adapter
 *
 * Bridges Discord messages to the QuantClaw agent via the gateway WebSocket RPC.
 * Runs as a subprocess managed by ChannelAdapterManager.
 *
 * Environment variables (set by adapter manager):
 *   QUANTCLAW_GATEWAY_URL    — ws://127.0.0.1:18789
 *   QUANTCLAW_AUTH_TOKEN     — gateway auth token
 *   QUANTCLAW_CHANNEL_NAME   — "discord"
 *   QUANTCLAW_CHANNEL_CONFIG — JSON: {"token":"...","allowedChannels":[...]}
 *
 * Usage:
 *   npm install
 *   npx tsx discord.ts
 */

import { Client, GatewayIntentBits, Message } from "discord.js";
import { ChannelAdapter, runAdapter } from "./base.js";

class DiscordAdapter extends ChannelAdapter {
  private client: Client;
  private prefix: string;
  private requireMention: boolean;
  private groupPolicy: string;
  private dmPolicy: string;

  constructor() {
    super();

    const cfg = this.channelConfig as Record<string, unknown>;
    this.prefix = (cfg.prefix as string) ?? "!";
    this.requireMention = (cfg.requireMention as boolean) ?? true;
    this.groupPolicy = (cfg.groupPolicy as string) ?? "mention";
    this.dmPolicy = (cfg.dmPolicy as string) ?? "open";

    console.log(`[discord] Config: requireMention=${this.requireMention}, groupPolicy=${this.groupPolicy}, dmPolicy=${this.dmPolicy}, prefix="${this.prefix}"`);

    this.client = new Client({
      intents: [
        GatewayIntentBits.Guilds,
        GatewayIntentBits.GuildMessages,
        GatewayIntentBits.MessageContent,
        GatewayIntentBits.DirectMessages,
      ],
    });

    this.client.on("clientReady", () => {
      console.log(
        `[discord] Bot logged in as ${this.client.user?.tag} (ID: ${this.client.user?.id})`
      );
    });

    this.client.on("messageCreate", (msg) => this.onMessage(msg));
  }

  private async onMessage(msg: Message): Promise<void> {
    // Ignore own messages
    if (msg.author.id === this.client.user?.id) return;

    let content = msg.content.trim();
    const botMentioned = msg.mentions.has(this.client.user!);
    const isDM = !msg.guild;

    console.log(`[discord] Message from ${msg.author.tag} in ${isDM ? "DM" : `guild#${msg.channel.id}`}: "${content.slice(0, 80)}"`);

    if (isDM) {
      // DM policy: "open" = respond to all, "closed" = ignore
      if (this.dmPolicy === "closed") {
        console.log("[discord] DM ignored (dmPolicy=closed)");
        return;
      }
    } else {
      // Guild message
      if (this.groupPolicy === "closed") {
        console.log("[discord] Guild message ignored (groupPolicy=closed)");
        return;
      }

      if (botMentioned) {
        // Always respond to @mentions, strip the mention text
        content = content
          .replace(new RegExp(`<@!?${this.client.user!.id}>`, "g"), "")
          .trim();
      } else if (content.startsWith(this.prefix)) {
        // Prefix command
        content = content.slice(this.prefix.length).trim();
      } else if (this.requireMention || this.groupPolicy === "mention") {
        // requireMention=true or groupPolicy="mention" → ignore plain messages
        return;
      }
      // groupPolicy="open" + requireMention=false → respond to all messages
    }

    if (!content) return;

    console.log(`[discord] Processing: "${content.slice(0, 80)}" (session: channel:discord:${msg.channel.id})`);

    // Show typing while processing
    try {
      await msg.channel.sendTyping();
    } catch (e) {
      console.warn("[discord] Failed to send typing indicator");
    }

    await this.handlePlatformMessage(
      msg.author.id,
      msg.channel.id,
      content,
      msg.id
    );
  }

  protected async startPlatform(): Promise<void> {
    const token =
      this.channelConfig.token || process.env.DISCORD_BOT_TOKEN || "";
    if (!token) {
      throw new Error(
        "Discord bot token required. Set in channel config or DISCORD_BOT_TOKEN env."
      );
    }

    await this.client.login(token);
  }

  protected async stopPlatform(): Promise<void> {
    await this.client.destroy();
  }

  protected async sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    const channel = await this.client.channels.fetch(channelId);
    if (!channel?.isTextBased()) return;

    // Discord limit: 2000 chars per message
    const chunks = text.match(/[\s\S]{1,2000}/g) ?? [text];

    for (let i = 0; i < chunks.length; i++) {
      if (i === 0 && replyTo && "messages" in channel) {
        try {
          const ref = await (channel as any).messages.fetch(replyTo);
          await ref.reply(chunks[i]);
          continue;
        } catch {
          // Fall through to regular send
        }
      }

      if ("send" in channel) {
        await (channel as any).send(chunks[i]);
      }
    }
  }
}

runAdapter(DiscordAdapter);
