---
name: bug-bounty-header-analysis
emoji: "🧾"
description: Authorized bug bounty HTTP header analysis workflow with multi-endpoint sampling, redirect-aware collection, CORS review, and evidence-ready reporting.
requires:
  bins:
    - curl
    - jq
    - awk
    - sed
    - sort
    - uniq
  anyBins:
    - httpx
    - openssl
always: false
commands:
  - name: recon-headers
    description: Analyze HTTP security headers and policy posture for scoped targets
    toolName: system.run
    argMode: freeform
---

Use this skill for **authorized bug bounty reconnaissance** focused on HTTP response header posture.

If targets, paths, or auth context are not clearly in scope, stop and clarify before testing.

## When to Use

- You already identified live web assets and need quick policy-level security triage.
- You need reproducible header evidence for bug bounty reports.
- You want consistent analysis across protocols, redirects, and environments.

## Required Inputs

- `normalized/alive-http.txt` or equivalent web target list
- Approved path list (public endpoints and permitted authenticated contexts)
- Program policy for request rates and authentication handling

## Output Contract

- `raw/headers-<host>.txt` (raw capture per tested target)
- `reports/headers.csv` (normalized comparative dataset)
- `reports/header-findings.md` (finding-by-finding evidence and rationale)
- `reports/header-summary.md` (coverage + prioritized remediation themes)

## Procedure

### 1) Build a target matrix

1. Normalize target URLs by host, scheme, and relevant web ports.
2. Select representative paths (root + high-value app paths where allowed).
3. Define test dimensions: protocol (HTTP/HTTPS), redirect path, and endpoint class.

**Completion checks**
- Matrix includes host + scheme + path context.
- All targets are confirmed in scope.

### 2) Collect headers with context fidelity

1. Capture response headers for each matrix entry.
2. Preserve both initial and final headers across redirects.
3. Store captures with timestamp and request context metadata.

**Decision points**
- If header behavior varies by path, expand sampling for that host.
- If intermediaries/CDNs modify responses, annotate edge-vs-origin uncertainty.

**Completion checks**
- Raw evidence exists for each analyzed finding.
- Redirect chains are preserved, not flattened away.

### 3) Baseline security header evaluation

Evaluate presence, quality, and effective behavior for:
- `Strict-Transport-Security`
- `Content-Security-Policy`
- `X-Frame-Options` and/or `frame-ancestors`
- `X-Content-Type-Options`
- `Referrer-Policy`
- `Permissions-Policy`
- Legacy or conflicting headers

**Decision points**
- If CSP is present but permissive (e.g., broad wildcards / unsafe directives), downgrade confidence in protection.
- If duplicate/conflicting directives exist, prioritize effective browser behavior in analysis.

### 4) CORS, caching, and disclosure checks

1. Analyze `Access-Control-*` behavior (origin reflection, credentials interplay).
2. Review cache headers for sensitive endpoint patterns.
3. Note unnecessary technology disclosure (`Server`, `X-Powered-By`, debug headers).

**Decision points**
- If CORS behavior is dynamic, test multiple origin scenarios before classifying.
- If caching differs by auth state, separate anonymous vs authenticated observations.

### 5) Cookie-policy cross-check

1. For responses setting cookies, inspect `Secure`, `HttpOnly`, and `SameSite` posture.
2. Correlate cookie attributes with transport and CORS behaviors.
3. Flag risky combinations with reproduction notes.

### 6) False-positive control and confidence labeling

1. Re-test high-impact findings to ensure reproducibility.
2. Distinguish framework defaults from exploitable misconfigurations.
3. Assign finding confidence (`high`, `medium`, `low`) based on consistency.

**Completion checks**
- High-impact findings are confirmed in repeat captures.
- Non-actionable informational observations are clearly separated.

### 7) Reporting and remediation-oriented triage

1. Record each finding with: host/path, header evidence, impact rationale, confidence.
2. Group findings by remediation type (transport, framing, CSP, CORS, disclosure).
3. Produce a top-priority remediation list for fast hardening.

Suggested scoring model:
- Priority = Exposure (0–3) + Exploitability (0–3) + Evidence Confidence (0–2)

## Quality Bar

- Every conclusion is backed by raw header evidence.
- Redirect and path context are preserved in findings.
- Report language distinguishes absence, weakness, and conflicting policy states.
