const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { test } = require("node:test");

const {
  findObservedPair,
  sessionFileForKey,
} = require("./feishu-live-verify.js");

test("findObservedPair matches a real user trigger to following status reply", () => {
  const messages = [
    {
      sender: { sender_type: "user" },
      create_time: "1777773827066",
      body: { content: '{"text":"@_user_1  /status manual-1"}' },
    },
    {
      sender: { sender_type: "app" },
      create_time: "1777773828554",
      body: {
        content:
          '{"text":"Session: agent:main:oc_group:ou_user\\nMessages: 10"}',
      },
    },
  ];

  const pair = findObservedPair(messages, {
    nonce: "manual-1",
    triggerText: "/status",
  });

  assert.equal(pair.trigger.text, "@_user_1  /status manual-1");
  assert.equal(pair.reply.sessionKey, "agent:main:oc_group:ou_user");
});

test("findObservedPair returns null when the trigger is absent", () => {
  assert.equal(
    findObservedPair(
      [
        {
          sender: { sender_type: "app" },
          body: { content: '{"text":"Session: agent:main:oc_group:ou_user"}' },
        },
      ],
      { nonce: "missing", triggerText: "/status" },
    ),
    null,
  );
});

test("sessionFileForKey resolves a local session jsonl path", () => {
  const sessionsDir = fs.mkdtempSync(path.join(os.tmpdir(), "qc-sessions-"));
  fs.writeFileSync(
    path.join(sessionsDir, "sessions.json"),
    JSON.stringify({
      "agent:main:oc_group:ou_user": { sessionId: "abc123" },
    }),
  );

  assert.equal(
    sessionFileForKey(sessionsDir, "agent:main:oc_group:ou_user"),
    path.join(sessionsDir, "abc123.jsonl"),
  );
});
