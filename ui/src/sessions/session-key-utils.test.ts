// Regression tests for issue #53: ui build failed because session-key-utils
// was missing, causing "Could not resolve" errors from app-chat.ts and
// app-render.ts which both import parseAgentSessionKey.

import { describe, expect, it } from "vitest";
import {
  parseAgentSessionKey,
  type ParsedAgentSessionKey,
} from "./session-key-utils.ts";

describe("parseAgentSessionKey", () => {
  it("parses a standard agent session key", () => {
    const result = parseAgentSessionKey("agent:main:chat-1");
    expect(result).toEqual<ParsedAgentSessionKey>({
      agentId: "main",
      sessionName: "chat-1",
    });
  });

  it("parses the default gateway session key", () => {
    // GatewayServer snapshot sends "agent:main:main" as mainSessionKey
    const result = parseAgentSessionKey("agent:main:main");
    expect(result).toEqual<ParsedAgentSessionKey>({
      agentId: "main",
      sessionName: "main",
    });
  });

  it("preserves extra colons in sessionName", () => {
    const result = parseAgentSessionKey("agent:foo:bar:baz");
    expect(result).toEqual<ParsedAgentSessionKey>({
      agentId: "foo",
      sessionName: "bar:baz",
    });
  });

  it("returns null for a bare session name with no prefix", () => {
    // Legacy or alias keys like "main" should not parse as agent keys
    expect(parseAgentSessionKey("main")).toBeNull();
  });

  it("returns null for keys with wrong prefix", () => {
    expect(parseAgentSessionKey("session:main:chat")).toBeNull();
  });

  it("returns null for keys with only two segments", () => {
    expect(parseAgentSessionKey("agent:main")).toBeNull();
  });

  it("returns null for an empty string", () => {
    expect(parseAgentSessionKey("")).toBeNull();
  });

  it("extracts agentId correctly for non-main agents", () => {
    const result = parseAgentSessionKey("agent:coder:task-42");
    expect(result?.agentId).toBe("coder");
    expect(result?.sessionName).toBe("task-42");
  });

  it("returns null for empty agentId segment (agent::session)", () => {
    expect(parseAgentSessionKey("agent::session")).toBeNull();
  });

  it("returns null for empty sessionName segment (agent:main:)", () => {
    expect(parseAgentSessionKey("agent:main:")).toBeNull();
  });
});
