<script lang="ts">
  /**
   * MissionPick — browse the auto-generated PvE mission offerings (grouped by
   * tier: solo / duo / company), choose a roster, and create a lobby; or join an
   * existing match by code (rts_design.md §3.1/§4.1/§4.3).
   *
   * An offering's content is a pure function of its id and is fixed the moment
   * the lobby is created (D62). The board refreshes hourly; a completed offering
   * is consumed. Loadout agency is the roster step: the knight is always fielded
   * and the player toggles the rest.
   */
  import { loadTexts } from '../lib/text';
  import { currentCharacter } from '../lib/stores';
  import {
    combatCreateRequest,
    combatJoinRequest,
    combatListRequest,
    getCombatOfferingsRequest,
    getRetinueRequest,
    handle_combat_error
  } from '../lib/api';
  import type { combat_offering_dto, combat_tier_dto, combat_match_summary_dto } from '../lib/api';
  import type { combat_retinue_member_dto } from './protocol';
  import RosterSelect from './RosterSelect.svelte';
  import StorePanel from './StorePanel.svelte';

  interface Props {
    on_joined: (matchId: string) => void;
    on_back: () => void;
  }

  let { on_joined, on_back }: Props = $props();

  let tiers = $state<combat_tier_dto[]>([]);
  let offerings = $state<combat_offering_dto[]>([]);
  let open_matches = $state<combat_match_summary_dto[]>([]);
  let selected_offering = $state<combat_offering_dto | null>(null);
  let roster = $state<combat_retinue_member_dto[]>([]);
  let selected_ids = $state<number[]>([]);
  let join_code = $state('');
  let busy = $state(false);
  let error = $state('');
  let store_open = $state(false);
  let texts = $state<Record<string, string>>({});

  $effect(() => {
    loadTexts([
      'combat_mission_title',
      'combat_mission_hint',
      'combat_mission_units_label',
      'combat_mission_choose_btn',
      'combat_mission_muster_btn',
      'combat_mission_back_btn',
      'combat_mission_join_heading',
      'combat_mission_code_label',
      'combat_mission_join_btn',
      'combat_mission_open_heading',
      'combat_mission_no_open',
      'combat_mission_enter_btn',
      'combat_mission_loading',
      'combat_mission_cancel_btn',
      'combat_mission_store_btn',
      'combat_tier_solo',
      'combat_tier_duo',
      'combat_tier_company',
      'combat_danger_trivial',
      'combat_danger_easy',
      'combat_danger_fair',
      'combat_danger_hard',
      'combat_danger_deadly',
      'combat_offering_reward_label',
      'combat_offering_map_label',
      'combat_offering_danger_label',
      'combat_offering_empty'
    ]).then((loaded) => {
      texts = loaded;
    });
    void load_world();
  });

  /**
   * The display label for a tier id (falls back to the raw id).
   *
   * @param tier_id - The tier id ("solo" | "duo" | "company")
   * @returns string - The localized tier label
   */
  function tier_label(tier_id: string): string {
    return texts[`combat_tier_${tier_id}`] ?? tier_id;
  }

  /**
   * The display label for a danger band id (falls back to the raw id).
   *
   * @param band - The danger band id
   * @returns string - The localized danger label
   */
  function danger_label(band: string): string {
    return texts[`combat_danger_${band}`] ?? band;
  }

  /**
   * The offerings belonging to one tier, in board order.
   *
   * @param tier_id - The tier id to filter by
   * @returns combat_offering_dto[] - Matching offerings
   */
  function offerings_for_tier(tier_id: string): combat_offering_dto[] {
    return offerings.filter((o) => o.tier === tier_id);
  }

  /**
   * Loads the character's current offering board and the list of open lobbies.
   *
   * @param none
   * @returns Promise<void>
   */
  async function load_world(): Promise<void> {
    if (!$currentCharacter) return;
    try {
      const board = await getCombatOfferingsRequest($currentCharacter.id);
      tiers = board.tiers ?? [];
      offerings = board.offerings ?? [];
    } catch (e) {
      error = handle_combat_error(e);
    }
    try {
      const list = await combatListRequest();
      open_matches = list.matches ?? [];
    } catch {
      // The lobby browser is best-effort; joining by code still works.
      open_matches = [];
    }
  }

  /**
   * Selects an offering and loads the player's fieldable roster for it.
   *
   * @param offering - The chosen offering
   * @returns Promise<void>
   */
  async function choose_offering(offering: combat_offering_dto): Promise<void> {
    if (!$currentCharacter) return;
    error = '';
    try {
      const result = await getRetinueRequest($currentCharacter.id);
      roster = result.members
        .filter((m) => m.health >= 0)
        .map((m) => ({
          member_id: m.id,
          display_name: m.display_name,
          unit_class: m.unit_class,
          level: m.level,
          is_knight: m.is_knight,
          health: m.health,
          fieldable: m.fieldable
        }));
      selected_ids = roster.filter((m) => !m.is_knight && m.fieldable).map((m) => m.member_id);
      selected_offering = offering;
    } catch (e) {
      error = handle_combat_error(e);
    }
  }

  /**
   * Creates the match from the selected offering and roster (the host's roster
   * subset is applied before the lobby opens).
   *
   * @param none
   * @returns Promise<void>
   */
  async function muster(): Promise<void> {
    if (!$currentCharacter || !selected_offering || busy) return;
    busy = true;
    error = '';
    try {
      const result = await combatCreateRequest($currentCharacter.id, {
        mode: 'pve',
        offering_id: selected_offering.id,
        unit_ids: selected_ids
      });
      on_joined(result.match_id);
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Joins an existing lobby by its shareable code.
   *
   * @param none
   * @returns Promise<void>
   */
  async function join_by_code(): Promise<void> {
    if (!$currentCharacter || busy) return;
    const code = join_code.trim().toUpperCase();
    if (!code) return;
    busy = true;
    error = '';
    try {
      const result = await combatJoinRequest($currentCharacter.id, code);
      on_joined(result.match_id);
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }
</script>

{#if store_open && $currentCharacter}
  <StorePanel character_id={$currentCharacter.id} on_close={() => (store_open = false)} />
{:else}
<div class="container py-4">
  <div class="d-flex justify-content-between mb-4">
    <button class="btn btn-outline-secondary" onclick={on_back}>
      &larr; {texts.combat_mission_back_btn ?? ''}
    </button>
    {#if $currentCharacter}
      <button class="btn btn-outline-info" onclick={() => (store_open = true)}>
        {texts.combat_mission_store_btn ?? ''}
      </button>
    {/if}
  </div>

  <h2 class="mb-1">{texts.combat_mission_title ?? ''}</h2>
  <p class="text-muted">{texts.combat_mission_hint ?? ''}</p>

  {#if error}
    <div class="alert alert-danger py-2">{error}</div>
  {/if}

  <div class="row g-4">
    <div class="col-lg-7">
      {#each tiers as tier}
        {#if offerings_for_tier(tier.id).length > 0}
          <h5 class="mt-3 mb-2">{tier_label(tier.id)}</h5>
          {#each offerings_for_tier(tier.id) as offering}
            <div
              class="card mb-3 {selected_offering?.id === offering.id ? 'border-primary' : 'border-secondary'}"
            >
              <div class="card-body">
                <div class="d-flex justify-content-between align-items-start">
                  <div>
                    <span class="badge text-bg-danger">
                      {texts.combat_offering_danger_label ?? ''}: {danger_label(offering.danger_band)}
                    </span>
                    <span class="badge text-bg-secondary ms-1">
                      {texts.combat_mission_units_label ?? ''} {offering.total_units}
                    </span>
                    <span class="badge text-bg-dark ms-1">
                      {texts.combat_offering_reward_label ?? ''}: {offering.xp_base}
                    </span>
                  </div>
                  <button
                    class="btn btn-sm btn-outline-primary"
                    onclick={() => void choose_offering(offering)}
                    disabled={busy}
                  >
                    {texts.combat_mission_choose_btn ?? ''}
                  </button>
                </div>
                <p class="text-muted mt-2 mb-1 small">
                  {texts.combat_offering_map_label ?? ''}: {offering.map_id}
                </p>
                {#if offering.warband && offering.warband.length > 0}
                  <ul class="list-inline mb-0">
                    {#each offering.warband as group}
                      <li class="list-inline-item">
                        <span class="badge text-bg-secondary">
                          {group.unit_class} ×{group.count}
                        </span>
                      </li>
                    {/each}
                  </ul>
                {/if}
              </div>
            </div>
          {/each}
        {/if}
      {/each}

      {#if offerings.length === 0}
        <div class="alert alert-secondary">{texts.combat_offering_empty ?? ''}</div>
      {/if}

      {#if selected_offering}
        <div class="mt-3">
          <RosterSelect members={roster} selected={selected_ids} on_change={(s) => (selected_ids = s)} />
          <div class="d-flex gap-2 mt-3">
            <button class="btn btn-primary" onclick={muster} disabled={busy}>
              {busy ? texts.combat_mission_loading : texts.combat_mission_muster_btn}
            </button>
            <button
              class="btn btn-outline-secondary"
              onclick={() => (selected_offering = null)}
              disabled={busy}
            >
              {texts.combat_mission_cancel_btn ?? ''}
            </button>
          </div>
        </div>
      {/if}
    </div>

    <div class="col-lg-5">
      <div class="card border-secondary mb-4">
        <div class="card-header">{texts.combat_mission_join_heading ?? ''}</div>
        <div class="card-body">
          <div class="mb-3">
            <label class="form-label" for="combat_mission_code">
              {texts.combat_mission_code_label ?? ''}
            </label>
            <input
              id="combat_mission_code"
              class="form-control text-uppercase"
              placeholder="ABCDEF"
              maxlength="6"
              bind:value={join_code}
              onkeydown={(e) => {
                if (e.key === 'Enter') void join_by_code();
              }}
            />
          </div>
          <button class="btn btn-outline-primary w-100" onclick={join_by_code} disabled={busy}>
            {busy ? texts.combat_mission_loading : texts.combat_mission_join_btn}
          </button>
        </div>
      </div>

      <div class="card border-secondary">
        <div class="card-header">{texts.combat_mission_open_heading ?? ''}</div>
        <ul class="list-group list-group-flush">
          {#each open_matches as match}
            <li class="list-group-item d-flex justify-content-between align-items-center">
              <span>
                <span class="fw-bold">{match.match_code}</span>
                <span class="text-muted ms-2">
                  {match.player_count}/{match.max_players}
                </span>
                {#if match.host_name}<span class="text-muted ms-2">· {match.host_name}</span>{/if}
              </span>
              <button
                class="btn btn-sm btn-outline-primary"
                onclick={() => {
                  join_code = match.match_code;
                  void join_by_code();
                }}
                disabled={busy}
              >
                {texts.combat_mission_enter_btn ?? ''}
              </button>
            </li>
          {/each}
          {#if open_matches.length === 0}
            <li class="list-group-item text-muted">{texts.combat_mission_no_open ?? ''}</li>
          {/if}
        </ul>
      </div>
    </div>
  </div>
</div>
{/if}
