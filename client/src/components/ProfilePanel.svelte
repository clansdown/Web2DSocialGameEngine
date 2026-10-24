<script lang="ts">
  /**
   * ProfilePanel is the hub "Profile" activity. It shows the player's current
   * portrait and manages both identity layers of the layered model:
   *
   *   - The safe pick: always present, shown to minors and used as the fallback
   *     for every viewer. Edited through the safe grid.
   *   - The custom upload: an optional 18+ image layered ON TOP for adult
   *     viewers of adult owners. Uploaded / removed here.
   *
   * Any avatar change is rate-limited to once per 24h per character (the
   * creation pick is exempt), so mutation controls are disabled while the
   * cooldown is active. Custom uploads are currently adult-gated only — there
   * is no content review yet (documented TODO).
   *
   * @param onBack - Returns to the hub
   */
  import { onMount, onDestroy } from 'svelte';
  import { currentCharacter } from '../lib/stores';
  import {
    getAvatarPoolRequest,
    setCharacterAvatarRequest,
    uploadCharacterAvatarRequest,
    removeCustomAvatarRequest,
    type safe_avatar_dto,
    type avatar_update_dto
  } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import {
    file_to_avatar_payload,
    avatar_cooldown_remaining
  } from '../lib/avatars';
  import SafeAvatarGrid from './SafeAvatarGrid.svelte';
  import PlayerAvatar from './PlayerAvatar.svelte';

  interface Props {
    onBack: () => void;
  }

  let { onBack }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_profile_back',
    'ui_profile_title',
    'ui_profile_current',
    'ui_profile_custom_badge',
    'ui_profile_safe',
    'ui_profile_safe_note',
    'ui_profile_custom',
    'ui_profile_adult_only',
    'ui_profile_upload_hint',
    'ui_profile_upload_btn',
    'ui_profile_remove',
    'ui_profile_cooldown',
    'ui_profile_saved',
    'ui_profile_uploaded',
    'ui_profile_removed',
    'ui_profile_err_not_image',
    'ui_profile_err_source_too_large',
    'ui_profile_err_canvas',
    'ui_profile_err_decode',
    'ui_confirm',
    'ui_loading'
  ];

  let texts = $state<Record<string, string>>({});
  let pool = $state<safe_avatar_dto[]>([]);
  let adult = $state(false);
  let loading = $state(true);
  let submitting = $state(false);
  let error_msg = $state('');
  let info_msg = $state('');
  let selected_safe = $state<string | null>(null);
  let upload_file = $state<File | null>(null);
  let upload_preview = $state<string | null>(null);
  let now_seconds = $state(Math.floor(Date.now() / 1000));
  let cooldown_timer: ReturnType<typeof setInterval> | null = null;

  let character = $derived($currentCharacter);
  let has_custom = $derived(!!character?.avatar_custom_id);
  let cooldown_remaining = $derived(
    avatar_cooldown_remaining(character?.avatar_changed_at, now_seconds)
  );
  let cooldown_active = $derived(cooldown_remaining > 0);
  let cooldown_hours = $derived(Math.floor(cooldown_remaining / 3600));
  let cooldown_minutes = $derived(Math.floor((cooldown_remaining % 3600) / 60));
  let can_save_safe = $derived(
    !!selected_safe && selected_safe !== character?.avatar_safe_id && !submitting && !cooldown_active
  );
  let can_upload = $derived(!!upload_file && !submitting && !cooldown_active);

  /**
   * Looks up a text id and substitutes `{key}` tokens.
   *
   * @param id - Text id
   * @param tokens - Optional `{key: value}` substitutions
   * @returns string - Resolved text (empty when missing)
   */
  function t(id: string, tokens?: Record<string, string>): string {
    let value = texts[id] ?? '';
    if (tokens) {
      for (const [key, replacement] of Object.entries(tokens)) {
        value = value.replace(new RegExp(`\\{${key}\\}`, 'g'), replacement);
      }
    }
    return value;
  }

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    try {
      // The safe pool is gender-split; request the character's own pool.
      const gender = character?.sex === 'female' ? 'female' : 'male';
      const data = await getAvatarPoolRequest(gender);
      pool = data.safe;
      adult = data.adult;
      selected_safe = character?.avatar_safe_id ?? null;
    } catch (e) {
      error_msg = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
    cooldown_timer = setInterval(() => {
      now_seconds = Math.floor(Date.now() / 1000);
    }, 60000);
  });

  onDestroy(() => {
    if (cooldown_timer) clearInterval(cooldown_timer);
    if (upload_preview) URL.revokeObjectURL(upload_preview);
  });

  /**
   * Merges a partial avatar update into the current character store.
   *
   * @param update - Avatar fields returned by the server
   * @param cleared_custom - True for removeCustomAvatar (which does not return
   *   avatar_custom_id); forces the custom layer off.
   */
  function apply_update(update: avatar_update_dto, cleared_custom = false): void {
    const c = $currentCharacter;
    if (!c) return;
    currentCharacter.set({
      ...c,
      avatar_safe_id: update.avatar_safe_id ?? c.avatar_safe_id,
      avatar_custom_id: cleared_custom ? null : (update.avatar_custom_id ?? c.avatar_custom_id),
      avatar_storage: update.avatar_storage ?? c.avatar_storage,
      avatar_changed_at: update.avatar_changed_at ?? c.avatar_changed_at,
      avatar_url: update.avatar_url ?? c.avatar_url
    });
  }

  /** Persists the selected safe-pool avatar (the minor/fallback layer). */
  async function save_safe(): Promise<void> {
    const c = $currentCharacter;
    if (!c || !selected_safe) return;
    submitting = true;
    error_msg = '';
    info_msg = '';
    try {
      const update = await setCharacterAvatarRequest(c.id, selected_safe);
      apply_update(update);
      info_msg = t('ui_profile_saved');
    } catch (e) {
      error_msg = e instanceof Error ? e.message : String(e);
    } finally {
      submitting = false;
    }
  }

  /**
   * Tracks the chosen file and shows a local preview.
   *
   * @param event - The file input's change event
   */
  function on_file_change(event: Event): void {
    const input = event.currentTarget as HTMLInputElement;
    const file = input.files?.[0] ?? null;
    if (upload_preview) URL.revokeObjectURL(upload_preview);
    upload_file = file;
    upload_preview = file ? URL.createObjectURL(file) : null;
    error_msg = '';
    info_msg = '';
  }

  /**
   * Maps a client-side image-processing error code to a localized message.
   *
   * @param e - The thrown error
   * @returns string - Localized message, or the raw server error
   */
  function upload_error_text(e: unknown): string {
    const msg = e instanceof Error ? e.message : String(e);
    if (msg === 'not_an_image') return t('ui_profile_err_not_image');
    if (msg === 'image_too_large_source') return t('ui_profile_err_source_too_large');
    if (msg === 'canvas_unavailable') return t('ui_profile_err_canvas');
    if (msg === 'image_decode_failed') return t('ui_profile_err_decode');
    return msg;
  }

  /** Pre-scales the chosen image and uploads it as the 18+ custom layer. */
  async function upload_custom(): Promise<void> {
    const c = $currentCharacter;
    if (!c || !upload_file) return;
    submitting = true;
    error_msg = '';
    info_msg = '';
    try {
      const payload = await file_to_avatar_payload(upload_file);
      const update = await uploadCharacterAvatarRequest(c.id, payload.base64, payload.format);
      apply_update(update);
      info_msg = t('ui_profile_uploaded');
      if (upload_preview) URL.revokeObjectURL(upload_preview);
      upload_preview = null;
      upload_file = null;
    } catch (e) {
      error_msg = upload_error_text(e);
    } finally {
      submitting = false;
    }
  }

  /** Removes the custom layer, reverting display to the safe pick. */
  async function remove_custom(): Promise<void> {
    const c = $currentCharacter;
    if (!c || !has_custom) return;
    submitting = true;
    error_msg = '';
    info_msg = '';
    try {
      const update = await removeCustomAvatarRequest(c.id);
      apply_update(update, true);
      info_msg = t('ui_profile_removed');
    } catch (e) {
      error_msg = e instanceof Error ? e.message : String(e);
    } finally {
      submitting = false;
    }
  }
