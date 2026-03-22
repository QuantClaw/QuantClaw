import { describe, expect, it } from "vitest";
import { stripInboundMetadata, stripLeadingInboundMetadata } from "./strip-inbound-meta.ts";

describe("strip inbound metadata", () => {
  const malformedLeadingBlock = [
    "Conversation info (untrusted metadata):",
    "```json",
    '{"conversationId":"abc"}',
    "",
    "Hello",
  ].join("\n");

  it("does not discard trailing content when a fenced metadata block is unclosed", () => {
    expect(stripInboundMetadata(malformedLeadingBlock)).toBe(malformedLeadingBlock);
  });

  it("leaves malformed leading metadata untouched when the closing fence is missing", () => {
    expect(stripLeadingInboundMetadata(malformedLeadingBlock)).toBe(malformedLeadingBlock);
  });
});
