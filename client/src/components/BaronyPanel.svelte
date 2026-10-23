<script lang="ts">
  /**
   * Shows the character's barony (name, standing, member count) and lets them
   * leave it, becoming a freeholder of an Outlying Freehold. A founder must pick
   * a successor (another member) unless they are the last member, in which case
   * the barony is tombstoned. Leaving stamps a join cooldown.
   */
  import { currentCharacter, playerGameState } from '../lib/stores';
  import { getBaronyMembersRequest, leaveBaronyRequest, type BaronyMember } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';
  import DialogOverlay from './DialogOverlay.svelte';

  interface Props {
    onLeft: () => void;
    onBack: () => void;
  }

  let { onLeft, onBack }: Props = $props();

  let texts = $state<Record<string, string>>({});
  let members = $state<BaronyMember[]>([]);
  let successorId = $state<number | null>(null);
  let loadingMembers = $state(false);
  let leaving = $state(false);
  let showConfirm = $state(false);

  const TEXT_IDS: string[] = [
    'ui_barony_title', 'ui_barony_role', 'ui_barony_members', 'ui_barony_leave',
    'ui_barony_leave_confirm', 'ui_barony_successor', 'ui_barony_defunct_warning',
    'ui_barony_role_mesne_lord', 'ui_barony_role_baron', 'ui_barony_role_member',
    'ui_confirm', 'ui_cancel', 'ui_back_button'
  ];

  const barony = $derived($playerGameState?.barony ?? null);
  const isFounder = $derived(barony?.role === 'mesne_lord' || barony?.role === 'baron');
  const needsSuccessor = $derived(!!barony && isFounder && barony.member_count > 1);

  $effect(() => {
    loadTexts(TEXT_IDS).then(t => { texts = t; }).catch(() => {});
  });

  /**
   * Returns the localized label for a barony role.
   *
   * @param role - 'mesne_lord' | 'baron' | 'member'
   * @returns string - Localized role label
   */
  function roleLabel(role: string): string {
    if (role === 'mesne_lord') return texts['ui_barony_role_mesne_lord'] || 'Mesne Lord';
    if (role === 'baron') return texts['ui_barony_role_baron'] || 'Baron';
    return texts['ui_barony_role_member'] || 'Member';
  }

  /**
   * Opens the leave confirmation, fetching the member list when a founder must
   * choose a successor.
   */
  async function openConfirm(): Promise<void> {
    showConfirm = true;
    successorId = null;
    members = [];
    if (!barony || !needsSuccessor) return;
    loadingMembers = true;
    try {
      const creds = auth.getInMemoryCredentials();
      const token = auth.getSessionToken();
      if (creds && token) {
        const all = await getBaronyMembersRequest(barony.id, { username: creds.username, token });
        members = all.filter(m => m.character_id !== $currentCharacter?.id);
      }
    } catch (e) {
      handleError('Failed to load members', e);
    } finally {
      loadingMembers = false;
    }
  }

  /**
   * Leaves the barony. A founder with other members must have chosen a successor.
   */
  async function leave(): Promise<void> {
    if (!$currentCharacter || leaving) return;
    if (needsSuccessor && successorId === null) return;
    leaving = true;
    try {
      const creds = auth.getInMemoryCredentials();
      const token = auth.getSessionToken();
      if (!creds || !token) {
        handleError('Not authenticated', new Error('No session'));
        return;
      }
      await leaveBaronyRequest($currentCharacter.id, needsSuccessor ? successorId : null, {
        username: creds.username,
        token
      });
      showConfirm = false;
      onLeft();
    } catch (e) {
      handleError('Failed to leave barony', e);
    } finally {
      leaving = false;
    }
  }
</script>

<div class="container py-4">
  <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
    &larr; {texts['ui_back_button'] || 'Back'}
  </button>

  <h2 class="mb-4" style="font-family: serif;">{texts['ui_barony_title'] || 'Your Barony'}</h2>

  {#if !barony}
    <div class="alert alert-warning">You are not a member of a barony.</div>
  {:else}
    <div class="card mb-4">
      <div class="card-body">
        <h4 class="card-title mb-3">{barony.name}</h4>
        <p class="mb-1">
          <span class="text-muted">{texts['ui_barony_role'] || 'Your standing'}:</span>
          {roleLabel(barony.role)}
        </p>
        <p class="mb-0">
          <span class="text-muted">{texts['ui_barony_members'] || 'Members'}:</span>
          {barony.member_count}
        </p>
      </div>
    </div>

    <button class="btn btn-outline-danger" onclick={openConfirm}>
      {texts['ui_barony_leave'] || 'Leave the Barony'}
    </button>
  {/if}
</div>

{#if showConfirm && barony}
  <DialogOverlay
    title={texts['ui_barony_leave'] || 'Leave the Barony'}
    onDismiss={() => (showConfirm = false)}
    hideContinue
  >
    <p style="font-size: 1.05rem; line-height: 1.6; font-family: serif;">
      {texts['ui_barony_leave_confirm'] || ''}
    </p>

    {#if needsSuccessor}
      <div class="mb-3">
        <label for="successor" class="form-label">
          {texts['ui_barony_successor'] || 'Name your successor as baron'}
        </label>
        {#if loadingMembers}
          <div class="spinner-border spinner-border-sm" role="status"></div>
        {:else}
          <select id="successor" class="form-select" bind:value={successorId}>
            <option value={null}>—</option>
            {#each members as member}
              <option value={member.character_id}>{member.display_name}</option>
            {/each}
          </select>
        {/if}
      </div>
    {:else if isFounder}
      <div class="alert alert-warning py-2">
        {texts['ui_barony_defunct_warning'] || ''}
      </div>
    {/if}

    <div class="d-flex justify-content-end gap-2">
      <button class="btn btn-outline-secondary" onclick={() => (showConfirm = false)} disabled={leaving}>
        {texts['ui_cancel'] || 'Cancel'}
      </button>
      <button
        class="btn btn-danger"
        onclick={leave}
        disabled={leaving || (needsSuccessor && successorId === null)}
      >
        {#if leaving}
          <span class="spinner-border spinner-border-sm me-1" role="status"></span>
        {/if}
        {texts['ui_barony_leave'] || 'Leave the Barony'}
      </button>
    </div>
  </DialogOverlay>
{/if}
