import { beforeEach, describe, expect, it, vi } from "vitest";
import { loadLogs, parseLogLine, type LogsState } from "./logs.ts";

function createState(overrides: Partial<LogsState> = {}): LogsState {
  return {
    client: null,
    connected: true,
    logsLoading: false,
    logsError: null,
    logsCursor: null,
    logsFile: null,
    logsEntries: [],
    logsTruncated: false,
    logsLastFetchAt: null,
    logsLimit: 100,
    logsMaxBytes: 65536,
    ...overrides,
  };
}

describe("parseLogLine", () => {
  it("falls back to raw text for blank and malformed lines", () => {
    expect(parseLogLine("   ")).toEqual({ raw: "   ", message: "   " });
    expect(parseLogLine("not-json")).toEqual({ raw: "not-json", message: "not-json" });
  });

  it("extracts structured fields from json logs", () => {
    const line = JSON.stringify({
      _meta: {
        date: "2026-04-07T08:00:00.000Z",
        logLevelName: "WARN",
        name: '{"subsystem":"gateway"}',
      },
      "1": "Gateway connected",
    });

    const parsed = parseLogLine(line);

    expect(parsed.time).toBe("2026-04-07T08:00:00.000Z");
    expect(parsed.level).toBe("warn");
    expect(parsed.subsystem).toBe("gateway");
    expect(parsed.message).toBe("Gateway connected");
    expect(parsed.meta).toMatchObject({ logLevelName: "WARN" });
  });

  it("uses context fallback and normalizes level from meta.level", () => {
    const line = JSON.stringify({
      _meta: {
        level: "ERROR",
      },
      "0": "worker-pool",
      message: "ignored-when-0-is-string",
    });

    const parsed = parseLogLine(line);

    expect(parsed.level).toBe("error");
    expect(parsed.subsystem).toBe("worker-pool");
    expect(parsed.message).toBe("worker-pool");
  });
});

describe("loadLogs", () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it("returns early when disconnected or without client", async () => {
    const request = vi.fn();
    const disconnected = createState({
      client: { request } as unknown as LogsState["client"],
      connected: false,
    });
    const noClient = createState({ connected: true, client: null });

    await loadLogs(disconnected);
    await loadLogs(noClient);

    expect(request).not.toHaveBeenCalled();
  });

  it("skips non-quiet loads while already loading", async () => {
    const request = vi.fn();
    const state = createState({
      client: { request } as unknown as LogsState["client"],
      logsLoading: true,
    });

    await loadLogs(state);

    expect(request).not.toHaveBeenCalled();
  });

  it("loads logs with reset semantics and updates cursor metadata", async () => {
    const request = vi.fn().mockResolvedValue({
      file: "/tmp/quantclaw.log",
      cursor: 42,
      truncated: true,
      lines: [
        JSON.stringify({ _meta: { level: "info" }, "0": "gateway", "1": "hello" }),
        "plain line",
      ],
    });
    const nowSpy = vi.spyOn(Date, "now").mockReturnValue(123456);
    const state = createState({
      client: { request } as unknown as LogsState["client"],
      logsCursor: null,
      logsEntries: [{ raw: "old", message: "old" }],
    });

    await loadLogs(state);

    expect(request).toHaveBeenCalledWith("logs.tail", {
      cursor: undefined,
      limit: 100,
      maxBytes: 65536,
    });
    expect(state.logsEntries).toHaveLength(2);
    expect(state.logsEntries[0].message).toBe("hello");
    expect(state.logsEntries[1].message).toBe("plain line");
    expect(state.logsCursor).toBe(42);
    expect(state.logsFile).toBe("/tmp/quantclaw.log");
    expect(state.logsTruncated).toBe(true);
    expect(state.logsLastFetchAt).toBe(123456);
    expect(state.logsError).toBeNull();
    expect(state.logsLoading).toBe(false);

    nowSpy.mockRestore();
  });

  it("appends lines when not resetting", async () => {
    const request = vi.fn().mockResolvedValue({
      cursor: 9,
      lines: ["fresh-entry"],
      reset: false,
    });
    const state = createState({
      client: { request } as unknown as LogsState["client"],
      logsCursor: 3,
      logsEntries: [{ raw: "existing", message: "existing" }],
    });

    await loadLogs(state);

    expect(state.logsEntries).toHaveLength(2);
    expect(state.logsEntries[0].message).toBe("existing");
    expect(state.logsEntries[1].message).toBe("fresh-entry");
    expect(state.logsCursor).toBe(9);
  });

  it("respects explicit reset and replaces the buffer", async () => {
    const request = vi.fn().mockResolvedValue({
      cursor: 10,
      reset: true,
      lines: ["new-tail"],
    });
    const state = createState({
      client: { request } as unknown as LogsState["client"],
      logsCursor: 4,
      logsEntries: [{ raw: "existing", message: "existing" }],
    });

    await loadLogs(state);

    expect(state.logsEntries).toEqual([{ raw: "new-tail", message: "new-tail" }]);
  });

  it("allows quiet polling while loading and keeps loading flag unchanged", async () => {
    const request = vi.fn().mockResolvedValue({ lines: [] });
    const state = createState({
      client: { request } as unknown as LogsState["client"],
      logsLoading: true,
      logsCursor: 5,
    });

    await loadLogs(state, { quiet: true });

    expect(request).toHaveBeenCalledWith("logs.tail", {
      cursor: 5,
      limit: 100,
      maxBytes: 65536,
    });
    expect(state.logsLoading).toBe(true);
  });

  it("captures errors and clears loading in non-quiet mode", async () => {
    const request = vi.fn().mockRejectedValue(new Error("tail failed"));
    const state = createState({
      client: { request } as unknown as LogsState["client"],
    });

    await loadLogs(state);

    expect(state.logsError).toContain("tail failed");
    expect(state.logsLoading).toBe(false);
  });
});
