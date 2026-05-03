#!/usr/bin/env node

const gatewayToken = process.env.QUANTCLAW_AUTH_TOKEN;
const baseUrl = process.env.QUANTCLAW_BASE_URL ?? "http://127.0.0.1:18801";

if (!gatewayToken) {
  console.error("Missing QUANTCLAW_AUTH_TOKEN");
  process.exit(2);
}

async function postJson(path, body) {
  const res = await fetch(`${baseUrl}${path}`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${gatewayToken}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify(body),
  });

  const text = await res.text();
  if (!res.ok) {
    throw new Error(`${path} ${res.status}: ${text}`);
  }
  return JSON.parse(text);
}

async function getJson(path) {
  const res = await fetch(`${baseUrl}${path}`, {
    headers: {
      Authorization: `Bearer ${gatewayToken}`,
    },
  });

  const text = await res.text();
  if (!res.ok) {
    throw new Error(`${path} ${res.status}: ${text}`);
  }
  return JSON.parse(text);
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

async function main() {
  const channel = "feishu";
  const channelId = `smoke-group-${Date.now()}`;
  const senderA = "smoke-user-A";
  const senderB = "smoke-user-B";
  const sessionKeys = [];

  try {
    const first = await postJson("/api/channel/message", {
      channel,
      channelId,
      senderId: senderA,
      message: "alpha",
    });
    const second = await postJson("/api/channel/message", {
      channel,
      channelId,
      senderId: senderB,
      message: "beta",
    });
    sessionKeys.push(first.sessionKey, second.sessionKey);

    assert(
      first.sessionKey !== second.sessionKey,
      `Expected distinct session keys, got ${first.sessionKey}`,
    );
    assert(
      first.sessionKey === `agent:main:${channelId}:${senderA}`,
      `Unexpected session key for sender A: ${first.sessionKey}`,
    );
    assert(
      second.sessionKey === `agent:main:${channelId}:${senderB}`,
      `Unexpected session key for sender B: ${second.sessionKey}`,
    );

    const historyA = await getJson(
      `/api/sessions/history?sessionKey=${encodeURIComponent(first.sessionKey)}&limit=10`,
    );
    const historyB = await getJson(
      `/api/sessions/history?sessionKey=${encodeURIComponent(second.sessionKey)}&limit=10`,
    );

    const userTextA = JSON.stringify(historyA);
    const userTextB = JSON.stringify(historyB);

    assert(userTextA.includes("alpha"), "Sender A history missing alpha");
    assert(!userTextA.includes("beta"), "Sender A history polluted with beta");
    assert(userTextB.includes("beta"), "Sender B history missing beta");
    assert(!userTextB.includes("alpha"), "Sender B history polluted with alpha");

    console.log(
      JSON.stringify(
        {
          ok: true,
          baseUrl,
          sessionKeyA: first.sessionKey,
          sessionKeyB: second.sessionKey,
        },
        null,
        2,
      ),
    );
  } finally {
    await Promise.allSettled(
      sessionKeys.map((sessionKey) =>
        postJson("/api/sessions/delete", { sessionKey }),
      ),
    );
  }
}

main().catch((error) => {
  console.error(error.message || String(error));
  process.exit(1);
});
