/**
 * Combat game wire protocol — mirrors docs/combat_protocol.md on the server.
 *
 * Envelope (JSON codec): { v: 1, fmt: 'json', type, match_id, tick, payload }
 *
 * Server -> client types: welcome, match_state (full), match_update (partial),
 *   match_ended, chat, voice, error
 * Client -> server types: auth (first message), command, chat, voice, ready,
 *   request_state, leave
 *
 * The combat_codec interface is the client mirror of the server's pluggable
 * serializer: swap in a binary codec later without touching game logic.
 */

export const COMBAT_PROTOCOL_VERSION = 1;

export interface combat_envelope {
  v: number;
  fmt: string;
  type: string;
  match_id: string;
  tick: number;
  payload?: unknown;
}

/** A soldier in battle (matches the server's combat_unit wire row). */
export interface combat_unit_dto {
  id: number;
  owner: number;
  name: string;
  unit_class: string;
  level: number;
  x: number;
  y: number;
  hp: number;
  max_hp: number;
  status: string;
}

export interface combat_event_dto {
  type: string;
  payload: Record<string, unknown>;
}

/** Payload of match_state / match_update messages. */
export interface combat_state_payload {
  full: boolean;
  phase: string;
  countdown?: number;
  battle_time?: number;
  units: combat_unit_dto[];
  removed: number[];
  events: combat_event_dto[];
}

export interface combat_player_dto {
  player_id: number;
  team: number;
  display_name: string;
  ready: boolean;
  connected: boolean;
}

export interface combat_retinue_member_dto {
  member_id: number;
  display_name: string;
  unit_class: string;
  level: number;
  is_knight: boolean;
}

/** Payload of the welcome message. */
export interface combat_welcome_payload {
  player_id: number;
  team: number;
  mode: string;
  match_code: string;
  ruleset_id: string;
  ruleset: Record<string, unknown>;
  map: Record<string, unknown>;
  players: combat_player_dto[];
  retinue: combat_retinue_member_dto[];
}

/** Payload of the match_ended message. */
export interface combat_ended_payload {
  winner_team: number;
  reason: string;
  casualties: { member_id: number; owner: number }[];
}

export interface combat_chat_payload {
  from: number;
  from_name: string;
  team: number;
  text: string;
  timestamp: number;
}

export interface combat_voice_payload {
  from: number;
  from_name: string;
  signal_type: string;
  data: Record<string, unknown>;
}

/** Client mirror of the server codec interface (see CombatCodec.hpp). */
export interface combat_codec {
  format_id(): string;
  /** Serializes an outbound message. */
  encode(type: string, match_id: string, tick: number, payload: unknown): string;
  /** Parses an inbound message; throws on malformed JSON. */
  decode(raw: string): combat_envelope;
}

/** Default JSON codec — mirrors the server's json_codec envelope shape. */
export class json_codec implements combat_codec {
  format_id(): string {
    return 'json';
  }

  encode(type: string, match_id: string, tick: number, payload: unknown): string {
    return JSON.stringify({
      v: COMBAT_PROTOCOL_VERSION,
      fmt: 'json',
      type,
      match_id,
      tick,
      payload
    });
  }

  decode(raw: string): combat_envelope {
    return JSON.parse(raw) as combat_envelope;
  }
}
