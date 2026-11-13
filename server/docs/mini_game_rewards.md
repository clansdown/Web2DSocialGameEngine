# Mini-game rewards

Mini-game rewards come from three places:

1. **Campaign quest files** — per-quest overrides.
2. **`game/config/mini_games.json`** — the default campaign reward table.
3. **Ongoing configs** — regular/replay silver rewards.

## Campaign quest overrides

Tower-defense quests live in `game/config/tower_defense/spawn_schedules/`.
Weeding/Assarting quests live in `game/config/weeding/`.

Each quest file may define:

```json
{
  "display_name_key": "campaign_wolf_marche",
  "default_level_reward": { "gold": 0, "grain": 0 },
  "completion_bonus": {
    "base_unlock": true,
    "resources": { "gold": 0, "wood": 0, "grain": 0 }
  },
  "levels": {
    "1": {
      "reward": { "gold": 0, "grain": 0 }
    }
  }
}
```

Precedence for a campaign level reward:

1. `levels.<id>.reward` in the quest file.
2. `default_level_reward` in the quest file.
3. `mini_games.json` `levels[]` / `baron_levels[]` reward.

Precedence for the one-time completion bonus:

1. Quest `completion_bonus`.
2. `mini_games.json` `completion_bonus`.

An explicit zero map is still an override; it does not fall through to the
default table. Omitting the quest fields keeps the `mini_games.json` behavior.

## Current campaign content

All four campaign quest files configure zero level rewards and zero completion
bonuses:

- `tower_defense/spawn_schedules/wolf_marche.json`
- `tower_defense/spawn_schedules/great_wolf_marche.json`
- `weeding/wildlands_marche.json`
- `weeding/great_wildlands_marche.json`

Regular/replay mini-game events continue to use
`tower_defense/ongoing.json`, `weeding/ongoing.json`, and
`economy.json.reward_pools`.

## Payout rules

- Campaign rewards are credited only on the **first completion** of a level.
  The `mini_game_progress.completed` flag is checked before the progression
  update, so a server-side replay returns `rewards: {}` and pays nothing.
- The server allows a replay (the client is responsible for locking completed
  campaign levels); first-completion gating prevents reward farming.
- Completion bonuses are awarded only during the one-time phase transition:
  `initial_mission -> land_patent` or `baron_track -> baron_right`.
- Rewards are credited to the character's fiefdom through
  `FiefdomFetcher::updateFiefdomResources`. Pre-manor rewards are not banked;
  keep pre-manor quest rewards at zero.
- In-match currency (`mobs.json.reward_gold`, Tower Defense gold) is a separate
  system and is never used for these rewards.
- Ongoing mode already credits its silver pool via `apply_ongoing_reward`; do
  not credit it a second time as a campaign reward.

## Client display

`client/src/minigames/MiniGameContainer.svelte` filters reward and completion
bonus entries to positive amounts and hides the whole Rewards block when
nothing positive remains. The `Completion bonus` label is
`ui_minigame_completion_bonus`.

Completed campaign levels are not clickable in the campaign grids, and a
manually entered `#/game/<game>/<level>` URL is redirected back to the task
page when that level is already completed.
