<script lang="ts">
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import {
    getRetinueRequest,
    listRecruitCandidatesRequest,
    hireRecruitRequest,
    setRetinuePriorityRequest,
    getRetinueGearRequest,
    equipGearRequest,
    deassignGearRequest,
    sellItemRequest,
    type retinue_member_dto,
    type retinue_capacity_dto,
    type recruit_market_dto,
    type recruit_candidate_dto,
    type retinue_gear_dto
  } from '../lib/api';

  /**
   * The manor's Retinue panel: roster (health/maintained/fieldable), the named
   * recruit market, and gear (armory equip/sell + general storage sell).
   *
   * @param characterId - The owning character id
   */
  let { characterId }: { characterId: number } = $props();

  const texts = $state<Record<string, string>>({});
  const members = $state<retinue_member_dto[]>([]);
  let capacity = $state<retinue_capacity_dto | null>(null);
  let market = $state<recruit_market_dto | null>(null);
  let gear = $state<retinue_gear_dto | null>(null);
  let activeTab = $state<'roster' | 'recruit' | 'gear'>('roster');
  let errorMsg = $state('');
  let busy = $state(false);
  const equipTarget = $state<Record<number, number>>({});

  const RETINUE_TEXT_IDS = [
    'retinue_title', 'retinue_tab_roster', 'retinue_tab_recruit', 'retinue_tab_gear',
    'retinue_capacity', 'retinue_health', 'retinue_funded', 'retinue_unfunded',
    'retinue_infirmary', 'retinue_hire', 'retinue_fee', 'retinue_expires',
    'retinue_no_candidates', 'retinue_full', 'retinue_armory', 'retinue_storage',
    'retinue_equip', 'retinue_deassign', 'retinue_sell', 'retinue_empty',
    'retinue_error', 'retinue_equipped',
    'retinue_priority_hint', 'retinue_move_up', 'retinue_move_down'
  ];

  async function refresh(): Promise<void> {
    try {
      const [r, m, g] = await Promise.all([
        getRetinueRequest(characterId),
        listRecruitCandidatesRequest(characterId),
        getRetinueGearRequest(characterId)
      ]);
      members.length = 0;
      members.push(...r.members);
      // Display in strict priority order (knight first, then ascending rank).
      members.sort((a, b) => a.priority - b.priority);
      capacity = r.capacity;
      market = m;
      gear = g;
      errorMsg = '';
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    }
  }

  async function hire(c: recruit_candidate_dto): Promise<void> {
    busy = true;
    try {
      await hireRecruitRequest(characterId, c);
      await refresh();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }

  async function equip(armoryId: number, memberId: number): Promise<void> {
    busy = true;
    try {
      await equipGearRequest(characterId, armoryId, memberId);
      await refresh();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }

  async function deassign(memberId: number, slot: string): Promise<void> {
    busy = true;
    try {
      await deassignGearRequest(characterId, memberId, slot);
      await refresh();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }

  async function sellArmory(armoryId: number): Promise<void> {
    busy = true;
    try {
      await sellItemRequest(characterId, 'armory', armoryId, '');
      await refresh();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }

  async function sellStorage(itemId: string): Promise<void> {
    busy = true;
    try {
      await sellItemRequest(characterId, 'storage', 0, itemId);
      await refresh();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Moves a member up/down one rank in the roster (knight stays pinned first)
   * and persists the new order immediately. `members` is kept priority-sorted,
   * so index `dir = -1|+1` is a simple swap followed by a server save.
   */
  async function moveMember(index: number, dir: -1 | 1): Promise<void> {
    const j = index + dir;
    if (index < 0 || j < 0 || j >= members.length) return;
    // The knight is always pinned at index 0 — never move it.
    const a = members[index];
    const b = members[j];
    if (a.is_knight || b.is_knight) return;
    members[index] = b;
    members[j] = a;
    busy = true;
    try {
      const ordered = members.filter((m) => !m.is_knight).map((m) => m.id);
      const res = await setRetinuePriorityRequest(characterId, ordered);
      // Reflect the server-confirmed ranking back onto the local list.
      const rankById = new Map<number, number>(res.members.map((m) => [m.id, m.priority]));
      for (const m of members) {
        const p = rankById.get(m.id);
        if (p !== undefined) m.priority = p;
      }
      members.sort((x, y) => x.priority - y.priority);
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
      await refresh();
    } finally {
      busy = false;
    }
  }

  function formatFee(fee: Record<string, number> | undefined): string {
    if (!fee) return '';
    const parts: string[] = [];
    for (const [res, amt] of Object.entries(fee)) {
      if (amt <= 0) continue;
      parts.push(`${res} ${Math.round(amt)}`);
    }
    return parts.join(', ');
  }

  function equippedSlots(m: retinue_member_dto): string {
    if (!m.equipment || typeof m.equipment !== 'object') return '';
    const eq = m.equipment as Record<string, string>;
    return Object.entries(eq).map(([slot, id]) => `${slot}: ${id}`).join(', ');
  }

  onMount(async () => {
    const loaded = await loadTexts(RETINUE_TEXT_IDS);
    Object.assign(texts, loaded);
    await refresh();
  });
</script>

{#if errorMsg}
  <div class="alert alert-danger small py-1 mb-2">{texts['retinue_error']}: {errorMsg}</div>
{/if}

<ul class="nav nav-tabs small mb-2">
  <li class="nav-item">
    <a class="nav-link {activeTab === 'roster' ? 'active' : ''}" href="#" onclick={(e) => { e.preventDefault(); activeTab = 'roster'; }}>{texts['retinue_tab_roster']}</a>
  </li>
  <li class="nav-item">
    <a class="nav-link {activeTab === 'recruit' ? 'active' : ''}" href="#" onclick={(e) => { e.preventDefault(); activeTab = 'recruit'; }}>{texts['retinue_tab_recruit']}</a>
  </li>
  <li class="nav-item">
    <a class="nav-link {activeTab === 'gear' ? 'active' : ''}" href="#" onclick={(e) => { e.preventDefault(); activeTab = 'gear'; }}>{texts['retinue_tab_gear']}</a>
  </li>
</ul>

{#if activeTab === 'roster'}
  <div class="small mb-2">{texts['retinue_capacity']}: {capacity?.recruited ?? 0}/{capacity?.total ?? 0}</div>
  <div class="small text-muted mb-1">{texts['retinue_priority_hint']}</div>
  {#each members as m, i}
    <div class="border rounded p-1 mb-1 small">
      <div class="d-flex justify-content-between align-items-start">
        <div class="me-2">
          <div class="fw-semibold">{m.display_name}</div>
          <div class="text-muted">{m.unit_class} L{m.level}{m.is_knight ? ' · Knight' : ''}</div>
          <div class="text-muted">{texts['retinue_health']}: {Math.floor(m.health)}%{m.fieldable ? '' : ` · ${texts['retinue_infirmary']}`}</div>
          <div class="text-muted">{m.maintained ? texts['retinue_funded'] : texts['retinue_unfunded']}{equippedSlots(m) ? ` · ${equippedSlots(m)}` : ''}</div>
        </div>
        {#if !m.is_knight}
          <div class="d-flex flex-column gap-1">
            <button class="btn btn-sm btn-outline-secondary py-0 px-1" title={texts['retinue_move_up']} disabled={busy || i <= 1} onclick={() => moveMember(i, -1)}>↑</button>
            <button class="btn btn-sm btn-outline-secondary py-0 px-1" title={texts['retinue_move_down']} disabled={busy || i >= members.length - 1} onclick={() => moveMember(i, 1)}>↓</button>
          </div>
        {/if}
      </div>
    </div>
  {/each}
{/if}

{#if activeTab === 'recruit'}
  {#if market && market.candidates.length === 0}
    <div class="small text-muted">{texts['retinue_no_candidates']}</div>
  {/if}
  {#each market?.candidates ?? [] as c}
    <div class="border rounded p-1 mb-1 small">
      <div class="d-flex justify-content-between">
        <span class="fw-semibold">{c.display_name}</span>
        <span class="text-muted">{c.unit_class} L{c.level} · {c.gender}</span>
      </div>
      <div class="text-muted">{texts['retinue_fee']}: {formatFee(c.fee)}</div>
      <button class="btn btn-sm btn-primary mt-1" disabled={busy || (capacity ? capacity.available <= 0 : false)} onclick={() => hire(c)}>{texts['retinue_hire']}</button>
      {#if c.hour_bucket < (market?.current_bucket ?? 0)}
        <span class="badge text-bg-warning ms-1">{texts['retinue_expires']}</span>
      {/if}
    </div>
  {/each}
  {#if capacity && capacity.available <= 0}
    <div class="small text-warning">{texts['retinue_full']}</div>
  {/if}
{/if}

{#if activeTab === 'gear'}
  <div class="fw-semibold small">{texts['retinue_armory']}</div>
  {#if gear && gear.armory.length === 0}
    <div class="small text-muted">{texts['retinue_empty']}</div>
  {/if}
  {#each gear?.armory ?? [] as a}
    <div class="border rounded p-1 mb-1 small">
      <div class="d-flex justify-content-between">
        <span class="fw-semibold">{a.item?.name ?? a.item_id}</span>
        <span class="text-muted">{a.item?.slot ?? ''}</span>
      </div>
      {#if a.member_id == null}
        <div class="d-flex gap-1 mt-1">
          <select class="form-select form-select-sm" style="max-width: 150px;" bind:value={equipTarget[a.id]}>
            {#each members.filter((m) => !m.is_knight) as m}
              <option value={m.id}>{m.display_name}</option>
            {/each}
          </select>
          <button class="btn btn-sm btn-outline-secondary" disabled={busy} onclick={() => { const mid = equipTarget[a.id]; if (mid) equip(a.id, mid); }}>{texts['retinue_equip']}</button>
          <button class="btn btn-sm btn-outline-danger" disabled={busy} onclick={() => sellArmory(a.id)}>{texts['retinue_sell']}</button>
        </div>
      {:else}
        <span class="badge text-bg-secondary">{texts['retinue_equipped']}: {members.find((m) => m.id === a.member_id)?.display_name ?? a.member_id}</span>
        <button class="btn btn-sm btn-outline-secondary ms-1" disabled={busy} onclick={() => { const slot = a.item?.slot ?? ''; if (a.member_id != null && slot) deassign(a.member_id, slot); }}>{texts['retinue_deassign']}</button>
      {/if}
    </div>
  {/each}

  <div class="fw-semibold small mt-2">{texts['retinue_storage']}</div>
  {#each gear?.storage ?? [] as s}
    <div class="border rounded p-1 mb-1 small d-flex justify-content-between">
      <span>{s.item?.name ?? s.item_id} × {s.count}</span>
      <button class="btn btn-sm btn-outline-danger" disabled={busy} onclick={() => sellStorage(s.item_id)}>{texts['retinue_sell']}</button>
    </div>
  {/each}
{/if}
