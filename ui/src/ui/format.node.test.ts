import { describe, expect, it } from "vitest";
import { stripThinkingTags } from "./format.ts";

describe("format helpers", () => {
  it("loads the shared reasoning tag helper from the TypeScript source module", () => {
    expect(stripThinkingTags("<think>secret</think>\nHello")).toBe("Hello");
  });
});
