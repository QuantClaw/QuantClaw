export function stripFeishuMentions(content: string): string {
  return content
    .replace(/<at\s+[^>]*>[^<]*<\/at>/gi, " ")
    .replace(/^@\S+\s+/u, "")
    .trim();
}
