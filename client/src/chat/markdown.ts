/**
 * Minimal, safe Markdown renderer for chat messages.
 *
 * The message body is HTML-escaped FIRST, so no user-supplied markup can ever
 * reach the DOM. Only a small whitelist of formatting is then applied. Links are
 * restricted to http/https. This avoids pulling in a sanitizer dependency.
 */

function escape_html(input: string): string {
  return input
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

/**
 * Renders a chat message body as safe HTML.
 *
 * @param input Raw message text (may contain Markdown).
 * @returns HTML string safe to inject with `{@html ...}`.
 */
export function render_markdown(input: string): string {
  let text: string = escape_html(input);

  // Inline code (before other inline rules so `*` inside code is literal).
  text = text.replace(/`([^`\n]+)`/g, '<code>$1</code>');
  // Bold, then italic, then strikethrough.
  text = text.replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>');
  text = text.replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>');
  text = text.replace(/~~([^~\n]+)~~/g, '<del>$1</del>');
  // Links: only http/https.
  text = text.replace(
    /\[([^\]\n]+)\]\((https?:\/\/[^\s)]+)\)/g,
    '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>'
  );

  // Blockquotes: lines beginning with "&gt; " (escaped ">").
  text = text
    .split('\n')
    .map((line: string): string =>
      line.startsWith('&gt; ') ? `<blockquote class="mb-1">${line.slice(5)}</blockquote>` : line
    )
    .join('\n');

  return text.replace(/\n/g, '<br>');
}

/**
 * Strips Markdown down to plain text (used for reply excerpts / previews).
 *
 * @param input Raw message text.
 * @returns Plain text with common Markdown markers removed.
 */
export function markdown_to_text(input: string): string {
  return input
    .replace(/`([^`\n]+)`/g, '$1')
    .replace(/\*\*([^*\n]+)\*\*/g, '$1')
    .replace(/\*([^*\n]+)\*/g, '$1')
    .replace(/~~([^~\n]+)~~/g, '$1')
    .replace(/\[([^\]\n]+)\]\((https?:\/\/[^\s)]+)\)/g, '$1');
}
