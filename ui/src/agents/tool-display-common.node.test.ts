import { describe, expect, it } from "vitest";
import { resolveExecDetail } from "./tool-display-common.ts";

describe("resolveExecDetail", () => {
  it("preserves literal backslashes in command summaries", () => {
    const detail = resolveExecDetail({ command: "python C:\\repo\\tool.py" });
    const summary = detail?.split("\n\n")[0];

    expect(summary).toBe("run python C:\\repo\\tool.py");
  });

  it("splits newline-delimited command blocks into separate stages", () => {
    const detail = resolveExecDetail({ command: "npm test\nnpm run lint" });
    const summary = detail?.split("\n\n")[0];

    expect(summary).toContain("run tests");
    expect(summary).toContain("run lint");
  });

  it("splits CRLF-delimited command blocks into separate stages", () => {
    const detail = resolveExecDetail({ command: "npm test\r\nnpm run lint" });
    const summary = detail?.split("\n\n")[0];

    expect(summary).toContain("run tests");
    expect(summary).toContain("run lint");
  });
});
