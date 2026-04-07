import { describe, expect, it } from "vitest";
import { groupSkills } from "./skills-grouping.ts";
import type { SkillStatusEntry } from "../types.ts";

function makeSkill(overrides: Partial<SkillStatusEntry> = {}): SkillStatusEntry {
  const key = overrides.skillKey ?? "skill:demo";
  return {
    name: overrides.name ?? "demo",
    description: overrides.description ?? "demo skill",
    source: overrides.source ?? "quantclaw-workspace",
    filePath: overrides.filePath ?? `/skills/${key}.md`,
    baseDir: overrides.baseDir ?? "/skills",
    skillKey: key,
    bundled: overrides.bundled,
    primaryEnv: overrides.primaryEnv,
    emoji: overrides.emoji,
    homepage: overrides.homepage,
    always: overrides.always ?? false,
    disabled: overrides.disabled ?? false,
    blockedByAllowlist: overrides.blockedByAllowlist ?? false,
    eligible: overrides.eligible ?? true,
    requirements: overrides.requirements ?? {
      bins: [],
      env: [],
      config: [],
      os: [],
    },
    missing: overrides.missing ?? {
      bins: [],
      env: [],
      config: [],
      os: [],
    },
    configChecks: overrides.configChecks ?? [],
    install: overrides.install ?? [],
  };
}

describe("groupSkills", () => {
  it("groups known sources in the expected display order", () => {
    const grouped = groupSkills([
      makeSkill({ skillKey: "skill:workspace", source: "quantclaw-workspace" }),
      makeSkill({ skillKey: "skill:builtin", source: "quantclaw-managed", bundled: true }),
      makeSkill({ skillKey: "skill:installed", source: "quantclaw-managed" }),
      makeSkill({ skillKey: "skill:extra", source: "quantclaw-extra" }),
    ]);

    expect(grouped.map((group) => group.id)).toEqual([
      "workspace",
      "built-in",
      "installed",
      "extra",
    ]);
    expect(grouped[0]?.skills.map((skill) => skill.skillKey)).toEqual(["skill:workspace"]);
    expect(grouped[1]?.skills.map((skill) => skill.skillKey)).toEqual(["skill:builtin"]);
    expect(grouped[2]?.skills.map((skill) => skill.skillKey)).toEqual(["skill:installed"]);
    expect(grouped[3]?.skills.map((skill) => skill.skillKey)).toEqual(["skill:extra"]);
  });

  it("places unmatched sources into the Other group at the end", () => {
    const grouped = groupSkills([
      makeSkill({ skillKey: "skill:workspace", source: "quantclaw-workspace" }),
      makeSkill({ skillKey: "skill:unknown", source: "third-party" }),
    ]);

    expect(grouped.map((group) => group.id)).toEqual(["workspace", "other"]);
    expect(grouped[1]?.label).toBe("Other Skills");
    expect(grouped[1]?.skills.map((skill) => skill.skillKey)).toEqual(["skill:unknown"]);
  });

  it("omits empty groups entirely", () => {
    const grouped = groupSkills([]);
    expect(grouped).toEqual([]);
  });
});
