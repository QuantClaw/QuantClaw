/**
 * Gateway event constants and types
 * Mirrors the C++ protocol definitions
 */

// Event names
export const GATEWAY_EVENT_TEXT_DELTA = "agent.text_delta";
export const GATEWAY_EVENT_TOOL_USE = "agent.tool_use";
export const GATEWAY_EVENT_TOOL_RESULT = "agent.tool_result";
export const GATEWAY_EVENT_MESSAGE_END = "agent.message_end";
export const GATEWAY_EVENT_TICK = "gateway.tick";
export const GATEWAY_EVENT_QUEUE_STARTED = "queue.started";
export const GATEWAY_EVENT_QUEUE_COMPLETED = "queue.completed";
export const GATEWAY_EVENT_QUEUE_DROPPED = "queue.dropped";
export const GATEWAY_EVENT_UPDATE_AVAILABLE = "gateway.update_available";

// OpenClaw-compatible event names
export const GATEWAY_EVENT_AGENT = "agent";
export const GATEWAY_EVENT_CHAT = "chat";

// Event payload types
export interface GatewayUpdateAvailableEventPayload {
  updateAvailable?: {
    currentVersion: string;
    latestVersion: string;
    channel: string;
  } | null;
}
