#!/usr/bin/env node

const DEFAULT_BASE_URL = "http://127.0.0.1:18801";
const DEFAULT_TIMEOUT_MS = 60_000;
const DEFAULT_POLL_INTERVAL_MS = 500;

function requireGatewayToken(env = process.env) {
  const token = env.QUANTCLAW_AUTH_TOKEN;
  if (!token) {
    throw new Error("Missing QUANTCLAW_AUTH_TOKEN");
  }
  return token;
}

function resolveConfig(env = process.env) {
  return {
    baseUrl: env.QUANTCLAW_BASE_URL || DEFAULT_BASE_URL,
    gatewayToken: requireGatewayToken(env),
    timeoutMs: Number(env.QA_CHANNEL_TIMEOUT_MS || DEFAULT_TIMEOUT_MS),
    pollIntervalMs: Number(
      env.QA_CHANNEL_POLL_INTERVAL_MS || DEFAULT_POLL_INTERVAL_MS,
    ),
  };
}

function buildSessionKey({ channelId, senderId, agentId = "main" }) {
  return `agent:${agentId}:${channelId}:${senderId}`;
}

function makeQaChannel(config = resolveConfig()) {
  async function requestJson(path, { method = "GET", body } = {}) {
    const res = await fetch(`${config.baseUrl}${path}`, {
      method,
      headers: {
        Authorization: `Bearer ${config.gatewayToken}`,
        "Content-Type": "application/json",
      },
      body: body === undefined ? undefined : JSON.stringify(body),
    });

    const text = await res.text();
    let parsed;
    try {
      parsed = text ? JSON.parse(text) : {};
    } catch {
      parsed = { raw: text };
    }
    if (!res.ok) {
      throw new Error(`${method} ${path} failed (${res.status}): ${text}`);
    }
    return parsed;
  }

  async function injectInboundMessage({
    channelId,
    senderId,
    text,
    channel = "qa",
  }) {
    return requestJson("/api/channel/message", {
      method: "POST",
      body: {
        channel,
        channelId,
        senderId,
        message: text,
      },
    });
  }

  async function readTransportTranscript(sessionKey, limit = 50) {
    return requestJson(
      `/api/sessions/history?sessionKey=${encodeURIComponent(
        sessionKey,
      )}&limit=${limit}`,
    );
  }

  async function resetTransport(sessionKeys) {
    const keys = Array.isArray(sessionKeys) ? sessionKeys : [sessionKeys];
    const results = await Promise.allSettled(
      keys
        .filter(Boolean)
        .map((sessionKey) =>
          requestJson("/api/sessions/delete", {
            method: "POST",
            body: { sessionKey },
          }),
        ),
    );
    return results;
  }

  async function waitForTransportOutboundMessage({
    sessionKey,
    predicate = () => true,
    limit = 50,
    timeoutMs = config.timeoutMs,
  }) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const transcript = await readTransportTranscript(sessionKey, limit);
      const match = transcript.find(
        (message) => message.role === "assistant" && predicate(message),
      );
      if (match) return match;
      await new Promise((resolve) =>
        setTimeout(resolve, config.pollIntervalMs),
      );
    }
    throw new Error(`Timed out waiting for assistant message in ${sessionKey}`);
  }

  return {
    buildSessionKey,
    injectInboundMessage,
    readTransportTranscript,
    requestJson,
    resetTransport,
    waitForTransportOutboundMessage,
  };
}

function stringifyTranscript(transcript) {
  return JSON.stringify(transcript);
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function assertTranscriptContains(transcript, expected) {
  assert(
    stringifyTranscript(transcript).includes(expected),
    `Transcript missing expected text: ${expected}`,
  );
}

function assertTranscriptNotContains(transcript, forbidden) {
  assert(
    !stringifyTranscript(transcript).includes(forbidden),
    `Transcript unexpectedly contains text: ${forbidden}`,
  );
}

async function runSessionIsolationScenario(qa = makeQaChannel()) {
  const channelId = `qa-room-${Date.now()}`;
  const senderA = "qa-user-a";
  const senderB = "qa-user-b";
  const textA = `qa-alpha-${Date.now()}`;
  const textB = `qa-beta-${Date.now()}`;
  const expectedA = buildSessionKey({ channelId, senderId: senderA });
  const expectedB = buildSessionKey({ channelId, senderId: senderB });
  const sessionKeys = [expectedA, expectedB];

  try {
    const first = await qa.injectInboundMessage({
      channelId,
      senderId: senderA,
      text: textA,
    });
    const second = await qa.injectInboundMessage({
      channelId,
      senderId: senderB,
      text: textB,
    });

    assert(first.sessionKey === expectedA, `Unexpected A key: ${first.sessionKey}`);
    assert(second.sessionKey === expectedB, `Unexpected B key: ${second.sessionKey}`);
    assert(first.sessionKey !== second.sessionKey, "Session keys must differ");

    const historyA = await qa.readTransportTranscript(expectedA);
    const historyB = await qa.readTransportTranscript(expectedB);

    assertTranscriptContains(historyA, textA);
    assertTranscriptNotContains(historyA, textB);
    assertTranscriptContains(historyB, textB);
    assertTranscriptNotContains(historyB, textA);

    return {
      ok: true,
      scenario: "session-isolation",
      channelId,
      senderA,
      senderB,
      sessionKeyA: first.sessionKey,
      sessionKeyB: second.sessionKey,
    };
  } finally {
    await qa.resetTransport(sessionKeys);
  }
}

async function run() {
  const result = await runSessionIsolationScenario();
  console.log(JSON.stringify(result, null, 2));
  return result;
}

module.exports = {
  assertTranscriptContains,
  assertTranscriptNotContains,
  buildSessionKey,
  makeQaChannel,
  resolveConfig,
  run,
  runSessionIsolationScenario,
};

if (require.main === module) {
  run().catch((error) => {
    console.error(error.message || String(error));
    process.exit(1);
  });
}
