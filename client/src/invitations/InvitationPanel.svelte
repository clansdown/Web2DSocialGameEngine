<script lang="ts">
  /**
   * Player Invitations utility. Visible from the hub only while the player has
   * allowance remaining. Lists the player's own codes and generates new ones.
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import {
    createInvitationCodeRequest,
    getInvitationStatusRequest,
    type invitation_code_dto,
    type invitation_status_dto
  } from './invitation_api';

  interface Props {
    onBack: () => void;
    onChanged?: () => void;
  }
  let { onBack, onChanged }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_invitations_title', 'ui_invitations_back', 'ui_invitations_remaining',
    'ui_invitations_daily_limit', 'ui_invitations_lifetime_limit',
    'ui_invitations_generate', 'ui_invitations_empty', 'ui_invitations_used',
    'ui_invitations_unused', 'ui_invitations_revoked', 'ui_invitations_created',
    'ui_invitations_code_created', 'ui_invitations_copy', 'ui_invitations_load_error'
  ];

  let texts = $state<Record<string, string>>({});
  let status = $state<invitation_status_dto | null>(null);
  let loading = $state(true);
  let generating = $state(false);
  let error = $state('');
  let new_code = $state('');

  function t(id: string): string {
    return texts[id] ?? id;
  }

  async function load_status(): Promise<void> {
    loading = true;
    error = '';
    try {
      status = await getInvitationStatusRequest();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
  }

  async function generate(): Promise<void> {
    generating = true;
    error = '';
    try {
      const result = await createInvitationCodeRequest();
      new_code = result.code;
      await load_status();
      onChanged?.();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      generating = false;
    }
  }

  async function copy_code(code: string): Promise<void> {
    try {
      await navigator.clipboard.writeText(code);
    } catch {
      // Clipboard can be unavailable (permissions/insecure context); the code
      // remains selectable in the text field.
    }
  }

  function code_state(code_record: invitation_code_dto): string {
    if (code_record.revoked) return t('ui_invitations_revoked');
    if (code_record.used) return t('ui_invitations_used');
    return t('ui_invitations_unused');
  }

  function code_state_class(code_record: invitation_code_dto): string {
    if (code_record.revoked) return 'text-bg-danger';
    if (code_record.used) return 'text-bg-secondary';
    return 'text-bg-success';
  }

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    await load_status();
  });
</script>

<div class="container py-4" style="max-width: 760px;">
  <div class="d-flex align-items-center gap-2 mb-3">
    <button class="btn btn-sm btn-outline-secondary" onclick={onBack}>
      &larr; {t('ui_invitations_back')}
    </button>
    <h2 class="h4 mb-0 flex-grow-1">{t('ui_invitations_title')}</h2>
  </div>

  {#if error}
    <div class="alert alert-danger py-2">
      {t('ui_invitations_load_error')}: {error}
    </div>
  {/if}

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status"></div>
    </div>
  {:else if status}
    <div class="card mb-3">
      <div class="card-body">
        <div class="row g-3">
          <div class="col-sm-4">
            <div class="small text-secondary">{t('ui_invitations_remaining')}</div>
            <div class="fs-4">{status.daily_remaining} / {status.daily_limit}</div>
          </div>
          <div class="col-sm-4">
            <div class="small text-secondary">{t('ui_invitations_lifetime_limit')}</div>
            <div class="fs-4">{status.lifetime_remaining} / {status.lifetime_limit}</div>
          </div>
          <div class="col-sm-4">
            <div class="small text-secondary">{t('ui_invitations_daily_limit')}</div>
            <div class="fs-4">{status.generated_today}</div>
          </div>
        </div>

        <button
          class="btn btn-primary mt-3"
          onclick={generate}
          disabled={generating || !status.can_generate}
        >
          {#if generating}
            <span class="spinner-border spinner-border-sm me-2" role="status" aria-hidden="true"></span>
          {/if}
          {t('ui_invitations_generate')}
        </button>

        {#if new_code}
          <div class="alert alert-success mt-3 mb-0">
            <div class="small mb-1">{t('ui_invitations_code_created')}</div>
            <div class="d-flex gap-2 align-items-center">
              <code class="fs-5 user-select-all flex-grow-1">{new_code}</code>
              <button class="btn btn-sm btn-outline-success" onclick={() => copy_code(new_code)}>
                {t('ui_invitations_copy')}
              </button>
            </div>
          </div>
        {/if}
      </div>
    </div>

    <h3 class="h6">{t('ui_invitations_created')}</h3>
    {#if status.codes.length === 0}
      <div class="text-secondary py-3">{t('ui_invitations_empty')}</div>
    {:else}
      <div class="list-group">
        {#each status.codes as code_record (code_record.id)}
          <div class="list-group-item">
            <div class="d-flex flex-wrap align-items-center gap-2">
              <code class="fs-6 user-select-all flex-grow-1">{code_record.code}</code>
              <span class="badge {code_state_class(code_record)}">{code_state(code_record)}</span>
            </div>
            <div class="small text-secondary mt-1">
              {new Date(code_record.created_at * 1000).toLocaleString()}
            </div>
          </div>
        {/each}
      </div>
    {/if}
  {/if}
</div>