</script>

<div class="container py-4">
  <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
    &larr; {t('ui_profile_back')}
  </button>

  <h2 class="mb-4">{t('ui_profile_title')}</h2>

  {#if error_msg}
    <div class="alert alert-danger small py-1">{error_msg}</div>
  {/if}
  {#if info_msg}
    <div class="alert alert-success small py-1">{info_msg}</div>
  {/if}

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">{t('ui_loading')}</span>
      </div>
    </div>
  {:else}
    <div class="row g-4">
      <div class="col-12 col-md-4 text-center">
        <h5 class="mb-3">{t('ui_profile_current')}</h5>
        <div class="d-inline-block position-relative">
          <PlayerAvatar
            portrait_url={character?.avatar_url ?? ''}
            width={160}
          />
          {#if has_custom}
            <span class="badge text-bg-primary position-absolute top-0 start-100 translate-middle">
              {t('ui_profile_custom_badge')}
            </span>
          {/if}
        </div>
      </div>

      <div class="col-12 col-md-8">
        {#if cooldown_active}
          <div class="alert alert-info small py-1">
            {t('ui_profile_cooldown', {
              hours: String(cooldown_hours),
              minutes: String(cooldown_minutes)
            })}
          </div>
        {/if}

        <h5>{t('ui_profile_safe')}</h5>
        <p class="text-muted small">{t('ui_profile_safe_note')}</p>
        <SafeAvatarGrid
          avatars={pool}
          selected={selected_safe}
          onSelect={(id) => { selected_safe = id; }}
        />
        <div class="mt-3">
          <button class="btn btn-primary" disabled={!can_save_safe} onclick={save_safe}>
            {t('ui_confirm')}
          </button>
        </div>

        <hr class="my-4" />

        <h5>{t('ui_profile_custom')}</h5>
        {#if !adult}
          <p class="text-muted small mb-0">{t('ui_profile_adult_only')}</p>
        {:else}
          <p class="text-muted small">{t('ui_profile_upload_hint')}</p>
          <div class="d-flex align-items-center gap-3 flex-wrap">
            <input
              class="form-control form-control-sm"
              style="max-width: 20rem;"
              type="file"
              accept="image/png,image/jpeg,image/webp,image/avif"
              onchange={on_file_change}
            />
            {#if upload_preview}
              <img
                src={upload_preview}
                alt=""
                style="width: 64px; height: 96px; object-fit: contain; border-radius: 6px; background: rgba(0, 0, 0, 0.35);"
              />
            {/if}
            <button class="btn btn-outline-primary btn-sm" disabled={!can_upload} onclick={upload_custom}>
              {t('ui_profile_upload_btn')}
            </button>
            {#if has_custom}
              <button
                class="btn btn-outline-danger btn-sm"
                disabled={submitting || cooldown_active}
                onclick={remove_custom}
              >
                {t('ui_profile_remove')}
              </button>
            {/if}
          </div>
        {/if}
      </div>
    </div>
  {/if}
</div>
