<script lang="ts">
  /**
   * CombatCommandBar — the classic bottom command card (rts_design.md §12.4).
   *
   * Shows selection info (name, class, level, HP, front/side/back defense &
   * dodge) and the order buttons: Move / Attack (→ Lob for AoE units) / Stop /
   * Hold / Stance / Construct / Formation, plus the queue toggle. All labels
   * come from the text system or from server-provided config names.
   */
  import { loadTexts } from '../lib/text';
  import type { combat_unit_dto } from './protocol';
  import type { combat_structure_def_dto, combat_formation_def_dto } from '../lib/api';

  interface Props {
    selected: combat_unit_dto[];
    structures: combat_structure_def_dto[];
    formations: combat_formation_def_dto[];
    order_mode: string;
    queue_enabled: boolean;
    on_set_order_mode: (mode: string) => void;
    on_stop: () => void;
    on_hold: () => void;
    on_stance: (stance: string) => void;
    on_construct: (structureType: string) => void;
    on_formation: (formationId: string) => void;
    on_toggle_queue: () => void;
  }

  let {
    selected,
    structures,
    formations,
    order_mode,
    queue_enabled,
    on_set_order_mode,
    on_stop,
    on_hold,
    on_stance,
    on_construct,
    on_formation,
    on_toggle_queue
  }: Props = $props();

  let texts = $state<Record<string, string>>({});

  $effect(() => {
    loadTexts([
      'combat_sel_none',
      'combat_sel_defense',
      'combat_sel_dodge',
      'combat_cmd_move',
      'combat_cmd_attack_move',
      'combat_cmd_patrol',
      'combat_cmd_attack',
      'combat_cmd_lob',
      'combat_cmd_stop',
      'combat_cmd_hold',
      'combat_cmd_stance',
      'combat_cmd_construct',
      'combat_cmd_formation',
      'combat_cmd_queue',
      'combat_stance_aggressive',
      'combat_stance_defensive',
      'combat_stance_hold',
      'combat_form_none'
    ]).then((loaded) => {
      texts = loaded;
    });
  });

  let single = $derived(selected.length === 1 ? selected[0] : null);
  let has_aoe = $derived(selected.some((u) => u.unit_class === 'alchemist'));
</script>

<div class="d-flex flex-wrap align-items-center gap-2 p-2 bg-dark border border-secondary rounded mt-2">
  {#if selected.length === 0}
    <span class="text-muted small">{texts.combat_sel_none ?? ''}</span>
  {:else if single}
    <span class="badge text-bg-primary fs-6">{single.name}</span>
    <span class="badge text-bg-dark">{single.unit_class}</span>
    <span class="badge text-bg-dark">Lv {single.level}</span>
    <span class="badge text-bg-secondary">{Math.round(single.hp)}/{Math.round(single.max_hp)} HP</span>
    <span class="badge text-bg-info" title={texts.combat_sel_defense}>
      {texts.combat_sel_defense ?? ''} {single.def_front?.toFixed(2) ?? '-'} /
      {single.def_side?.toFixed(2) ?? '-'} / {single.def_back?.toFixed(2) ?? '-'}
    </span>
    <span class="badge text-bg-info" title={texts.combat_sel_dodge}>
      {texts.combat_sel_dodge ?? ''} {single.dodge_front?.toFixed(2) ?? '-'} /
      {single.dodge_side?.toFixed(2) ?? '-'} / {single.dodge_back?.toFixed(2) ?? '-'}
    </span>
  {:else}
    <span class="badge text-bg-primary fs-6">{selected.length} selected</span>
  {/if}

  <div class="ms-auto d-flex flex-wrap gap-1 align-items-center">
    <button
      class="btn btn-sm {order_mode === 'move' ? 'btn-warning' : 'btn-outline-light'}"
      onclick={() => on_set_order_mode('move')}
      disabled={selected.length === 0}
    >
      {texts.combat_cmd_move ?? ''}
    </button>
    <button
      class="btn btn-sm {order_mode === 'attack_move' ? 'btn-warning' : 'btn-outline-light'}"
      onclick={() => on_set_order_mode('attack_move')}
      disabled={selected.length === 0}
    >
      {texts.combat_cmd_attack_move ?? ''}
    </button>
    <button
      class="btn btn-sm {order_mode === 'patrol' ? 'btn-warning' : 'btn-outline-light'}"
      onclick={() => on_set_order_mode('patrol')}
      disabled={selected.length === 0}
    >
      {texts.combat_cmd_patrol ?? ''}
    </button>
    <button
      class="btn btn-sm {order_mode === 'attack' ? 'btn-warning' : 'btn-outline-light'}"
      onclick={() => on_set_order_mode('attack')}
      disabled={selected.length === 0}
    >
      {texts.combat_cmd_attack ?? ''}
    </button>
    <button
      class="btn btn-sm {order_mode === 'lob' ? 'btn-warning' : 'btn-outline-light'}"
      onclick={() => on_set_order_mode('lob')}
      disabled={!has_aoe}
    >
      {texts.combat_cmd_lob ?? ''}
    </button>
    <button class="btn btn-sm btn-outline-light" onclick={on_stop} disabled={selected.length === 0}>
      {texts.combat_cmd_stop ?? ''}
    </button>
    <button class="btn btn-sm btn-outline-light" onclick={on_hold} disabled={selected.length === 0}>
      {texts.combat_cmd_hold ?? ''}
    </button>

    <div class="dropdown">
      <button
        class="btn btn-sm btn-outline-light dropdown-toggle"
        data-bs-toggle="dropdown"
        disabled={selected.length === 0}
      >
        {texts.combat_cmd_stance ?? ''}
      </button>
      <ul class="dropdown-menu dropdown-menu-dark">
        <li><button class="dropdown-item" onclick={() => on_stance('aggressive')}>{texts.combat_stance_aggressive ?? ''}</button></li>
        <li><button class="dropdown-item" onclick={() => on_stance('defensive')}>{texts.combat_stance_defensive ?? ''}</button></li>
        <li><button class="dropdown-item" onclick={() => on_stance('hold')}>{texts.combat_stance_hold ?? ''}</button></li>
      </ul>
    </div>

    <div class="dropdown">
      <button
        class="btn btn-sm {order_mode === 'construct' ? 'btn-warning' : 'btn-outline-light'} dropdown-toggle"
        data-bs-toggle="dropdown"
        disabled={selected.length === 0}
      >
        {texts.combat_cmd_construct ?? ''}
      </button>
      <ul class="dropdown-menu dropdown-menu-dark">
        {#each structures as structure}
          <li>
            <button class="dropdown-item" onclick={() => on_construct(structure.id)}>
              {structure.name}
            </button>
          </li>
        {/each}
      </ul>
    </div>

    <div class="dropdown">
      <button class="btn btn-sm btn-outline-light dropdown-toggle" data-bs-toggle="dropdown">
        {texts.combat_cmd_formation ?? ''}
      </button>
      <ul class="dropdown-menu dropdown-menu-dark">
        <li><button class="dropdown-item" onclick={() => on_formation('none')}>{texts.combat_form_none ?? ''}</button></li>
        {#each formations as formation}
          <li>
            <button class="dropdown-item" onclick={() => on_formation(formation.id)}>
              {formation.name}
            </button>
          </li>
        {/each}
      </ul>
    </div>

    <button
      class="btn btn-sm {queue_enabled ? 'btn-info' : 'btn-outline-info'}"
      onclick={on_toggle_queue}
    >
      {texts.combat_cmd_queue ?? ''}
    </button>
  </div>
</div>
