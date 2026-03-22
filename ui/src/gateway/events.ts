import type { UpdateAvailable } from "../infra/update-startup.js";

export const GATEWAY_EVENT_UPDATE_AVAILABLE = "gateway.update_available" as const;

export type GatewayUpdateAvailableEventPayload = {
  updateAvailable: UpdateAvailable | null;
};
