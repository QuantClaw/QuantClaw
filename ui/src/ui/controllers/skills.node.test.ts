import { describe, expect, it, vi } from "vitest";
import {
  installSkill,
  loadSkills,
  saveSkillApiKey,
  updateSkillEdit,
  updateSkillEnabled,
  type SkillsState,
} from "./skills.ts";
import type { SkillStatusReport } from "../types.ts";

function createState(overrides: Partial<SkillsState> = {}): SkillsState {
  return {
    client: null,
    connected: true,
    skillsLoading: false,
    skillsReport: null,
    skillsError: null,
    skillsBusyKey: null,
    skillEdits: {},
    skillMessages: {},
    ...overrides,
  };
}

function makeReport(): SkillStatusReport {
  return {
    workspaceDir: "/workspace",
    managedSkillsDir: "/workspace/.quantclaw/skills",
    skills: [],
  };
}

describe("loadSkills", () => {
  it("can clear transient messages before loading", async () => {
    const request = vi.fn();
    const state = createState({
      connected: false,
      client: { request } as unknown as SkillsState["client"],
      skillMessages: {
        "skill:demo": { kind: "error", message: "old" },
      },
    });

    await loadSkills(state, { clearMessages: true });

    expect(state.skillMessages).toEqual({});
    expect(request).not.toHaveBeenCalled();
  });

  it("returns early if a load is already in progress", async () => {
    const request = vi.fn();
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
      skillsLoading: true,
    });

    await loadSkills(state);

    expect(request).not.toHaveBeenCalled();
  });

  it("stores skills report on success", async () => {
    const report = makeReport();
    const request = vi.fn().mockResolvedValue(report);
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
      skillsError: "stale-error",
    });

    await loadSkills(state);

    expect(request).toHaveBeenCalledWith("skills.status", {});
    expect(state.skillsReport).toEqual(report);
    expect(state.skillsError).toBeNull();
    expect(state.skillsLoading).toBe(false);
  });

  it("captures request failures", async () => {
    const request = vi.fn().mockRejectedValue("gateway down");
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
    });

    await loadSkills(state);

    expect(state.skillsError).toBe("gateway down");
    expect(state.skillsLoading).toBe(false);
  });
});

describe("updateSkillEdit", () => {
  it("updates skill edits immutably", () => {
    const state = createState({
      skillEdits: {
        "skill:a": "old-a",
      },
    });

    updateSkillEdit(state, "skill:b", "new-b");

    expect(state.skillEdits).toEqual({
      "skill:a": "old-a",
      "skill:b": "new-b",
    });
  });
});

describe("updateSkillEnabled", () => {
  it("updates backend and stores a success message", async () => {
    const report = makeReport();
    const request = vi.fn().mockImplementation(async (method: string) => {
      if (method === "skills.update") {
        return {};
      }
      if (method === "skills.status") {
        return report;
      }
      throw new Error(`unexpected method: ${method}`);
    });
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
    });

    await updateSkillEnabled(state, "skill:weather", true);

    expect(request.mock.calls[0]).toEqual([
      "skills.update",
      { skillKey: "skill:weather", enabled: true },
    ]);
    expect(request.mock.calls[1]).toEqual(["skills.status", {}]);
    expect(state.skillsReport).toEqual(report);
    expect(state.skillsBusyKey).toBeNull();
    expect(state.skillsError).toBeNull();
    expect(state.skillMessages["skill:weather"]).toEqual({
      kind: "success",
      message: "Skill enabled",
    });
  });

  it("stores an error message when update fails", async () => {
    const request = vi.fn().mockRejectedValue(new Error("permission denied"));
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
    });

    await updateSkillEnabled(state, "skill:weather", false);

    expect(state.skillsBusyKey).toBeNull();
    expect(state.skillsError).toBe("permission denied");
    expect(state.skillMessages["skill:weather"]).toEqual({
      kind: "error",
      message: "permission denied",
    });
  });
});

describe("saveSkillApiKey", () => {
  it("submits the edited key and reports success", async () => {
    const report = makeReport();
    const request = vi.fn().mockImplementation(async (method: string) => {
      if (method === "skills.update") {
        return {};
      }
      if (method === "skills.status") {
        return report;
      }
      throw new Error(`unexpected method: ${method}`);
    });
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
      skillEdits: {
        "skill:maps": "api-key-123",
      },
    });

    await saveSkillApiKey(state, "skill:maps");

    expect(request.mock.calls[0]).toEqual([
      "skills.update",
      { skillKey: "skill:maps", apiKey: "api-key-123" },
    ]);
    expect(state.skillMessages["skill:maps"]).toEqual({
      kind: "success",
      message: "API key saved",
    });
  });

  it("stores errors when save fails", async () => {
    const request = vi.fn().mockRejectedValue("save failed");
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
    });

    await saveSkillApiKey(state, "skill:maps");

    expect(state.skillsError).toBe("save failed");
    expect(state.skillMessages["skill:maps"]).toEqual({
      kind: "error",
      message: "save failed",
    });
    expect(state.skillsBusyKey).toBeNull();
  });
});

describe("installSkill", () => {
  it("stores backend success message when installation succeeds", async () => {
    const report = makeReport();
    const request = vi.fn().mockImplementation(async (method: string) => {
      if (method === "skills.install") {
        return { message: "Installed from catalog" };
      }
      if (method === "skills.status") {
        return report;
      }
      throw new Error(`unexpected method: ${method}`);
    });
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
    });

    await installSkill(state, "skill:deploy", "deploy", "catalog:deploy");

    expect(request.mock.calls[0]).toEqual([
      "skills.install",
      { name: "deploy", installId: "catalog:deploy", timeoutMs: 120000 },
    ]);
    expect(state.skillMessages["skill:deploy"]).toEqual({
      kind: "success",
      message: "Installed from catalog",
    });
    expect(state.skillsBusyKey).toBeNull();
  });

  it("falls back to a default success message when API omits one", async () => {
    const report = makeReport();
    const request = vi.fn().mockImplementation(async (method: string) => {
      if (method === "skills.install") {
        return {};
      }
      if (method === "skills.status") {
        return report;
      }
      throw new Error(`unexpected method: ${method}`);
    });
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
    });

    await installSkill(state, "skill:deploy", "deploy", "catalog:deploy");

    expect(state.skillMessages["skill:deploy"]).toEqual({
      kind: "success",
      message: "Installed",
    });
  });

  it("stores installation errors", async () => {
    const request = vi.fn().mockRejectedValue(new Error("install failed"));
    const state = createState({
      client: { request } as unknown as SkillsState["client"],
    });

    await installSkill(state, "skill:deploy", "deploy", "catalog:deploy");

    expect(state.skillsError).toBe("install failed");
    expect(state.skillMessages["skill:deploy"]).toEqual({
      kind: "error",
      message: "install failed",
    });
    expect(state.skillsBusyKey).toBeNull();
  });
});
