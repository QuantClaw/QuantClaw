import { describe, expect, it, vi } from "vitest";
import { loadNodes, type NodesState } from "./nodes.ts";

function createState(overrides: Partial<NodesState> = {}): NodesState {
  return {
    client: null,
    connected: true,
    nodesLoading: false,
    nodes: [],
    lastError: "previous-error",
    ...overrides,
  };
}

describe("loadNodes", () => {
  it("returns early when disconnected or client is missing", async () => {
    const request = vi.fn();
    const disconnected = createState({
      client: { request } as unknown as NodesState["client"],
      connected: false,
    });
    const noClient = createState({ connected: true, client: null });

    await loadNodes(disconnected);
    await loadNodes(noClient);

    expect(request).not.toHaveBeenCalled();
    expect(disconnected.nodesLoading).toBe(false);
    expect(noClient.nodesLoading).toBe(false);
  });

  it("returns early when a nodes request is already in-flight", async () => {
    const request = vi.fn();
    const state = createState({
      client: { request } as unknown as NodesState["client"],
      nodesLoading: true,
    });

    await loadNodes(state);

    expect(request).not.toHaveBeenCalled();
  });

  it("loads nodes and clears stale error by default", async () => {
    const request = vi.fn().mockResolvedValue({ nodes: [{ id: "node-1" }, { id: "node-2" }] });
    const state = createState({
      client: { request } as unknown as NodesState["client"],
      lastError: "stale",
    });

    await loadNodes(state);

    expect(request).toHaveBeenCalledWith("node.list", {});
    expect(state.nodes).toEqual([{ id: "node-1" }, { id: "node-2" }]);
    expect(state.lastError).toBeNull();
    expect(state.nodesLoading).toBe(false);
  });

  it("normalizes non-array node payloads to an empty list", async () => {
    const request = vi.fn().mockResolvedValue({ nodes: "invalid" });
    const state = createState({
      client: { request } as unknown as NodesState["client"],
      nodes: [{ id: "old" }],
    });

    await loadNodes(state);

    expect(state.nodes).toEqual([]);
    expect(state.nodesLoading).toBe(false);
  });

  it("captures request errors when not in quiet mode", async () => {
    const request = vi.fn().mockRejectedValue(new Error("node list unavailable"));
    const state = createState({
      client: { request } as unknown as NodesState["client"],
    });

    await loadNodes(state);

    expect(state.lastError).toContain("node list unavailable");
    expect(state.nodesLoading).toBe(false);
  });

  it("does not overwrite existing error in quiet mode", async () => {
    const request = vi.fn().mockRejectedValue("quiet-failure");
    const state = createState({
      client: { request } as unknown as NodesState["client"],
      lastError: "keep-me",
    });

    await loadNodes(state, { quiet: true });

    expect(state.lastError).toBe("keep-me");
    expect(state.nodesLoading).toBe(false);
  });
});
