import test from "node:test";
import assert from "node:assert/strict";

import { ChannelAdapter } from "./base.js";

class TestAdapter extends ChannelAdapter {
  public requests: Array<{ message: string; sessionKey?: string }> = [];
  public sent: Array<{ channelId: string; text: string; replyTo?: string }> = [];

  protected async startPlatform(): Promise<void> {}
  protected async stopPlatform(): Promise<void> {}

  protected async sendToPlatform(
    channelId: string,
    text: string,
    replyTo?: string
  ): Promise<void> {
    this.sent.push({ channelId, text, replyTo });
  }

  async agentRequest(message: string, sessionKey?: string): Promise<string> {
    this.requests.push({ message, sessionKey });
    return "adapter reply";
  }
}

function withChannelConfig(
  config: Record<string, unknown>,
  fn: () => Promise<void>
): Promise<void> {
  const previousName = process.env.QUANTCLAW_CHANNEL_NAME;
  const previousConfig = process.env.QUANTCLAW_CHANNEL_CONFIG;

  process.env.QUANTCLAW_CHANNEL_NAME = "discord";
  process.env.QUANTCLAW_CHANNEL_CONFIG = JSON.stringify(config);

  return fn().finally(() => {
    if (previousName === undefined) {
      delete process.env.QUANTCLAW_CHANNEL_NAME;
    } else {
      process.env.QUANTCLAW_CHANNEL_NAME = previousName;
    }

    if (previousConfig === undefined) {
      delete process.env.QUANTCLAW_CHANNEL_CONFIG;
    } else {
      process.env.QUANTCLAW_CHANNEL_CONFIG = previousConfig;
    }
  });
}

test("allowedIds permits messages when sender is allowlisted", async () => {
  await withChannelConfig(
    { token: "discord-token", allowedIds: ["user-1"] },
    async () => {
      const adapter = new TestAdapter();
      await adapter.handlePlatformMessage("user-1", "channel-9", "hello");

      assert.equal(adapter.requests.length, 1);
      assert.equal(adapter.sent.length, 1);
    }
  );
});

test("allowedIds permits messages when channel is allowlisted", async () => {
  await withChannelConfig(
    { token: "discord-token", allowedIds: ["channel-9"] },
    async () => {
      const adapter = new TestAdapter();
      await adapter.handlePlatformMessage("user-1", "channel-9", "hello");

      assert.equal(adapter.requests.length, 1);
      assert.equal(adapter.sent.length, 1);
    }
  );
});

test("allowedIds blocks messages when neither sender nor channel matches", async () => {
  await withChannelConfig(
    { token: "discord-token", allowedIds: ["user-2", "channel-2"] },
    async () => {
      const adapter = new TestAdapter();
      await adapter.handlePlatformMessage("user-1", "channel-9", "hello");

      assert.equal(adapter.requests.length, 0);
      assert.equal(adapter.sent.length, 0);
    }
  );
});
