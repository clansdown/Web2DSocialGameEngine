/**
 * WebSocket client for the realtime combat game. Wraps connection lifecycle,
 * first-message auth, typed sends, and automatic reconnect with backoff.
 *
 * Transport notes:
 *  - The socket URL comes from VITE_WS_BASE_URL (or is derived from the API
 *    origin). Empty values fall back to the page origin, which the Vite dev
 *    proxy forwards to the local server. See docs/combat_protocol.md.
 *  - Auth happens in the FIRST message ({type:'auth'}) using the session
 *    token from auth.ts — the same credentials the REST API uses.
 *  - On unexpected close the client reconnects with exponential backoff and
 *    re-authenticates; the server resumes the connection to the match and
 *    sends a fresh full state snapshot.
 */
import { getSessionToken, getStoredUsername } from '../lib/auth';
import { websocket_url } from '../lib/public_urls';
import { json_codec, type combat_codec, type combat_envelope } from './protocol';

export type combat_connection_status = 'connecting' | 'open' | 'reconnecting' | 'closed';

export interface combat_net_callbacks {
  on_message: (envelope: combat_envelope) => void;
  on_status?: (status: combat_connection_status) => void;
  on_error?: (message: string) => void;
}

export class CombatNetClient {
  private ws: WebSocket | null = null;
  private codec: combat_codec;
  private callbacks: combat_net_callbacks;
  private character_id: number;
  private match_id: string;
  private reconnect_delay_ms = 1000;
  private max_reconnect_delay_ms = 10000;
  private closed_by_user = false;
  private reconnect_timer: ReturnType<typeof setTimeout> | null = null;
  private tick = 0;

  constructor(characterId: number, matchId: string, callbacks: combat_net_callbacks) {
    this.character_id = characterId;
    this.match_id = matchId;
    this.callbacks = callbacks;
    this.codec = new json_codec();
  }

  /**
   * Opens the WebSocket and authenticates. Safe to call again after a close.
   *
   * @param none
   * @returns void
   */
  connect(): void {
    this.closed_by_user = false;
    this.set_status('connecting');
    const ws_url = websocket_url('/ws/combat');

    try {
      this.ws = new WebSocket(ws_url);
    } catch (e) {
      this.callbacks.on_error?.('WebSocket creation failed');
      this.set_status('closed');
      return;
    }

    this.ws.onopen = () => {
      this.reconnect_delay_ms = 1000;
      this.set_status('open');
      this.send_auth();
    };

    this.ws.onmessage = (event: MessageEvent) => {
      let envelope: combat_envelope;
      try {
        envelope = this.codec.decode(String(event.data));
      } catch {
        this.callbacks.on_error?.('Received malformed combat message');
        return;
      }
      if (envelope.tick && envelope.tick > this.tick) {
        this.tick = envelope.tick;
      }
      this.callbacks.on_message(envelope);
    };

    this.ws.onerror = () => {
      // onclose follows; the error itself is only useful for diagnostics.
    };

    this.ws.onclose = () => {
      this.ws = null;
      if (this.closed_by_user) {
        this.set_status('closed');
        return;
      }
      this.schedule_reconnect();
    };
  }

  /**
   * Closes the connection permanently (no reconnect). Used on match exit.
   *
   * @param none
   * @returns void
   */
  close(): void {
    this.closed_by_user = true;
    if (this.reconnect_timer !== null) {
      clearTimeout(this.reconnect_timer);
      this.reconnect_timer = null;
    }
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.set_status('closed');
  }

  /**
   * Sends a typed message with the current match id and tick.
   *
   * @param type - Message type (command, chat, voice, ready, request_state, leave)
   * @param payload - JSON-serializable payload
   * @returns boolean - true if the message was queued for send
   */
  send(type: string, payload: unknown): boolean {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return false;
    this.ws.send(this.codec.encode(type, this.match_id, this.tick, payload));
    return true;
  }

  /** Requests a full state snapshot from the server (gap recovery). */
  request_full_state(): void {
    this.send('request_state', {});
  }

  /** Signals readiness in the lobby (starts the countdown when all ready). */
  ready(): void {
    this.send('ready', {});
  }

  /**
   * Issues a move command for the given units.
   *
   * @param unitIds - Retinue member ids to move
   * @param targetX - Normalized 0..1 map x
   * @param targetY - Normalized 0..1 map y
   */
  move_units(unitIds: number[], targetX: number, targetY: number, queued = false): void {
    this.send('command', {
      cmd: 'move',
      unit_ids: unitIds,
      target_x: targetX,
      target_y: targetY,
      queued
    });
  }

  /**
   * Issues an attack command (mechanics scaffolded; relayed as an event).
   *
   * @param unitIds - Attacking units
   * @param targetUnitId - Target unit id
   */
  attack(unitIds: number[], targetUnitId: number, queued = false): void {
    this.send('command', {
      cmd: 'attack',
      unit_ids: unitIds,
      target_unit_id: targetUnitId,
      queued
    });
  }

  /**
   * Issues an ability command (scaffolded; relayed as an event).
   *
   * @param abilityId - Ability id from the unit's abilities
   */
  ability(abilityId: string): void {
    this.send('command', { cmd: 'ability', ability_id: abilityId });
  }

  /**
   * Sets the fielded roster subset (lobby/deploy only). The knight is always
   * fielded server-side; an empty array means "field everything".
   *
   * @param unitIds - Chosen fieldable member ids (knight excluded)
   */
  set_roster(unitIds: number[]): void {
    this.send('roster', { unit_ids: unitIds });
  }

