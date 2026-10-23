<script lang="ts">
  /**
   * MatchLobby — the in-match lobby after a player has created or joined a
   * match: share the code, pick the fielded roster, and signal Ready
   * (rts_design.md §4.3). Joining players roster-select here; the host chose
   * theirs at mission pick. The match advances once every player is ready.
   *
   * The opportunity's warband is already fixed at create (D62) and cannot be
   * changed by a joiner.
   */
  import { untrack } from 'svelte';
  import { loadTexts } from '../lib/text';
  import type { CombatNetClient } from './CombatNetClient';
  import type { combat_welcome_payload } from './protocol';
  import RosterSelect from './RosterSelect.svelte';
  import CombatBanner from './CombatBanner.svelte';

  interface Props {
    net: CombatNetClient;
    welcome: combat_welcome_payload;
    my_player_id: number;
    on_leave: () => void;
  }

  let { net, welcome, my_player_id, on_leave }: Props = $props();

  // Default to fielding every fieldable non-knight member; the parent owns the
  // selected array and we notify the server on each change. `untrack` avoids a
  // Svelte warning (the welcome snapshot is stable for the match's lifetime).
  let selected_ids = $state<number[]>(
    untrack(() =>
      welcome.retinue.filter((m) => !m.is_knight && m.fieldable).map((m) => m.member_id)
    )
  );
  let texts = $state<Record<string, string>>({});

  $effect(() => {
    loadTexts([
      'combat_lobby_title',
      'combat_lobby_waiting',
      'combat_ready_btn',
      'combat_lobby_share_note',
      'combat_lobby_code_label',
      'combat_results_back_btn'
    ]).then((loaded) => {
      texts = loaded;
    });
  });

  /**
   * Sends the roster subset to the server (empty = field everything).
   *
   * @param ids - The selected non-knight member ids
   * @returns void
   */
  function update_roster(ids: number[]): void {
    selected_ids = ids;
    net.set_roster(ids);
  }
</script>

<div class="container py-4">
  <div class="d-flex justify-content-between align-items-center mb-3">
    <div class="d-flex align-items-center gap-3">
      <CombatBanner banner={welcome.banner} size={36} title={texts.combat_lobby_title ?? ''} />
      <h2 class="mb-0">{texts.combat_lobby_title ?? ''}</h2>
    </div>
    <span class="badge text-bg-primary fs-5">{welcome.match_code}</span>
  </div>
  <p class="text-muted">{texts.combat_lobby_share_note ?? ''}</p>

  <div class="row g-4">
    <div class="col-md-6">
      <div class="card border-secondary">
        <div class="card-header">{texts.combat_lobby_waiting ?? ''}</div>
        <ul class="list-group list-group-flush">
          {#each welcome.players as player}
            <li class="list-group-item d-flex justify-content-between align-items-center">
              <span>
                {player.display_name}
                {#if player.player_id === my_player_id}
                  <span class="badge text-bg-primary ms-2">You</span>
                {/if}
              </span>
              <span class="badge {player.ready ? 'text-bg-success' : 'text-bg-secondary'}">
                {player.ready ? texts.combat_ready_btn : '…'}
              </span>
            </li>
          {/each}
        </ul>
      </div>
    </div>
    <div class="col-md-6">
      <RosterSelect members={welcome.retinue} selected={selected_ids} on_change={update_roster} />
    </div>
  </div>

  <div class="d-flex gap-2 mt-4">
    <button class="btn btn-primary btn-lg" onclick={() => net.ready()}>
      {texts.combat_ready_btn ?? ''}
    </button>
    <button class="btn btn-outline-danger btn-lg" onclick={on_leave}>
      {texts.combat_results_back_btn ?? ''}
    </button>
  </div>
</div>
