<script lang="ts">
  import type { ReeveAction, ReeveReport, ReeveTarget } from '../lib/api';
  import { language, currentCharacter } from '../lib/stores';
  import { loadTexts } from '../lib/text';
  import {
    collect_reeve_text_ids,
    reeve_severity_class
  } from '../lib/reeve_text';
  import ReeveText from './ReeveText.svelte';

  interface Props {
    report: ReeveReport | null;
    loading: boolean;
    /** Text id for a report-loading failure (never raw server English). */
    error: string;
    /** Text id for the last action failure. */
    action_error: string;
    action_busy: boolean;
    road_link_preview: boolean;
    on_close: () => void;
    on_refresh: () => void;
    on_jump: (target: ReeveTarget) => void;
    on_accept: (action: ReeveAction) => void;
    on_manual_place: (action: ReeveAction) => void;
    on_confirm_road_link: () => void;
    on_cancel_road_link: () => void;
  }

  let {
    report,
    loading,
    error,
    action_error,
    action_busy,
    road_link_preview,
    on_close,
    on_refresh,
    on_jump,
    on_accept,
    on_manual_place,
    on_confirm_road_link,
    on_cancel_road_link
  }: Props = $props();

  const PANEL_TEXT_IDS: string[] = [
    'reeve_panel_title',
    'reeve_close',
    'reeve_refresh',
    'reeve_accept',
    'reeve_place_manually',
    'reeve_confirm_road_link',
    'reeve_cancel',
    'reeve_loading',
    'reeve_empty',
    'reeve_open_hint',
    'reeve_open_economy'
  ];

  let texts = $state<Record<string, string>>({});

  $effect(() => {
    $language;
    $currentCharacter;
    const ids = new Set<string>(PANEL_TEXT_IDS);
    if (report) {
      for (const id of collect_reeve_text_ids(report)) ids.add(id);
    }
    if (error) ids.add(error);
    if (action_error) ids.add(action_error);

    let cancelled = false;
    loadTexts([...ids]).then((loaded) => {
      if (!cancelled) texts = loaded;
    });
    return () => { cancelled = true; };
  });
</script>

<div class="d-flex justify-content-between align-items-start gap-2 mb-2">
  <h6 class="card-title mb-0">{texts['reeve_panel_title'] ?? ''}</h6>
  <div class="d-flex gap-1">
    <button
      class="btn btn-sm btn-outline-light"
      title={texts['reeve_refresh'] ?? ''}
      disabled={loading}
      onclick={on_refresh}
    >
      &#8635;
    </button>
    <button
      class="btn-close"
      aria-label={texts['reeve_close'] ?? ''}
      onclick={on_close}
    ></button>
  </div>
</div>

{#if loading}
  <div class="d-flex align-items-center gap-2 text-muted small">
    <span class="spinner-border spinner-border-sm" role="status"></span>
    {texts['reeve_loading'] ?? ''}
  </div>
{/if}

{#if error}
  <div class="alert alert-danger py-1 px-2 small mt-2 mb-2">
    <ReeveText text={{ text_id: error }} {texts} />
  </div>
{/if}

{#if report}
  <ReeveText text={report.headline} {texts} class="small fw-semibold mb-2" />

  {#each report.sections as section (section.id)}
    <div class="border-top pt-2 mt-2">
      <div class="d-flex justify-content-between align-items-start gap-2">
        <button
          class="btn btn-link p-0 text-start text-decoration-none {reeve_severity_class(section.severity)}"
          onclick={() => on_jump(section.target)}
        >
          <ReeveText text={section.title} {texts} inline class="small fw-semibold" />
        </button>
        <div class="small text-end {reeve_severity_class(section.severity)}">
          <ReeveText text={section.status} {texts} inline />
        </div>
      </div>
      {#each section.details as detail, index (section.id + ':' + index)}
        <button
          class="btn btn-link p-0 text-start d-block text-decoration-none small {reeve_severity_class(detail.severity)}"
          onclick={() => on_jump(detail.target)}
        >
          <ReeveText text={detail.text} {texts} inline />
        </button>
      {/each}
    </div>
  {/each}

  <div class="card mt-3">
    <div class="card-body py-2 px-3">
      {#if report.recommendation.kind === 'none'}
        <ReeveText text={report.recommendation.text} {texts} class="small" />
      {:else}
        <ReeveText
          text={report.recommendation.text}
          {texts}
          class="small fw-semibold {reeve_severity_class(report.recommendation.severity)}"
        />
        {#if report.recommendation.why && report.recommendation.why.text_id}
          {@const why = report.recommendation.why}
          <ReeveText
            text={why}
            {texts}
            class="small text-muted mt-1"
          />
        {/if}

        {#if report.recommendation.kind === 'hold'}
          {#if report.recommendation.reason_text_id}
            {@const reason = report.recommendation.reason_text_id}
            <div class="small text-warning mt-1">
              <ReeveText text={{ text_id: reason }} {texts} />
            </div>
          {/if}
          <button
            class="btn btn-sm btn-outline-light mt-2"
            onclick={() => on_jump(report.recommendation.target)}
          >
            {texts['reeve_open_economy'] ?? ''}
          </button>
        {:else if report.recommendation.kind === 'action' && report.recommendation.action}
          {@const action = report.recommendation.action as ReeveAction}
          <div class="d-flex flex-wrap gap-2 mt-2">
            <button
              class="btn btn-sm btn-primary"
              disabled={action_busy}
              onclick={() => on_accept(action)}
            >
              {texts['reeve_accept'] ?? ''}
            </button>
            {#if action.kind === 'build' || action.kind === 'build_road_link'}
              <button
                class="btn btn-sm btn-outline-light"
                disabled={action_busy}
                onclick={() => on_manual_place(action)}
              >
                {texts['reeve_place_manually'] ?? ''}
              </button>
            {/if}
          </div>
        {/if}
      {/if}

      {#if road_link_preview}
        <div class="d-flex flex-wrap gap-2 mt-2">
          <button
            class="btn btn-sm btn-primary"
            disabled={action_busy}
            onclick={on_confirm_road_link}
          >
            {texts['reeve_confirm_road_link'] ?? ''}
          </button>
          <button
            class="btn btn-sm btn-outline-secondary"
            disabled={action_busy}
            onclick={on_cancel_road_link}
          >
            {texts['reeve_cancel'] ?? ''}
          </button>
        </div>
      {/if}

      {#if action_error}
        <div class="alert alert-warning py-1 px-2 small mt-2 mb-0">
          <ReeveText text={{ text_id: action_error }} {texts} />
        </div>
      {/if}
    </div>
  </div>
{:else if !loading}
  <p class="small text-muted">{texts['reeve_empty'] ?? ''}</p>
{/if}
