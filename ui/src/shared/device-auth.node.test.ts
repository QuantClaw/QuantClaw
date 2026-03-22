import { describe, expect, it } from "vitest";
import { normalizeDeviceAuthScopes } from "./device-auth.ts";

describe("normalizeDeviceAuthScopes", () => {
  it("works even when Array.prototype.toSorted is unavailable", () => {
    const descriptor = Object.getOwnPropertyDescriptor(Array.prototype, "toSorted");

    try {
      delete (Array.prototype as Array<unknown> & { toSorted?: unknown }).toSorted;

      expect(normalizeDeviceAuthScopes([" beta ", "alpha", "beta"])).toEqual(["alpha", "beta"]);
    } finally {
      if (descriptor) {
        Object.defineProperty(Array.prototype, "toSorted", descriptor);
      }
    }
  });
});
