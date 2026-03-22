import { describe, expect, it } from "vitest";
import { readConnectErrorDetailCode } from "./connect-error-details.ts";

describe("readConnectErrorDetailCode", () => {
  it("returns the trimmed error code", () => {
    expect(readConnectErrorDetailCode({ code: "  AUTH_REQUIRED  " })).toBe("AUTH_REQUIRED");
  });
});
