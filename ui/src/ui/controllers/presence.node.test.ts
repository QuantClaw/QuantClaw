import { describe, expect, it, vi } from "vitest";
import { loadPresence, type PresenceState } from "./presence.ts";

function createState(overrides: Partial<PresenceState> = {}): PresenceState {
  return {
    client: null,
    connected: true,
    presenceLoading: false,
    presenceEntries: [],
    presenceError: "old-error",
    presenceStatus: "old-status",
    ...overrides,
  };
}

describe("loadPresence", () => {
  it("returns early when disconnected or without a client", async () => {
    const request = vi.fn();
    const disconnected = createState({
      client: { request } as unknown as PresenceState["client"],
      connected: false,
    });
    const noClient = createState({ connected: true, client: null });

    await loadPresence(disconnected);
    await loadPresence(noClient);

    expect(request).not.toHaveBeenCalled();
  });

  it("returns early while a request is already in-flight", async () => {
    const request = vi.fn();
    const state = createState({
      client: { request } as unknown as PresenceState["client"],
      presenceLoading: true,
    });

    await loadPresence(state);

    expect(request).not.toHaveBeenCalled();
  });

  it("stores returned presence entries", async () => {
    const payload = [{ instanceId: "edge-1" }, { instanceId: "edge-2" }];
    const request = vi.fn().mockResolvedValue(payload);
    const state = createState({
      client: { request } as unknown as PresenceState["client"],
    });

    await loadPresence(state);

    expect(request).toHaveBeenCalledWith("system-presence", {});
    expect(state.presenceEntries).toEqual(payload);
    expect(state.presenceStatus).toBeNull();
    expect(state.presenceError).toBeNull();
    expect(state.presenceLoading).toBe(false);
  });

  it("sets a friendly status when no instances are present", async () => {
    const request = vi.fn().mockResolvedValue([]);
    const state = createState({
      client: { request } as unknown as PresenceState["client"],
      presenceEntries: [{ instanceId: "old" }],
    });

    await loadPresence(state);

    expect(state.presenceEntries).toEqual([]);
    expect(state.presenceStatus).toBe("No instances yet.");
    expect(state.presenceError).toBeNull();
  });

  it("handles malformed payloads", async () => {
    const request = vi.fn().mockResolvedValue({ unexpected: true });
    const state = createState({
      client: { request } as unknown as PresenceState["client"],
      presenceEntries: [{ instanceId: "old" }],
    });

    await loadPresence(state);

    expect(state.presenceEntries).toEqual([]);
    expect(state.presenceStatus).toBe("No presence payload.");
    expect(state.presenceError).toBeNull();
  });

  it("captures request failures", async () => {
    const request = vi.fn().mockRejectedValue(new Error("presence offline"));
    const state = createState({
      client: { request } as unknown as PresenceState["client"],
      presenceStatus: "stale",
    });

    await loadPresence(state);

    expect(state.presenceError).toContain("presence offline");
    expect(state.presenceStatus).toBeNull();
    expect(state.presenceLoading).toBe(false);
  });
});
