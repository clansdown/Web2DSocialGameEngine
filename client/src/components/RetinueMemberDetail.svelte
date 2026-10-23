<script lang="ts">
  /**
   * RetinueMemberDetail is the per-member management view: identity, health
   * status (with a recovery estimate), priority reordering, equipped gear, and
   * the armory/storage actions for this member. It is opened from the
   * management grid and hosts all member management so the grid stays compact.
   *
   * @param member - The member being managed
   * @param members - The full roster (priority order) for move-button bounds
   * @param gear - The fiefdom armory + storage snapshot
   * @param recovery - Recovery parameters for the hours-to-heal estimate
   * @param busy - True while a server mutation is in flight
   * @param on_back - Return to the management grid
   * @param on_move - Reorder this member by -1 (up) or +1 (down)
   * @param on_equip - Equip an armory item to this member
   * @param on_deassign - Return an equipped slot to the armory
   * @param on_sell_armory - Sell an armory item
   * @param on_sell_storage - Sell a storage item
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import RetinuePortrait from './RetinuePortrait.svelte';
  import type { retinue_member_dto, retinue_gear_dto, retinue_recovery_dto } from '../lib/api';

  interface Props {
    member: retinue_member_dto;
    members: retinue_member_dto[];
    gear: retinue_gear_dto | null;
    recovery: retinue_recovery_dto | null;
    busy: boolean;
    on_back: () => void;
    on_move: (member_id: number, dir: -1 | 1) => void;
    on_equip: (armory_id: number, member_id: number) => void;
    on_deassign: (member_id: number, slot: string) => void;
    on_sell_armory: (armory_id: number) => void;
    on_sell_storage: (item_id: string) => void;
  }

  let {
    member,
    members,
    gear,
    recovery,
    busy,
    on_back,
    on_move,
    on_equip,
    on_deassign,
    on_sell_armory,
    on_sell_storage
  }: Props = $props();

  const TEXT_IDS: string[] = [
    'retinue_back', 'retinue_health', 'retinue_status_ready', 'retinue_status_infirmary',
    'retinue_recovery_estimate', 'retinue_funded', 'retinue_unfunded',
    'retinue_move_up', 'retinue_move_down', 'retinue_equipped', 'retinue_deassign',
    'retinue_armory', 'retinue_storage', 'retinue_equip', 'retinue_sell', 'retinue_empty',
    'retinue_priority_hint'
  ];

  const texts = $state<Record<string, string>>({});

  onMount(async () => {
    Object.assign(texts, await loadTexts(TEXT_IDS));
  });

  const equipped = $derived(gear ? gear.armory.filter((a) => a.member_id === member.id) : []);
  const unassigned = $derived(gear ? gear.armory.filter((a) => a.member_id == null) : []);

  const member_index = $derived(members.findIndex((m) => m.id === member.id));

  // Estimated whole hours until full health at the current infirmary rate.
  const hours_remaining = $derived.by((): number => {
    if (!recovery || member.health >= 100) return 0;
    const rate = (100 / recovery.max_recovery_hours) * (recovery.multiplier || 1);
    if (rate <= 0) return 0;
    return Math.ceil((100 - member.health) / rate);
  });

  const equipped_slots = $derived(
    equipped
      .map((a) => ({ id: a.id, slot: a.item?.slot ?? a.item_id }))
      .filter((entry) => entry.slot.length > 0)
  );
</script>

<div class="manager-detail">
  <button class="btn btn-sm btn-outline-secondary mb-2" onclick={on_back}>
    ← {texts['retinue_back'] || 'Back'}
  </button>

  <div class="d-flex gap-3 align-items-start">
    <RetinuePortrait
      portrait_url={member.portrait_url}
      fallback_class={member.unit_class}
      variant="management"
      size={128}
    />
    <div class="flex-grow-1 small">
      <div class="fw-semibold fs-5">{member.display_name}</div>
      <div class="text-muted">{member.unit_class} L{member.level}{member.is_knight ? ' · Knight' : ''} · {member.gender}</div>

      <div class="mt-2">
        <div class="d-flex justify-content-between">
          <span>{texts['retinue_health'] || 'Health'}: {Math.floor(member.health)}%</span>
          <span class="badge {member.fieldable ? 'text-bg-success' : 'text-bg-warning'}">
            {member.fieldable ? (texts['retinue_status_ready'] || 'Ready') : (texts['retinue_status_infirmary'] || 'Infirmary')}
          </span>
        </div>
        <div class="progress" style="height: 8px;">
          <div
            class="progress-bar {member.health < 50 ? 'bg-danger' : (member.health < 100 ? 'bg-warning' : 'bg-success')}"
            style="width: {Math.max(0, Math.min(100, member.health))}%;"
          ></div>
        </div>
        {#if hours_remaining > 0}
          <div class="text-muted mt-1">{texts['retinue_recovery_estimate'] || 'Recovers in'} ~{hours_remaining}h</div>
        {/if}
      </div>

      <div class="text-muted mt-1">{member.maintained ? (texts['retinue_funded'] || 'Funded') : (texts['retinue_unfunded'] || 'Unfunded')}</div>

      {#if !member.is_knight}
        <div class="d-flex gap-1 mt-2">
          <button
            class="btn btn-sm btn-outline-secondary py-0 px-2"
            title={texts['retinue_move_up'] || 'Up'}
            disabled={busy || member_index <= 1}
            onclick={() => on_move(member.id, -1)}
          >↑</button>
          <button
            class="btn btn-sm btn-outline-secondary py-0 px-2"
            title={texts['retinue_move_down'] || 'Down'}
            disabled={busy || member_index < 0 || member_index >= members.length - 1}
            onclick={() => on_move(member.id, 1)}
          >↓</button>
          <span class="text-muted align-self-center small">{texts['retinue_priority_hint'] || ''}</span>
        </div>
      {/if}
    </div>
  </div>

  <div class="mt-3">
    <div class="fw-semibold small">{texts['retinue_equipped'] || 'Equipped'}</div>
    {#if equipped_slots.length === 0}
      <div class="small text-muted">{texts['retinue_empty'] || 'None'}</div>
    {/if}
    {#each equipped_slots as entry}
      <div class="border rounded p-1 mb-1 small d-flex justify-content-between align-items-center">
        <span>{member.display_name} · {entry.slot}</span>
        <button class="btn btn-sm btn-outline-secondary py-0" disabled={busy} onclick={() => on_deassign(member.id, entry.slot)}>
          {texts['retinue_deassign'] || 'Remove'}
        </button>
      </div>
    {/each}
  </div>

  <div class="mt-3">
    <div class="fw-semibold small">{texts['retinue_armory'] || 'Armory'}</div>
    {#if unassigned.length === 0}
      <div class="small text-muted">{texts['retinue_empty'] || 'Empty'}</div>
    {/if}
    {#each unassigned as a}
      <div class="border rounded p-1 mb-1 small d-flex justify-content-between align-items-center">
        <span>{a.item?.name ?? a.item_id} <span class="text-muted">({a.item?.slot ?? ''})</span></span>
        <span class="d-flex gap-1">
          <button class="btn btn-sm btn-outline-secondary py-0" disabled={busy} onclick={() => on_equip(a.id, member.id)}>
            {texts['retinue_equip'] || 'Equip'}
          </button>
          <button class="btn btn-sm btn-outline-danger py-0" disabled={busy} onclick={() => on_sell_armory(a.id)}>
            {texts['retinue_sell'] || 'Sell'}
          </button>
        </span>
      </div>
    {/each}
  </div>

  <div class="mt-3">
    <div class="fw-semibold small">{texts['retinue_storage'] || 'Storage'}</div>
    {#if !gear || gear.storage.length === 0}
      <div class="small text-muted">{texts['retinue_empty'] || 'Empty'}</div>
    {/if}
    {#each gear?.storage ?? [] as s}
      <div class="border rounded p-1 mb-1 small d-flex justify-content-between align-items-center">
        <span>{s.item?.name ?? s.item_id} × {s.count}</span>
        <button class="btn btn-sm btn-outline-danger py-0" disabled={busy} onclick={() => on_sell_storage(s.item_id)}>
          {texts['retinue_sell'] || 'Sell'}
        </button>
      </div>
    {/each}
  </div>
</div>

<style>
  .manager-detail {
    /* Bootstrap handles the layout; nothing custom needed beyond a little
       breathing room for the sticky-feeling back button. */
    padding-bottom: 1rem;
  }
</style>
