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

test("messages from different channels use separate session keys", async () => {
  await withChannelConfig({ token: "t" }, async () => {
    process.env.QUANTCLAW_CHANNEL_NAME = "telegram";
    const adapter = new TestAdapter();
    await adapter.handlePlatformMessage("user-1", "chat-100", "msg1");
    await adapter.handlePlatformMessage("user-1", "chat-200", "msg2");

    assert.equal(adapter.requests.length, 2);
    assert.notEqual(adapter.requests[0].sessionKey, adapter.requests[1].sessionKey);
    assert.ok(adapter.requests[0].sessionKey?.includes("chat-100"));
    assert.ok(adapter.requests[1].sessionKey?.includes("chat-200"));
  });
});

test("allowedUsers blocks messages from non-allowlisted sender", async () => {
  await withChannelConfig(
    { token: "t", allowedUsers: ["user-allowed"] },
    async () => {
      const adapter = new TestAdapter();
      await adapter.handlePlatformMessage("user-blocked", "channel-1", "hello");

      assert.equal(adapter.requests.length, 0);
      assert.equal(adapter.sent.length, 0);
    }
  );
});

test("allowedChannels blocks messages from non-allowlisted channel", async () => {
  await withChannelConfig(
    { token: "t", allowedChannels: ["channel-allowed"] },
    async () => {
      const adapter = new TestAdapter();
      await adapter.handlePlatformMessage("user-1", "channel-blocked", "hello");

      assert.equal(adapter.requests.length, 0);
      assert.equal(adapter.sent.length, 0);
    }
  );
});

test("default gateway URL is ws://127.0.0.1:18800 when env is unset", () => {
  const prev = process.env.QUANTCLAW_GATEWAY_URL;
  delete process.env.QUANTCLAW_GATEWAY_URL;

  const adapter = new TestAdapter();
  assert.equal((adapter as unknown as { gatewayUrl: string }).gatewayUrl, "ws://127.0.0.1:18800");

  if (prev === undefined) {
    delete process.env.QUANTCLAW_GATEWAY_URL;
  } else {
    process.env.QUANTCLAW_GATEWAY_URL = prev;
  }
});
