<script lang="ts">
  /**
   * Staff Invitations tab. Lists every plaintext invitation code (staff-only),
   * generates new ones in invite_only mode, and revokes unused codes.
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import {
    staffCreateInvitationCodeRequest,
    staffListInvitationCodesRequest,
    staffRevokeInvitationCodeRequest,
    type staff_invitation_code_dto
  } from './staff_api';

  interface Props {
    can_generate: boolean;
    can_revoke: boolean;
    reason: string;
    on_changed?: () => void;
  }
  let { can_generate, can_revoke, reason, on_changed }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_staff_invitations', 'ui_staff_invitations_generate',
    'ui_staff_invitations_revoke', 'ui_staff_invitations_code',
    'ui_staff_invitations_creator', 'ui_staff_invitations_status',
    'ui_staff_invitations_used_by', 'ui_staff_invitations_search',
    'ui_staff_no_results', 'ui_staff_error', 'ui_invitations_used',
    'ui_invitations_unused', 'ui_invitations_revoked', 'ui_invitations_code_created',
    'ui_invitations_all'
  ];

  let texts = $state<Record<string, string>>({});
  let codes = $state<staff_invitation_code_dto[]>([]);
  let mode = $state('off');
  let loading = $state(false);
  let error = $state('');
  let info = $state('');
  let status_filter = $state('all');
  let query = $state('');
  let new_code = $state('');

  function t(id: string): string {
    return texts[id] ?? id;
  }

  function code_status(code: staff_invitation_code_dto): string {
    if (code.revoked) return t('ui_invitations_revoked');
    if (code.used) return t('ui_invitations_used');
    return t('ui_invitations_unused');
  }

  function code_status_class(code: staff_invitation_code_dto): string {
    if (code.revoked) return 'text-bg-danger';
    if (code.used) return 'text-bg-secondary';
    return 'text-bg-success';
  }

  async function load_codes(): Promise<void> {
    loading = true;
    error = '';
    try {
      const result = await staffListInvitationCodesRequest({
        status: status_filter,
        query,
        limit: 200
      });
      codes = result.codes;
      mode = result.mode;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
  }

  async function generate(): Promise<void> {
    error = '';
    info = '';
    try {
      const result = await staffCreateInvitationCodeRequest(reason);
      new_code = result.code;
      info = `${t('ui_invitations_code_created')}: ${result.code}`;
      await load_codes();
      on_changed?.();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function revoke(code: staff_invitation_code_dto): Promise<void> {
    error = '';
    info = '';
    try {
      await staffRevokeInvitationCodeRequest(code.id, reason);
      info = `${t('ui_staff_invitations_revoke')}: ${code.code}`;
      await load_codes();
      on_changed?.();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    await load_codes();
  });
</script>

<div>
  {#if error}<div class="alert alert-danger py-2">{t('ui_staff_error')}: {error}</div>{/if}
  {#if info}<div class="alert alert-success py-2">{info}</div>{/if}

  <div class="d-flex flex-wrap gap-2 align-items-center mb-3">
    <select class="form-select form-select-sm" style="max-width: 180px;" bind:value={status_filter} onchange={load_codes}>
      <option value="all">{t('ui_invitations_all')}</option>
      <option value="unused">{t('ui_invitations_unused')}</option>
      <option value="used">{t('ui_invitations_used')}</option>
      <option value="revoked">{t('ui_invitations_revoked')}</option>
    </select>
    <input
      class="form-control form-control-sm"
      style="max-width: 260px;"
      placeholder={t('ui_staff_invitations_search')}
      bind:value={query}
      onkeydown={(e) => { if (e.key === 'Enter') load_codes(); }}
    />
    <button class="btn btn-sm btn-outline-secondary" onclick={load_codes}>
      {t('ui_staff_invitations_search')}
    </button>
    {#if can_generate && mode === 'invite_only'}
      <button class="btn btn-sm btn-primary ms-auto" onclick={generate}>
        {t('ui_staff_invitations_generate')}
      </button>
    {/if}
  </div>

  {#if new_code}
    <div class="alert alert-success">
      <div class="small">{t('ui_invitations_code_created')}</div>
      <code class="fs-5 user-select-all">{new_code}</code>
    </div>
  {/if}

  {#if loading}
    <div class="d-flex justify-content-center py-4"><div class="spinner-border"></div></div>
  {:else if codes.length === 0}
    <div class="text-secondary py-3">{t('ui_staff_no_results')}</div>
  {:else}
    <div class="table-responsive">
      <table class="table table-sm align-middle">
        <thead>
          <tr>
            <th>{t('ui_staff_invitations_code')}</th>
            <th>{t('ui_staff_invitations_status')}</th>
            <th>{t('ui_staff_invitations_creator')}</th>
            <th>{t('ui_staff_invitations_used_by')}</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {#each codes as code (code.id)}
            <tr>
              <td><code class="user-select-all">{code.code}</code></td>
              <td><span class="badge {code_status_class(code)}">{code_status(code)}</span></td>
              <td>
                {code.created_by_username || code.created_by_account_id}
                <span class="badge text-bg-dark ms-1">{code.created_by_kind}</span>
              </td>
              <td>
                {#if code.used}
                  {code.used_by_username || code.used_by_account_id}
                {:else}
                  <span class="text-secondary">—</span>
                {/if}
              </td>
              <td class="text-end">
                {#if can_revoke && !code.used && !code.revoked}
                  <button class="btn btn-sm btn-outline-danger" onclick={() => revoke(code)}>
                    {t('ui_staff_invitations_revoke')}
                  </button>
                {/if}
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
  {/if}
</div>
