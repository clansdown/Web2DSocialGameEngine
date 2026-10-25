<script lang="ts">
  /**
   * Account-level notification drawer. Notifications are system-generated and
   * persist until read; this component only renders them and marks them read.
   *
   * @param onClose - Closes the drawer
   * @param onChanged - Called after the unread count changes (hub badge refresh)
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import { navigate } from '../lib/router';
  import {
    getNotificationsRequest,
    markAllNotificationsReadRequest,
    markNotificationReadRequest,
    type notification_dto
  } from './support_api';

  interface Props {
    onClose: () => void;
    onChanged?: (unread: number) => void;
  }
  let { onClose, onChanged }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_notification_title', 'ui_notification_empty', 'ui_notification_mark_all',
    'ui_notification_close', 'ui_notification_load_error'
  ];

  let texts = $state<Record<string, string>>({});
  let notifications = $state<notification_dto[]>([]);
  let unread = $state(0);
  let loading = $state(true);
  let error = $state('');

  function t(id: string): string {
    return texts[id] ?? '';
  }

  function resolve_args(record: notification_dto): string {
    let value = texts[record.text_id] ?? record.text_id;
    for (const [key, replacement] of Object.entries(record.text_args ?? {})) {
      value = value.replace(new RegExp(`\\{${key}\\}`, 'g'), String(replacement));
    }
    return value;
  }

  async function load(): Promise<void> {
    loading = true;
    error = '';
    try {
      const result = await getNotificationsRequest(50, 0);
      notifications = result.notifications;
      unread = result.unread;
      const ids = new Set<string>(TEXT_IDS);
      for (const record of notifications) ids.add(record.text_id);
      texts = await loadTexts([...ids]);
      onChanged?.(unread);
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
  }

  async function open(record: notification_dto): Promise<void> {
    if (!record.read) {
      try {
        const result = await markNotificationReadRequest(record.id);
        unread = result.unread;
        notifications = notifications.map((entry) =>
          entry.id === record.id ? { ...entry, read: true, read_at: Date.now() / 1000 } : entry
        );
        onChanged?.(unread);
      } catch {
        /* read state is best-effort when opening */
      }
    }
    if (record.route) {
      const hash = record.route.replace(/^#\//, '');
      const segments = hash.split('/').filter((part) => part.length > 0);
      if (segments[0] === 'activity' && segments[1]) {
        navigate({ type: 'activity', id: segments[1], rest: segments.slice(2) });
        onClose();
      }
    }
  }

  async function mark_all(): Promise<void> {
    try {
      const result = await markAllNotificationsReadRequest();
      unread = result.unread;
      notifications = notifications.map((entry) => ({ ...entry, read: true }));
      onChanged?.(unread);
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    await load();
  });
</script>

<div class="card shadow-sm notification-drawer">
  <div class="card-header d-flex align-items-center gap-2">
    <strong class="flex-grow-1">{t('ui_notification_title')}</strong>
    {#if unread > 0}
      <span class="badge text-bg-primary">{unread}</span>
      <button class="btn btn-sm btn-outline-secondary" onclick={mark_all}>
        {t('ui_notification_mark_all')}
      </button>
    {/if}
    <button class="btn btn-sm btn-outline-secondary" onclick={onClose}>
      {t('ui_notification_close')}
    </button>
  </div>
  <div class="list-group list-group-flush notification-list">
    {#if loading}
      <div class="p-3 text-center"><div class="spinner-border spinner-border-sm"></div></div>
    {:else if error}
      <div class="alert alert-warning m-2">{t('ui_notification_load_error')}: {error}</div>
    {:else if notifications.length === 0}
      <div class="p-3 text-secondary small">{t('ui_notification_empty')}</div>
    {:else}
      {#each notifications as record (record.id)}
        <button
          class="list-group-item list-group-item-action text-start"
          class:fw-semibold={!record.read}
          onclick={() => open(record)}
        >
          <div class="d-flex gap-2">
            <span class="flex-grow-1">{resolve_args(record)}</span>
            {#if !record.read}<span class="badge text-bg-primary">•</span>{/if}
          </div>
          <small class="text-secondary">
            {new Date(record.created_at * 1000).toLocaleString()}
          </small>
        </button>
      {/each}
    {/if}
  </div>
</div>

<style>
  .notification-drawer {
    position: absolute;
    top: 3.5rem;
    right: 1rem;
    width: min(28rem, calc(100vw - 2rem));
    z-index: 30;
  }
  .notification-list {
    max-height: 60vh;
    overflow-y: auto;
  }
</style>
