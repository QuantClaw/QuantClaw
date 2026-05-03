#!/usr/bin/env node

const DEFAULT_DOMAIN = "https://open.feishu.cn";
const DEFAULT_TIMEOUT_MS = 90_000;
const DEFAULT_POLL_INTERVAL_MS = 2_000;

function envBool(value) {
  return ["1", "true", "yes", "on"].includes(String(value ?? "").toLowerCase());
}

function splitCsv(value) {
  return String(value ?? "")
    .split(",")
    .map((part) => part.trim())
    .filter(Boolean);
}

function normalizeText(value) {
  return String(value ?? "").replace(/\s+/g, " ").trim();
}

function resolveSenders(env) {
  const senders = [];
  if (env.FEISHU_E2E_APP_ID || env.FEISHU_E2E_APP_SECRET) {
    senders.push({
      name: "sender-a",
      appId: env.FEISHU_E2E_APP_ID,
      appSecret: env.FEISHU_E2E_APP_SECRET,
    });
  }
  if (env.FEISHU_E2E_APP_ID_B || env.FEISHU_E2E_APP_SECRET_B) {
    senders.push({
      name: "sender-b",
      appId: env.FEISHU_E2E_APP_ID_B,
      appSecret: env.FEISHU_E2E_APP_SECRET_B,
    });
  }

  for (const sender of senders) {
    if (!sender.appId || !sender.appSecret) {
      throw new Error(
        `Incomplete credentials for ${sender.name}: both app id and app secret are required`,
      );
    }
  }
  return senders;
}

function resolveConfig(env = process.env) {
  const senders = resolveSenders(env);
  if (senders.length === 0) {
    throw new Error(
      "Feishu E2E requires at least one sender. Set FEISHU_E2E_APP_ID and FEISHU_E2E_APP_SECRET.",
    );
  }

  const dmOpenIds = splitCsv(env.FEISHU_E2E_DM_OPEN_IDS);
  const groupChatId = env.FEISHU_E2E_GROUP_CHAT_ID || "";
  if (dmOpenIds.length === 0 && !groupChatId) {
    throw new Error(
      "Feishu E2E requires at least one target. Set FEISHU_E2E_DM_OPEN_IDS or FEISHU_E2E_GROUP_CHAT_ID.",
    );
  }

  return {
    domain: env.FEISHU_E2E_DOMAIN || DEFAULT_DOMAIN,
    senders,
    dmOpenIds,
    groupChatId,
    targetBotName: env.FEISHU_E2E_BOT_NAME || "Quantbot",
    mentionId: env.FEISHU_E2E_BOT_MENTION_ID || "",
    mentionIdType: env.FEISHU_E2E_BOT_MENTION_ID_TYPE || "open_id",
    timeoutMs: Number(env.FEISHU_E2E_TIMEOUT_MS || DEFAULT_TIMEOUT_MS),
    pollIntervalMs: Number(
      env.FEISHU_E2E_POLL_INTERVAL_MS || DEFAULT_POLL_INTERVAL_MS,
    ),
    strictIsolation: envBool(env.FEISHU_E2E_STRICT_ISOLATION),
  };
}

function redactConfig(config) {
  return {
    ...config,
    senders: config.senders.map((sender) => ({
      ...sender,
      appSecret: sender.appSecret ? "***" : "",
    })),
  };
}

function buildStatusText({ nonce, targetBotName, mentionId, mentionIdType }) {
  const command = `/status ${nonce}`;
  if (!mentionId) return command;

  // Feishu text messages use this at-tag shape for bot mentions.
  const attr = mentionIdType === "user_id" ? "user_id" : "user_id";
  return `${command} <at ${attr}="${mentionId}">${targetBotName}</at>`;
}

function extractText(message) {
  const content = message?.body?.content ?? message?.content ?? "";
  if (typeof content !== "string") return "";
  try {
    const parsed = JSON.parse(content);
    return String(parsed.text ?? parsed.content ?? content);
  } catch {
    return content;
  }
}

function parseSessionKey(text) {
  const match = String(text).match(/agent:[A-Za-z0-9:._-]+/);
  return match?.[0] ?? "";
}

function messageTimeMs(message) {
  const raw =
    message?.create_time ??
    message?.update_time ??
    message?.message?.create_time ??
    message?.message?.update_time;
  const n = Number(raw);
  if (!Number.isFinite(n) || n <= 0) return 0;
  return n > 10_000_000_000 ? n : n * 1000;
}

