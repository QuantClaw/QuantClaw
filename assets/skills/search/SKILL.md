---
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
