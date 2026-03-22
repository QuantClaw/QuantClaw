import { describe, expect, it } from "vitest";
import { buildDeviceAuthPayload } from "./device-auth.ts";

describe("buildDeviceAuthPayload", () => {
  it("rejects nonces that contain the payload delimiter", () => {
    expect(() =>
      buildDeviceAuthPayload({
        deviceId: "device-1",
        clientId: "client-1",
        clientMode: "webchat",
        role: "operator",
        scopes: ["operator.admin"],
        signedAtMs: 123,
        token: "shared-token",
        nonce: "bad|nonce",
      }),
    ).toThrow(/nonce/i);
  });
});