  /**
   * Places squad members inside the team deploy zone (§4.4). Positions are
   * normalized 0..1 map coordinates; `facing` in radians defaults to the
   * server's toward-the-enemy-camp angle when omitted.
   *
   * @param unitIds - Members to place
   * @param targetX - Normalized map x inside the deploy zone
   * @param targetY - Normalized map y inside the deploy zone
   * @param facing - Optional facing in radians
   */
  deploy(unitIds: number[], targetX: number, targetY: number, facing?: number): void {
    const payload: Record<string, unknown> = {
      cmd: 'deploy',
      unit_ids: unitIds,
      x: targetX,
      y: targetY
    };
    if (facing !== undefined) payload.facing = facing;
    this.send('command', payload);
  }

  /**
   * Issues a stance order (`aggressive` | `defensive` | `hold`).
   *
   * @param unitIds - Units affected
   * @param stance - The new stance
   */
  stance(unitIds: number[], stance: string): void {
    this.send('command', { cmd: 'stance', unit_ids: unitIds, stance });
  }

  /**
   * Stops the given units (cancels orders; engage in aggro radius).
   *
   * @param unitIds - Units affected
   */
  stop(unitIds: number[]): void {
    this.send('command', { cmd: 'stop', unit_ids: unitIds });
  }

  /**
   * Holds position (engage within range but never leave the square).
   *
   * @param unitIds - Units affected
   */
  hold(unitIds: number[]): void {
    this.send('command', { cmd: 'hold', unit_ids: unitIds });
  }

  /**
   * Attack-move: move along a path while engaging hostiles en route (§9).
   *
   * @param unitIds - Units affected
   * @param targetX - Normalized map x
   * @param targetY - Normalized map y
   * @param queued - Append rather than replace the order queue
   */
  attack_move(unitIds: number[], targetX: number, targetY: number, queued = false): void {
    this.send('command', {
      cmd: 'attack_move',
      unit_ids: unitIds,
      target_x: targetX,
      target_y: targetY,
      queued
    });
  }

  /**
   * Patrol: loop/ping-pong between the unit's position and a point, engaging
   * along the path (§9).
   *
   * @param unitIds - Units affected
   * @param targetX - Normalized far endpoint x
   * @param targetY - Normalized far endpoint y
   * @param queued - Append rather than replace the order queue
   */
  patrol(unitIds: number[], targetX: number, targetY: number, queued = false): void {
    this.send('command', {
      cmd: 'patrol',
      unit_ids: unitIds,
      target_x: targetX,
      target_y: targetY,
      queued
    });
  }

  /**
   * Explicit AoE Lob to a ground point (§6.3). Non-AoE units ignore the ground
   * point and fall back to a normal attack.
   *
   * @param unitIds - AoE units (alchemists)
   * @param targetX - Normalized blast center x
   * @param targetY - Normalized blast center y
   */
  attack_ground(unitIds: number[], targetX: number, targetY: number, queued = false): void {
    this.send('command', {
      cmd: 'attack',
      unit_ids: unitIds,
      ground_x: targetX,
      ground_y: targetY,
      queued
    });
  }

  /**
   * Constructs a temporary structure at a point (builders channel the build).
   *
   * @param unitIds - Builder units
   * @param structureType - Structure type id (barricade/spikes/wall/tower)
   * @param targetX - Normalized map x
   * @param targetY - Normalized map y
   */
  construct(unitIds: number[], structureType: string, targetX: number, targetY: number): void {
    this.send('command', {
      cmd: 'construct',
      unit_ids: unitIds,
      structure_type: structureType,
      x: targetX,
      y: targetY
    });
  }

  /**
   * Sets (or clears) the persistent formation modifier (§8.3).
   *
   * @param formationId - Shape id, or 'none' to clear
   * @param rotation - Rotation handle in radians
   */
  formation(formationId: string, rotation = 0): void {
    this.send('command', { cmd: 'formation', formation_id: formationId, rotation });
  }

  /**
   * Sends a team chat message.
   *
   * @param text - Message text (max 500 chars; server enforces)
   */
  chat(text: string): void {
    this.send('chat', { text });
  }

  /**
   * Sends a team chat message using a server-owned safe catalog id. Required
   * for minor accounts; custom text is rejected server-side.
   *
   * @param safe_message_id - Safe message catalog id
   */
  chat_safe(safe_message_id: string): void {
    this.send('chat', { safe_message_id });
  }

  /**
   * Relays a WebRTC signaling message to a teammate.
   *
   * @param toPlayer - Target player id
   * @param signalType - 'offer' | 'answer' | 'ice'
   * @param data - SDP or ICE candidate payload
   */
  voice(toPlayer: number, signalType: string, data: Record<string, unknown>): void {
    this.send('voice', { to_player: toPlayer, signal_type: signalType, data });
  }

  /** Forfeits the match (units abandon the field) and closes the socket. */
  leave(): void {
    this.send('leave', {});
    this.closed_by_user = true;
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.set_status('closed');
  }

  private send_auth(): void {
    const token = getSessionToken();
    const username = getStoredUsername();
    if (!token || !username) {
      this.callbacks.on_error?.('Not authenticated');
      this.close();
      return;
    }
    const payload = {
      username,
      token,
      character_id: this.character_id,
      match_id: this.match_id
    };
    this.ws?.send(this.codec.encode('auth', this.match_id, 0, payload));
  }

  private schedule_reconnect(): void {
    if (this.closed_by_user) return;
    this.set_status('reconnecting');
    this.reconnect_timer = setTimeout(() => {
      this.reconnect_timer = null;
      this.connect();
    }, this.reconnect_delay_ms);
    this.reconnect_delay_ms = Math.min(this.reconnect_delay_ms * 2, this.max_reconnect_delay_ms);
  }

  private set_status(status: combat_connection_status): void {
    this.callbacks.on_status?.(status);
  }
}
