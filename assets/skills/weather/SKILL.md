---
name: weather
emoji: "\U0001F326\uFE0F"
description: >
  Check weather forecasts and current conditions for any city. This skill MUST be used whenever
  the user asks about weather, temperature, forecast, rain, snow, wind, humidity, or climate.
  Examples: "What's the weather in Chengdu?", "成都的天气怎么样", "Will it rain tomorrow in Tokyo?",
  "北京今天温度多少", "weather in Paris", "Is it sunny in London?". ALWAYS call the skill tool
  with command="weather" for ANY weather-related questions. NEVER use exec tool directly for weather queries.
always: true
requires:
  bins:
    - curl
allowedTools:
  - exec
  - web_fetch
commands:
  - name: weather
    description: "Check weather for a city (debug mode)"
    toolName: exec
    argMode: freeform
---

You can check the weather for any location using the `system.run` tool.

**Usage:** Run `curl "wttr.in/{location}?format=3"` to get a compact weather summary.

For detailed forecast: `curl "wttr.in/{location}"`

Examples:
- `curl "wttr.in/Beijing?format=3"` → Beijing: ☀️ +25°C
- `curl "wttr.in/Tokyo?format=%C+%t+%w"` → Clear +22°C ↗10km/h
- `curl "wttr.in/London?lang=zh"` → Chinese output
