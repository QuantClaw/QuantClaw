import { describe, expect, it } from "vitest";
import { GATEWAY_EVENT_UPDATE_AVAILABLE as gatewayEvent } from "./events.ts";
import { GATEWAY_EVENT_UPDATE_AVAILABLE as uiEvent } from "../ui/gateway-events.ts";

describe("gateway events", () => {
  it("keeps the update event name aligned with the UI gateway consumer", () => {
    expect(gatewayEvent).toBe(uiEvent);
  });
});
