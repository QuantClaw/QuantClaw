export type UpdateAvailable = {
  version: string;
  downloadUrl?: string;
};

export const GATEWAY_EVENT_UPDATE_AVAILABLE = "update.available" as const;

export type GatewayUpdateAvailableEventPayload = {
  updateAvailable: UpdateAvailable | null;
};
