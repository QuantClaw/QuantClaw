const assert = require("node:assert/strict");
const { test } = require("node:test");

const {
  assertTranscriptContains,
  assertTranscriptNotContains,
  buildSessionKey,
  runSessionIsolationScenario,
} = require("./qa-channel.js");

test("buildSessionKey scopes by channel and sender", () => {
  assert.equal(
    buildSessionKey({ channelId: "room-1", senderId: "user-1" }),
    "agent:main:room-1:user-1",
  );
});

test("transcript assertions catch presence and pollution", () => {
  const transcript = [
    { role: "user", content: [{ type: "text", text: "alpha" }] },
    { role: "assistant", content: [{ type: "text", text: "reply" }] },
  ];

  assertTranscriptContains(transcript, "alpha");
  assertTranscriptNotContains(transcript, "beta");
  assert.throws(
    () => assertTranscriptNotContains(transcript, "alpha"),
    /unexpectedly contains/i,
  );
});

test("runSessionIsolationScenario uses qa transport contract", async () => {
  const calls = [];
  const transcripts = new Map();
  const qa = {
    async injectInboundMessage({ channelId, senderId, text }) {
      const sessionKey = buildSessionKey({ channelId, senderId });
      calls.push({ type: "inbound", channelId, senderId, text, sessionKey });
      transcripts.set(sessionKey, [
        { role: "user", content: [{ type: "text", text }] },
        { role: "assistant", content: [{ type: "text", text: "mock reply" }] },
      ]);
      return { sessionKey, response: "mock reply" };
    },
    async readTransportTranscript(sessionKey) {
      return transcripts.get(sessionKey) || [];
    },
    async resetTransport(sessionKeys) {
      calls.push({ type: "reset", sessionKeys });
    },
  };

  const result = await runSessionIsolationScenario(qa);

  assert.equal(result.ok, true);
  assert.equal(result.scenario, "session-isolation");
  assert.notEqual(result.sessionKeyA, result.sessionKeyB);
  assert.equal(calls.filter((call) => call.type === "inbound").length, 2);
  assert.equal(calls.at(-1).type, "reset");
});
