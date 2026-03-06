---
name: github
emoji: "\U0001F419"
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
