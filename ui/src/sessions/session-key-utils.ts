// Session key utilities shared across UI modules.
//
// Agent session keys follow the format: "agent:<agentId>:<sessionName>"
// Example: "agent:main:chat-1"

export interface ParsedAgentSessionKey {
  agentId: string;
  sessionName: string;
}

/**
 * Parse an agent session key of the form "agent:<agentId>:<sessionName>".
 * Returns null if the key is not in the expected format.
 */
export function parseAgentSessionKey(
  key: string,
): ParsedAgentSessionKey | null {
  if (!key) return null;
  const parts = key.split(":");
  if (parts.length < 3 || parts[0] !== "agent") return null;
  const agentId = parts[1];
  const sessionName = parts.slice(2).join(":");
  if (!agentId || !sessionName) return null;
  return { agentId, sessionName };
}
