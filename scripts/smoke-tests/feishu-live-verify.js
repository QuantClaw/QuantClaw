#!/usr/bin/env node

const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const {
  extractText,
  findReply,
  findUserTrigger,
  getTenantToken,
  listMessages,
  messageTimeMs,
  parseSessionKey,
} = require("./feishu-e2e.js");

const DEFAULT_DOMAIN = "https://open.feishu.cn";
const DEFAULT_GROUP_ID = "oc_065bf2b9c0e3abb31eb30e949796367b";
const DEFAULT_TIMEOUT_MS = 90_000;
const DEFAULT_POLL_INTERVAL_MS = 2_000;

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function loadLocalFeishuConfig(env = process.env) {
  if (env.FEISHU_E2E_APP_ID && env.FEISHU_E2E_APP_SECRET) {
    return {
      appId: env.FEISHU_E2E_APP_ID,
      appSecret: env.FEISHU_E2E_APP_SECRET,
      domain: env.FEISHU_E2E_DOMAIN || DEFAULT_DOMAIN,
    };
  }

  const configPath =
    env.QUANTCLAW_CONFIG ||
    path.join(os.homedir(), ".quantclaw", "quantclaw.json");
  const cfg = readJson(configPath);
  const feishu = cfg.channels?.feishu;
  if (!feishu?.appId || !feishu?.appSecret) {
    throw new Error(
      "Missing Feishu credentials. Set FEISHU_E2E_APP_ID/FEISHU_E2E_APP_SECRET or configure channels.feishu in ~/.quantclaw/quantclaw.json.",
    );
  }

  return {
    appId: feishu.appId,
    appSecret: feishu.appSecret,
    domain: feishu.domain || DEFAULT_DOMAIN,
  };
}

function resolveConfig(env = process.env) {
  const feishu = loadLocalFeishuConfig(env);
  return {
    domain: feishu.domain,
    senders: [
      {
        name: "observer",
        appId: feishu.appId,
        appSecret: feishu.appSecret,
      },
    ],
    groupChatId: env.FEISHU_E2E_GROUP_CHAT_ID || DEFAULT_GROUP_ID,
    nonce: env.FEISHU_VERIFY_NONCE || "",
    triggerText: env.FEISHU_VERIFY_TRIGGER_TEXT || "/status",
    expectedSessionPrefix:
      env.FEISHU_VERIFY_EXPECTED_SESSION_PREFIX || "agent:main:",
    sessionsDir:
      env.QUANTCLAW_SESSIONS_DIR ||
      path.join(os.homedir(), ".quantclaw", "agents", "main", "sessions"),
    timeoutMs: Number(env.FEISHU_E2E_TIMEOUT_MS || DEFAULT_TIMEOUT_MS),
    pollIntervalMs: Number(
      env.FEISHU_E2E_POLL_INTERVAL_MS || DEFAULT_POLL_INTERVAL_MS,
    ),
    historyLookbackSeconds: Number(env.FEISHU_VERIFY_LOOKBACK_SECONDS || 7200),
  };
}

async function listRecentMessages(config, tenantToken) {
  return listMessages(
    {
      ...config,
      timeoutMs: config.timeoutMs,
      pollIntervalMs: config.pollIntervalMs,
      historyLookbackSeconds: config.historyLookbackSeconds,
    },
    tenantToken,
    { type: "group", id: config.groupChatId },
  );
}

function sortMessagesAscending(messages) {
  return [...messages].sort((a, b) => messageTimeMs(a) - messageTimeMs(b));
}

function findObservedPair(messages, config) {
  const sorted = sortMessagesAscending(messages);
  const trigger = findUserTrigger(sorted, {
    nonce: config.nonce,
    triggerText: config.triggerText,
  });
  if (!trigger) return null;

  const triggerTime = messageTimeMs(trigger.message);
  const reply = findReply(sorted, {
    nonce: config.nonce,
    outboundText: trigger.text,
    sinceMs: triggerTime,
  });
  if (!reply) return { trigger, reply: null };
  return { trigger, reply };
}

function sessionFileForKey(sessionsDir, sessionKey) {
  const indexPath = path.join(sessionsDir, "sessions.json");
  if (!fs.existsSync(indexPath)) return null;
  const index = readJson(indexPath);
  const sessionId = index[sessionKey]?.sessionId;
  if (!sessionId) return null;
  return path.join(sessionsDir, `${sessionId}.jsonl`);
}

function assertNoLlmHistoryPollution({ sessionsDir, sessionKey, triggerText }) {
  const file = sessionFileForKey(sessionsDir, sessionKey);
  if (!file || !fs.existsSync(file)) {
    return {
      checked: false,
      reason: "session file not found in local sessions index",
    };
  }

  const raw = fs.readFileSync(file, "utf8");
  if (triggerText && raw.includes(triggerText)) {
    throw new Error(
      `Command trigger leaked into LLM history: ${path.basename(file)}`,
    );
  }
  return { checked: true, file };
}

async function waitForObservedPair(config, tenantToken) {
  const deadline = Date.now() + config.timeoutMs;
  let lastCount = 0;
  while (Date.now() < deadline) {
    const messages = await listRecentMessages(config, tenantToken);
    lastCount = messages.length;
    const pair = findObservedPair(messages, config);
    if (pair?.trigger && pair.reply) return pair;
    await new Promise((resolve) =>
      setTimeout(resolve, config.pollIntervalMs),
    );
  }
  throw new Error(
    `Timed out waiting for Feishu trigger/reply pair; last message count=${lastCount}`,
  );
}

async function run(config = resolveConfig()) {
  const tenantToken = await getTenantToken(config, config.senders[0]);
  const pair = await waitForObservedPair(config, tenantToken);
  const sessionKey = parseSessionKey(pair.reply.text);
  if (!sessionKey) {
    throw new Error(`Reply did not contain a QuantClaw session key: ${pair.reply.text}`);
  }
  if (!sessionKey.startsWith(config.expectedSessionPrefix)) {
    throw new Error(
      `Unexpected session key prefix: ${sessionKey}; expected ${config.expectedSessionPrefix}`,
    );
  }
  if (!sessionKey.includes(config.groupChatId)) {
    throw new Error(
      `Session key does not include group chat id ${config.groupChatId}: ${sessionKey}`,
    );
  }

  const pollution = assertNoLlmHistoryPollution({
    sessionsDir: config.sessionsDir,
    sessionKey,
    triggerText: config.nonce || config.triggerText,
  });

  const result = {
    ok: true,
    mode: "observe",
    groupChatId: config.groupChatId,
    triggerText: pair.trigger.text,
    replyText: pair.reply.text,
    sessionKey,
    llmHistoryPollution: pollution.checked ? "not_found" : "not_checked",
    llmHistoryFile: pollution.checked ? pollution.file : "",
    note: pollution.checked
      ? "Command trigger was not written into local LLM history."
      : pollution.reason,
  };
  console.log(JSON.stringify(result, null, 2));
  return result;
}

module.exports = {
  assertNoLlmHistoryPollution,
  findObservedPair,
  loadLocalFeishuConfig,
  resolveConfig,
  run,
  sessionFileForKey,
};

if (require.main === module) {
  run().catch((error) => {
    console.error(error.message || String(error));
    process.exit(1);
  });
}
