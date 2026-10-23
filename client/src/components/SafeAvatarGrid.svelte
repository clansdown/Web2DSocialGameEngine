<script lang="ts">
  /**
   * SafeAvatarGrid renders the approved (safe) portrait pool as a selectable
   * card grid. Shared by the character-creation picker and the Profile panel so
   * both present the same pool identically.
   *
   * Each portrait is shown in a portrait-shaped (1:1.5 w:h) area with
   * `object-fit: contain`, so the whole image is visible (aspect preserved)
   * rather than cropped to a square.
   *
   * @param avatars - Safe pool entries (id + served URL)
   * @param selected - Currently selected avatar id (null = none)
   * @param onSelect - Called with the chosen avatar id
   */
  import type { safe_avatar_dto } from '../lib/api';

  interface Props {
    avatars: safe_avatar_dto[];
    selected: string | null;
    onSelect: (id: string) => void;
  }

  let { avatars, selected, onSelect }: Props = $props();
</script>

<div class="row g-3 justify-content-center">
  {#each avatars as avatar (avatar.id)}
    <div class="col-4 col-sm-3 col-md-2">
      <div
        class="card h-100 {selected === avatar.id ? 'border-primary border-2' : ''}"
        style="cursor: pointer; overflow: hidden;"
        role="button"
        tabindex="0"
        onclick={() => onSelect(avatar.id)}
        onkeydown={(e) => { if (e.key === 'Enter') onSelect(avatar.id); }}
      >
        <img
          src={avatar.url}
          alt=""
          class="card-img-top"
          style="aspect-ratio: 1 / 1.5; object-fit: contain; background: rgba(0, 0, 0, 0.35);"
        />
      </div>
    </div>
  {/each}
</div>
