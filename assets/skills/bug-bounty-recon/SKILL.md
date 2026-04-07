---
name: bug-bounty-recon
emoji: "🕵️"
description: Authorized bug bounty recon orchestrator that routes to independent deep skills for subdomain enumeration, port scanning, and HTTP header analysis.
requires:
  bins:
    - jq
    - awk
    - sed
    - sort
    - uniq
  anyBins:
    - subfinder
    - amass
    - assetfinder
    - naabu
    - nmap
    - rustscan
    - httpx
always: false
commands:
  - name: recon
    description: Route recon workflow to one or more specialized skills
    toolName: system.run
    argMode: freeform
---

Use this as the **orchestration/index skill** for authorized bug bounty recon.

For deep execution guidance, use the independent phase skills:

1. `bug-bounty-subdomain-enum`
2. `bug-bounty-port-scanning`
3. `bug-bounty-header-analysis`

If scope or authorization is missing, stop and clarify before any recon activity.

## What This Skill Produces

- A clear phase-selection plan
- Artifact handoff contracts between phases
- A prioritized execution order for the chosen recon objective

## Phase Selector

- Choose **`bug-bounty-subdomain-enum`** when you need discovery and inventory.
- Choose **`bug-bounty-port-scanning`** when you already have hosts and need exposed services.
- Choose **`bug-bounty-header-analysis`** when you have live web targets and need HTTP policy triage.

## Handoff Contracts

### Subdomain → Port Scanning

- Input: `normalized/resolved.txt`
- Output expected from next phase: `normalized/open-ports.txt`, `normalized/services.txt`

### Subdomain/Port → Header Analysis

- Input: `normalized/alive-http.txt` or web targets derived from open web ports
- Output expected: `reports/headers.csv`, `reports/header-findings.md`

## Recommended Execution Patterns

### Pattern A: Full pipeline
1. Run subdomain enumeration
2. Run port scanning on resolved hosts
3. Run header analysis on live web services

### Pattern B: App-focused quick pass
1. Run subdomain enumeration
2. Probe alive web endpoints
3. Run header analysis directly

### Pattern C: Service exposure deep dive
1. Start from existing host inventory
2. Run port scanning only
3. Report service-risk triage

## Global Completion Checks

- All artifacts are reproducible and in-scope.
- Each phase output can be consumed by the next phase without manual cleanup.
- Reports include scope notes, timestamps, and confidence labels.

## Safety Note

Perform reconnaissance only where you have explicit authorization (bug bounty scope, written permission, or equivalent).
