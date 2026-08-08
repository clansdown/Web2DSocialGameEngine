<script lang="ts">
  /**
   * MatchLobby — create or join a PvE combat match.
   *
   * Creating a match makes this character the host and returns a shareable
   * match code; barony members (or any invited player) join with that code.
   * PvP matchmaking is stubbed (see combatMatchmaking).
   */
  import { loadTexts } from '../lib/text';
  import { currentCharacter } from '../lib/stores';
  import {
    combatCreateRequest,
    combatGetConfigsRequest,
    combatJoinRequest,
    handle_combat_error
  } from '../lib/api';
  import type { combat_ruleset_dto, combat_map_dto } from '../lib/api';

  interface Props {
    on_joined: (matchId: string) => void;
    on_back: () => void;
  }

  let { on_joined, on_back }: Props = $props();

  let rulesets = $state<combat_ruleset_dto[]>([]);
  let maps = $state<combat_map_dto[]>([]);
  let selected_ruleset = $state('');
  let selected_map = $state('');
  let join_code = $state('');
  let busy = $state(false);
  let error = $state('');
  let texts = $state<Record<string, string>>({});

  $effect(() => {
    loadTexts([
      'combat_lobby_title',
      'combat_lobby_create_heading',
      'combat_lobby_join_heading',
      'combat_lobby_ruleset_label',
      'combat_lobby_map_label',
      'combat_lobby_create_btn',
      'combat_lobby_code_label',
      'combat_lobby_join_btn',
      'combat_lobby_back_btn',
      'combat_lobby_loading'
    ]).then((loaded) => {
      texts = loaded;
    });
    void load_configs();
  });

  /**
   * Loads the available rulesets and maps from the server.
   *
   * @param none
   * @returns Promise<void>
   */
  async function load_configs(): Promise<void> {
    try {
      const configs = await combatGetConfigsRequest();
      rulesets = configs.rulesets;
      maps = configs.maps;
      if (rulesets.length > 0) {
        selected_ruleset = rulesets[0].id;
      }
      if (maps.length > 0) {
        selected_map = maps[0].id;
      }
    } catch (e) {
      error = handle_combat_error(e);
    }
  }

  /**
   * Creates a new PvE match with the selected ruleset/map.
   *
   * @param none
   * @returns Promise<void>
   */
  async function create_match(): Promise<void> {
    if (!$currentCharacter || busy) return;
    busy = true;
    error = '';
    try {
      const result = await combatCreateRequest($currentCharacter.id, {
        mode: 'pve',
        ruleset_id: selected_ruleset,
        map_id: selected_map
      });
      on_joined(result.match_id);
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Joins an existing lobby match by its shareable code.
   *
   * @param none
   * @returns Promise<void>
   */
  async function join_match(): Promise<void> {
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

<div class="container py-4">
  <button class="btn btn-outline-secondary mb-4" onclick={on_back}>
    &larr; {texts.combat_lobby_back_btn}
  </button>

  <h2 class="mb-4">{texts.combat_lobby_title}</h2>

  {#if error}
    <div class="alert alert-danger py-2">{error}</div>
  {/if}

  <div class="row g-4">
    <div class="col-md-6">
      <div class="card border-primary">
        <div class="card-header">{texts.combat_lobby_create_heading}</div>
        <div class="card-body">
          <div class="mb-3">
            <label class="form-label" for="combat_ruleset_select">{texts.combat_lobby_ruleset_label}</label>
            <select id="combat_ruleset_select" class="form-select" bind:value={selected_ruleset}>
              {#each rulesets as ruleset}
                <option value={ruleset.id}>{ruleset.name}</option>
              {/each}
            </select>
          </div>
          <div class="mb-3">
            <label class="form-label" for="combat_map_select">{texts.combat_lobby_map_label}</label>
            <select id="combat_map_select" class="form-select" bind:value={selected_map}>
              {#each maps as map}
                <option value={map.id}>{map.id}</option>
              {/each}
            </select>
          </div>
          <button
            class="btn btn-primary w-100"
            onclick={create_match}
            disabled={busy || rulesets.length === 0 || maps.length === 0}
          >
            {busy ? texts.combat_lobby_loading : texts.combat_lobby_create_btn}
          </button>
        </div>
      </div>
    </div>

    <div class="col-md-6">
      <div class="card border-secondary">
        <div class="card-header">{texts.combat_lobby_join_heading}</div>
        <div class="card-body">
          <div class="mb-3">
            <label class="form-label" for="combat_code_input">{texts.combat_lobby_code_label}</label>
            <input
              id="combat_code_input"
              class="form-control text-uppercase"
              placeholder="ABCDEF"
              maxlength="6"
              bind:value={join_code}
              onkeydown={(e) => {
                if (e.key === 'Enter') void join_match();
              }}
            />
          </div>
          <button class="btn btn-outline-primary w-100" onclick={join_match} disabled={busy}>
            {busy ? texts.combat_lobby_loading : texts.combat_lobby_join_btn}
          </button>
        </div>
      </div>
    </div>
  </div>
</div>
