#!/usr/bin/env python3
"""Backfill English authoring context for static game text.

The translation tool reads free-form English context from:

- ``game/text/en/classes/<prefix>.meta`` for a whole text-ID family
- ``game/text/en/<id>.meta`` for one text's specifics

This script seeds those files. Class contexts cover the mechanical bulk (UI
labels, error strings, button text); per-file contexts are written for prose,
letters, codex entries, and other texts over 150 bytes. Existing context is
never overwritten unless ``--force`` is passed.

Usage::

    ./tools/backfill_text_meta.py --dry-run
    ./tools/backfill_text_meta.py
    ./tools/backfill_text_meta.py --force
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Final

PROJECT_ROOT: Final[Path] = Path(__file__).resolve().parent.parent
TEXT_DIR: Final[Path] = PROJECT_ROOT / "game" / "text"
SOURCE_DIR: Final[Path] = TEXT_DIR / "en"
CLASS_DIR: Final[Path] = SOURCE_DIR / "classes"
PROSE_BYTE_THRESHOLD: Final[int] = 150

# Shared context for a text-ID prefix family. Every top-level prefix in the
# corpus has an entry so every text resolves to at least one class context.
CLASS_CONTEXTS: Final[dict[str, str]] = {
    "ui": (
        "These are player-facing interface strings for Ravenest: Build and "
        "Battle. The speaker is the game interface, not a character; the "
        "addressee is the player. Use a neutral, clear, period-appropriate "
        "register. Translate the meaning, not the English wording. Preserve "
        "placeholders, capitalisation conventions, and the brevity needed for "
        "buttons, labels, tabs, and tooltips. Do not add explanatory text or "
        "punctuation the source does not have."
    ),
    "ui_manor": (
        "Manor management screen. The speaker is the game UI; the addressee is "
        "the player. These strings cover buildings, production, land, water, "
        "morale, retinue, and trade. Use the established fiefdom/building/"
        "resource terminology consistently. Preserve compact cost, rate, and "
        "percentage formatting."
    ),
    "ui_building": (
        "Names of fiefdom buildings. The speaker is the game UI; the addressee "
        "is the player. Use the established translated building terminology "
        "consistently with building descriptions, production panels, and tech "
        "trees. These are labels, not descriptions."
    ),
    "ui_resource": (
        "Names of fiefdom resources. The speaker is the game UI; the addressee "
        "is the player. Translate each resource consistently across the whole "
        "game; do not use different synonyms on different screens."
    ),
    "ui_chat": (
        "Chat interface strings. The speaker is the game UI; the addressee is "
        "the player. Do not translate user-generated content, character names, "
        "or sample message text. Preserve placeholders such as {chat_name}. "
        "Keep labels short and unambiguous."
    ),
    "ui_staff": (
        "Staff console interface. The speaker is the staff tool; the addressee "
        "is a staff member. Use neutral administrative language. Do not soften "
        "moderation, account, or error text."
    ),
    "ui_support": (
        "Player support ticket interface. The speaker is the support system; "
        "the addressee is the player. Keep categories, states, and privacy "
        "wording plain and unambiguous."
    ),
    "ui_profile": (
        "Account/profile screen. The speaker is the UI; the addressee is the "
        "player. Keep privacy, age, and avatar wording precise. Do not "
        "translate technical format names (PNG, WebP, etc.) unless the source "
        "does."
    ),
    "ui_notification": (
        "Notification inbox labels and system notification titles. The speaker "
        "is the system; the addressee is the player. Keep them short and "
        "factual."
    ),
    "ui_barony": (
        "Barony membership and creation screen. A barony is a player "
        "organization, not a building. The speaker is the game UI; the "
        "addressee is the player. Use the established translation for "
        "barony/honor and keep role names consistent. Safe name/tag catalogues "
        "are server-owned and are not translated here."
    ),
    "ui_dukedom": (
        "Dukedom placeholder strings. A dukedom is a higher noble title and "
        "organization; keep it distinct from a barony. The speaker is the game "
        "UI; the addressee is the player. These are mostly labels."
    ),
    "ui_patent": (
        "Land/barony patent and honor-grant UI. The speaker is the Crown or the "
        "royal administration; the addressee is the player. Use formal "
        "medieval legal and administrative language. Preserve the distinction "
        "between land patent, freehold, honor, and barony."
    ),
    "ui_path": (
        "Starting-path selection (Wolf Warden / Assarter of the Wildlands). The "
        "speaker is the game/narrator; the addressee is the player choosing a "
        "path. Keep the two path names in English as required by the global "
        "instructions. Translate descriptions naturally; do not invent lore."
    ),
    "ui_campaign": (
        "Campaign and marche introduction UI. The speaker is the Crown or game "
        "narrator; the addressee is the player. Use a formal but readable "
        "briefing tone. Keep place and marche names consistent."
    ),
    "ui_minigame": (
        "Mini-game selection and description UI. The speaker is the game UI; "
        "the addressee is the player. Describe the tower-defence and weeding "
        "games accurately. Do not translate proper names or image paths."
    ),
    "ui_letters": (
        "In-game letter inbox UI. The speaker is the game UI; the addressee is "
        "the player. Keep labels short. Actual letter bodies are separate "
        "texts."
    ),
    "ui_invitations": (
        "Invitation-code UI. The speaker is the game UI; the addressee is the "
        "player. Keep single-use, limit, and revocation wording precise. Do not "
        "translate code values."
    ),
    "ui_registration": (
        "Registration and account-creation UI. The speaker is the game UI; the "
        "addressee is a prospective player. Keep age, invitation, and legal "
        "wording precise; do not soften requirements."
    ),
    "ui_language": (
        "Language-selection labels. These are language names shown in the "
        "selector. Use the target language's own endonym where that is the "
        "convention (for example Español, Deutsch, 日本語), otherwise the "
        "standard target-language name. Do not translate language codes."
    ),
    "ui_unit": (
        "Player unit and class names. The speaker is the game UI; the addressee "
        "is the player. Use the established translated class names consistently "
        "across retinue, combat, and tech UI. Do not translate proper names."
    ),
    "ui_mission": (
        "Mission-selection UI. The speaker is the game UI or briefing officer; "
        "the addressee is the player. Keep mission names distinct and "
        "consistent with combat offering text."
    ),
    "ui_activity": (
        "Hub activity card titles. These are short navigation labels; translate "
        "for clarity and keep them distinct. Do not add descriptions."
    ),
    "ui_character": (
        "Character selection and profile labels. The speaker is the UI; the "
        "addressee is the player. Keep labels short and avoid gendered "
        "assumptions about the player."
    ),
    "ui_honor": (
        "Honor and barony naming UI. The speaker is the Crown or royal "
        "administration; the addressee is the player. Use formal noble/legal "
        "language distinct from land patent and freehold."
    ),
    "combat": (
        "Realtime combat UI and mission text. The speaker is the game UI, a "
        "briefing officer, or the narrator; the addressee is the player "
        "commander. Use terse, military language for commands and HUD labels, "
        "and slightly more descriptive language for briefings. Preserve class "
        "names, placeholders, map ids, and match codes."
    ),
    "combat_store": (
        "Premium cosmetic store UI. The speaker is the store UI; the addressee "
        "is the player. These are appearance-only items; do not imply gameplay "
        "power. Keep tier and slot names consistent. Platinum is a product "
        "term and is not translated."
    ),
    "combat_mission": (
        "PvE mission and offering copy. The speaker is the muster or briefing "
        "voice; the addressee is the player. Describe danger and objectives "
        "clearly. Keep mission names and difficulty bands consistent."
    ),
    "combat_cmd": (
        "Combat command-bar labels (Move, Attack, Patrol, Lob, etc.). Use the "
        "shortest natural target-language verb that fits a button. Do not add "
        "punctuation."
    ),
    "combat_results": (
        "Post-battle results labels. The speaker is the game UI; the addressee "
        "is the player. Keep victory, defeat, wound, and XP text factual and "
        "concise."
    ),
    "combat_lobby": (
        "Combat lobby and matchmaking UI. The speaker is the UI; the addressee "
        "is the player. Keep share-code and join wording precise. Do not "
        "translate match codes."
    ),
    "combat_deploy": (
        "Deployment phase UI. The speaker is the UI; the addressee is the "
        "player commander. Keep placement, facing, and ready instructions short "
        "and unambiguous."
    ),
    "combat_voice": (
        "Voice-chat UI. The speaker is the UI; the addressee is the player. "
        "Keep permission and error wording precise. Do not translate browser or "
        "technical terms unless the source does."
    ),
    "combat_chat": (
        "In-match text chat UI. The speaker is the UI; the addressee is the "
        "player. Do not translate user-generated messages, player names, or team "
        "names. Keep placeholders intact."
    ),
    "combat_offering": (
        "Generated mission offering labels. The speaker is the game UI; the "
        "addressee is the player. Keep party size, power, and danger labels "
        "consistent with the combat system terminology."
    ),
    "combat_danger": (
        "Danger-band labels. Use a consistent severity scale. These are short "
        "labels, not sentences."
    ),
    "combat_phase": (
        "Match-phase labels (lobby, deploy, battle, ended). Keep them short and "
        "consistent with the combat HUD."
    ),
    "reeve": (
        "The Reeve is the player's manor advisor. The speaker is the Reeve; the "
        "addressee is the player. Use a practical, knowledgeable, slightly "
        "formal voice. Advice must stay actionable and concrete. Preserve "
        "placeholders such as {building}, {resource}, {amount}, and {percent}. "
        "Do not add new recommendations."
    ),
    "reeve_error": (
        "Reeve action error messages. The speaker is the Reeve or the system; "
        "the addressee is the player. State the failure plainly and, where the "
        "source does, point to the missing requirement. Do not soften errors."
    ),
    "reeve_detail": (
        "Reeve report detail panels. The speaker is the Reeve; the addressee is "
        "the player. Explain the cause and effect of manor conditions in plain "
        "language. Keep resource and building terms consistent."
    ),
    "reeve_status": (
        "Reeve status labels. These are short assessments such as ok, tight, "
        "heavy, and critical. Keep the severity scale consistent across every "
        "status text."
    ),
    "reeve_headline": (
        "Reeve report headlines. The speaker is the Reeve; the addressee is the "
        "player. Match the severity of the underlying condition and keep them "
        "short."
    ),
    "reeve_why": (
        "Reeve 'why' explanations. The speaker is the Reeve; the addressee is "
        "the player. Explain the reason for the recommendation without adding "
        "new advice."
    ),
    "reeve_style": (
        "Reeve style descriptions. The speaker is the game UI; the addressee is "
        "the player. Describe advisor styles accurately; do not change the "
        "meaning of the style names."
    ),
    "wd": (
        "Weeding mini-game text. The speaker is the narrator or game UI; the "
        "addressee is the player, the Assarter. Use an instructional, grounded, "
        "slightly wry medieval voice. Preserve all HTML tags, image paths, and "
        "Markdown headings exactly. Plant and tool names are game terms; "
        "translate them consistently."
    ),
    "wd_intro": (
        "Weeding level introduction. The speaker is the narrator; the addressee "
        "is the player. Explain the new weed or tool and what the player must "
        "do. Preserve inline images and layout."
    ),
    "wd_outro": (
        "Weeding level completion text. The speaker is the narrator; the "
        "addressee is the player. Confirm what was cleared and introduce the "
        "next idea. Preserve inline images."
    ),
    "wd_plant": (
        "Weeding codex entry for a weed. The speaker is the game codex; the "
        "addressee is the player. Keep the heading and image tag. Describe the "
        "plant and its threat clearly."
    ),
    "wd_tool": (
        "Weeding codex entry for a tool. The speaker is the game codex; the "
        "addressee is the player. Keep the heading and image tag. Describe what "
        "the tool is for."
    ),
    "wd_smother": (
        "Weeding codex entry for a smother crop. The speaker is the game codex; "
        "the addressee is the player. Keep the heading and image tag. Explain "
        "the crop's role."
    ),
    "wd_special": (
        "Weeding special or event text. The speaker is the narrator or game UI; "
        "the addressee is the player. Preserve any special mechanics and image "
        "tags."
    ),
    "wd_weeding": (
        "Weeding campaign overview. The speaker is the Crown or narrator; the "
        "addressee is the player. Use a formal briefing tone. Keep marche and "
        "hyde terminology consistent."
    ),
    "wd_ongoing": (
        "Weeding ongoing and reward info. The speaker is the game UI; the "
        "addressee is the player. Keep reward placeholders and silver "
        "formatting intact."
    ),
    "td": (
        "Tower-defence mini-game text. The speaker is the narrator or game UI; "
        "the addressee is the player, the Wolf Warden. Use an instructional, "
        "immersive voice. Preserve Markdown headings, HTML image tags, and "
        "placeholders. Unit, tower, and enemy names are game terms; translate "
        "them consistently."
    ),
    "td_book": (
        "Tower-defence codex entry. The speaker is the game codex; the addressee "
        "is the player. Keep the heading and image tag. Describe the unit, "
        "tower, or enemy and its tactical role clearly."
    ),
    "td_custom": (
        "Custom tower-defence game setup. The speaker is the UI; the addressee "
        "is the player. Keep round, difficulty, and reward wording precise."
    ),
    "td_ongoing": (
        "Tower-defence ongoing and reward info. The speaker is the game UI; the "
        "addressee is the player. Keep reward placeholders and silver "
        "formatting intact."
    ),
    "retinue": (
        "Retinue management UI. The speaker is the game UI; the addressee is the "
        "player. Keep class, role, equipment, and health labels consistent. Do "
        "not translate soldier names. Preserve placeholders."
    ),
    "activity": (
        "Hub activity card titles. These are short navigation labels; translate "
        "for clarity and keep them distinct. Do not add descriptions."
    ),
    "names": (
        "Proper-name pools for retinue recruits. Each file is one full name per "
        "line. Preserve the exact line count and order. Localize naturally: "
        "translate transparent occupational or descriptive bynames, transliterate "
        "personal names when the target script requires it, and keep names that "
        "read as proper names. Preserve gender. Do not number, merge, split, or "
        "annotate lines."
    ),
    "names_knight": (
        "Knight names use Sir/Dame, Norman or French surnames, and descriptive "
        "bynames. Localize titles and transparent bynames; keep surnames and "
        "'de' particles where natural."
    ),
    "names_archer": (
        "Archer names use given names plus occupational and descriptive bynames. "
        "Translate the bynames idiomatically; transliterate given names as needed."
    ),
    "names_spearman": (
        "Spearman names use given names plus place and descriptive bynames. "
        "Translate the bynames idiomatically."
    ),
    "names_alchemist": (
        "Alchemist names use given names plus occupational and descriptive "
        "bynames (pitch-maker, distiller, and so on). Translate the bynames "
        "idiomatically."
    ),
    "names_mage": (
        "Mage names are given names, many Greek or mythological. Use the "
        "conventional target-language form; do not add surnames or titles."
    ),
    "game": (
        "Mini-game result and loading strings. The speaker is the game UI; the "
        "addressee is the player. Keep victory, defeat, and loading text short "
        "and factual."
    ),
    "path": (
        "Starting-path introduction. The speaker is the narrator; the addressee "
        "is the player choosing a path. Keep 'Wolf Warden' and 'Assarter of the "
        "Wildlands' in English. Translate descriptions naturally; do not invent "
        "lore. Preserve PLACEHOLDER markers as authoring markers, not final "
        "player-facing prose."
    ),
    "mission": (
        "Mission-selection introduction and title. The speaker is the Crown or "
        "briefing voice; the addressee is the player. Keep mission names "
        "distinct and consistent."
    ),
    "letter": (
        "In-game letter. The speaker is the named sender; the addressee is the "
        "player character. Use formal medieval epistolary language. Preserve "
        "salutations, sign-offs, gender tokens, and <seal> tags. Do not "
        "translate the sender's name."
    ),
    "barony": (
        "Barony patent text. The speaker is the Crown; the addressee is the "
        "player. Use formal legal and noble language. Preserve gender tokens and "
        "placeholders."
    ),
    "kings": (
        "Royal message. The speaker is the King or his officer; the addressee is "
        "the player character. Use formal medieval court language. Preserve "
        "gender tokens and sign-offs."
    ),
    "land": (
        "Land patent. The speaker is the Crown; the addressee is the player. Use "
        "formal legal language. Preserve Markdown and gender tokens."
    ),
    "manor": (
        "Manor HUD label. The speaker is the game UI; the addressee is the "
        "player. Keep it short."
    ),
}

SPECIAL_CONTEXTS: Final[dict[str, str]] = {
    "barony_patent_earned": (
        "Formal letters patent from the King of Ravenest granting the player "
        "the title of Mesne Lord and the right to found an Honor. Speaker: the "
        "King of Ravenest, writing through the royal chancery. Addressee: the "
        "player character, addressed by name. Intent: confer the honor, define "
        "the grant, and explain the path from Honor to hereditary Barony. Tone: "
        "formal, archaic, legal, ceremonial. Notes: preserve the legal cadence, "
        "the {character_name}, {honor_name}, and {son|daughter}/{his|her} "
        "gender tokens, and the <seal> tag. Do not translate the royal style or "
        "invent additional titles."
    ),
    "land_patent_earned": (
        "Formal letters patent from the King of Ravenest granting the player a "
        "land patent and the right to hold a manor within a barony. Speaker: "
        "the King of Ravenest, writing through the royal chancery. Addressee: "
        "the player character, addressed by name. Intent: confer the land grant "
        "and set out the feudal obligations. Tone: formal, archaic, legal. "
        "Notes: preserve the Markdown bold opening, the {character_name}, "
        "{his|her}, and {him|her} gender tokens, and the <seal> tag. Keep the "
        "distinction between land patent, manor, barony, and fealty."
    ),
    "kings_message_unlock": (
        "Royal message announcing that the King has granted the player the use "
        "of a new unit. Speaker: Lord Stokeguile, the King's Master Wolf-Hunter, "
        "writing on the King's behalf. Addressee: the player character, "
        "addressed as Lord or Lady. Intent: announce the unlock and thank the "
        "player for service as Wolf Warden. Tone: formal, courtly, slightly "
        "warm. Notes: preserve the {Lord|Lady}, {character_name}, and {unit_name} "
        "tokens, the <unit> element, and the <seal> tag. Do not translate the "
        "sender's name or title."
    ),
    "letter_mesne_lord_eligible": (
        "Letter from the Crown informing the player that they are eligible to "
        "become a Mesne Lord and found an Honor. Speaker: the King's Steward, "
        "writing for the Crown. Addressee: the player character, addressed as "
        "Lord or Lady. Intent: explain the offer, the alternative of remaining a "
        "lone knight, and the choice the player must make. Tone: formal, "
        "measured, courteous. Notes: preserve the {Lord|Lady} and "
        "{character_name} tokens and the <seal> tag. Keep the distinction "
        "between Mesne Lord, Honor, Outlying Freehold, and hereditary Baron."
    ),
    "ui_patent_body": (
        "Explanation shown after the player receives a Land Patent. Speaker: the "
        "game UI or royal administration. Addressee: the player. Intent: explain "
        "that the patent entitles them to found a manor and set out the two "
        "paths: join an existing barony or found their own. Tone: informative, "
        "slightly formal, encouraging at the end. Notes: keep barony, manor, "
        "Honor, Mesne Lord, and Baron distinct."
    ),
    "ui_patent_honor_body": (
        "Prompt shown when the player has earned the right to found an Honor. "
        "Speaker: the Crown or royal administration. Addressee: the player. "
        "Intent: explain the grant and ask the player to choose the Honor's "
        "name. Tone: formal, ceremonial, encouraging. Notes: keep Honor, Barony, "
        "and Letters Patent distinct. Do not translate the name the player will "
        "enter."
    ),
    "ui_honor_body": (
        "Explanation shown when the player names an Honor. Speaker: the Crown or "
        "royal administration. Addressee: the player. Intent: explain that the "
        "manor becomes the seat of the Honor and that underlords and eventual "
        "Barony follow. Tone: formal, encouraging. Notes: keep Mesne Lord, "
        "Honor, manor, underlord, and Baron distinct."
    ),
    "td_intro": (
        "Narration at the start of the Wolf Warden tower-defence mission. "
        "Speaker: the narrator, briefing the player as Wolf Warden. Addressee: "
        "the player. Intent: explain the player's role, the map, the hunt "
        "economy, hiring, rounds, permanent towers, repositioning, and recalling "
        "units. Tone: instructional, immersive, medieval. Notes: keep 'Wolf "
        "Warden' in English; preserve Markdown and line breaks. Translate "
        "'men at arms', 'tower-builders', and 'Master Wolf Hunter' consistently "
        "with the rest of the mini-game."
    ),
    "wd_weeding_intro": (
        "Overview text for the weeding/assart mini-game. Speaker: the narrator "
        "or game tutorial. Addressee: the player, the Assarter. Intent: explain "
        "the turn economy, tools, smother crops, weed spread, par, and finishing "
        "the assart. Tone: instructional, grounded, slightly wry. Notes: "
        "preserve the line breaks and any markup. Keep assart, bane-weed, "
        "smother crop, par, and forfeit/finish terminology consistent."
    ),
    "ui_path_select_intro": (
        "Introduction shown on the starting-path selection screen. Speaker: the "
        "narrator or the Crown. Addressee: the player choosing a starting path. "
        "Intent: set the scene and explain that the player must choose between "
        "the Wolf Warden and the Assarter of the Wildlands. Tone: atmospheric, "
        "formal, inviting. Notes: keep both path names in English. Preserve "
        "Markdown and line breaks. Do not invent lore beyond the source."
    ),
    "ui_minigame_tower_defense_desc": (
        "Description of the tower-defence mini-game. Speaker: the game UI. "
        "Addressee: the player choosing an activity. Intent: explain that the "
        "player raises towers, musters men, and holds the line against wolves "
        "and beasts. Tone: concise, exciting, and clear. Notes: keep the game's "
        "mechanics accurate and do not translate the mini-game name unless the "
        "source does."
    ),
    "td_defeat_message": (
        "Defeat message for the tower-defence mini-game. Speaker: the narrator "
        "or game UI. Addressee: the player, the Wolf Warden. Intent: explain "
        "that beasts got past the defenders and the land remains unsafe. Tone: "
        "blunt, disappointed, not cruel. Notes: match the source's short length; "
        "keep beast, tower, and Wolf Warden terminology consistent."
    ),
    "wd_defeat_message": (
        "Defeat message for the weeding mini-game. Speaker: the narrator or game "
        "UI. Addressee: the player, the Assarter. Intent: explain that the weeds "
        "spread faster than the player could clear them and the land remains "
        "unarable. Tone: disappointed but grounded, slightly wry. Notes: keep "
        "weed, assart, and bane-weed terminology consistent."
    ),
    "wd_ongoing_info": (
        "Ongoing weeding information. Speaker: the narrator or game UI. "
        "Addressee: the player, the Assarter. Intent: explain the boundless task "
        "beyond the marches and how to choose the next plot. Tone: instructional, "
        "grounded, slightly wry. Notes: preserve any placeholders and image tags; "
        "keep assart and marche terminology consistent."
    ),
    "wd_ongoing_rewards": (
        "Ongoing weeding reward text. Speaker: the game UI. Addressee: the "
        "player, the Assarter. Intent: explain the silver reward for a cleared "
        "plot and the finest spoils. Tone: clear and reward-focused. Notes: "
        "preserve the {rewards} placeholder and silver formatting."
    ),
    "td_custom_game_info": (
        "Setup text for a custom tower-defence game. Speaker: the game UI. "
        "Addressee: the player. Intent: explain the round and difficulty "
        "choices and how rewards scale. Tone: clear and instructional. Notes: "
        "keep rounds, difficulty, and rewards consistent with the rest of the "
        "tower-defence UI."
    ),
    "td_custom_game_rewards": (
        "Reward explanation for a custom tower-defence game. Speaker: the game "
        "UI. Addressee: the player. Intent: explain that more rounds mean "
        "greater risk and greater reward. Tone: clear and encouraging. Notes: "
        "preserve any placeholders and silver formatting."
    ),
    "td_ongoing_info": (
        "Ongoing tower-defence information. Speaker: the game UI or narrator. "
        "Addressee: the player. Intent: explain the current hunt, waves, and "
        "what the player should expect. Tone: instructional and immersive. "
        "Notes: keep unit, tower, enemy, and round terminology consistent."
    ),
    "td_ongoing_rewards": (
        "Ongoing tower-defence reward text. Speaker: the game UI. Addressee: "
        "the player. Intent: explain the silver reward and the finest spoils. "
        "Tone: clear and reward-focused. Notes: preserve the {rewards} "
        "placeholder and silver formatting."
    ),
    "wd_weeding_marche": (
        "Introduction to the Wildlands-Marche. Speaker: the Crown or narrator. "
        "Addressee: the player, the Assarter. Intent: explain the marche's "
        "composition and the work ahead. Tone: formal briefing with a practical "
        "edge. Notes: keep marche, hyde, and assart terminology consistent."
    ),
    "wd_weeding_great_wildlands_marche": (
        "Introduction to the Great Wildlands Marche. Speaker: the Crown or "
        "narrator. Addressee: the player, the Assarter. Intent: present the "
        "King's most challenging commission and its sixteen hydes. Tone: formal, "
        "daunting, encouraging. Notes: keep marche, hyde, and assart terminology "
        "consistent."
    ),
    "ui_campaign_intro_wolf_warden": (
        "Wolf-Marche campaign introduction. Speaker: the Crown or narrator. "
        "Addressee: the player, the Wolf Warden. Intent: explain the marche's "
        "composition and the campaign ahead. Tone: formal briefing. Notes: keep "
        "Wolf Warden and Wolf-Marche in English where the source does; keep "
        "hyde/marche terminology consistent."
    ),
    "ui_campaign_intro_great_wolf_warden": (
        "Great Wolf Marche campaign introduction. Speaker: the Crown or "
        "narrator. Addressee: the player, the Wolf Warden. Intent: present the "
        "King's most challenging wolf hunt and its sixteen hydes. Tone: formal, "
        "daunting, encouraging. Notes: keep Wolf Warden and Great Wolf Marche in "
        "English where the source does."
    ),
    "path_assarter_intro_body": (
        "Placeholder introduction body for the Assarter of the Wildlands path. "
        "Speaker: the narrator. Addressee: the player choosing the Assarter "
        "path. Intent: introduce the settler/land-clearing fantasy. Tone: "
        "atmospheric and grounded. Notes: this is explicitly PLACEHOLDER text; "
        "preserve the PLACEHOLDER marker rather than treating it as final prose."
    ),
    "path_wolf_warden_intro_body": (
        "Placeholder introduction body for the Wolf Warden path. Speaker: the "
        "narrator. Addressee: the player choosing the Wolf Warden path. Intent: "
        "introduce the wilderness guardian fantasy. Tone: atmospheric and "
        "grounded. Notes: this is explicitly PLACEHOLDER text; preserve the "
        "PLACEHOLDER marker rather than treating it as final prose."
    ),
}


def _read_text(path: Path) -> str:
    """Read a UTF-8 text file.

    Args:
        path: File to read.

    Returns:
        The file contents, or an empty string when missing.
    """
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def _write_context(path: Path, content: str, force: bool, dry_run: bool) -> bool:
    """Write one context file unless it already exists and force is false.

    Args:
        path: Destination .meta path.
        content: Free-form English context.
        force: Overwrite an existing file.
        dry_run: Print instead of writing.

    Returns:
        True when a write happened (or would happen in dry-run mode).
    """
    if path.exists() and not force:
        return False
    if dry_run:
        print(f"[dry-run] write {path.relative_to(PROJECT_ROOT)}")
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.strip() + "\n", encoding="utf-8")
    print(f"wrote {path.relative_to(PROJECT_ROOT)}")
    return True


def _matching_prefixes(text_id: str, prefixes: list[str]) -> list[str]:
    """Return every class prefix that applies to a text ID.

    Args:
        text_id: Text ID (filename stem).
        prefixes: Known class prefixes.

    Returns:
        Matching prefixes, broadest first.
    """
    matches: list[str] = [
        prefix for prefix in prefixes
        if text_id == prefix or text_id.startswith(prefix + "_")
    ]
    matches.sort(key=len)
    return matches


def _humanize(text_id: str) -> str:
    """Turn a text ID into a readable label for generated context.

    Args:
        text_id: Text ID.

    Returns:
        A space-separated label.
    """
    return text_id.replace("_", " ")


def _heading_title(content: str) -> str | None:
    """Extract a Markdown heading from the first non-empty line.

    Args:
        content: Text file content.

    Returns:
        The heading text without leading # characters, or None.
    """
    for line in content.splitlines():
        stripped: str = line.strip()
        if not stripped:
            continue
        if stripped.startswith("#"):
            return stripped.lstrip("#").strip()
        return None
    return None


def build_generated_context(text_id: str, content: str) -> str:
    """Build a reasonable per-file context from the ID and content.

    Args:
        text_id: Text ID.
        content: English source text.

    Returns:
        Free-form English context.
    """
    title: str | None = _heading_title(content)
    humanized: str = _humanize(text_id)

    if text_id.startswith("td_book_"):
        subject: str = title or humanized.removeprefix("td book ")
        return (
            f"Tower-defence codex entry for {subject}. Speaker: the game codex. "
            f"Addressee: the player. Intent: describe {subject} and its tactical "
            "role. Tone: informative, in-world, concise. Notes: preserve the "
            "Markdown heading, HTML image tag, and layout exactly. Keep unit, "
            "tower, and enemy names consistent with the rest of the mini-game."
        )
    if text_id.startswith(("wd_plant_", "wd_tool_", "wd_smother_")):
        subject = title or humanized
        return (
            f"Weeding codex entry for {subject}. Speaker: the game codex. "
            f"Addressee: the player. Intent: describe {subject} and its role in "
            "the assart. Tone: informative, grounded, in-world. Notes: preserve "
            "the Markdown heading, HTML image tag, and layout exactly. Keep "
            "plant, tool, and smother-crop terminology consistent."
        )
    if text_id.startswith("wd_intro_"):
        return (
            "Weeding level introduction. Speaker: the narrator. Addressee: the "
            "player, the Assarter. Intent: introduce the new weed or tool and "
            "explain what the player must do in this level. Tone: instructional, "
            "grounded, slightly wry. Notes: preserve inline images, HTML tags, "
            "and line breaks. Keep bane-weed and tool names consistent."
        )
    if text_id.startswith("wd_outro_"):
        return (
            "Weeding level completion text. Speaker: the narrator. Addressee: "
            "the player, the Assarter. Intent: confirm what was cleared and "
            "introduce the next idea or reward. Tone: satisfied, instructional, "
            "slightly wry. Notes: preserve inline images, HTML tags, and line "
            "breaks."
        )
    if text_id.startswith("td_"):
        return (
            f"Tower-defence text for {humanized}. Speaker: the narrator or game "
            "UI. Addressee: the player, the Wolf Warden. Intent: explain the "
            "mechanic, unit, tower, enemy, or result. Tone: instructional and "
            "immersive. Notes: preserve Markdown, HTML tags, image paths, and "
            "placeholders. Keep unit, tower, and enemy names consistent."
        )
    if text_id.startswith("wd_"):
        return (
            f"Weeding text for {humanized}. Speaker: the narrator or game UI. "
            "Addressee: the player, the Assarter. Intent: explain the weed, "
            "tool, crop, or level outcome. Tone: grounded, instructional, "
            "slightly wry. Notes: preserve HTML tags, image paths, and layout."
        )
    if text_id.startswith("ui_"):
        return (
            f"Player-facing UI text for {humanized}. Speaker: the game UI. "
            "Addressee: the player. Intent: explain the screen, state, or "
            "choice clearly. Tone: neutral and consistent with the surrounding "
            "interface. Notes: preserve placeholders and keep the text concise."
        )
    if text_id.startswith("reeve_"):
        return (
            f"Reeve advisor text for {humanized}. Speaker: the Reeve. Addressee: "
            "the player. Intent: explain the manor condition or action. Tone: "
            "practical, knowledgeable, slightly formal. Notes: preserve "
            "placeholders; do not add new advice."
        )
    if text_id.startswith("combat_"):
        return (
            f"Combat text for {humanized}. Speaker: the game UI or briefing "
            "voice. Addressee: the player commander. Intent: convey the command, "
            "mission, or result. Tone: terse and military for UI, more "
            "descriptive for briefings. Notes: preserve class names, "
            "placeholders, and codes."
        )
    return (
        f"Player-facing game text for {humanized}. Speaker: the game UI or "
        "narrator. Addressee: the player. Intent: convey the information in the "
        "source clearly. Tone: match the source. Notes: preserve Markdown, HTML "
        "tags, image paths, and placeholders."
    )


def _should_write_per_file(text_id: str, content: str, byte_size: int) -> bool:
    """Decide whether a text needs its own per-file context.

    Args:
        text_id: Text ID.
        content: English source text.
        byte_size: UTF-8 byte size.

    Returns:
        True for prose/letters and any text without class coverage.
    """
    if text_id in SPECIAL_CONTEXTS:
        return True
    if text_id.startswith("names_"):
        return False
    return byte_size > PROSE_BYTE_THRESHOLD


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments.

    Returns:
        Parsed namespace with force and dry_run fields.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Backfill free-form English translation context sidecars."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        default=False,
        help="Overwrite existing class/per-file context files",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        default=False,
        help="Print pending writes without touching the filesystem",
    )
    return parser.parse_args()


def main() -> int:
    """Create missing class and per-file context files.

    Returns:
        0 on success.
    """
    args: argparse.Namespace = parse_args()
    if not SOURCE_DIR.exists():
        print(f"Error: source text directory not found: {SOURCE_DIR}")
        return 1

    prefixes: list[str] = sorted(CLASS_CONTEXTS.keys(), key=len, reverse=True)
    class_written: int = 0
    per_file_written: int = 0
    unresolved: list[str] = []
    review: list[str] = []

    for prefix, content in CLASS_CONTEXTS.items():
        if _write_context(
            CLASS_DIR / f"{prefix}.meta", content, args.force, args.dry_run
        ):
            class_written += 1

    for text_path in sorted(SOURCE_DIR.glob("*.txt")):
        text_id: str = text_path.stem
        content: str = _read_text(text_path)
        byte_size: int = len(content.encode("utf-8"))
        matches: list[str] = _matching_prefixes(text_id, prefixes)
        if not matches:
            unresolved.append(text_id)
            _write_context(
                SOURCE_DIR / f"{text_id}.meta",
                build_generated_context(text_id, content),
                args.force,
                args.dry_run,
            )
            continue

        if _should_write_per_file(text_id, content, byte_size):
            context: str = SPECIAL_CONTEXTS.get(
                text_id, build_generated_context(text_id, content)
            )
            if _write_context(
                SOURCE_DIR / f"{text_id}.meta", context, args.force, args.dry_run
            ):
                per_file_written += 1
            review.append(text_id)

    print(
        f"\nSummary: {class_written} class context file(s), "
        f"{per_file_written} per-file context file(s), "
        f"{len(review)} per-file review item(s), "
        f"{len(unresolved)} unresolved text(s)."
    )
    if review:
        print("Review the generated prose context for:")
        for text_id in review:
            print(f"  - {text_id}")
    if unresolved:
        print("Unresolved texts (no class prefix):")
        for text_id in unresolved:
            print(f"  - {text_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
