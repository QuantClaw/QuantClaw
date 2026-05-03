import test from "node:test";
import assert from "node:assert/strict";

import { stripFeishuMentions } from "./feishu_utils.js";

test("stripFeishuMentions removes Feishu at tags before slash commands", () => {
  assert.equal(
    stripFeishuMentions('<at user_id="ou_bot">Quantbot</at> /status smoke'),
    "/status smoke",
  );
});

test("stripFeishuMentions removes rendered mention prefixes before slash commands", () => {
  assert.equal(stripFeishuMentions("@_user_1 /status smoke"), "/status smoke");
});

test("stripFeishuMentions leaves ordinary text unchanged", () => {
  assert.equal(stripFeishuMentions("hello Quantbot"), "hello Quantbot");
});
