<script lang="ts">
  /**
   * ProfilePanel is the hub "Profile" activity. It shows the player's current
   * portrait/name and manages both identity layers:
   *
   *   - Safe identity: the safe name and safe avatar pick. Always present,
   *     shown to minors, and used as the fallback for every viewer.
   *   - Custom identity: an optional adult custom display name and custom
   *     avatar layered ON TOP for adult viewers of adult owners.
   *
   * For adults the custom sections appear first, above the safe sections.
   * Minors see only the safe sections plus the 18+ status request; custom
   * sections are hidden. Any avatar change is rate-limited to once per 24h per
   * character (the creation pick is exempt), so avatar mutation controls are
   * disabled while the cooldown is active.
   *
   * @param onBack - Returns to the hub
   */
  import { onMount, onDestroy } from 'svelte';
  import { currentCharacter, user } from '../lib/stores';
  import {
    getAvatarPoolRequest,
    setCharacterAvatarRequest,
    uploadCharacterAvatarRequest,
    removeCustomAvatarRequest,
    updateCharacterProfileRequest,
    type safe_avatar_dto,
    type avatar_update_dto,
    type CharacterProfileResponse
  } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import { navigate } from '../lib/router';
  import { createSupportTicketRequest } from '../support/support_api';
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
    'ui_profile_custom_name',
    'ui_profile_custom_name_note',
    'ui_profile_custom_name_placeholder',
    'ui_profile_custom_name_save',
    'ui_profile_custom_name_clear',
    'ui_profile_custom_name_saved',
    'ui_profile_custom_name_cleared',
    'ui_profile_custom_name_adult_only',
    'ui_profile_safe_name',
    'ui_profile_safe_name_note',
    'ui_profile_safe_name_current',
    'ui_profile_safe_name_word1',
    'ui_profile_safe_name_word2',
    'ui_profile_safe_name_save',
    'ui_profile_safe_name_saved',
    'ui_profile_safe_name_words_required',
    'ui_profile_name_locked',
    'ui_profile_name_too_long',
    'ui_profile_safe_words_error',
    'ui_profile_safe',
    'ui_profile_safe_note',
    'ui_profile_custom',
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
    'ui_profile_adult_status_title',
    'ui_profile_adult_status_body',
    'ui_profile_adult_status_note',
    'ui_profile_adult_status_note_placeholder',
    'ui_profile_adult_status_note_required',
    'ui_profile_adult_status_request',
    'ui_profile_adult_status_help',
    'ui_profile_adult_status_subject',
    'ui_profile_adult_status_message',
    'ui_profile_adult_status_hide_for',
    'ui_profile_adult_status_hide',
    'ui_profile_adult_status_months',
    'ui_profile_adult_status_years',
    'ui_support_locked',
    'ui_auth_server_unavailable',
    'ui_confirm',
    'ui_loading'
  ];

  let texts = $state<Record<string, string>>({});
  let pool = $state<safe_avatar_dto[]>([]);
  let adult = $state(false);
  let safe_words_1 = $state<string[]>([]);
  let safe_words_2 = $state<string[]>([]);
  let safe_word_1 = $state('');
  let safe_word_2 = $state('');
  let custom_name_input = $state('');
  let name_submitting = $state(false);
  let loading = $state(true);
  let submitting = $state(false);
  let error_msg = $state('');
  let info_msg = $state('');
  let selected_safe = $state<string | null>(null);
  let upload_file = $state<File | null>(null);
  let upload_preview = $state<string | null>(null);
  let now_seconds = $state(Math.floor(Date.now() / 1000));
  let cooldown_timer: ReturnType<typeof setInterval> | null = null;

  const ADULT_PROMPT_KEY_PREFIX = 'adult_status_prompt_hidden_until:';
  let adult_status_note = $state('');
  let adult_status_error = $state('');
  let adult_status_submitting = $state(false);
  let adult_status_hidden = $state(false);
  let adult_status_dismiss_value = $state(6);
  let adult_status_dismiss_unit = $state<'months' | 'years'>('months');

  let character = $derived($currentCharacter);
  let has_custom = $derived(!!character?.has_custom_avatar);
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
  let has_custom_name = $derived(
    !!character && character.display_name !== character.safe_display_name
  );
  let name_locked = $derived(character?.name_locked === true);
  let can_save_custom_name = $derived(
    adult && !name_locked && !name_submitting &&
      custom_name_input.trim().length > 0 &&
      custom_name_input.trim() !== character?.display_name
  );
  let can_clear_custom_name = $derived(
    adult && !name_locked && !name_submitting && has_custom_name
  );
  let can_save_safe_name = $derived(
    !name_locked && !name_submitting && !!safe_word_1 && !!safe_word_2
  );

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

  /**
   * Builds the per-account localStorage key for the dismissed 18+ prompt.
   *
   * @returns Storage key for the current account
   */
  function adult_prompt_storage_key(): string {
    return `${ADULT_PROMPT_KEY_PREFIX}${$user?.id ?? 0}`;
  }

  /**
   * Reads the local dismissal timestamp for the 18+ prompt.
   *
   * @returns Unix milliseconds, or 0 when absent/unreadable
   */
  function adult_prompt_hidden_until(): number {
    try {
      const value = Number(localStorage.getItem(adult_prompt_storage_key()) ?? 0);
      return Number.isFinite(value) ? value : 0;
    } catch {
      return 0;
    }
  }

  /**
   * Adds calendar months, clamping to the last day of the target month.
   *
   * @param date - Starting date
   * @param months - Number of calendar months to add
   * @returns New date
   */
  function add_calendar_months(date: Date, months: number): Date {
    const result = new Date(date.getTime());
    const day = result.getDate();
    result.setDate(1);
    result.setMonth(result.getMonth() + months);
    const last_day = new Date(result.getFullYear(), result.getMonth() + 1, 0).getDate();
    result.setDate(Math.min(day, last_day));
    return result;
  }

  /** Hides the 18+ prompt for the chosen number of months/years. */
  function dismiss_adult_status(): void {
    const raw_value = Number(adult_status_dismiss_value);
    if (!Number.isFinite(raw_value) || raw_value <= 0) return;
    const count = Math.max(1, Math.floor(raw_value));
    const months = adult_status_dismiss_unit === 'years' ? count * 12 : count;
    const until = add_calendar_months(new Date(), months).getTime();
    try {
      localStorage.setItem(adult_prompt_storage_key(), String(until));
    } catch {
      // Storage may be unavailable in private/restricted contexts.
    }
    adult_status_hidden = true;
  }

  /**
   * Picks a random safe word so the selects are never empty on first load.
   *
   * @param words - Safe-word list
   * @returns string - Random word, or "" for an empty list
   */
  function pick_random_safe_word(words: string[]): string {
    if (words.length === 0) return '';
    return words[Math.floor(Math.random() * words.length)];
  }

  /**
   * Best-effort split of the current safe display name back into its two words.
   *
   * @param safe_name - Current `safe_display_name`
   * @returns The matching word pair, or null
   */
  function find_safe_name_words(safe_name: string): { word1: string; word2: string } | null {
    const candidates: string[] = [safe_name];
    const without_dash_suffix = safe_name.replace(/-\d+$/, '');
    if (without_dash_suffix !== safe_name) candidates.push(without_dash_suffix);
    const without_digit_suffix = safe_name.replace(/\d+$/, '');
    if (without_digit_suffix !== safe_name && !candidates.includes(without_digit_suffix)) {
      candidates.push(without_digit_suffix);
    }

    for (const candidate of candidates) {
      for (const first of safe_words_1) {
        if (!candidate.startsWith(first)) continue;
        const second = candidate.slice(first.length);
        if (safe_words_2.includes(second)) return { word1: first, word2: second };
      }
    }
    return null;
  }

  /** Re-selects the current safe words, falling back to random words. */
  function sync_safe_word_selection(): void {
    const c = $currentCharacter;
    if (!c) return;
    const found = find_safe_name_words(c.safe_display_name);
    safe_word_1 = found?.word1 ?? pick_random_safe_word(safe_words_1);
    safe_word_2 = found?.word2 ?? pick_random_safe_word(safe_words_2);
  }

  /** Keeps the custom-name input in sync with the current character. */
  function sync_custom_name_input(): void {
    const c = $currentCharacter;
    custom_name_input =
      c && c.display_name !== c.safe_display_name ? c.display_name : '';
  }

  /**
   * Loads the safe-word catalogs used by the safe-name editor.
   */
  async function load_safe_words(): Promise<void> {
    try {
      const response = await fetch('/safe_words.json');
      const data = await response.json() as {
        safe_words_1: string[];
        safe_words_2: string[];
      };
      safe_words_1 = Array.isArray(data.safe_words_1) ? data.safe_words_1 : [];
      safe_words_2 = Array.isArray(data.safe_words_2) ? data.safe_words_2 : [];
      sync_safe_word_selection();
    } catch (e) {
      console.error('Failed to load safe words:', e);
      error_msg = t('ui_profile_safe_words_error');
    }
  }

  /**
   * Maps a profile-update error to a localized message.
   *
   * @param error - Thrown error
   * @returns string - Localized message or raw server error
   */
  function profile_error_text(error: unknown): string {
    const message = error instanceof Error ? error.message : String(error);
    if (message === 'name_locked') return t('ui_profile_name_locked');
    if (message === 'display_name_too_long') return t('ui_profile_name_too_long');
    if (message === 'word1 and word2 required to change safe name') {
      return t('ui_profile_safe_name_words_required');
    }
    if (message === 'Invalid word1 or word2 - words must exist in safe word lists') {
      return t('ui_profile_safe_name_words_required');
    }
    if (message === 'display_name can only be set if account is adult') {
      return t('ui_profile_custom_name_adult_only');
    }
    return message;
  }

  /**
   * Merges a profile-name update into the current-character store.
   *
   * @param update - Updated self projection from the server
   */
  function apply_profile_update(update: CharacterProfileResponse): void {
    const c = $currentCharacter;
    if (!c) return;
    currentCharacter.set({
      ...c,
      display_name: update.display_name,
      safe_display_name: update.safe_display_name,
      is_adult: update.is_adult ?? c.is_adult,
      name_locked: update.name_locked ?? c.name_locked
    });
    sync_custom_name_input();
    sync_safe_word_selection();
  }

  /** Saves the adult custom display name. */
  async function save_custom_name(): Promise<void> {
    const c = $currentCharacter;
    const name = custom_name_input.trim();
    if (!c || !name || name_locked) return;
    name_submitting = true;
    error_msg = '';
    info_msg = '';
    try {
      const update = await updateCharacterProfileRequest(c.id, { display_name: name });
      apply_profile_update(update);
      info_msg = t('ui_profile_custom_name_saved');
    } catch (e) {
      error_msg = profile_error_text(e);
    } finally {
      name_submitting = false;
    }
  }

  /** Clears the custom display name, reverting to the safe name. */
  async function clear_custom_name(): Promise<void> {
    const c = $currentCharacter;
    if (!c || name_locked || !has_custom_name) return;
    name_submitting = true;
    error_msg = '';
    info_msg = '';
    try {
      const update = await updateCharacterProfileRequest(c.id, { clear_display_name: true });
      apply_profile_update(update);
      info_msg = t('ui_profile_custom_name_cleared');
    } catch (e) {
      error_msg = profile_error_text(e);
    } finally {
      name_submitting = false;
    }
  }

  /** Regenerates the safe display name from the selected word pair. */
  async function save_safe_name(): Promise<void> {
    const c = $currentCharacter;
    if (!c || name_locked || !safe_word_1 || !safe_word_2) return;
    name_submitting = true;
    error_msg = '';
    info_msg = '';
    try {
      const update = await updateCharacterProfileRequest(c.id, {
        word1: safe_word_1,
        word2: safe_word_2
      });
      apply_profile_update(update);
      info_msg = t('ui_profile_safe_name_saved');
    } catch (e) {
      error_msg = profile_error_text(e);
    } finally {
      name_submitting = false;
    }
  }

  /**
   * Creates a prefilled account support ticket requesting 18+ verification.
   *
   * Staff review is the current path. The deferred automatic Digital
   * Credentials flow is documented in docs/adult_verification.md.
   */
  async function request_adult_status(): Promise<void> {
    const note = adult_status_note.trim();
    if (!note) {
      adult_status_error = t('ui_profile_adult_status_note_required');
      return;
    }
    adult_status_submitting = true;
    adult_status_error = '';
    try {
      const result = await createSupportTicketRequest(
        t('ui_profile_adult_status_subject'),
        `${t('ui_profile_adult_status_message')}\n\n${note}`,
        'account',
        $currentCharacter?.id
      );
      navigate({ type: 'activity', id: 'support', rest: [result.ticket.id] });
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      if (message === 'support_locked') {
        adult_status_error = t('ui_support_locked');
      } else if (message === 'refresh_unavailable') {
        adult_status_error = t('ui_auth_server_unavailable');
      } else {
        adult_status_error = message;
      }
    } finally {
      adult_status_submitting = false;
    }
  }

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    adult_status_hidden = ($user?.id ?? 0) > 0 &&
      adult_prompt_hidden_until() > Date.now();
    sync_custom_name_input();
    await load_safe_words();
    try {
      // The safe pool is gender-split; request the character's own pool.
      const gender = character?.sex === 'female' ? 'female' : 'male';
      const data = await getAvatarPoolRequest(gender);
      pool = data.safe;
      adult = data.adult || character?.is_adult === true;
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
      has_custom_avatar: cleared_custom
        ? false
        : (update.has_custom_avatar ?? (update.avatar_custom_id ? true : c.has_custom_avatar)),
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
        <div class="fw-semibold mt-2">{character?.display_name ?? ''}</div>
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

        {#if adult}
          <h5>{t('ui_profile_custom_name')}</h5>
          <p class="text-muted small">{t('ui_profile_custom_name_note')}</p>
          <div class="d-flex align-items-center gap-3 flex-wrap">
            <input
              class="form-control form-control-sm"
              style="max-width: 20rem;"
              type="text"
              maxlength="64"
              bind:value={custom_name_input}
              placeholder={t('ui_profile_custom_name_placeholder')}
              disabled={name_submitting || name_locked}
            />
            <button
              class="btn btn-outline-primary btn-sm"
              disabled={!can_save_custom_name}
              onclick={save_custom_name}
            >
              {t('ui_profile_custom_name_save')}
            </button>
            {#if has_custom_name}
              <button
                class="btn btn-outline-secondary btn-sm"
                disabled={!can_clear_custom_name}
                onclick={clear_custom_name}
              >
                {t('ui_profile_custom_name_clear')}
              </button>
            {/if}
          </div>

          <h5 class="mt-4">{t('ui_profile_custom')}</h5>
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

          <hr class="my-4" />
        {/if}

        <h5>{t('ui_profile_safe_name')}</h5>
        <p class="text-muted small">{t('ui_profile_safe_name_note')}</p>
        {#if name_locked}
          <p class="text-muted small mb-2">{t('ui_profile_name_locked')}</p>
        {/if}
        <p class="text-muted small">
          {t('ui_profile_safe_name_current', { name: character?.safe_display_name ?? '' })}
        </p>
        {#if safe_words_1.length === 0 || safe_words_2.length === 0}
          <p class="text-muted small mb-0">{t('ui_profile_safe_words_error')}</p>
        {:else}
          <div class="row g-2">
            <div class="col-12 col-sm-6">
              <label class="form-label small" for="safe-name-word-1">
                {t('ui_profile_safe_name_word1')}
              </label>
              <select
                id="safe-name-word-1"
                class="form-select form-select-sm"
                bind:value={safe_word_1}
                disabled={name_submitting || name_locked}
              >
                {#each safe_words_1 as word}
                  <option value={word}>{word}</option>
                {/each}
              </select>
            </div>
            <div class="col-12 col-sm-6">
              <label class="form-label small" for="safe-name-word-2">
                {t('ui_profile_safe_name_word2')}
              </label>
              <select
                id="safe-name-word-2"
                class="form-select form-select-sm"
                bind:value={safe_word_2}
                disabled={name_submitting || name_locked}
              >
                {#each safe_words_2 as word}
                  <option value={word}>{word}</option>
                {/each}
              </select>
            </div>
          </div>
          <div class="mt-3">
            <button
              class="btn btn-outline-primary btn-sm"
              disabled={!can_save_safe_name}
              onclick={save_safe_name}
            >
              {t('ui_profile_safe_name_save')}
            </button>
          </div>
        {/if}

        <h5 class="mt-4">{t('ui_profile_safe')}</h5>
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

        {#if !adult && !adult_status_hidden}
          <hr class="my-4" />

          <h5>{t('ui_profile_adult_status_title')}</h5>
          <p class="text-muted small">{t('ui_profile_adult_status_body')}</p>

          <label class="form-label small" for="adult-status-note">
            {t('ui_profile_adult_status_note')}
          </label>
          <textarea
            id="adult-status-note"
            class="form-control form-control-sm"
            rows="4"
            bind:value={adult_status_note}
            placeholder={t('ui_profile_adult_status_note_placeholder')}
            oninput={() => { adult_status_error = ''; }}
            disabled={adult_status_submitting}
            required
          ></textarea>

          {#if adult_status_error}
            <div class="alert alert-danger small py-1 mt-2">{adult_status_error}</div>
          {/if}

          <div class="d-flex align-items-center gap-3 flex-wrap mt-3">
            <button
              class="btn btn-outline-primary btn-sm"
              disabled={adult_status_submitting || !adult_status_note.trim()}
              onclick={request_adult_status}
            >
              {t('ui_profile_adult_status_request')}
            </button>
          </div>
          <p class="text-muted small mt-2 mb-0">{t('ui_profile_adult_status_help')}</p>

          <div class="d-flex align-items-center gap-2 flex-wrap mt-3 small">
            <span>{t('ui_profile_adult_status_hide_for')}</span>
            <input
              class="form-control form-control-sm"
              style="width: 5rem;"
              type="number"
              min="1"
              max="120"
              bind:value={adult_status_dismiss_value}
            />
            <select class="form-select form-select-sm" style="width: auto;" bind:value={adult_status_dismiss_unit}>
              <option value="months">{t('ui_profile_adult_status_months')}</option>
              <option value="years">{t('ui_profile_adult_status_years')}</option>
            </select>
            <button class="btn btn-sm btn-link text-secondary p-0" onclick={dismiss_adult_status}>
              {t('ui_profile_adult_status_hide')}
            </button>
          </div>
        {/if}
      </div>
    </div>
  {/if}
</div>