async function requestJson({ url, method = "GET", tenantToken, body }) {
  const headers = {
    "Content-Type": "application/json; charset=utf-8",
  };
  if (tenantToken) {
    headers.Authorization = `Bearer ${tenantToken}`;
  }

  const res = await fetch(url, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await res.text();
  let parsed;
  try {
    parsed = text ? JSON.parse(text) : {};
  } catch {
    parsed = { raw: text };
  }

  if (!res.ok || (parsed.code !== undefined && parsed.code !== 0)) {
    throw new Error(`${method} ${url} failed: ${text}`);
  }
  return parsed;
}

async function getTenantToken(config, sender) {
  const url = `${config.domain}/open-apis/auth/v3/tenant_access_token/internal`;
  const parsed = await requestJson({
    url,
    method: "POST",
    body: {
      app_id: sender.appId,
      app_secret: sender.appSecret,
    },
  });
  return parsed.tenant_access_token;
}

function receiveIdType(target) {
  if (target.type === "group") return "chat_id";
  return "open_id";
}

async function sendText(config, tenantToken, target, text) {
  const url = `${config.domain}/open-apis/im/v1/messages?receive_id_type=${receiveIdType(
    target,
  )}`;
  const parsed = await requestJson({
    url,
    method: "POST",
    tenantToken,
    body: {
      receive_id: target.id,
      msg_type: "text",
      content: JSON.stringify({ text }),
    },
  });
  return parsed.data;
}

async function listMessages(config, tenantToken, target) {
  if (target.type !== "group") {
    throw new Error(
      "Feishu message polling currently supports group chat history. For DM, verify manually or provide a user-token based receiver.",
    );
  }

  const endTime = Math.floor(Date.now() / 1000);
  const startTime = endTime - (config.historyLookbackSeconds || 600);
  const url =
    `${config.domain}/open-apis/im/v1/messages?` +
    new URLSearchParams({
      container_id_type: "chat",
      container_id: target.id,
      start_time: String(startTime),
      end_time: String(endTime),
      page_size: "50",
    });
  const parsed = await requestJson({ url, tenantToken });
  return parsed.data?.items ?? [];
}

function findReply(messages, { nonce, outboundText, sinceMs = 0 }) {
  for (const message of messages) {
    const text = extractText(message);
    const createdAt = messageTimeMs(message);
    if (sinceMs && createdAt && createdAt < sinceMs) continue;
    if (outboundText && text.trim() === outboundText.trim()) continue;
    if (nonce && text.includes(nonce) && !parseSessionKey(text)) continue;
    if (!parseSessionKey(text) && !text.includes("Session:")) continue;
    return { message, text, sessionKey: parseSessionKey(text) };
  }
  return null;
}

function findUserTrigger(messages, { nonce, triggerText, sinceMs = 0 }) {
  const normalizedTrigger = normalizeText(triggerText);
  for (const message of messages) {
    const text = extractText(message);
    const createdAt = messageTimeMs(message);
    if (sinceMs && createdAt && createdAt < sinceMs) continue;
    if (message?.sender?.sender_type && message.sender.sender_type !== "user") {
      continue;
    }
    if (nonce && !text.includes(nonce)) continue;
    if (normalizedTrigger && !normalizeText(text).includes(normalizedTrigger)) {
      continue;
    }
    return { message, text };
  }
  return null;
}

async function waitForReply(
  config,
  tenantToken,
  target,
  nonce,
  outboundText,
  sinceMs,
) {
  const deadline = Date.now() + config.timeoutMs;
  let lastCount = 0;
  while (Date.now() < deadline) {
    const messages = await listMessages(config, tenantToken, target);
    lastCount = messages.length;
    const reply = findReply(messages, { nonce, outboundText, sinceMs });
    if (reply) return reply;
    await new Promise((resolve) => setTimeout(resolve, config.pollIntervalMs));
  }
  throw new Error(
    `Timed out waiting for Quantbot reply containing nonce ${nonce}; last message count=${lastCount}`,
  );
}

function buildTargets(config) {
  const targets = [];
  for (const openId of config.dmOpenIds) {
    targets.push({ type: "dm", id: openId, label: `dm:${openId}` });
  }
  if (config.groupChatId) {
    targets.push({
      type: "group",
      id: config.groupChatId,
      label: `group:${config.groupChatId}`,
    });
  }
  return targets;
}

async function run(config = resolveConfig()) {
  console.log(JSON.stringify({ config: redactConfig(config) }, null, 2));

  const senders = [];
  for (const sender of config.senders) {
    const tenantToken = await getTenantToken(config, sender);
    senders.push({ ...sender, tenantToken });
  }

  const results = [];
  for (const target of buildTargets(config)) {
    if (target.type === "dm") {
      const nonce = `qc-feishu-e2e-dm-${Date.now()}`;
      await sendText(config, senders[0].tenantToken, target, `/status ${nonce}`);
      results.push({
        target: target.label,
        sender: senders[0].name,
        nonce,
        status: "sent",
        note: "DM sent. Feishu bot tokens cannot read p2p user-side history; verify the bot reply in Feishu, or use group polling for automated assertion.",
      });
      continue;
    }

    for (const sender of senders) {
      const nonce = `qc-feishu-e2e-group-${sender.name}-${Date.now()}`;
      const text = buildStatusText({
        nonce,
        targetBotName: config.targetBotName,
        mentionId: config.mentionId,
        mentionIdType: config.mentionIdType,
      });
      const sinceMs = Date.now();
      const sent = await sendText(config, sender.tenantToken, target, text);
      const reply = await waitForReply(
        config,
        sender.tenantToken,
        target,
        nonce,
        text,
        sinceMs,
      );
      results.push({
        target: target.label,
        sender: sender.name,
        nonce,
        status: "replied",
        sentMessageId: sent?.message_id || "",
        sessionKey: reply.sessionKey,
        replyText: reply.text,
      });
    }
  }

  if (config.strictIsolation && senders.length >= 2 && config.groupChatId) {
    const groupResults = results.filter(
      (result) =>
        result.target === `group:${config.groupChatId}` && result.sessionKey,
    );
    const uniqueKeys = new Set(groupResults.map((result) => result.sessionKey));
    if (uniqueKeys.size !== groupResults.length) {
      throw new Error(
        `Expected distinct group session keys, got ${JSON.stringify(
          groupResults.map((result) => result.sessionKey),
        )}`,
      );
    }
  }

  console.log(JSON.stringify({ ok: true, results }, null, 2));
  return results;
}

module.exports = {
  buildStatusText,
  extractText,
  findUserTrigger,
  findReply,
  getTenantToken,
  listMessages,
  messageTimeMs,
  normalizeText,
  parseSessionKey,
  redactConfig,
  resolveConfig,
  run,
};

if (require.main === module) {
  run().catch((error) => {
    console.error(error.message || String(error));
    process.exit(1);
  });
}
