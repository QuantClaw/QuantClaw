const assert = require("node:assert/strict");
const { test } = require("node:test");

const {
  buildStatusText,
  extractText,
  parseSessionKey,
  redactConfig,
  resolveConfig,
  findReply,
  findUserTrigger,
  messageTimeMs,
  normalizeText,
} = require("./feishu-e2e.js");

test("resolveConfig fails without sender credentials", () => {
  assert.throws(
    () => resolveConfig({ FEISHU_E2E_GROUP_CHAT_ID: "oc_group" }),
    /at least one sender/i,
  );
});

test("resolveConfig fails without dm or group target", () => {
  assert.throws(
    () =>
      resolveConfig({
        FEISHU_E2E_APP_ID: "cli_sender",
        FEISHU_E2E_APP_SECRET: "secret",
      }),
    /at least one target/i,
  );
});

test("buildStatusText keeps slash command first and appends mention", () => {
  const text = buildStatusText({
    nonce: "qc-e2e-123",
    targetBotName: "Quantbot",
    mentionId: "ou_bot",
    mentionIdType: "open_id",
  });

  assert.equal(
    text,
    '/status qc-e2e-123 <at user_id="ou_bot">Quantbot</at>',
  );
});

test("parseSessionKey extracts the QuantClaw status response key", () => {
  assert.equal(
    parseSessionKey("Session: agent:main:oc_group:ou_user_a"),
    "agent:main:oc_group:ou_user_a",
  );
});

test("extractText handles Feishu text message content JSON", () => {
  assert.equal(extractText({ body: { content: '{"text":"hello"}' } }), "hello");
  assert.equal(extractText({ body: { content: "plain" } }), "plain");
});

test("redactConfig removes secrets from diagnostic output", () => {
  const cfg = resolveConfig({
    FEISHU_E2E_APP_ID: "cli_sender",
    FEISHU_E2E_APP_SECRET: "secret-value",
    FEISHU_E2E_GROUP_CHAT_ID: "oc_group",
  });

  const redacted = redactConfig(cfg);
  assert.equal(redacted.senders[0].appSecret, "***");
  assert.equal(redacted.senders[0].appId, "cli_sender");
});

test("findReply ignores the outbound probe message", () => {
  const messages = [
    { body: { content: '{"text":"/status nonce-1"}' } },
    { body: { content: '{"text":"Session: agent:main:oc_group:ou_user"}' } },
  ];

  const reply = findReply(messages, {
    nonce: "nonce-1",
    outboundText: "/status nonce-1",
  });

  assert.equal(reply.sessionKey, "agent:main:oc_group:ou_user");
});

test("messageTimeMs normalizes Feishu second and millisecond timestamps", () => {
  assert.equal(messageTimeMs({ create_time: "1777772208" }), 1777772208000);
  assert.equal(messageTimeMs({ create_time: "1777772208123" }), 1777772208123);
});

test("normalizeText collapses whitespace", () => {
  assert.equal(normalizeText("@_user_1  /status   smoke"), "@_user_1 /status smoke");
});

test("findUserTrigger finds user messages and skips app messages", () => {
  const messages = [
    {
      sender: { sender_type: "app" },
      body: { content: '{"text":"Session: agent:main:oc_group:ou_user"}' },
    },
    {
      sender: { sender_type: "user" },
      body: { content: '{"text":"@_user_1  /status smoke-1"}' },
      create_time: "1777772208123",
    },
  ];

  const trigger = findUserTrigger(messages, {
    nonce: "smoke-1",
    triggerText: "/status",
  });

  assert.equal(trigger.text, "@_user_1  /status smoke-1");
});
