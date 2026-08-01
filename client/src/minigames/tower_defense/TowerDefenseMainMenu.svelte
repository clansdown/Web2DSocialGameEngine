<script lang="ts">
  /**
   * Campaign wrapper for Tower Defense. Renders the campaign map for the
   * player's current track (wolf marche or great wolf marche). Only shown
   * during campaign phases — ongoing mode uses TowerDefenseOngoing instead.
   */

  import { playerGameState } from '../../lib/stores';
  import TowerDefenseCampaign from './TowerDefenseCampaign.svelte';

  interface Props {
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { onStartLevel, onBack }: Props = $props();

  let isBaronTrack = $derived($playerGameState?.game_phase === 'baron_track');
  let campaignSize = $derived(isBaronTrack ? 4 : 3);
  let minLevelId = $derived(isBaronTrack ? 10 : 1);
  let campaignId = $derived(isBaronTrack ? 'great_wolf_marche' : 'wolf_marche');
</script>

<TowerDefenseCampaign
  campaignId={campaignId}
  gridSize={campaignSize}
  minLevelId={minLevelId}
  {onStartLevel}
  {onBack}
/>
