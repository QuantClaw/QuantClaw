---
name: weather
emoji: "\U0001F326\uFE0F"
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
