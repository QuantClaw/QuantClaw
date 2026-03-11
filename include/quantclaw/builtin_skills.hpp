// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Built-in skills embedded at compile time from assets/skills/.
// Each entry is {directory_name, SKILL.md_content}.
// Onboarding writes these to ~/.quantclaw/skills/ (skips existing files).

#pragma once

#include <vector>

namespace quantclaw {

struct BuiltinSkill {
  const char* name;     // directory name (== skill name)
  const char* content;  // full SKILL.md content
};

// Returns the compile-time registry of built-in skills embedded from
// assets/skills/.  The vector is static and initialised once; callers receive
// a const reference that remains valid for the lifetime of the process.
//
// Each entry contains:
//   name    – the skill directory name (used as the skill identifier)
//   content – the full SKILL.md text written to ~/.quantclaw/skills/<name>/
//
// Onboarding copies these files to the user workspace, skipping any that
// already exist so user edits are preserved.
inline const std::vector<BuiltinSkill>& GetBuiltinSkills() {
  // Raw-string delimiter SKILL avoids conflicts with any character in the
  // markdown content (backticks, quotes, closing parens, arrows, etc.).
  static const std::vector<BuiltinSkill> kSkills = {
      {"search",
       R"SKILL(---
name: search
emoji: "🔍"
description: Web search with automatic provider fallback (Tavily → DuckDuckGo)
always: true
commands:
  - name: search
    description: Search the web for a query
    toolName: web_search
    argMode: freeform
---

Use the `web_search` tool to search the web. Results include titles, URLs, and snippets.

**Tool:** `web_search`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | ✓ | Search query |
| `count` | integer | — | Number of results (1–10, default 5) |
| `freshness` | string | — | Time filter: `day`, `week`, `month`, `year` |

**Provider cascade** (first available key wins):
1. **Tavily** (`TAVILY_API_KEY`) — recommended, high-quality results
2. **DuckDuckGo** — free, no API key required, always available as fallback

Set `TAVILY_API_KEY` in your environment or config for best results:
```json
{ "providers": { "tavily": { "apiKey": "tvly-..." } } }
```

**Examples:**
```
web_search({"query": "latest OpenAI news"})
web_search({"query": "Python asyncio tutorial", "count": 3})
web_search({"query": "market open price TSLA", "freshness": "day"})
```

**Slash command:** `/search <query>` triggers an immediate web search.
)SKILL"},
      {"weather",
       R"SKILL(---
name: weather
emoji: "🌦️"
description: Check current weather using wttr.in
always: true
---

You can check the weather for any location using the `system.run` tool.

**Usage:** Run `curl "wttr.in/{location}?format=3"` to get a compact weather summary.

For detailed forecast: `curl "wttr.in/{location}"`

Examples:
- `curl "wttr.in/Beijing?format=3"` → Beijing: ☀️ +25°C
- `curl "wttr.in/Tokyo?format=%C+%t+%w"` → Clear +22°C ↗10km/h
- `curl "wttr.in/London?lang=zh"` → Chinese output
)SKILL"},
      {"github",
       R"SKILL(---
name: github
emoji: "🐙"
description: Interact with GitHub via gh CLI
requires:
  bins:
    - gh
metadata:
  openclaw:
    install:
      apt: gh
---

You can interact with GitHub using the `gh` CLI tool via `system.run`.

**Common operations:**
- List repos: `gh repo list`
- View issues: `gh issue list -R owner/repo`
- Create issue: `gh issue create -R owner/repo --title "..." --body "..."`
- View PR: `gh pr view 123 -R owner/repo`
- Create PR: `gh pr create --title "..." --body "..."`
- Search code: `gh search code "query" --language python`
- View notifications: `gh api notifications`

**Authentication:** Ensure `gh auth login` has been run first.
)SKILL"},
      {"healthcheck",
       R"SKILL(---
name: healthcheck
emoji: "🏥"
description: System health audit and diagnostics
always: true
commands:
  - name: healthcheck
    description: Run system health check
    toolName: system.run
    argMode: none
---

You can perform system health checks and diagnostics using standard Linux tools.

**Checks to perform:**
1. **Disk usage:** `df -h` — check for filesystems over 90%
2. **Memory:** `free -h` — check available memory
3. **CPU load:** `uptime` — check load averages
4. **Network:** `ping -c 1 8.8.8.8` — check internet connectivity
5. **DNS:** `dig google.com +short` — check DNS resolution
6. **Services:** `systemctl --user list-units --state=running` — check running services
7. **Logs:** `journalctl --user -n 20 --no-pager` — recent log entries

**QuantClaw specific:**
- Gateway status: `quantclaw status`
- Config check: `quantclaw doctor`
- Health endpoint: `quantclaw health`
)SKILL"},
      {"skill-creator",
       R"SKILL(---
name: skill-creator
emoji: "🎨"
description: Guide for creating new QuantClaw skills
always: true
commands:
  - name: create-skill
    description: Create a new skill from template
    toolName: system.run
    argMode: freeform
---

You help create new skills for QuantClaw. A skill is a directory containing a `SKILL.md` file with YAML frontmatter and markdown instructions.

**Skill structure:**
```
~/.quantclaw/agents/main/workspace/skills/{skill-name}/
├── SKILL.md          # Required: frontmatter + instructions
├── scripts/          # Optional: helper scripts
├── references/       # Optional: reference documents
└── assets/           # Optional: images, templates, etc.
```

**SKILL.md frontmatter format:**
```yaml
---
name: my-skill
emoji: "🔧"
description: Short description of the skill
requires:
  bins:
    - required-binary
  env:
    - REQUIRED_ENV_VAR
  anyBins:
    - option-a
    - option-b
os:
  - linux
  - darwin
always: false
metadata:
  openclaw:
    install:
      apt: package-name
      node: npm-package
---
```

The markdown body after the frontmatter becomes the skill context injected into the LLM prompt.
)SKILL"},
  };
  return kSkills;
}

}  // namespace quantclaw
