---
name: bug-bounty-port-scanning
emoji: "📡"
description: Authorized bug bounty port scanning workflow with policy-gated execution, staged coverage, service fingerprinting, and reproducible validation.
requires:
  bins:
    - jq
    - awk
    - sed
    - sort
    - uniq
  anyBins:
    - naabu
    - nmap
    - rustscan
    - masscan
    - httpx
always: false
commands:
  - name: recon-ports
    description: Perform scoped, policy-aware port scanning and service triage
    toolName: system.run
    argMode: freeform
---

Use this skill for **authorized bug bounty reconnaissance** focused on network-exposed services.

Never scan out-of-scope targets. Respect published rate limits and prohibited techniques.

## When to Use

- You have a validated host inventory and need service exposure mapping.
- You need reproducible `host:port` evidence for triage and reporting.
- You want a staged approach (fast discovery → deeper fingerprinting).

## Required Inputs

- `normalized/resolved.txt` from asset discovery
- Program-safe scanning policy (concurrency, packet rate, restricted port ranges)
- Prioritization hints (high-value subdomains, critical environments)

## Output Contract

- `raw/ports-discovery.txt` (initial open-port candidates)
- `normalized/open-ports.txt` (`host:port` canonical list)
- `normalized/services.txt` (`host,port,protocol,service,version,confidence`)
- `reports/port-scan-summary.md` (coverage, confidence, caveats, next steps)

## Procedure

### 1) Policy gate and scope lock

1. Validate all target hosts are in approved scope.
2. Translate program policy into explicit scan settings (threads/rate/timeouts).
3. Define disallowed actions (aggressive timing, intrusive scripts, forbidden ports).

**Completion checks**
- Scope + policy assumptions are documented before first packet.
- A conservative default profile is selected.

### 2) Host segmentation

1. Group hosts by likely behavior (web edge, API, infra-like, unknown).
2. Flag CDN/WAF-fronted hosts for careful interpretation.
3. Create a high-priority host subset for deeper follow-up.

**Decision points**
- If hosts are likely CDN-fronted, expect limited port visibility and focus on permitted origin candidates.
- If many hosts are ephemeral/cloud, prioritize reproducibility over exhaustive one-off results.

### 3) Stage A — Fast discovery scan

1. Run a low-noise scan over common/high-signal ports first.
2. Capture all open candidates and timestamps.
3. Deduplicate into `normalized/open-ports.txt`.

**Decision points**
- If timeout/error rates rise, reduce concurrency and expand timeout windows.
- If findings are sparse, validate target liveness and DNS freshness before broadening scope.

**Completion checks**
- Initial open-port set exists and is deduped.
- Retry profile is documented if defaults were changed.

### 4) Stage B — Coverage expansion

1. Expand scanning to broader ranges for prioritized hosts.
2. Keep scan windows bounded and policy-compliant.
3. Track expanded findings separately from Stage A for auditability.

**Decision points**
- If policy restricts full-range scans, restrict to approved service classes and document limitation.
- If rate-limiting appears, switch to slower batched strategy.

### 5) Stage C — Service fingerprinting

1. Enrich each `host:port` with protocol/service/version clues where allowed.
2. Assign confidence per fingerprint (`high`, `medium`, `low`).
3. Normalize records into `normalized/services.txt`.

**Completion checks**
- High-value hosts include service fingerprints.
- Low-confidence detections are clearly tagged for manual verification.

### 6) Validation pass and de-noising

1. Re-check a sample (or all high-value findings) to reduce transient false positives.
2. Remove non-reproducible entries or label as unstable.
3. Add notes for filtered ports and reasons.

**Completion checks**
- High-priority findings are reproducible.
- Unstable findings are not presented as confirmed.

### 7) Exposure triage and handoff

1. Prioritize by attack-surface value:
   - Admin/auth services exposed externally
   - Non-standard services on internet-facing hosts
   - Legacy/insecure protocol exposures
2. Prepare top targets for deeper application testing and header analysis.

Suggested scoring model:
- Priority = Exposure (0–4) + Service Risk (0–3) + Reproducibility (0–2)

## Quality Bar

- All findings are in-scope and policy-compliant.
- Open ports are reproducible and timestamped.
- Service metadata distinguishes confidence levels.
