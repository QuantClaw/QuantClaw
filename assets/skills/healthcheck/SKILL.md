---
name: healthcheck
emoji: "\U0001F3E5"
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
