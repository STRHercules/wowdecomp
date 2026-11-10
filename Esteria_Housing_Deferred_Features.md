# Esteria Housing — Separate Feature Concepts

> **Status:** Proposed housing features maintained separately from the main [Esteria Housing Feature Specification](Esteria_Housing_Feature_Specification.md).
>
> **Purpose:** Preserve the original detailed designs for four features that are no longer part of the main housing specification. This file does not assume they are in the active implementation roadmap.

## Features

### 1. Home Defense System

Introduce an optional **PvE defense mode** in which a property becomes the setting for a wave-based encounter.

- Owners place defenses such as guards, turrets, traps, barricades, and support objects.
- Start an encounter from a housing control interface or defensive command object.
- Spawn waves of attackers with configurable challenge tiers and themes.
- Scale difficulty using supported rules for group size and progression.
- Permit friends or authorized guests to join the defense.
- Show wave progress, objectives, surviving defenses, and end-of-event results.
- Run the encounter in an **isolated event instance or equivalent isolated state** so ordinary visitors are unaffected.
- Restore all decorative furniture and permanent structures after the event; no persistent destruction.
- Offer optional progression or cosmetic rewards with cooldowns/limits that prevent reward farming exploits.

---

### 2. Functional Kitchen

Create a kitchen area with usable cooking stations and interactive furniture.

- Place stoves, ovens, preparation tables, cooking fires, counters, and storage fixtures.
- Craft supported food and drink recipes using the appropriate profession rules.
- Provide animations for supported cooking or preparation interactions.
- Optionally connect kitchen storage to tavern inventory through explicit owner actions.
- Allow owners to designate kitchen staff or public cooking access if those systems are enabled.
- Keep cosmetic food displays separate from actual crafted, consumable items.

---

### 2.2 Player-Owned Taverns

Turn a property into a functioning tavern with a **Bartender NPC**, seating, and consumable sales.

- Stock food, drinks, and eligible crafted consumables for purchase.
- Let the owner configure menu items, prices, bartender appearance, and greetings.
- Allow visitors to sit at tables, counters, and other compatible furniture.
- Support ordering from the bartender or, where feasible, interactive tables.
- Offer temporary **resting or social bonuses** while visiting or after using tavern services.
- Allow decorations, lighting, jukeboxes, and other housing systems to contribute to the tavern atmosphere.
- Keep the tavern accessible and stocked while the owner is offline, subject to visitor permissions.

**Balance rule:** Bonuses should be modest, clearly described, and designed not to replace stronger combat consumables or become mandatory for competitive content.

---

### 3. Shared Guild Halls

Allow guilds to own or designate a shared hall with collaborative management.

- Assign ownership and management permissions to guild leadership and authorized ranks.
- Support rank-based rights for decorating, placing NPCs, accessing storage, and configuring services.
- Enable shared meeting spaces, guild trophies, crafting stations, mailboxes, and guild bank access.
- Allow member teleportation or access based on guild permissions and travel rules.
- Maintain audit logs for important shared actions, including removing furniture and withdrawing shared items.
- Define ownership transfer and asset handling if a guild disbands or changes leadership.

---

### 3.5 Private PvP Arenas

Let homeowners create controlled dueling and practice spaces within their property.

- Designate an arena area and configure boundaries, starting positions, and spectator spots.
- Support consensual duels, team skirmishes, and optional custom match rules.
- Add match controls for starting, ending, resetting, and selecting participants.
- Offer a simple scoreboard for wins, losses, rounds, and match duration.
- Keep arena combat separated from visitors who are not participating.
- Prevent house objects from being permanently damaged by PvP combat.
- Allow spectators without automatically making them combat targets.

**Design rule:** Private arena matches should not award official ranked PvP progression unless Esteria deliberately integrates them with its ranked systems.

---

### 4. Table Games

Introduce placeable, interactive games for social spaces.

- Start with options such as chess, checkers, dice, or original Azeroth-themed tabletop games.
- Let visitors join, spectate, leave, or restart a match through a simple interface.
- Track game turns and enforce rules server-side where competitive outcomes matter.
- Support tables and themed game pieces as placeable furniture.
- Optionally display non-monetary achievements or friendly records.
- Avoid automated real-money or gold wagering mechanics.


