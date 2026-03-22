import { describe, expect, it } from "vitest";
import { CORE_TOOL_GROUPS } from "./tool-catalog.ts";

describe("CORE_TOOL_GROUPS", () => {
  it("freezes the group map and each group entry list", () => {
    expect(Object.isFrozen(CORE_TOOL_GROUPS)).toBe(true);
    expect(Object.values(CORE_TOOL_GROUPS).every((group) => Object.isFrozen(group))).toBe(true);
  });
});
