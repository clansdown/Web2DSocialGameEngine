<script lang="ts">
  import { marked } from 'marked';
  import type { ReeveText as ReeveTextData } from '../lib/api';
  import {
    apply_reeve_tokens,
    resolve_reeve_args,
    resolve_reeve_text
  } from '../lib/reeve_text';

  interface Props {
    text: ReeveTextData;
    texts: Record<string, string>;
    class?: string;
    inline?: boolean;
  }

  let { text, texts, class: className = '', inline = false }: Props = $props();

  let content = $derived(resolve_reeve_text(text, texts));
  let html = $derived(content
    ? (inline
        ? marked.parseInline(apply_reeve_tokens(content, resolve_reeve_args(text.args, texts))) as string
        : marked.parse(apply_reeve_tokens(content, resolve_reeve_args(text.args, texts))) as string)
    : '');
</script>

{#if html}
  {#if inline}
    <span class="reeve-text {className}">{@html html}</span>
  {:else}
    <div class="reeve-text {className}">{@html html}</div>
  {/if}
{/if}

<style>
  .reeve-text {
    line-height: 1.45;
  }
  .reeve-text :global(p) {
    margin-bottom: 0.35rem;
  }
  .reeve-text :global(p:last-child) {
    margin-bottom: 0;
  }
  .reeve-text :global(strong) {
    font-weight: 600;
  }
</style>
