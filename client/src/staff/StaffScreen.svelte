<script lang="ts">
  /**
   * Staff console shell. Access is gated server-side by staff capabilities; the
   * UI only hides tools the account cannot use. Grant/revoke is CLI-only, so
   * the staff section is a read-only list.
   *
   * @param onBack - Returns to the hub
   * @param rest - Nested route segments after #/activity/staff
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import { navigate } from '../lib/router';
  import StaffInvitationsPanel from './StaffInvitationsPanel.svelte';
  import type { support_message_dto, support_ticket_dto } from '../support/support_api';
  import {
    staffBanAvatarHashRequest,
    staffCancelConstructionRequest,
    staffCreateSupportTicketRequest,
    staffGetAccountRequest,
    staffGetContextRequest,
    staffGetIdentityHistoryRequest,
    staffGetJournalEntryRequest,
    staffGetSupportTicketRequest,
    staffGrantMonthlyPassRequest,
    staffListAvatarHashBansRequest,
    staffListJournalRequest,
    staffListStaffRequest,
    staffListSupportQueueRequest,
    staffLockIdentityRequest,
    staffPreflightUndoRequest,
    staffRemoveCustomAvatarRequest,
    staffReplySupportTicketRequest,
    staffRevokeMonthlyPassRequest,
    staffSearchAccountsRequest,
    staffSearchBaroniesRequest,
    staffSearchChatsRequest,
    staffSetAdultStatusRequest,
    staffSetBaronyAcceptsMinorsRequest,
    staffSetBaronyIdentityRequest,
    staffSetGamePhaseRequest,
    staffSetMarchProgressRequest,
    staffSetSocialRestrictionRequest,
    staffSetSupportLockRequest,
    staffSetSupportTicketStatusRequest,
    staffUnbanAvatarHashRequest,
    staffUndoRequest,
    staffUnstickGameSessionRequest,
    staffAdjustFiefdomResourceRequest,
    type staff_account_dto,
    type staff_barony_dto,
    type staff_context_dto,
    type staff_journal_entry_dto
  } from './staff_api';

  interface Props {
    onBack: () => void;
    rest?: string[];
  }
  let { onBack, rest = [] }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_staff_title', 'ui_staff_back', 'ui_staff_accounts', 'ui_staff_baronies',
    'ui_staff_support', 'ui_staff_audit', 'ui_staff_unstick', 'ui_staff_chats',
    'ui_staff_search', 'ui_staff_search_placeholder', 'ui_staff_account', 'ui_staff_username',
    'ui_staff_adult', 'ui_staff_social_restricted', 'ui_staff_support_locked', 'ui_staff_save',
    'ui_staff_reason', 'ui_staff_name', 'ui_staff_avatar', 'ui_staff_lock_name',
    'ui_staff_lock_avatar', 'ui_staff_revert_name', 'ui_staff_revert_avatar',
    'ui_staff_remove_custom_avatar', 'ui_staff_ban_hash', 'ui_staff_unban_hash',
    'ui_staff_hash', 'ui_staff_undo', 'ui_staff_preflight', 'ui_staff_status',
    'ui_staff_no_results', 'ui_staff_support_queue', 'ui_staff_ticket', 'ui_staff_reply',
    'ui_staff_internal_note', 'ui_staff_set_status', 'ui_staff_assign',
    'ui_staff_unstick_session', 'ui_staff_set_phase', 'ui_staff_set_march_progress',
    'ui_staff_cancel_construction', 'ui_staff_adjust_resource', 'ui_staff_character_id',
    'ui_staff_fiefdom_id', 'ui_staff_building_id', 'ui_staff_resource', 'ui_staff_value',
    'ui_staff_mini_game', 'ui_staff_next_level', 'ui_staff_apply', 'ui_staff_chat_search',
    'ui_staff_open_chat', 'ui_staff_journal', 'ui_staff_reversible', 'ui_staff_undo_action',
    'ui_staff_action_type', 'ui_staff_target', 'ui_staff_actor', 'ui_staff_created',
    'ui_staff_roles', 'ui_staff_loading', 'ui_staff_error', 'ui_staff_saved',
    'ui_staff_create_ticket', 'ui_staff_ticket_subject', 'ui_staff_ticket_body',
    'ui_staff_cancel', 'ui_staff_safe_name_id', 'ui_staff_safe_tag_ids',
    'ui_staff_accepts_minors', 'ui_staff_identity_history', 'ui_staff_moderation_mode',
    'ui_staff_invitations', 'ui_staff_pass', 'ui_staff_pass_active',
    'ui_staff_pass_permanent', 'ui_staff_pass_expired', 'ui_staff_pass_inactive',
    'ui_staff_pass_until', 'ui_staff_pass_month', 'ui_staff_pass_months', 'ui_staff_pass_add_months',
    'ui_staff_pass_make_permanent', 'ui_staff_pass_remove',
    'ui_staff_pass_history', 'ui_staff_pass_event_grant',
    'ui_staff_pass_event_revoke', 'ui_staff_pass_event_restore',
    'ui_staff_pass_event_unknown', 'ui_staff_pass_source_staff_grant',
    'ui_staff_pass_source_staff_revoke', 'ui_staff_pass_source_staff_undo',
    'ui_staff_pass_source_purchase', 'ui_staff_pass_source_promo',
    'ui_staff_pass_source_unknown'
  ];

  let texts = $state<Record<string, string>>({});
  let context = $state<staff_context_dto | null>(null);
  let loading = $state(true);
  let error = $state('');
  let info = $state('');
  let section = $state('accounts');
  let query = $state('');

  let accounts = $state<staff_account_dto[]>([]);
  let selected_account = $state<staff_account_dto | null>(null);
  let identity_history = $state<Record<number, { id: number; kind: string; value: string; changed_at: number }[]>>({});
  let hash_bans = $state<{ sha256: string; reason: string; created_at: number; revoked_at: number }[]>([]);
  let hash_value = $state('');
  let reason = $state('');
  let pass_months = $state<Record<number, number>>({});

  let baronies = $state<staff_barony_dto[]>([]);
  let selected_barony = $state<staff_barony_dto | null>(null);
  let barony_safe_name = $state('');
  let barony_safe_tags = $state('');
  let barony_name = $state('');
  let barony_description = $state('');

  let tickets = $state<support_ticket_dto[]>([]);
  let selected_ticket = $state<support_ticket_dto | null>(null);
  let ticket_messages = $state<support_message_dto[]>([]);
  let ticket_reply = $state('');
  let ticket_internal = $state(false);
  let new_ticket_account = $state(0);
  let new_ticket_subject = $state('');
  let new_ticket_body = $state('');
  let new_ticket_category = $state('other');

  let journal = $state<staff_journal_entry_dto[]>([]);
  let selected_action = $state<staff_journal_entry_dto | null>(null);
  let preflight = $state<{ status: string; reason: string } | null>(null);
  let staff_members = $state<{ account_id: number; username: string; roles: string[]; revoked_at: number }[]>([]);

  let unstick_character = $state(0);
  let unstick_phase = $state('sandbox');
  let unstick_mini_game = $state('tower_defense');
  let unstick_next_level = $state(1);
  let unstick_building = $state(0);
  let unstick_fiefdom = $state(0);
  let unstick_resource = $state('gold');
  let unstick_value = $state(0);

  let chat_query = $state('');
  let chat_results = $state<{ id: string; type: string; name: string; direct_key: string }[]>([]);

  function t(id: string): string {
    return texts[id] ?? id;
  }

  function has(capability: string): boolean {
    return context?.capabilities.includes(capability) ?? false;
  }

  function section_from_route(): void {
    if (rest.length > 0) {
      const first = rest[0];
      if (['accounts', 'baronies', 'support', 'audit', 'unstick', 'chats', 'invitations'].includes(first)) {
        section = first;
      }
      if (first === 'accounts' && rest[1]) void open_account(Number(rest[1]));
      if (first === 'baronies' && rest[1]) void open_barony(Number(rest[1]));
      if (first === 'support' && rest[1]) void open_ticket(rest[1]);
      if (first === 'audit' && rest[1]) void open_action(rest[1]);
    }
  }

  function go_section(next: string): void {
    navigate({ type: 'activity', id: 'staff', rest: [next] });
  }

  async function load_context(): Promise<void> {
    try {
      context = await staffGetContextRequest();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function search_accounts(): Promise<void> {
    error = '';
    try {
      const result = await staffSearchAccountsRequest(query);
      accounts = result.accounts;
      selected_account = null;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function open_account(account_id: number): Promise<void> {
    if (!account_id) return;
    try {
      selected_account = await staffGetAccountRequest(account_id);
      identity_history = {};
      hash_bans = (await staffListAvatarHashBansRequest(true)).bans;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function load_identity_history(character_id: number): Promise<void> {
    try {
      identity_history = {
        ...identity_history,
        [character_id]: (await staffGetIdentityHistoryRequest(character_id)).history
      };
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function set_adult(adult: boolean): Promise<void> {
    if (!selected_account) return;
    await run(async () =>
      staffSetAdultStatusRequest(selected_account!.account_id, adult, reason)
    );
    await open_account(selected_account.account_id);
  }

  async function set_social(restricted: boolean): Promise<void> {
    if (!selected_account) return;
    await run(async () =>
      staffSetSocialRestrictionRequest(selected_account!.account_id, restricted, reason)
    );
    await open_account(selected_account.account_id);
  }

  async function set_support_lock(locked: boolean): Promise<void> {
    if (!selected_account) return;
    await run(async () =>
      staffSetSupportLockRequest(selected_account!.account_id, locked, reason)
    );
    await open_account(selected_account.account_id);
  }

  function pass_month_value(character_id: number): number {
    return pass_months[character_id] ?? 1;
  }

  function set_pass_month_value(character_id: number, value: number): void {
    const bounded = Math.max(1, Math.min(1200, Math.trunc(value) || 1));
    pass_months = { ...pass_months, [character_id]: bounded };
  }

  function pass_event_label(event: string): string {
    if (event === 'grant') return t('ui_staff_pass_event_grant');
    if (event === 'revoke') return t('ui_staff_pass_event_revoke');
    if (event === 'restore') return t('ui_staff_pass_event_restore');
    return t('ui_staff_pass_event_unknown');
  }

  function pass_source_label(source: string): string {
    if (source === 'staff_grant') return t('ui_staff_pass_source_staff_grant');
    if (source === 'staff_revoke') return t('ui_staff_pass_source_staff_revoke');
    if (source === 'staff_undo') return t('ui_staff_pass_source_staff_undo');
    if (source === 'purchase') return t('ui_staff_pass_source_purchase');
    if (source === 'promo') return t('ui_staff_pass_source_promo');
    return t('ui_staff_pass_source_unknown');
  }

  async function grant_pass_months(character_id: number): Promise<void> {
    await run(async () =>
      staffGrantMonthlyPassRequest(character_id, {
        months: pass_month_value(character_id),
        reason
      })
    );
    if (selected_account) await open_account(selected_account.account_id);
  }

  async function make_pass_permanent(character_id: number): Promise<void> {
    await run(async () =>
      staffGrantMonthlyPassRequest(character_id, { permanent: true, reason })
    );
    if (selected_account) await open_account(selected_account.account_id);
  }

  async function revoke_pass(character_id: number): Promise<void> {
    await run(async () => staffRevokeMonthlyPassRequest(character_id, reason));
    if (selected_account) await open_account(selected_account.account_id);
  }

  async function lock_identity(
    character_id: number,
    options: { lock_name?: boolean; lock_avatar?: boolean; revert_name?: boolean; revert_avatar?: boolean }
  ): Promise<void> {
    await run(async () => staffLockIdentityRequest(character_id, { ...options, reason }));
    if (selected_account) await open_account(selected_account.account_id);
  }

  async function remove_custom_avatar(character_id: number): Promise<void> {
    await run(async () => staffRemoveCustomAvatarRequest(character_id, reason));
    if (selected_account) await open_account(selected_account.account_id);
  }

  async function ban_hash(): Promise<void> {
    await run(async () => staffBanAvatarHashRequest(hash_value.trim(), reason));
    hash_value = '';
    hash_bans = (await staffListAvatarHashBansRequest(true)).bans;
  }

  async function unban_hash(sha256: string): Promise<void> {
    await run(async () => staffUnbanAvatarHashRequest(sha256, reason));
    hash_bans = (await staffListAvatarHashBansRequest(true)).bans;
  }

  async function search_baronies(): Promise<void> {
    error = '';
    try {
      baronies = (await staffSearchBaroniesRequest(query)).baronies;
      selected_barony = null;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function open_barony(id: number): Promise<void> {
    if (!id) return;
    selected_barony = baronies.find((entry) => entry.id === id) ?? null;
    if (selected_barony) {
      barony_safe_name = selected_barony.safe_name_id;
      barony_safe_tags = selected_barony.safe_tag_ids.join(',');
      barony_name = selected_barony.name;
      barony_description = selected_barony.description;
    }
  }

  async function save_barony(): Promise<void> {
    if (!selected_barony) return;
    const tags = barony_safe_tags.split(',').map((tag) => tag.trim()).filter((tag) => tag.length > 0);
    await run(async () =>
      staffSetBaronyIdentityRequest(
        selected_barony!.id,
        barony_safe_name.trim(),
        tags,
        barony_name.trim(),
        barony_description,
        reason
      )
    );
    await search_baronies();
  }

  async function set_barony_accepts(accepts: boolean): Promise<void> {
    if (!selected_barony) return;
    await run(async () =>
      staffSetBaronyAcceptsMinorsRequest(selected_barony!.id, accepts, reason)
    );
    await search_baronies();
  }

  async function load_support_queue(): Promise<void> {
    error = '';
    try {
      tickets = (await staffListSupportQueueRequest({ query })).tickets;
      selected_ticket = null;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function open_ticket(ticket_id: string): Promise<void> {
    if (!ticket_id) return;
    try {
      const result = await staffGetSupportTicketRequest(ticket_id);
      selected_ticket = result.ticket;
      ticket_messages = result.messages;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function reply_ticket(): Promise<void> {
    if (!selected_ticket || !ticket_reply.trim()) return;
    await run(async () =>
      staffReplySupportTicketRequest(selected_ticket!.id, ticket_reply.trim(), ticket_internal)
    );
    ticket_reply = '';
    await open_ticket(selected_ticket.id);
  }

  async function set_ticket_status(status: string): Promise<void> {
    if (!selected_ticket) return;
    await run(async () => staffSetSupportTicketStatusRequest(selected_ticket!.id, status));
    await open_ticket(selected_ticket.id);
  }

  async function create_ticket(): Promise<void> {
    await run(async () =>
      staffCreateSupportTicketRequest(
        new_ticket_account,
        new_ticket_subject,
        new_ticket_body,
        new_ticket_category
      )
    );
    new_ticket_subject = '';
    new_ticket_body = '';
    await load_support_queue();
  }

  async function load_journal(): Promise<void> {
    error = '';
    try {
      journal = (await staffListJournalRequest({ query })).entries;
      selected_action = null;
      preflight = null;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function load_staff_members(): Promise<void> {
    error = '';
    try {
      staff_members = (await staffListStaffRequest()).staff;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function open_action(action_id: string): Promise<void> {
    try {
      const result = await staffGetJournalEntryRequest(action_id);
      selected_action = result.entry;
      preflight = result.preflight;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function undo_action(): Promise<void> {
    if (!selected_action) return;
    await run(async () => staffUndoRequest(selected_action!.id, reason));
    await open_action(selected_action.id);
  }

  async function preflight_action(): Promise<void> {
    if (!selected_action) return;
    try {
      preflight = await staffPreflightUndoRequest(selected_action.id);
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function run(fn: () => Promise<unknown>): Promise<void> {
    error = '';
    info = '';
    try {
      await fn();
      info = t('ui_staff_saved');
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  async function unstick_session(): Promise<void> {
    await run(async () => staffUnstickGameSessionRequest(unstick_character, reason));
  }

  async function set_phase(): Promise<void> {
    await run(async () => staffSetGamePhaseRequest(unstick_character, unstick_phase, reason));
  }

  async function set_progress(): Promise<void> {
    await run(async () =>
      staffSetMarchProgressRequest(unstick_character, unstick_mini_game, unstick_next_level, reason)
    );
  }

  async function cancel_construction(): Promise<void> {
    await run(async () => staffCancelConstructionRequest(unstick_building, reason));
  }

  async function adjust_resource(): Promise<void> {
    await run(async () =>
      staffAdjustFiefdomResourceRequest(unstick_fiefdom, unstick_resource, unstick_value, reason)
    );
  }

  async function search_chats(): Promise<void> {
    error = '';
    try {
      chat_results = (await staffSearchChatsRequest(chat_query)).chats;
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  function open_chat(chat_id: string): void {
    navigate({ type: 'activity', id: 'chat', rest: [chat_id] });
  }

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    await load_context();
    if (has('account.view')) await search_accounts();
    loading = false;
    section_from_route();
  });

  $effect(() => {
    const route_key = rest.join('/');
    if (route_key.length === 0) return;
    const first = rest[0];
    if (['accounts', 'baronies', 'support', 'audit', 'unstick', 'chats', 'invitations'].includes(first)) {
      section = first;
    }
    if (first === 'accounts' && rest[1] && selected_account?.account_id !== Number(rest[1])) {
      void open_account(Number(rest[1]));
    }
    if (first === 'support' && rest[1] && selected_ticket?.id !== rest[1]) {
      void open_ticket(rest[1]);
    }
    if (first === 'audit' && rest[1] && selected_action?.id !== rest[1]) {
      void open_action(rest[1]);
    }
  });

  $effect(() => {
    if (section === 'baronies' && baronies.length === 0) void search_baronies();
    if (section === 'support' && tickets.length === 0) void load_support_queue();
    if (section === 'audit' && journal.length === 0) void load_journal();
    if (section === 'stafflist' && staff_members.length === 0) void load_staff_members();
    if (section === 'accounts') void load_context();
  });
</script>

<div class="container-fluid py-3 staff-shell">
  <div class="d-flex align-items-center gap-2 mb-3">
    <button class="btn btn-sm btn-outline-secondary" onclick={onBack}>&larr; {t('ui_staff_back')}</button>
    <h2 class="h4 mb-0 flex-grow-1">{t('ui_staff_title')}</h2>
    {#if context}
      <span class="badge text-bg-dark">{context.roles.join(', ')}</span>
    {/if}
  </div>

  {#if error}<div class="alert alert-danger py-2">{t('ui_staff_error')}: {error}</div>{/if}
  {#if info}<div class="alert alert-success py-2">{info}</div>{/if}

  {#if loading}
    <div class="d-flex justify-content-center py-5"><div class="spinner-border"></div></div>
  {:else if !context?.is_staff}
    <div class="alert alert-warning">{t('ui_staff_error')}</div>
  {:else}
    <ul class="nav nav-tabs mb-3">
      {#if has('account.view')}<li class="nav-item"><button class="nav-link" class:active={section === 'accounts'} onclick={() => go_section('accounts')}>{t('ui_staff_accounts')}</button></li>{/if}
      {#if has('barony.view')}<li class="nav-item"><button class="nav-link" class:active={section === 'baronies'} onclick={() => go_section('baronies')}>{t('ui_staff_baronies')}</button></li>{/if}
      {#if has('support.ticket.view')}<li class="nav-item"><button class="nav-link" class:active={section === 'support'} onclick={() => go_section('support')}>{t('ui_staff_support')}</button></li>{/if}
      {#if context?.invitations_enabled && has('registration.invites.view')}<li class="nav-item"><button class="nav-link" class:active={section === 'invitations'} onclick={() => go_section('invitations')}>{t('ui_staff_invitations')}</button></li>{/if}
      {#if has('staff.audit.read')}<li class="nav-item"><button class="nav-link" class:active={section === 'audit'} onclick={() => go_section('audit')}>{t('ui_staff_audit')}</button></li>{/if}
      {#if has('staff.audit.read')}<li class="nav-item"><button class="nav-link" class:active={section === 'stafflist'} onclick={() => go_section('stafflist')}>{t('ui_staff_roles')}</button></li>{/if}
      <li class="nav-item"><button class="nav-link" class:active={section === 'unstick'} onclick={() => go_section('unstick')}>{t('ui_staff_unstick')}</button></li>
      {#if has('chat.moderate_any')}<li class="nav-item"><button class="nav-link" class:active={section === 'chats'} onclick={() => go_section('chats')}>{t('ui_staff_chats')}</button></li>{/if}
    </ul>

    <div class="mb-3">
      <label class="form-label small" for="staff-reason">{t('ui_staff_reason')}</label>
      <input id="staff-reason" class="form-control form-control-sm" bind:value={reason} />
    </div>

    {#if section === 'accounts'}
      <div class="input-group mb-3">
        <input class="form-control" placeholder={t('ui_staff_search_placeholder')} bind:value={query} onkeydown={(e) => { if (e.key === 'Enter') search_accounts(); }} />
        <button class="btn btn-outline-secondary" onclick={search_accounts}>{t('ui_staff_search')}</button>
      </div>
      {#if selected_account}
        <div class="card mb-3">
          <div class="card-body">
            <h3 class="h5">{selected_account.username} <small class="text-secondary">#{selected_account.account_id}</small></h3>
            <div class="d-flex flex-wrap gap-3 my-2">
              <label class="form-check"><input class="form-check-input" type="checkbox" checked={selected_account.adult} onchange={(e) => set_adult(e.currentTarget.checked)} /><span class="form-check-label">{t('ui_staff_adult')}</span></label>
              <label class="form-check"><input class="form-check-input" type="checkbox" checked={selected_account.social_restricted} onchange={(e) => set_social(e.currentTarget.checked)} /><span class="form-check-label">{t('ui_staff_social_restricted')}</span></label>
              <label class="form-check"><input class="form-check-input" type="checkbox" checked={selected_account.support_locked} onchange={(e) => set_support_lock(e.currentTarget.checked)} /><span class="form-check-label">{t('ui_staff_support_locked')}</span></label>
            </div>
            <h4 class="h6 mt-3">{t('ui_staff_account')}</h4>
            {#each selected_account.characters ?? [] as character (character.id)}
              <div class="border rounded p-2 mb-2">
                <div class="d-flex flex-wrap gap-2 align-items-center">
                  <strong class="flex-grow-1">{character.safe_display_name} <small class="text-secondary">#{character.id}</small></strong>
                  <button class="btn btn-sm btn-outline-secondary" onclick={() => load_identity_history(character.id)}>{t('ui_staff_identity_history')}</button>
                </div>
                <div class="d-flex flex-wrap gap-1 mt-2">
                  <button class="btn btn-sm btn-outline-primary" onclick={() => lock_identity(character.id, { lock_name: !character.name_locked })}>{t('ui_staff_lock_name')}</button>
                  <button class="btn btn-sm btn-outline-primary" onclick={() => lock_identity(character.id, { lock_avatar: !character.avatar_locked })}>{t('ui_staff_lock_avatar')}</button>
                  <button class="btn btn-sm btn-outline-secondary" onclick={() => lock_identity(character.id, { revert_name: true })}>{t('ui_staff_revert_name')}</button>
                  <button class="btn btn-sm btn-outline-secondary" onclick={() => lock_identity(character.id, { revert_avatar: true })}>{t('ui_staff_revert_avatar')}</button>
                  {#if character.has_custom_avatar}
                    <button class="btn btn-sm btn-outline-danger" onclick={() => remove_custom_avatar(character.id)}>{t('ui_staff_remove_custom_avatar')}</button>
                  {/if}
                </div>
                <div class="border-top mt-2 pt-2">
                  <div class="d-flex flex-wrap align-items-center gap-2">
                    <strong class="small">{t('ui_staff_pass')}:</strong>
                    {#if character.monthly_pass?.permanent}
                      <span class="badge text-bg-success">{t('ui_staff_pass_permanent')}</span>
                    {:else if character.monthly_pass?.active}
                      <span class="badge text-bg-info">{t('ui_staff_pass_active')}</span>
                      <span class="small text-secondary">{t('ui_staff_pass_until')} {new Date((character.monthly_pass?.expires_at ?? 0) * 1000).toLocaleDateString()}</span>
                    {:else if (character.monthly_pass?.expires_at ?? 0) > 0}
                      <span class="badge text-bg-secondary">{t('ui_staff_pass_expired')}</span>
                      <span class="small text-secondary">{t('ui_staff_pass_until')} {new Date((character.monthly_pass?.expires_at ?? 0) * 1000).toLocaleDateString()}</span>
                    {:else}
                      <span class="badge text-bg-secondary">{t('ui_staff_pass_inactive')}</span>
                    {/if}
                  </div>
                  <div class="d-flex flex-wrap align-items-center gap-2 mt-2">
                    <input
                      class="form-control form-control-sm"
                      style="max-width: 7rem;"
                      type="number"
                      min="1"
                      max="1200"
                      value={pass_month_value(character.id)}
                      oninput={(e) => set_pass_month_value(character.id, Number(e.currentTarget.value))}
                      disabled={character.monthly_pass?.permanent}
                    />
                    <button class="btn btn-sm btn-outline-primary" onclick={() => grant_pass_months(character.id)} disabled={character.monthly_pass?.permanent}>{t('ui_staff_pass_add_months')}</button>
                    <button class="btn btn-sm btn-outline-success" onclick={() => make_pass_permanent(character.id)} disabled={character.monthly_pass?.permanent}>{t('ui_staff_pass_make_permanent')}</button>
                    <button class="btn btn-sm btn-outline-danger" onclick={() => revoke_pass(character.id)} disabled={!character.monthly_pass?.active}>{t('ui_staff_pass_remove')}</button>
                  </div>
                  {#if (character.monthly_pass?.events ?? []).length > 0}
                    <div class="small mt-2">
                      <div class="text-secondary">{t('ui_staff_pass_history')}</div>
                      <ul class="mb-0">
                        {#each character.monthly_pass?.events ?? [] as event (event.id)}
                          <li>
                            {new Date(event.created_at * 1000).toLocaleString()} · {pass_event_label(event.event)} · {pass_source_label(event.source)}
                            {#if event.permanent}
                              · {t('ui_staff_pass_permanent')}
                            {:else if event.months > 0}
                              · {event.months} {event.months === 1 ? t('ui_staff_pass_month') : t('ui_staff_pass_months')}
                            {/if}
                            {#if event.expires_at > 0}
                              · {t('ui_staff_pass_until')} {new Date(event.expires_at * 1000).toLocaleDateString()}
                            {/if}
                            {#if event.reason}
                              · {event.reason}
                            {/if}
                          </li>
                        {/each}
                      </ul>
                    </div>
                  {/if}
                </div>
                {#if (identity_history[character.id] ?? []).length > 0}
                  <ul class="small mt-2 mb-0">
                    {#each identity_history[character.id] ?? [] as entry (entry.id)}
                      <li>{entry.kind}: {entry.value} <span class="text-secondary">{new Date(entry.changed_at * 1000).toLocaleString()}</span></li>
                    {/each}
                  </ul>
                {/if}
              </div>
            {/each}
          </div>
        </div>
      {:else}
        <div class="list-group mb-3">
          {#each accounts as account (account.account_id)}
            <button class="list-group-item list-group-item-action" onclick={() => navigate({ type: 'activity', id: 'staff', rest: ['accounts', String(account.account_id)] })}>
              <strong>{account.username}</strong> <small class="text-secondary">#{account.account_id}</small>
              {#if account.social_restricted}<span class="badge text-bg-warning ms-2">{t('ui_staff_social_restricted')}</span>{/if}
              {#if account.support_locked}<span class="badge text-bg-danger ms-2">{t('ui_staff_support_locked')}</span>{/if}
              <div class="small text-secondary">
                {#each account.characters ?? [] as character, index (character.id)}
                  {character.safe_display_name}{#if index < (account.characters?.length ?? 0) - 1}, {/if}
                {/each}
              </div>
            </button>
          {/each}
          {#if accounts.length === 0}<div class="p-3 text-secondary">{t('ui_staff_no_results')}</div>{/if}
        </div>
      {/if}
      <div class="card">
        <div class="card-body">
          <h3 class="h6">{t('ui_staff_ban_hash')}</h3>
          <div class="input-group mb-2">
            <input class="form-control form-control-sm" placeholder={t('ui_staff_hash')} bind:value={hash_value} />
            <button class="btn btn-sm btn-outline-danger" onclick={ban_hash}>{t('ui_staff_ban_hash')}</button>
          </div>
          {#each hash_bans as ban (ban.sha256)}
            <div class="d-flex gap-2 small border-top py-1">
              <span class="text-truncate flex-grow-1">{ban.sha256}</span>
              {#if ban.revoked_at === 0}
                <button class="btn btn-sm btn-link p-0" onclick={() => unban_hash(ban.sha256)}>{t('ui_staff_unban_hash')}</button>
              {/if}
            </div>
          {/each}
        </div>
      </div>
    {:else if section === 'baronies'}
      <div class="input-group mb-3">
        <input class="form-control" placeholder={t('ui_staff_search_placeholder')} bind:value={query} onkeydown={(e) => { if (e.key === 'Enter') search_baronies(); }} />
        <button class="btn btn-outline-secondary" onclick={search_baronies}>{t('ui_staff_search')}</button>
      </div>
      {#if selected_barony}
        <div class="card mb-3"><div class="card-body">
          <h3 class="h5">{selected_barony.name}</h3>
          <label class="form-label small" for="barony-safe-name">{t('ui_staff_safe_name_id')}</label>
          <input id="barony-safe-name" class="form-control form-control-sm mb-2" bind:value={barony_safe_name} />
          <label class="form-label small" for="barony-safe-tags">{t('ui_staff_safe_tag_ids')}</label>
          <input id="barony-safe-tags" class="form-control form-control-sm mb-2" bind:value={barony_safe_tags} />
          <label class="form-label small" for="barony-name">{t('ui_staff_name')}</label>
          <input id="barony-name" class="form-control form-control-sm mb-2" bind:value={barony_name} />
          <label class="form-label small" for="barony-description">{t('ui_staff_ticket_body')}</label>
          <textarea id="barony-description" class="form-control form-control-sm mb-2" rows="3" bind:value={barony_description}></textarea>
          <label class="form-check"><input class="form-check-input" type="checkbox" checked={selected_barony.accepts_minors} onchange={(e) => set_barony_accepts(e.currentTarget.checked)} /><span class="form-check-label">{t('ui_staff_accepts_minors')}</span></label>
          <button class="btn btn-primary mt-2" onclick={save_barony}>{t('ui_staff_save')}</button>
        </div></div>
      {:else}
        <div class="list-group">
          {#each baronies as barony (barony.id)}
            <button class="list-group-item list-group-item-action" onclick={() => navigate({ type: 'activity', id: 'staff', rest: ['baronies', String(barony.id)] })}>
              <strong>{barony.name}</strong> <small class="text-secondary">#{barony.id} · {barony.owner_name} · {barony.member_count}</small>
              {#if barony.defunct_at > 0}<span class="badge text-bg-secondary ms-2">defunct</span>{/if}
            </button>
          {/each}
          {#if baronies.length === 0}<div class="p-3 text-secondary">{t('ui_staff_no_results')}</div>{/if}
        </div>
      {/if}
    {:else if section === 'support'}
      <div class="input-group mb-3">
        <input class="form-control" placeholder={t('ui_staff_search_placeholder')} bind:value={query} onkeydown={(e) => { if (e.key === 'Enter') load_support_queue(); }} />
        <button class="btn btn-outline-secondary" onclick={load_support_queue}>{t('ui_staff_search')}</button>
      </div>
      {#if selected_ticket}
        <div class="card mb-3"><div class="card-body">
          <h3 class="h5">{selected_ticket.subject}</h3>
          <div class="small text-secondary mb-2">
            {selected_ticket.username} · {selected_ticket.category} · {selected_ticket.status}
          </div>
          {#each ticket_messages as message (message.id)}
            <div class="border rounded p-2 mb-2" class:border-primary={message.is_internal}>
              <div class="small text-secondary">{message.author_kind}{#if message.is_internal} · {t('ui_staff_internal_note')}{/if} · {new Date(message.created_at * 1000).toLocaleString()}</div>
              <div class="preserve-lines">{message.body}</div>
            </div>
          {/each}
          <textarea class="form-control mb-2" rows="3" bind:value={ticket_reply}></textarea>
          <div class="d-flex flex-wrap gap-2 align-items-center">
            <label class="form-check"><input class="form-check-input" type="checkbox" bind:checked={ticket_internal} /><span class="form-check-label">{t('ui_staff_internal_note')}</span></label>
            <button class="btn btn-primary" onclick={reply_ticket}>{t('ui_staff_reply')}</button>
            <select class="form-select form-select-sm w-auto" onchange={(e) => set_ticket_status(e.currentTarget.value)}>
              <option value="">{t('ui_staff_set_status')}</option>
              {#each ['open','awaiting_staff','awaiting_player','resolved','closed'] as status (status)}<option value={status}>{status}</option>{/each}
            </select>
          </div>
        </div></div>
      {:else}
        <div class="list-group mb-3">
          {#each tickets as ticket (ticket.id)}
            <button class="list-group-item list-group-item-action" onclick={() => navigate({ type: 'activity', id: 'staff', rest: ['support', ticket.id] })}>
              <strong>{ticket.subject}</strong> <small class="text-secondary">#{ticket.id} · {ticket.username} · {ticket.status}</small>
              {#if ticket.unread_for_staff > 0}<span class="badge text-bg-primary ms-2">{ticket.unread_for_staff}</span>{/if}
            </button>
          {/each}
          {#if tickets.length === 0}<div class="p-3 text-secondary">{t('ui_staff_no_results')}</div>{/if}
        </div>
        <div class="card"><div class="card-body">
          <h4 class="h6">{t('ui_staff_create_ticket')}</h4>
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_account')} bind:value={new_ticket_account} />
          <input class="form-control form-control-sm mb-2" placeholder={t('ui_staff_ticket_subject')} bind:value={new_ticket_subject} />
          <textarea class="form-control form-control-sm mb-2" rows="3" placeholder={t('ui_staff_ticket_body')} bind:value={new_ticket_body}></textarea>
          <button class="btn btn-primary" onclick={create_ticket}>{t('ui_staff_create_ticket')}</button>
        </div></div>
      {/if}
    {:else if section === 'invitations'}
      <StaffInvitationsPanel
        can_generate={has('registration.invites.generate')}
        can_revoke={has('registration.invites.revoke')}
        {reason}
      />
    {:else if section === 'audit'}
      <div class="input-group mb-3">
        <input class="form-control" placeholder={t('ui_staff_search_placeholder')} bind:value={query} onkeydown={(e) => { if (e.key === 'Enter') load_journal(); }} />
        <button class="btn btn-outline-secondary" onclick={load_journal}>{t('ui_staff_search')}</button>
      </div>
      {#if selected_action}
        <div class="card mb-3"><div class="card-body">
          <h3 class="h5">{selected_action.action_type}</h3>
          <div class="small text-secondary mb-2">#{selected_action.id} · {selected_action.actor_type} #{selected_action.actor_account_id} · {selected_action.status}</div>
          <div class="preserve-lines small">{JSON.stringify(selected_action.after_state, null, 2)}</div>
          {#if preflight}<div class="alert alert-info py-2 mt-2">{t('ui_staff_preflight')}: {preflight.status} {preflight.reason}</div>{/if}
          <div class="d-flex gap-2 mt-2">
            <button class="btn btn-sm btn-outline-secondary" onclick={preflight_action}>{t('ui_staff_preflight')}</button>
            {#if selected_action.reversible && selected_action.status === 'applied'}
              <button class="btn btn-sm btn-danger" onclick={undo_action}>{t('ui_staff_undo_action')}</button>
            {/if}
          </div>
        </div></div>
      {:else}
        <div class="list-group">
          {#each journal as entry (entry.id)}
            <button class="list-group-item list-group-item-action" onclick={() => navigate({ type: 'activity', id: 'staff', rest: ['audit', entry.id] })}>
              <strong>{entry.action_type}</strong>
              <small class="text-secondary"> · {entry.actor_type} #{entry.actor_account_id} · {entry.status} · {new Date(entry.created_at * 1000).toLocaleString()}</small>
              <div class="small">{entry.target_type} #{entry.target_id} {entry.reason}</div>
            </button>
          {/each}
          {#if journal.length === 0}<div class="p-3 text-secondary">{t('ui_staff_no_results')}</div>{/if}
        </div>
      {/if}
    {:else if section === 'stafflist'}
      <div class="list-group">
        {#each staff_members as member (member.account_id)}
          <div class="list-group-item">
            <strong>{member.username}</strong> <small class="text-secondary">#{member.account_id}</small>
            <span class="badge text-bg-dark ms-2">{member.roles.join(', ')}</span>
            {#if member.revoked_at > 0}<span class="badge text-bg-secondary ms-2">revoked</span>{/if}
          </div>
        {/each}
        {#if staff_members.length === 0}<div class="p-3 text-secondary">{t('ui_staff_no_results')}</div>{/if}
      </div>
    {:else if section === 'unstick'}
      <div class="row g-3">
        <div class="col-md-6"><div class="card h-100"><div class="card-body">
          <h4 class="h6">{t('ui_staff_unstick_session')}</h4>
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_character_id')} bind:value={unstick_character} />
          <button class="btn btn-sm btn-primary" onclick={unstick_session}>{t('ui_staff_apply')}</button>
        </div></div></div>
        <div class="col-md-6"><div class="card h-100"><div class="card-body">
          <h4 class="h6">{t('ui_staff_set_phase')}</h4>
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_character_id')} bind:value={unstick_character} />
          <select class="form-select form-select-sm mb-2" bind:value={unstick_phase}>
            {#each ['initial_mission','land_patent','baron_track','baron_right','freehold','freehold_qualified','sandbox'] as phase (phase)}<option value={phase}>{phase}</option>{/each}
          </select>
          <button class="btn btn-sm btn-primary" onclick={set_phase}>{t('ui_staff_apply')}</button>
        </div></div></div>
        <div class="col-md-6"><div class="card h-100"><div class="card-body">
          <h4 class="h6">{t('ui_staff_set_march_progress')}</h4>
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_character_id')} bind:value={unstick_character} />
          <input class="form-control form-control-sm mb-2" placeholder={t('ui_staff_mini_game')} bind:value={unstick_mini_game} />
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_next_level')} bind:value={unstick_next_level} />
          <button class="btn btn-sm btn-primary" onclick={set_progress}>{t('ui_staff_apply')}</button>
        </div></div></div>
        <div class="col-md-6"><div class="card h-100"><div class="card-body">
          <h4 class="h6">{t('ui_staff_cancel_construction')}</h4>
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_building_id')} bind:value={unstick_building} />
          <button class="btn btn-sm btn-primary" onclick={cancel_construction}>{t('ui_staff_apply')}</button>
        </div></div></div>
        <div class="col-md-6"><div class="card h-100"><div class="card-body">
          <h4 class="h6">{t('ui_staff_adjust_resource')}</h4>
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_fiefdom_id')} bind:value={unstick_fiefdom} />
          <input class="form-control form-control-sm mb-2" placeholder={t('ui_staff_resource')} bind:value={unstick_resource} />
          <input class="form-control form-control-sm mb-2" type="number" placeholder={t('ui_staff_value')} bind:value={unstick_value} />
          <button class="btn btn-sm btn-primary" onclick={adjust_resource}>{t('ui_staff_apply')}</button>
        </div></div></div>
      </div>
    {:else if section === 'chats'}
      <div class="input-group mb-3">
        <input class="form-control" placeholder={t('ui_staff_chat_search')} bind:value={chat_query} onkeydown={(e) => { if (e.key === 'Enter') search_chats(); }} />
        <button class="btn btn-outline-secondary" onclick={search_chats}>{t('ui_staff_search')}</button>
      </div>
      <div class="list-group">
        {#each chat_results as chat (chat.id)}
          <button class="list-group-item list-group-item-action" onclick={() => open_chat(chat.id)}>
            <strong>{chat.name || chat.id}</strong> <small class="text-secondary">{chat.type} · #{chat.id}</small>
            <span class="badge text-bg-primary ms-2">{t('ui_staff_open_chat')}</span>
          </button>
        {/each}
      </div>
    {/if}
  {/if}
</div>

<style>
  .staff-shell { max-width: 72rem; }
  .preserve-lines { white-space: pre-wrap; word-break: break-word; }
  .nav-link { cursor: pointer; }
</style>
