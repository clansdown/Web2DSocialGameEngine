<script lang="ts">
  /**
   * Player support inbox. Support is account-level and text-only. The list is
   * the default view; #/activity/support/<ticket_id> opens a thread. A
   * support-locked account can read but not create or reply.
   *
   * @param onBack - Returns to the hub
   * @param rest - Nested route segments ([ticket_id] for a thread)
   */
  import { onMount } from 'svelte';
  import { currentCharacter } from '../lib/stores';
  import { loadTexts } from '../lib/text';
  import { navigate } from '../lib/router';
  import {
    createSupportTicketRequest,
    getSupportTicketRequest,
    listSupportTicketsRequest,
    reopenSupportTicketRequest,
    replySupportTicketRequest,
    type support_category,
    type support_message_dto,
    type support_ticket_dto
  } from './support_api';

  interface Props {
    onBack: () => void;
    rest?: string[];
  }
  let { onBack, rest = [] }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_support_title', 'ui_support_back', 'ui_support_new', 'ui_support_subject',
    'ui_support_category', 'ui_support_message', 'ui_support_send', 'ui_support_reply',
    'ui_support_no_tickets', 'ui_support_locked', 'ui_support_status', 'ui_support_loading',
    'ui_support_error', 'ui_support_created', 'ui_support_closed', 'ui_support_resolved',
    'ui_support_reopen', 'ui_support_cancel', 'ui_support_thread', 'ui_support_from_staff',
    'ui_support_from_you', 'ui_support_category_appeal', 'ui_support_category_report',
    'ui_support_category_technical', 'ui_support_category_account', 'ui_support_category_other',
    'ui_support_status_open', 'ui_support_status_awaiting_staff',
    'ui_support_status_awaiting_player', 'ui_support_status_resolved',
    'ui_support_status_closed'
  ];

  const CATEGORIES: support_category[] = ['appeal', 'report', 'technical', 'account', 'other'];

  let texts = $state<Record<string, string>>({});
  let tickets = $state<support_ticket_dto[]>([]);
  let messages = $state<support_message_dto[]>([]);
  let selected = $state<support_ticket_dto | null>(null);
  let supportLocked = $state(false);
  let loading = $state(true);
  let error = $state('');
  let creating = $state(false);
  let subject = $state('');
  let category = $state<support_category>('other');
  let body = $state('');
  let reply = $state('');
  let submitting = $state(false);

  let ticket_id = $derived(rest.length > 0 ? rest[0] : '');

  function t(id: string): string {
    return texts[id] ?? '';
  }

  function status_label(status: string): string {
    const key = `ui_support_status_${status}`;
    return texts[key] ?? status.replace(/_/g, ' ');
  }

  function category_label(value: string): string {
    return texts[`ui_support_category_${value}`] ?? value;
  }

  async function load_list(): Promise<void> {
    loading = true;
    error = '';
    try {
      const result = await listSupportTicketsRequest({ limit: 100 });
      tickets = result.tickets;
      supportLocked = result.support_locked;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
  }

  async function load_ticket(id: string): Promise<void> {
    loading = true;
    error = '';
    try {
      const result = await getSupportTicketRequest(id);
      selected = result.ticket;
      messages = result.messages;
      supportLocked = result.support_locked;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
  }

  async function refresh(): Promise<void> {
    if (ticket_id) await load_ticket(ticket_id);
    else await load_list();
  }

  async function create_ticket(): Promise<void> {
    if (!subject.trim() || !body.trim() || submitting) return;
    submitting = true;
    error = '';
    try {
      const result = await createSupportTicketRequest(
        subject.trim(),
        body.trim(),
        category,
        $currentCharacter?.id
      );
      subject = '';
      body = '';
      creating = false;
      navigate({ type: 'activity', id: 'support', rest: [result.ticket.id] });
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      submitting = false;
    }
  }

  async function send_reply(): Promise<void> {
    if (!selected || !reply.trim() || submitting) return;
    submitting = true;
    error = '';
    try {
      await replySupportTicketRequest(selected.id, reply.trim());
      reply = '';
      await load_ticket(selected.id);
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      submitting = false;
    }
  }

  async function reopen(): Promise<void> {
    if (!selected) return;
    try {
      await reopenSupportTicketRequest(selected.id);
      await load_ticket(selected.id);
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    await refresh();
  });

  $effect(() => {
    const id = ticket_id;
    if (!loading && id && selected?.id !== id) void load_ticket(id);
    if (!loading && !id && selected !== null) {
      selected = null;
      messages = [];
      void load_list();
    }
  });
</script>

<div class="container py-4 support-shell">
  <div class="d-flex align-items-center gap-2 mb-3">
    <button class="btn btn-sm btn-outline-secondary" onclick={onBack}>
      &larr; {t('ui_support_back')}
    </button>
    <h2 class="h4 mb-0 flex-grow-1">{t('ui_support_title')}</h2>
    {#if !ticket_id && !creating && !supportLocked}
      <button class="btn btn-sm btn-primary" onclick={() => (creating = true)}>
        {t('ui_support_new')}
      </button>
    {/if}
  </div>

  {#if supportLocked}
    <div class="alert alert-warning py-2">{t('ui_support_locked')}</div>
  {/if}
  {#if error}
    <div class="alert alert-danger py-2">{t('ui_support_error')}: {error}</div>
  {/if}

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status"></div>
      <span class="ms-2">{t('ui_support_loading')}</span>
    </div>
  {:else if creating}
    <div class="card">
      <div class="card-body">
        <div class="mb-3">
          <label class="form-label" for="support-subject">{t('ui_support_subject')}</label>
          <input id="support-subject" class="form-control" maxlength="160" bind:value={subject} />
        </div>
        <div class="mb-3">
          <label class="form-label" for="support-category">{t('ui_support_category')}</label>
          <select id="support-category" class="form-select" bind:value={category}>
            {#each CATEGORIES as value (value)}
              <option value={value}>{category_label(value)}</option>
            {/each}
          </select>
        </div>
        <div class="mb-3">
          <label class="form-label" for="support-body">{t('ui_support_message')}</label>
          <textarea id="support-body" class="form-control" rows="6" bind:value={body}></textarea>
        </div>
        <div class="d-flex gap-2">
          <button class="btn btn-primary" onclick={create_ticket} disabled={submitting || !subject.trim() || !body.trim()}>
            {t('ui_support_send')}
          </button>
          <button class="btn btn-outline-secondary" onclick={() => (creating = false)}>
            {t('ui_support_cancel')}
          </button>
        </div>
      </div>
    </div>
  {:else if selected}
    <div class="mb-3">
      <button class="btn btn-sm btn-link px-0" onclick={() => navigate({ type: 'activity', id: 'support', rest: [] })}>
        &larr; {t('ui_support_title')}
      </button>
      <h3 class="h5 mb-1">{selected.subject}</h3>
      <div class="small text-secondary">
        {category_label(selected.category)} · {status_label(selected.status)}
        {#if selected.closed_at}· {t('ui_support_closed')}{/if}
      </div>
    </div>
    <div class="card mb-3">
      <div class="card-body">
        {#each messages as message (message.id)}
          <div class="mb-3" class:staff-message={message.author_kind === 'staff'}>
            <div class="small text-secondary mb-1">
              {message.author_kind === 'staff' ? t('ui_support_from_staff') : t('ui_support_from_you')}
              · {new Date(message.created_at * 1000).toLocaleString()}
            </div>
            <div class="border rounded p-2 bg-body-tertiary preserve-lines">{message.body}</div>
          </div>
        {/each}
      </div>
    </div>
    {#if selected.status === 'resolved' && !supportLocked}
      <button class="btn btn-outline-primary mb-3" onclick={reopen}>{t('ui_support_reopen')}</button>
    {/if}
    {#if selected.status !== 'closed' && !supportLocked}
      <div class="mb-2">
        <label class="form-label" for="support-reply">{t('ui_support_reply')}</label>
        <textarea id="support-reply" class="form-control" rows="3" bind:value={reply}></textarea>
      </div>
      <button class="btn btn-primary" onclick={send_reply} disabled={submitting || !reply.trim()}>
        {t('ui_support_send')}
      </button>
    {/if}
  {:else if tickets.length === 0}
    <div class="alert alert-secondary">{t('ui_support_no_tickets')}</div>
  {:else}
    <div class="list-group">
      {#each tickets as ticket (ticket.id)}
        <button
          class="list-group-item list-group-item-action text-start"
          onclick={() => navigate({ type: 'activity', id: 'support', rest: [ticket.id] })}
        >
          <div class="d-flex gap-2">
            <span class="fw-semibold flex-grow-1">{ticket.subject}</span>
            {#if ticket.unread_for_player > 0}
              <span class="badge text-bg-primary">{ticket.unread_for_player}</span>
            {/if}
          </div>
          <div class="small text-secondary">
            {category_label(ticket.category)} · {status_label(ticket.status)}
            · {new Date(ticket.updated_at * 1000).toLocaleDateString()}
          </div>
          {#if ticket.last_message_preview}
            <div class="small text-truncate">{ticket.last_message_preview}</div>
          {/if}
        </button>
      {/each}
    </div>
  {/if}
</div>

<style>
  .support-shell {
    max-width: 52rem;
  }
  .preserve-lines {
    white-space: pre-wrap;
    word-break: break-word;
  }
  .staff-message .border {
    border-color: var(--bs-primary) !important;
  }
</style>
