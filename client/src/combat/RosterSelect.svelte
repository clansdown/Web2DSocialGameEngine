<script lang="ts">
  /**
   * RosterSelect — fieldable-member toggles for a mission (rts_design.md §4.2).
   *
   * The knight is always fielded and rendered locked; every other fieldable
   * member can be toggled. The parent owns the `selected` array and is notified
   * of changes via `on_change` (the component never mutates its props).
   */
  import { loadTexts } from '../lib/text';
  import type { combat_retinue_member_dto } from './protocol';

  interface Props {
    members: combat_retinue_member_dto[];
    selected: number[];
    on_change?: (selected: number[]) => void;
    disabled?: boolean;
  }

  let { members, selected, on_change, disabled = false }: Props = $props();

  let texts = $state<Record<string, string>>({});

  $effect(() => {
    loadTexts(['combat_roster_title', 'combat_roster_knight_locked', 'combat_roster_note']).then(
      (loaded) => {
        texts = loaded;
      }
    );
  });

  /** Fieldable non-knights — the only members the player may toggle. */
  let selectable = $derived(members.filter((m) => !m.is_knight && m.fieldable));

  /**
   * Returns whether a member is currently selected.
   *
   * @param memberId - The retinue member id
   * @returns boolean
   */
  function is_selected(memberId: number): boolean {
    return selected.includes(memberId);
  }

  /**
   * Toggles a member's fielded state and notifies the parent.
   *
   * @param memberId - The retinue member id
   * @returns void
   */
  function toggle(memberId: number): void {
    if (disabled) return;
    const next = is_selected(memberId)
      ? selected.filter((id) => id !== memberId)
      : [...selected, memberId];
    on_change?.(next);
  }
</script>

<div class="card border-secondary">
  <div class="card-header d-flex justify-content-between align-items-center">
    <span>{texts.combat_roster_title ?? ''}</span>
    <span class="badge text-bg-secondary">{selected.length}/{selectable.length}</span>
  </div>
  <ul class="list-group list-group-flush">
    {#each members as member}
      <li class="list-group-item d-flex justify-content-between align-items-center">
        <span>
          {member.display_name}
          <span class="badge text-bg-dark ms-2">{member.unit_class}</span>
          <span class="text-muted ms-2">Lv {member.level}</span>
        </span>
        {#if member.is_knight}
          <span class="badge text-bg-primary" title={texts.combat_roster_knight_locked}>
            {texts.combat_roster_knight_locked ?? ''}
          </span>
        {:else if !member.fieldable}
          <span class="badge text-bg-warning">{Math.round(member.health)}%</span>
        {:else}
          <button
            class="btn btn-sm {is_selected(member.member_id) ? 'btn-success' : 'btn-outline-secondary'}"
            onclick={() => toggle(member.member_id)}
            {disabled}
          >
            {is_selected(member.member_id) ? '✓' : '+'}
          </button>
        {/if}
      </li>
    {/each}
  </ul>
  <div class="card-footer text-muted small">{texts.combat_roster_note ?? ''}</div>
</div>
