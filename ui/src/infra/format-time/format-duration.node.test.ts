import { describe, expect, it } from "vitest";
import { formatDurationPrecise } from "./format-duration.ts";

describe("formatDurationPrecise", () => {
  it("rounds sub-second durations to whole milliseconds", () => {
    expect(formatDurationPrecise(500.123)).toBe("500ms");
  });
});
