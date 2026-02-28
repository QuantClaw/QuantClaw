---
name: skill-creator
emoji: "\U0001F3A8"
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
