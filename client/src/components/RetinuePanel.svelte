<script lang="ts">
  /**
   * The manor's Retinue panel. Two tabs:
   *  - Management: a grid of ALL members (portrait, name, class/level, health
   *    status). Selecting one opens RetinueMemberDetail, which hosts equipment,
   *    priority and armory/storage actions.
   *  - Recruit: the named hire market, shown as portrait cards with class/level
   *    as text underneath.
   *
   * @param characterId - The owning character id
   */
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
    type retinue_recovery_dto,
    type recruit_market_dto,
    type recruit_candidate_dto,
    type retinue_gear_dto
  } from '../lib/api';
  import RetinuePortrait from './RetinuePortrait.svelte';
  import RetinueMemberDetail from './RetinueMemberDetail.svelte';

  let { characterId }: { characterId: number } = $props();

  const texts = $state<Record<string, string>>({});
  const members = $state<retinue_member_dto[]>([]);
  let capacity = $state<retinue_capacity_dto | null>(null);
  let recovery = $state<retinue_recovery_dto | null>(null);
  let market = $state<recruit_market_dto | null>(null);
  let gear = $state<retinue_gear_dto | null>(null);
  let activeTab = $state<'management' | 'recruit'>('management');
  let selected_member_id = $state<number | null>(null);
  let errorMsg = $state('');
  let busy = $state(false);

  const RETINUE_TEXT_IDS = [
    'retinue_title', 'retinue_tab_management', 'retinue_tab_recruit',
    'retinue_capacity', 'retinue_health', 'retinue_funded', 'retinue_unfunded',
    'retinue_infirmary', 'retinue_hire', 'retinue_fee', 'retinue_expires',
    'retinue_no_candidates', 'retinue_full', 'retinue_empty',
    'retinue_error', 'retinue_status_ready', 'retinue_status_infirmary'
  ];

  const selected_member = $derived(
    selected_member_id == null ? null : (members.find((m) => m.id === selected_member_id) ?? null)
  );

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
      recovery = r.recovery;
      market = m;
      gear = g;
      // Drop a selection that no longer exists.
      if (selected_member_id != null && !members.some((mm) => mm.id === selected_member_id)) {
        selected_member_id = null;
      }
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
   * and persists the new order immediately. `members` is priority-sorted, so
   * index `dir = -1|+1` is a simple swap followed by a server save.
   */
  async function moveMember(memberId: number, dir: -1 | 1): Promise<void> {
    const index = members.findIndex((m) => m.id === memberId);
    const j = index + dir;
    if (index < 0 || j < 0 || j >= members.length) return;
    const a = members[index];
    const b = members[j];
    if (a.is_knight || b.is_knight) return;
    members[index] = b;
    members[j] = a;
    busy = true;
    try {
      const ordered = members.filter((m) => !m.is_knight).map((m) => m.id);
      const res = await setRetinuePriorityRequest(characterId, ordered);
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
    <a class="nav-link {activeTab === 'management' ? 'active' : ''}" href="#" onclick={(e) => { e.preventDefault(); activeTab = 'management'; }}>{texts['retinue_tab_management']}</a>
  </li>
  <li class="nav-item">
    <a class="nav-link {activeTab === 'recruit' ? 'active' : ''}" href="#" onclick={(e) => { e.preventDefault(); activeTab = 'recruit'; }}>{texts['retinue_tab_recruit']}</a>
  </li>
</ul>

{#if activeTab === 'management'}
  {#if selected_member}
    <RetinueMemberDetail
      member={selected_member}
      {members}
      {gear}
      {recovery}
      {busy}
      on_back={() => { selected_member_id = null; }}
      on_move={moveMember}
      on_equip={equip}
      on_deassign={deassign}
      on_sell_armory={sellArmory}
      on_sell_storage={sellStorage}
    />
  {:else}
    <div class="small mb-2">{texts['retinue_capacity']}: {capacity?.recruited ?? 0}/{capacity?.total ?? 0}</div>
    <div class="row g-2">
      {#each members as m (m.id)}
        <div class="col-6 col-sm-4">
          <div
            class="border rounded p-2 h-100 d-flex gap-2 align-items-start"
            role="button"
            tabindex="0"
            style="cursor: pointer;"
            onclick={() => { selected_member_id = m.id; }}
            onkeydown={(e) => { if (e.key === 'Enter') selected_member_id = m.id; }}
          >
            <RetinuePortrait
              portrait_url={m.portrait_url}
              fallback_class={m.unit_class}
              variant="management"
              size={64}
            />
            <div class="small flex-grow-1" style="min-width: 0;">
              <div class="fw-semibold text-truncate">{m.display_name}</div>
              <div class="text-muted">{m.unit_class} L{m.level}{m.is_knight ? ' · K' : ''}</div>
              <div class="progress mt-1" style="height: 6px;">
                <div
                  class="progress-bar {m.health < 50 ? 'bg-danger' : (m.health < 100 ? 'bg-warning' : 'bg-success')}"
                  style="width: {Math.max(0, Math.min(100, m.health))}%;"
                ></div>
              </div>
              <div class="text-muted">
                {Math.floor(m.health)}%{m.fieldable ? '' : ` · ${texts['retinue_infirmary']}`}
              </div>
            </div>
          </div>
        </div>
      {/each}
    </div>
  {/if}
{/if}

{#if activeTab === 'recruit'}
  {#if market && market.candidates.length === 0}
    <div class="small text-muted">{texts['retinue_no_candidates']}</div>
  {/if}
  <div class="row g-2">
    {#each market?.candidates ?? [] as c (c.unit_class + ':' + c.display_name + ':' + c.hour_bucket)}
      <div class="col-6 col-sm-4">
        <div class="border rounded p-2 h-100 text-center">
          <RetinuePortrait
            portrait_url={`/images/retinue/headshots/${c.headshot_id}`}
            fallback_class={c.unit_class}
            variant="recruit"
            size={96}
          />
          <div class="fw-semibold small text-truncate mt-1">{c.display_name}</div>
          <div class="text-muted small">{c.unit_class} L{c.level} · {c.gender}</div>
          <div class="text-muted small">{texts['retinue_fee']}: {formatFee(c.fee)}</div>
          {#if c.hour_bucket < (market?.current_bucket ?? 0)}
            <span class="badge text-bg-warning">{texts['retinue_expires']}</span>
          {/if}
          <button
            class="btn btn-sm btn-primary mt-1"
            disabled={busy || (capacity ? capacity.available <= 0 : false)}
            onclick={() => hire(c)}
          >{texts['retinue_hire']}</button>
        </div>
      </div>
    {/each}
  </div>
  {#if capacity && capacity.available <= 0}
    <div class="small text-warning mt-2">{texts['retinue_full']}</div>
  {/if}
{/if}
