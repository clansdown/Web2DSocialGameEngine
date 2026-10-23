<script lang="ts">
  /**
   * ResultsView — the post-battle screen (rts_design.md §4.6): victory/defeat/
   * draw banner + reason, per-member XP gained and level-ups, the wounds list,
   * and the (v1-empty) rewards stub.
   */
  import { loadTexts } from '../lib/text';
  import type { combat_ended_payload, combat_appearance_dto } from './protocol';
  import CombatBanner from './CombatBanner.svelte';

  interface Props {
    ended: combat_ended_payload;
    my_team: number;
    /** Maps member_id → display name for readable rows (from welcome.retinue). */
    member_names: Record<number, string>;
    banner?: combat_appearance_dto | null;
    on_back: () => void;
  }

  let { ended, my_team, member_names, banner = null, on_back }: Props = $props();

  let texts = $state<Record<string, string>>({});

  $effect(() => {
    loadTexts([
      'combat_results_title',
      'combat_results_victory',
      'combat_results_defeat',
      'combat_results_draw',
      'combat_results_back_btn',
      'combat_results_members_xp',
      'combat_results_xp_label',
      'combat_results_wounds',
      'combat_results_level_up',
      'combat_results_reason',
      'combat_results_none'
    ]).then((loaded) => {
      texts = loaded;
    });
  });

  /** True when the player's side won (PvE players are team 1). */
  let player_won = $derived(my_team === 1 ? ended.winner === 1 : ended.winner === 0);
  let is_draw = $derived(ended.winner === -1);

  let result_label = $derived(
    is_draw
      ? texts.combat_results_draw
      : player_won
        ? texts.combat_results_victory
        : texts.combat_results_defeat
  );

  let result_class = $derived(is_draw ? 'text-warning' : player_won ? 'text-success' : 'text-danger');

  /**
   * Returns the display name for a member id, falling back to its id.
   *
   * @param memberId - The retinue member id
   * @returns string
   */
  function name_of(memberId: number): string {
    return member_names[memberId] ?? `#${memberId}`;
  }
</script>

<div class="container py-5">
  <div class="text-center">
    {#if banner}
      <div class="mb-3"><CombatBanner {banner} size={48} title={texts.combat_results_title ?? ''} /></div>
    {/if}
    <h2>{texts.combat_results_title ?? ''}</h2>
    <div class="display-5 my-4 {result_class}">{result_label}</div>
    {#if ended.reason}
      <p class="text-muted">{texts.combat_results_reason ?? ''}: {ended.reason}</p>
    {/if}
  </div>

  <div class="row justify-content-center">
    <div class="col-md-7">
      {#if ended.xp_by_member && ended.xp_by_member.length > 0}
        <h6 class="mt-3 text-muted">{texts.combat_results_members_xp ?? ''}</h6>
        <ul class="list-group mb-4">
          {#each ended.xp_by_member as row}
            <li class="list-group-item d-flex justify-content-between align-items-center">
              <span>{name_of(row.member_id)}</span>
              <span>
                <span class="fw-bold text-success">
                  +{Math.round(row.xp)} {texts.combat_results_xp_label ?? ''}
                </span>
                {#each ended.level_ups ?? [] as up}
                  {#if up.member_id === row.member_id}
                    <span class="badge text-bg-warning ms-2">
                      {texts.combat_results_level_up ?? ''} +{up.level_ups}
                    </span>
                  {/if}
                {/each}
              </span>
            </li>
          {/each}
        </ul>
      {/if}

      <h6 class="text-muted">{texts.combat_results_wounds ?? ''}</h6>
      {#if ended.casualties.length > 0}
        <ul class="list-group mb-4">
          {#each ended.casualties as wound}
            <li class="list-group-item d-flex justify-content-between">
              <span>{name_of(wound.member_id)}</span>
              <span class="text-muted">{Math.round(wound.end_hp)}%</span>
            </li>
          {/each}
        </ul>
      {:else}
        <p class="text-muted">{texts.combat_results_none ?? ''}</p>
      {/if}

      <button class="btn btn-primary" onclick={on_back}>
        {texts.combat_results_back_btn ?? ''}
      </button>
    </div>
  </div>
</div>
