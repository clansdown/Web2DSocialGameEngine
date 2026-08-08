<script lang="ts">
  /**
   * CombatVoice — WebRTC voice chat (mesh topology) for the player's team.
   *
   * WebRTC is the web standard for P2P media: the server only relays
   * signaling (offer/answer/ICE) over the combat WebSocket; media flows
   * directly between players. The server never carries audio.
   *
   * ICE servers: a public STUN server is a placeholder — production will
   * need a TURN relay for players behind symmetric NATs. Configure it here
   * (and document in docs/combat_protocol.md).
   */
  import { onMount, onDestroy } from 'svelte';
  import { loadTexts } from '../lib/text';
  import type { CombatNetClient } from './CombatNetClient';
  import type { combat_voice_payload } from './protocol';

  // STUN placeholder — see docs/combat_protocol.md (TURN required in prod).
  const ICE_SERVERS: RTCIceServer[] = [
    { urls: 'stun:stun.l.google.com:19302' }
  ];

  interface Props {
    net: CombatNetClient;
    /** Teammates (excluding self) available for voice connections. */
    teammates: number[];
    /** Parent calls this with every inbound voice payload. */
    register_voice_handler: (handler: (payload: combat_voice_payload) => void) => void;
  }

  let { net, teammates, register_voice_handler }: Props = $props();

  let active = $state(false);
  let error_message = $state('');
  let texts = $state<Record<string, string>>({});
  let local_stream: MediaStream | null = null;
  const peers = new Map<number, RTCPeerConnection>();

  /**
   * Creates (or returns) the peer connection for a teammate.
   *
   * @param to_player - Teammate's character id
   * @returns RTCPeerConnection
   */
  function get_peer(to_player: number): RTCPeerConnection {
    const existing = peers.get(to_player);
    if (existing) return existing;

    const pc = new RTCPeerConnection({ iceServers: ICE_SERVERS });
    if (local_stream) {
      for (const track of local_stream.getTracks()) {
        pc.addTrack(track, local_stream);
      }
    }
    pc.onicecandidate = (event) => {
      if (event.candidate) {
        net.voice(to_player, 'ice', { candidate: event.candidate.toJSON() });
      }
    };
    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'failed' || pc.connectionState === 'closed') {
        peers.delete(to_player);
        pc.close();
      }
    };
    peers.set(to_player, pc);
    return pc;
  }

  /**
   * Sends an offer to a teammate and creates the local description.
   *
   * @param to_player - Teammate's character id
   * @returns Promise<void>
   */
  async function create_offer(to_player: number): Promise<void> {
    if (!local_stream) return;
    const pc = get_peer(to_player);
    try {
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      net.voice(to_player, 'offer', { sdp: offer.sdp });
    } catch (e) {
      console.error('[combat voice] offer failed:', e);
    }
  }

  /**
   * Starts voice chat: acquires the microphone and opens a peer connection
   * to every currently-known teammate.
   *
   * @param none
   * @returns Promise<void>
   */
  async function enable(): Promise<void> {
    try {
      local_stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    } catch (e) {
      error_message = 'Microphone access denied';
      console.error('[combat voice] getUserMedia failed:', e);
      return;
    }
    active = true;
    for (const teammate of teammates) {
      await create_offer(teammate);
    }
  }

  /**
   * Stops voice chat and tears down all peer connections.
   *
   * @param none
   * @returns void
   */
  function disable(): void {
    active = false;
    for (const [id, pc] of peers) {
      pc.close();
      peers.delete(id);
    }
    if (local_stream) {
      for (const track of local_stream.getTracks()) {
        track.stop();
      }
      local_stream = null;
    }
  }

  /**
   * Handles an inbound signaling message from a teammate.
   *
   * @param payload - combat_voice_payload relayed by the server
   * @returns Promise<void>
   */
  async function receive(payload: combat_voice_payload): Promise<void> {
    if (!active) return;
    const from = payload.from;
    const pc = get_peer(from);

    if (payload.signal_type === 'offer') {
      const sdp = (payload.data as { sdp?: string }).sdp ?? '';
      const offer: RTCSessionDescriptionInit = { type: 'offer', sdp };
      try {
        await pc.setRemoteDescription(offer);
        const answer = await pc.createAnswer();
        await pc.setLocalDescription(answer);
        net.voice(from, 'answer', { sdp: answer.sdp });
      } catch (e) {
        console.error('[combat voice] offer handling failed:', e);
      }
    } else if (payload.signal_type === 'answer') {
      const sdp = (payload.data as { sdp?: string }).sdp ?? '';
      try {
        await pc.setRemoteDescription({ type: 'answer', sdp });
      } catch (e) {
        console.error('[combat voice] answer handling failed:', e);
      }
    } else if (payload.signal_type === 'ice') {
      const candidate = (payload.data as { candidate?: RTCIceCandidateInit }).candidate;
      if (candidate) {
        try {
          await pc.addIceCandidate(candidate);
        } catch (e) {
          console.error('[combat voice] ice candidate failed:', e);
        }
      }
    }
  }

  onMount(() => {
    register_voice_handler((payload: combat_voice_payload) => {
      void receive(payload);
    });
    void enable();
  });

  onDestroy(() => {
    disable();
  });

  $effect(() => {
    loadTexts(['combat_voice_enabled', 'combat_voice_error']).then((loaded) => {
      texts = loaded;
    });
  });
</script>

{#if error_message}
  <div class="alert alert-warning py-1 px-2 mb-2">{error_message}</div>
{:else if active}
  <div class="badge text-bg-success">{texts.combat_voice_enabled}</div>
{/if}
