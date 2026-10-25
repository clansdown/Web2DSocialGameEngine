<script lang="ts">
  /**
   * CombatChat — team text chat over the combat WebSocket. Messages are
   * relayed by the server to the team topic; chat never touches the match
   * simulation thread.
   */
  import { onMount } from 'svelte';
  import { currentCharacter } from '../lib/stores';
  import { getSafeContentRequest, type safe_catalog_entry } from '../lib/api';
  import * as auth from '../lib/auth';
  import { loadTexts } from '../lib/text';
  import type { CombatNetClient } from './CombatNetClient';
  import type { combat_chat_payload } from './protocol';

  interface Props {
    net: CombatNetClient;
    /** Parent calls this with every inbound chat payload. */
    register_chat_handler: (handler: (payload: combat_chat_payload) => void) => void;
    on_close: () => void;
  }

  let { net, register_chat_handler, on_close }: Props = $props();

  interface chat_line {
    from_name: string;
    text: string;
    timestamp: number;
  }

  let lines = $state<chat_line[]>([]);
  let input = $state('');
  let safe_messages = $state<safe_catalog_entry[]>([]);
  let safe_message_id = $state('');
  let is_minor = $derived($currentCharacter?.is_adult !== true);
  let texts = $state<Record<string, string>>({});
  let list_el: HTMLDivElement;

  /**
   * Appends an inbound chat message to the visible list.
   *
   * @param payload - combat_chat_payload from the server
   * @returns void
   */
  function receive(payload: combat_chat_payload): void {
    lines = [
      ...lines,
      {
        from_name: payload.from_name,
        text: payload.text,
        timestamp: payload.timestamp * 1000
      }
    ];
  }

  /**
   * Sends the typed message (empty input is ignored).
   *
   * @param none
   * @returns void
   */
  function submit(): void {
    if (is_minor) {
      if (!safe_message_id) return;
      net.chat_safe(safe_message_id);
      safe_message_id = '';
      return;
    }
    const text = input.trim();
    if (!text) return;
    net.chat(text);
    input = '';
  }

  $effect(() => {
    loadTexts([
      'combat_chat_title',
      'combat_chat_placeholder',
      'combat_chat_send_btn',
      'combat_chat_close_btn',
      'ui_chat_safe_message'
    ]).then((loaded) => {
      texts = loaded;
    });
    if (is_minor) {
      const creds = auth.getInMemoryCredentials();
      const token = auth.getSessionToken();
      if (creds && token) {
        getSafeContentRequest({ username: creds.username, token })
          .then((safe) => {
            safe_messages = safe.messages.filter(
              (entry) => !entry.contexts || entry.contexts.includes('combat')
            );
          })
          .catch(() => {});
      }
    }
  });

  $effect(() => {
    if (list_el) {
      list_el.scrollTop = list_el.scrollHeight;
    }
  });

  onMount(() => {
    register_chat_handler(receive);
  });
</script>

<div class="card border-secondary shadow">
  <div class="card-header d-flex justify-content-between align-items-center">
    <span>{texts.combat_chat_title}</span>
    <button class="btn btn-sm btn-outline-secondary" onclick={on_close}>
      {texts.combat_chat_close_btn}
    </button>
  </div>
  <div
    bind:this={list_el}
    class="card-body overflow-auto"
    style="max-height: 220px;"
  >
    {#each lines as line}
      <div class="mb-1">
        <span class="fw-bold">{line.from_name}:</span>
        <span class="ms-1">{line.text}</span>
      </div>
    {/each}
  </div>
  <div class="card-footer d-flex gap-2">
    {#if is_minor}
      <select class="form-select form-select-sm" bind:value={safe_message_id}>
        <option value="">{texts.ui_chat_safe_message}</option>
        {#each safe_messages as message (message.id)}
          <option value={message.id}>{message.body ?? message.id}</option>
        {/each}
      </select>
    {:else}
      <input
        class="form-control form-control-sm"
        placeholder={texts.combat_chat_placeholder}
        bind:value={input}
        onkeydown={(e) => {
          if (e.key === 'Enter') submit();
        }}
      />
    {/if}
    <button class="btn btn-sm btn-primary" onclick={submit} disabled={is_minor && !safe_message_id}>
      {texts.combat_chat_send_btn}
    </button>
  </div>
</div>
