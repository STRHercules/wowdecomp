# In-game checklist: combat, training and adventure (spec section 3)

Live realm `esteria-coa:20261108f-housing` (slice26). New decor in the chest's **Utility** category (1 g each): Training
Dummy, Tank Dummy, Healing Dummy, Boss Dummy, Portal, Meeting Stone. `/reload` after the patch MPQ and addon update.

| # | Do | Expect |
|---|---|---|
| 1 | Buy and place a **Training Dummy**; target it. | The Training Hall panel opens. Attacking starts a session: time, damage, DPS and targets count up each second. |
| 2 | Stop attacking for 15 s, or press **Stop**. | The session ends with a summary; at least 10 s of work can set a **Personal best**. |
| 3 | **Settings** (owner): level 83, armor 10000, health 500000, *Can be defeated*, 60 s. **Save**. | The dummy takes the new values; it can die, and comes back 5 s later; the session ends at 60 s. |
| 4 | Place two dummies and hit both. | **Targets** reads 2 and damage adds up across them. |
| 5 | **Tank Dummy**: hit it once. | It fights back in melee; **Damage taken** counts. |
| 6 | **Healing Dummy**: target and heal it. | It loses health every 2 s; **Healing**, **HPS** and **Overhealing** count; letting it bottom out counts **Patient dropped**. |
| 7 | **Boss Dummy**: hit it. | Every 10 s it casts Shadow Bolt (interrupt it: **Interrupts 1 of 1**); every 12 s a void zone appears under you (standing in it counts **Puddle hits**); after the enrage time the session fails. Each drill can be turned off in Settings. |
| 8 | Place a **Portal**, click it. | Owner sees Name, Destination (Cities, My Homes), Look (4 portal models) and Who may use it; **Save**. **Travel** asks first, then goes. Cities check faction and level. |
| 9 | A party member clicks your portal (Who: Party members). | They can travel; anyone else is told it is not open to them. |
| 10 | Place a **Meeting Stone**, **Bind** it (Classic / Burning Crusade / Wrath). | It shows the dungeon and its level. **Travel** lands beside that dungeon's summoning stone, outside. **Take Party** asks each member beside you. |
| 11 | Open your spellbook (General). | **Teleport: Home** (15 min) and **Portal: Home** (1 h), both 10 s casts. They fail in dungeons, battlegrounds and combat. |
| 12 | Neighborhood window, My Homes, **Options**, *Teleport: Home leads here*. | That home shows (Home); Teleport: Home lands in front of its door. Portal: Home opens a portal for a minute that your party can use. |

## Send back

Screenshots, Lua errors, and `docker logs ac-worldserver 2>&1 | grep housing:`.

## Undo

- Client: `_backups\housing-adventure-*` in each client folder (patch MPQ and addon).
- Server: `G:\housing-build\live-backup-20261108-134147\`; point `docker-compose.twoforms.yml` back at
  `esteria-coa:20261108e-housing`.
