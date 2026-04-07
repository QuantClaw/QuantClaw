---
name: bug-bounty-subdomain-enum
emoji: "🌐"
description: Authorized bug bounty subdomain enumeration workflow with source fusion, wildcard handling, DNS validation, and triage-ready prioritization.
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
    - findomain
    - dnsx
    - puredns
    - shuffledns
    - massdns
    - httpx
always: false
commands:
  - name: recon-subdomains
    description: Enumerate and validate in-scope subdomains
    toolName: system.run
    argMode: freeform
---

Use this skill for **authorized bug bounty reconnaissance** focused on subdomain discovery.

If scope/authorization is not explicit, stop and request confirmation before any enumeration.

## When to Use

- You need a high-confidence inventory of subdomains for a scoped domain.
- You want reproducible asset discovery artifacts for downstream scanning.
- You need to reduce wildcard/noise before manual testing.

## Required Inputs

- In-scope root domains and known exclusions
- Program policy constraints (active vs passive allowed, rate limits)
- Time window and scan budget

## Output Contract

- `raw/subdomains-<source>.txt` (one file per source)
- `normalized/subdomains.txt` (merged + deduped)
- `normalized/resolved.txt` (DNS-resolved candidates)
- `normalized/alive-http.txt` (optional web-reachable subset)
- `reports/subdomain-summary.md` (counts, confidence notes, next actions)

## Procedure

### 1) Scope normalization and logging

1. Convert scope language into explicit root domains and exact exclusions.
2. Build a timestamped workspace with `raw/`, `normalized/`, and `reports/`.
3. Start an audit trail: source/tool used, run time, and policy assumptions.

**Completion checks**
- Scope list is explicit and saved.
- Out-of-scope patterns are documented before enumeration.

### 2) Multi-source passive collection

1. Collect candidates from at least **2–3 independent sources** when available.
2. Preserve per-source output separately to keep provenance.
3. Track source reliability (freshness, duplicates, stale records).

**Decision points**
- If only one source is available, compensate with additional passive datasets and stronger validation.
- If source output is stale/heavy-noise, lower confidence and prioritize verification.

**Completion checks**
- Raw outputs are separated by source.
- Source coverage and gaps are recorded in summary notes.

### 3) Candidate normalization and enrichment

1. Normalize case/format and dedupe candidates.
2. Enrich names via safe permutations (if policy permits) from observed labels.
3. Tag each candidate with provenance (`source_count`, `seed_or_permuted`).

**Decision points**
- If active expansion is disallowed, skip permutations and annotate this constraint.
- If candidate volume is very high, batch by root domain and confidence.

**Completion checks**
- `normalized/subdomains.txt` is deduped and provenance-aware.
- Expansion decisions are recorded with rationale.

### 4) Wildcard/sinkhole filtering

1. Detect wildcard DNS behavior per root using random-label probes.
2. Build root-specific wildcard fingerprints (IPs/CNAME patterns).
3. Filter probable wildcard artifacts while preserving uncertain edge cases for manual review.

**Decision points**
- If wildcard behavior is partial (only some resolvers/records), keep uncertain hosts in a `review-needed` bucket.
- If sinkhole patterns appear, quarantine and annotate as low-confidence.

**Completion checks**
- Wildcard method and fingerprints are documented.
- False positives are reduced without over-pruning unknown assets.

### 5) DNS validation and classification

1. Resolve candidate records and classify by type (`A`, `AAAA`, `CNAME`, other).
2. Mark states (`resolves`, `nxdomain`, `servfail`, `timeout`).
3. Keep stable resolvable assets in `normalized/resolved.txt`.

**Completion checks**
- Resolution results include status categories.
- `resolved.txt` excludes obvious dead/noise entries.

### 6) Optional HTTP reachability profiling

1. Probe `resolved.txt` for HTTP/HTTPS reachability.
2. Capture key metadata (status code, scheme, title, TLS hints).
3. Save web-reachable subset into `normalized/alive-http.txt`.

**Decision points**
- If probing is out-of-policy, skip and mark as deferred for later phases.
- If CDN/WAF is detected, tag hosts to guide downstream port scanning strategy.

### 7) Prioritization and handoff

1. Rank subdomains by practical bounty value:
   - Identity/admin names (`auth`, `admin`, `sso`, `login`)
   - Environment markers (`dev`, `staging`, `internal`)
   - API/data-plane naming (`api`, `files`, `cdn-origin`)
2. Produce a concise top-target list and pass it to port/header phases.

Suggested scoring model:
- Priority = Exposure (0–3) + Sensitivity (0–3) + Novelty (0–2)

## Quality Bar

- Every output is traceable to source/provenance.
- Wildcard handling is explicit and reproducible.
- Final inventory is low-noise and ready for downstream recon.
