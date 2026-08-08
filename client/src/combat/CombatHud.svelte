<script lang="ts">
  /**
   * CombatHud — Bootstrap overlay bar for the battle: match timer, phase,
   * unit counts per team, connection status, and action buttons.
   */
  import { loadTexts } from '../lib/text';
  import type { combat_player_dto, combat_unit_dto } from './protocol';

  interface Props {
    phase: string;
    battle_time: number;
    unit_counts: Map<number, number>;
    players: combat_player_dto[];
    connection_status: string;
    team: number;
    selected_count: number;
    on_toggle_chat: () => void;
    on_toggle_voice: () => void;
    on_leave: () => void;
    chat_open: boolean;
    voice_active: boolean;
  }

  let {
    phase,
    battle_time,
    unit_counts,
    players,
    connection_status,
    team,
    selected_count,
    on_toggle_chat,
    on_toggle_voice,
    on_leave,
    chat_open,
    voice_active
  }: Props = $props();

  let texts = $state<Record<string, string>>({});

  let unit_count_by_team = $derived(unit_counts);

  $effect(() => {
    loadTexts([
      'combat_team_label',
      'combat_units_label',
      'combat_selected_label',
      'combat_chat_btn',
      'combat_voice_btn',
      'combat_voice_off_btn',
      'combat_leave_btn',
      'combat_phase_countdown',
      'combat_phase_battle',
      'combat_phase_lobby',
      'combat_phase_ended'
    ]).then((loaded) => {
      texts = loaded;
    });
  });

  const phase_label = $derived(
    texts[`combat_phase_${phase}`] ?? phase
  );

  const elapsed_display = $derived.by(() => {
    const total = Math.floor(battle_time);
    const minutes = Math.floor(total / 60);
    const seconds = total % 60;
    return `${minutes}:${seconds.toString().padStart(2, '0')}`;
  });
</script>

<div class="d-flex flex-wrap align-items-center gap-2 p-2 bg-dark border border-secondary rounded">
  <span class="badge text-bg-primary fs-6">
    {phase_label}{#if phase === 'countdown'}: {elapsed_display}{/if}
  </span>  <span class="badge text-bg-secondary fs-6">
    {texts.combat_team_label} {team}
  </span>
  {#each [...unit_count_by_team.entries()] as [t, count]}
    <span class="badge text-bg-info fs-6">
      {texts.combat_units_label} ({t}): {count}
    </span>
  {/each}
  {#if selected_count > 0}
    <span class="badge text-bg-warning fs-6">
      {texts.combat_selected_label}: {selected_count}
    </span>
  {/if}
  <span class="badge {connection_status === 'open' ? 'text-bg-success' : 'text-bg-warning'} fs-6">
    {connection_status}
  </span>
  <div class="ms-auto d-flex gap-2">
    <button
      class="btn btn-sm {chat_open ? 'btn-info' : 'btn-outline-info'}"
      onclick={on_toggle_chat}
    >
      {texts.combat_chat_btn}
    </button>
    <button
      class="btn btn-sm {voice_active ? 'btn-success' : 'btn-outline-success'}"
      onclick={on_toggle_voice}
    >
      {voice_active ? texts.combat_voice_off_btn : texts.combat_voice_btn}
    </button>
    <button class="btn btn-sm btn-outline-danger" onclick={on_leave}>
      {texts.combat_leave_btn}
    </button>
  </div>
</div>
