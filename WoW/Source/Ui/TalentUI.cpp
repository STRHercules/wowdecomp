#include "WoW/Source/Ui/TalentUI.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lua.h"

// The circular tree (Wind-Sworn), generated from talents.json by gen_headers.py. The server's kTalents is the
// same table: the ids, parents and max ranks must match, and the order is talents.json's order, which the
// tally's rank string relies on. Tree: 0 dive, 1 glide, 2 wings, 3 none. Kind: 0 core, 1 node, 2 bridge,
// 3 big talent (cap), 4 linked. This mirrors the wire contract and is never authority: the server re-derives
// every rule and refuses what it does not allow.
namespace
{
    constexpr int kKindCore   = 0;
    constexpr int kKindNode   = 1;
    constexpr int kKindBridge = 2;
    constexpr int kKindCap    = 3;

    struct TalentEffectDef
    {
        const char* m_key;
        float       m_per;
    };

    struct TalentRowDef
    {
        int             m_id;
        int             m_tree;
        int             m_ring;
        int             m_kind;
        int             m_maxRank;
        int             m_nEffects;
        TalentEffectDef m_effects[3];
        int             m_parents[3];
        int             m_between[2];
        float           m_angle;
        const char*     m_name;
        const char*     m_desc;
    };

    // The bridges: the next-ring talent closest to each in the two trees it joins (see Talents.lua's links).
    struct BridgeReachDef
    {
        int m_bridge;
        int m_node[2];
    };

    constexpr BridgeReachDef kBridgeReach[] = {{140, {108, 123}}, {141, {121, 136}}, {142, {110, 134}},
                                             {143, {111, 126}}, {144, {124, 139}}, {145, {113, 137}}};

    const TalentRowDef kTalents[] =
    {
    {100, 3, 0, 0, 1, 1, {{"speed_all", 0.2f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {0, 0, 0}, {3, 3}, 0.0f, "Windborn", "All flight speeds +20%."},  // Windborn
    {101, 0, 1, 1, 3, 1, {{"dive_ms", -0.02f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {100, 0, 0}, {3, 3}, 67.0f, "Steep Descent", "Dives start 2% faster per rank."},  // Steep Descent
    {102, 0, 1, 1, 3, 1, {{"climb_decel", 0.03f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {100, 0, 0}, {3, 3}, 113.0f, "Held Breath", "Keep your dive speed 3% longer when you climb, per rank."},  // Held Breath
    {103, 0, 2, 1, 3, 1, {{"dive_speed", 0.02f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {101, 0, 0}, {3, 3}, 56.0f, "Terminal Velocity", "Dive speed +2% per rank."},  // Terminal Velocity
    {104, 0, 2, 1, 3, 1, {{"blend_ms", -0.03f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {101, 102, 0}, {3, 3}, 90.0f, "Sharp Banking", "Speed changes 3% faster per rank."},  // Sharp Banking
    {105, 0, 2, 1, 3, 1, {{"dive_yield", 0.04f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {102, 0, 0}, {3, 3}, 124.0f, "Dive Yield", "4% more progress while diving, per rank."},  // Dive Yield
    {106, 0, 3, 1, 3, 1, {{"shallow_pull", 9.0f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {103, 104, 0}, {3, 3}, 73.0f, "Shallow Pull", "Pull out of a dive 9 degrees shallower, per rank."},  // Shallow Pull
    {107, 0, 3, 1, 1, 1, {{"aftershock", 1.0f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {104, 105, 0}, {3, 3}, 107.0f, "Aftershock", "Releasing the boost key in a dive boosts at once."},  // Aftershock
    {108, 0, 4, 1, 3, 1, {{"dive_speed", 0.04f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {106, 0, 0}, {3, 3}, 60.0f, "Gravity Well", "Dive speed +4% per rank."},  // Gravity Well
    {109, 0, 4, 1, 3, 1, {{"boost_carry", 0.06f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {106, 107, 0}, {3, 3}, 90.0f, "Rebound", "Keep 6% more of a boost after it ends, per rank."},  // Rebound
    {110, 0, 4, 1, 2, 1, {{"boost_add_dive", 0.16f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {107, 0, 0}, {3, 3}, 120.0f, "Dive Bomb", "Boosts add 16% more speed while diving, per rank."},  // Dive Bomb
    {111, 0, 5, 1, 1, 1, {{"dive_ms", -0.35f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {108, 109, 0}, {3, 3}, 62.0f, "Meteor", "Dives start 35% faster."},  // Meteor
    {112, 0, 5, 1, 2, 1, {{"dive_speed", 0.1f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {109, 0, 0}, {3, 3}, 90.0f, "Hammerfall", "Dive speed +10% per rank."},  // Hammerfall
    {113, 0, 5, 1, 2, 2, {{"speed_cap", 200.0f}, {"dive_speed", 0.04f}, {nullptr, 0.0f}}, {109, 110, 0}, {3, 3}, 118.0f, "Redline", "Dive speed +4% per rank and a higher speed limit."},  // Redline
    {114, 1, 1, 1, 3, 1, {{"glide_ms", 0.02f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {100, 0, 0}, {3, 3}, 307.0f, "Long Glide", "Glide 2% longer after a dive, per rank."},  // Long Glide
    {115, 1, 1, 1, 3, 1, {{"climb_speed", 0.01f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {100, 0, 0}, {3, 3}, 353.0f, "Updraft", "Climb speed +1% per rank."},  // Updraft
    {116, 1, 2, 1, 3, 1, {{"level_speed", 0.02f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {114, 0, 0}, {3, 3}, 296.0f, "Skyward Path", "Level flight speed +2% per rank."},  // Skyward Path
    {117, 1, 2, 1, 3, 1, {{"yard_step", -0.05f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {114, 115, 0}, {3, 3}, 330.0f, "Slipstream", "Point costs grow 5% slower, per rank."},  // Slipstream
    {118, 1, 2, 1, 2, 1, {{"hover_ms", -0.12f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {115, 0, 0}, {3, 3}, 364.0f, "Hover Poise", "Leave a hover 12% faster, per rank."},  // Hover Poise
    {119, 1, 3, 1, 3, 1, {{"level_yield", 0.05f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {116, 117, 0}, {3, 3}, 313.0f, "Long Haul", "5% more progress in level flight and climbs, per rank."},  // Long Haul
    {120, 1, 3, 1, 1, 1, {{"boost_glide", 0.09f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {117, 118, 0}, {3, 3}, 347.0f, "Glide Anchor", "Boosts keep 9% more of your glide speed."},  // Glide Anchor
    {121, 1, 4, 1, 3, 1, {{"level_speed", 0.05f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {119, 0, 0}, {3, 3}, 300.0f, "Jetstream", "Level flight speed +5% per rank."},  // Jetstream
    {122, 1, 4, 1, 3, 1, {{"blend_ms", -0.05f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {119, 120, 0}, {3, 3}, 330.0f, "Draft", "Speed changes 5% faster per rank."},  // Draft
    {123, 1, 4, 1, 3, 1, {{"climb_speed", 0.06f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {120, 0, 0}, {3, 3}, 360.0f, "Updraft Master", "Climb speed +6% per rank."},  // Updraft Master
    {124, 1, 5, 1, 1, 1, {{"glide_ms", 0.5f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {121, 122, 0}, {3, 3}, 302.0f, "Eternal Current", "Glide 50% longer after a dive."},  // Eternal Current
    {125, 1, 5, 1, 2, 1, {{"level_speed", 0.1f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {122, 0, 0}, {3, 3}, 330.0f, "Highway", "Level flight speed +10% per rank."},  // Highway
    {126, 1, 5, 1, 2, 1, {{"boost_glide", 0.2f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {122, 123, 0}, {3, 3}, 358.0f, "Skybound", "Boosts keep 20% more of your glide speed, per rank."},  // Skybound
    {127, 2, 1, 1, 3, 1, {{"boost_add", 0.01f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {100, 0, 0}, {3, 3}, 187.0f, "Iron Wings", "Boosts add 1% more speed per rank."},  // Iron Wings
    {128, 2, 1, 1, 3, 1, {{"stack_interval", -0.01f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {100, 0, 0}, {3, 3}, 233.0f, "Quick Recovery", "Boost charges refill 1% faster per rank."},  // Quick Recovery
    {129, 2, 2, 1, 3, 1, {{"stack_dive_interval", -0.02f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {127, 0, 0}, {3, 3}, 176.0f, "Diving Wings", "Boost charges refill 2% faster while diving, per rank."},  // Diving Wings
    {130, 2, 2, 1, 3, 1, {{"boost_fade", 0.02f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {127, 128, 0}, {3, 3}, 210.0f, "Afterburn", "Boosts last 2% longer per rank."},  // Afterburn
    {131, 2, 2, 1, 1, 1, {{"boost_stacks", 1.0f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {128, 0, 0}, {3, 3}, 244.0f, "Wing Reserve", "Hold 1 more boost charge."},  // Wing Reserve
    {132, 2, 3, 1, 3, 1, {{"boost_carry", 0.06f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {129, 130, 0}, {3, 3}, 193.0f, "Banked Momentum", "Keep 6% more of a boost after it ends, per rank."},  // Banked Momentum
    {133, 2, 3, 1, 1, 1, {{"boost_speed", 0.04f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {130, 131, 0}, {3, 3}, 227.0f, "Overcharge", "Boost speed limit +4%."},  // Overcharge
    {134, 2, 4, 1, 3, 1, {{"boost_add", 0.05f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {132, 0, 0}, {3, 3}, 180.0f, "Surge Cells", "Boosts add 5% more speed per rank."},  // Surge Cells
    {135, 2, 4, 1, 3, 1, {{"stack_interval", -0.03f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {132, 133, 0}, {3, 3}, 210.0f, "Fast Cells", "Boost charges refill 3% faster per rank."},  // Fast Cells
    {136, 2, 4, 1, 1, 1, {{"start_stacks", 1.0f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {133, 0, 0}, {3, 3}, 240.0f, "Reserve Charge", "Start each flight with 1 more boost charge."},  // Reserve Charge
    {137, 2, 5, 1, 1, 1, {{"boost_stacks", 1.0f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {134, 135, 0}, {3, 3}, 182.0f, "Twin Vault", "Hold 1 more boost charge."},  // Twin Vault
    {138, 2, 5, 1, 2, 1, {{"boost_speed", 0.06f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {135, 0, 0}, {3, 3}, 210.0f, "Overdrive", "Boost speed limit +6% per rank."},  // Overdrive
    {139, 2, 5, 1, 2, 1, {{"boost_fade", 0.1f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {135, 136, 0}, {3, 3}, 238.0f, "Last Gasp", "Boosts last 10% longer per rank."},  // Last Gasp
    {140, 0, 3, 2, 1, 1, {{"climb_threshold", 25.0f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {103, 118, 0}, {0, 1}, 30.0f, "Still Air", "Climbing starts at 25 degrees instead of 15."},  // Still Air
    {141, 1, 3, 2, 1, 1, {{"dive_threshold", 65.0f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {116, 131, 0}, {1, 2}, 270.0f, "Tucked Wings", "Diving starts at 65 degrees instead of 75."},  // Tucked Wings
    {142, 2, 3, 2, 1, 1, {{"climb_decel", 0.24f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {105, 129, 0}, {2, 0}, 150.0f, "Rising Current", "Climbs keep 24% more of your speed."},  // Rising Current
    {143, 0, 5, 2, 2, 2, {{"level_speed", 0.04f}, {"dive_speed", 0.04f}, {nullptr, 0.0f}}, {108, 123, 0}, {0, 1}, 30.0f, "Skimmer", "Level and dive speed +4% per rank."},  // Skimmer
    {144, 1, 5, 2, 2, 2, {{"boost_add", 0.06f}, {"level_speed", 0.03f}, {nullptr, 0.0f}}, {121, 136, 0}, {1, 2}, 270.0f, "Windfall", "Boosts add 6% more speed and level speed +3%, per rank."},  // Windfall
    {145, 2, 5, 2, 2, 2, {{"stack_dive_interval", -0.08f}, {"dive_speed", 0.03f}, {nullptr, 0.0f}}, {110, 134, 0}, {2, 0}, 150.0f, "Thunderhead", "Diving charges refill 8% faster and dive speed +3%, per rank."},  // Thunderhead
    {146, 0, 6, 3, 1, 2, {{"dive_speed", 0.5f}, {"dive_ms", -0.3f}, {nullptr, 0.0f}}, {111, 112, 113}, {3, 3}, 90.0f, "Falcon's Stoop", "Dive speed +50% and dives start 30% faster."},  // Falcon's Stoop
    {147, 1, 6, 3, 1, 2, {{"level_speed", 0.4f}, {"climb_speed", 0.4f}, {nullptr, 0.0f}}, {124, 125, 126}, {3, 3}, 330.0f, "Endless Horizon", "Level and climb speed +40%."},  // Endless Horizon
    {148, 2, 6, 3, 1, 3, {{"boost_stacks", 1.0f}, {"boost_add", 0.25f}, {"boost_speed", 0.08f}}, {137, 138, 139}, {3, 3}, 210.0f, "Tempest Heart", "1 more boost charge, boosts add 25% more speed, boost speed limit +8%."},  // Tempest Heart
    {149, 0, 7, 4, 35, 1, {{"speed_all", 0.01f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {146, 0, 0}, {3, 3}, 90.0f, "Stoop Mastery", "All flight speeds +1% per rank."},  // Stoop Mastery
    {150, 1, 7, 4, 35, 1, {{"speed_all", 0.01f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {147, 0, 0}, {3, 3}, 330.0f, "Horizon Mastery", "All flight speeds +1% per rank."},  // Horizon Mastery
    {151, 2, 7, 4, 35, 1, {{"speed_all", 0.01f}, {nullptr, 0.0f}, {nullptr, 0.0f}}, {148, 0, 0}, {3, 3}, 210.0f, "Tempest Mastery", "All flight speeds +1% per rank."},  // Tempest Mastery
    };

    constexpr int kTalentCount = static_cast<int>(sizeof(kTalents) / sizeof(kTalents[0]));
    constexpr int kMaxPoints = 103;   // the most ranks any legal build can hold (core 1 + 102)

    // Ranks the panel may send in one B message: the addon cap is 255 bytes and a pair is about 7.
    constexpr int kBatchPairs = 12;

    // A batch or a refund the server has not answered is resent on this clock, up to kMaxSends times, so a
    // dropped addon message is retried instead of lost. Every entry is an absolute rank, so a resend is idempotent.
    constexpr uint32_t kResendMs = 3000;
    constexpr int      kMaxSends = 3;

    // While the panel is open the tally is re-requested this often (it recovers a server restart too).
    constexpr uint32_t kHeartbeatMs = 5000;

    // A refusal stays on the status line this long before an ordinary tally may wipe it.
    constexpr uint32_t kStatusHoldMs = 4000;

    struct State
    {
        bool     m_tablePushed = false;
        void*    m_pushedInto = nullptr;
        bool     m_dirty = true;

        uint32_t m_points = 0;
        uint32_t m_spent = 0;
        uint32_t m_unspent = 0;
        bool     m_haveTally = false;

        // Confirmed rank per talent, by position in kTalents, from the last tally.
        int      m_rank[kTalentCount] = {};

        // The absolute rank the player has staged, 0 = nothing asked. The panel is optimistic: a click moves
        // this, not the wire, and the tally retires it once the server's rank has caught up.
        int      m_want[kTalentCount] = {};

        // Progress toward the next point, from the WXLTT "P" body or the tally's tail.
        uint32_t m_partialPermille = 0;
        uint32_t m_needYards = 0;

        uint32_t m_nowMs = 0;
        uint32_t m_lastSync = 0;
        uint32_t m_lastQueryMs = 0;
        uint32_t m_statusAtMs = 0;
        char     m_status[96] = {};

        bool     m_confirming = false;
        uint32_t m_confirmSentMs = 0;
        int      m_confirmSends = 0;

        bool     m_resetPending = false;
        uint32_t m_resetSentMs = 0;
        int      m_resetSends = 0;
    };

    State s_state;

    lua_State* L()
    {
        return FrameScript_GetContext();
    }

    void SetGlobalNumber(const char* name, double value)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        lua_pushnumber(lua, value);
        lua_setglobal(lua, name);
    }

    void SetGlobalString(const char* name, const char* value)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        lua_pushstring(lua, value ? value : "");
        lua_setglobal(lua, name);
    }

    void ClearGlobal(const char* name)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        lua_pushnil(lua);
        lua_setglobal(lua, name);
    }

    // True when the global is a non-zero number or a true boolean; an absent one is false.
    bool ReadGlobalFlag(const char* name)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return false;
        }

        lua_getglobal(lua, name);

        const bool on = lua_isnumber(lua, -1) ? lua_tonumber(lua, -1) != 0.0 : lua_toboolean(lua, -1) != 0;

        lua_pop(lua, 1);
        return on;
    }

    // SpeedFlying puts Manastorm_TalentSend on the wire (WXLTT prefix, throttled) and then clears it, so the slot is
    // free again once it has gone. A body queued while the slot is still full would overwrite an unsent one.
    bool QueueSend(const char* body)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return false;
        }

        lua_getglobal(lua, "Manastorm_TalentSend");

        const bool busy = lua_isstring(lua, -1) && lua_tostring(lua, -1)[0] != '\0';

        lua_pop(lua, 1);

        if (busy)
        {
            return false;
        }

        SetGlobalString("Manastorm_TalentSend", body);
        return true;
    }

    int IndexOfId(int id)
    {
        for (int i = 0; i < kTalentCount; i++)
        {
            if (kTalents[i].m_id == id)
            {
                return i;
            }
        }

        return -1;
    }

    // --- the tree's rules, restated for the grey-out and the batch builder only: the server vets every click --

    bool IsBridge(int i)
    {
        return kTalents[i].m_kind == kKindBridge;
    }

    bool IsCap(int i)
    {
        return kTalents[i].m_kind == kKindCap;
    }

    // The unlock half of the rule: some parent at full rank, or a bridge's forward reach. `ranks` is one rank per
    // talent in table order; the core is always open.
    bool ParentsOpen(const int* ranks, int i)
    {
        const TalentRowDef& t = kTalents[i];

        if (t.m_kind == kKindCore)
        {
            return true;
        }

        bool open = false;

        for (int p = 0; p < 3 && !open; p++)
        {
            const int at = t.m_parents[p] ? IndexOfId(t.m_parents[p]) : -1;

            open = at >= 0 && ranks[at] >= kTalents[at].m_maxRank;
        }

        // a bridge (ring 3 or 5) at full rank opens the next-ring talent closest to it in the tree on its far side
        // (the server's ParentFull): reached from a tree it leads into the other, never back
        if (t.m_kind == kKindNode)
        {
            for (const BridgeReachDef& r : kBridgeReach)
            {
                const int b = IndexOfId(r.m_bridge);

                if (b < 0 || ranks[b] < kTalents[b].m_maxRank)
                {
                    continue;
                }

                for (int s = 0; s < 2 && !open; s++)
                {
                    const int other = IndexOfId(r.m_node[1 - s]);

                    if (r.m_node[s] != t.m_id || other < 0)
                    {
                        continue;
                    }

                    for (int p = 0; p < 3 && !open; p++)
                    {
                        const int at = kTalents[b].m_parents[p] ? IndexOfId(kTalents[b].m_parents[p]) : -1;

                        open = at >= 0 && kTalents[at].m_tree == kTalents[other].m_tree &&
                               ranks[at] >= kTalents[at].m_maxRank;
                    }
                }
            }
        }

        return open;
    }

    // The rule the server runs (CanRank): max rank, one big talent, and a parent at full rank for anything that is
    // not the core. `ranks` is one rank per talent in table order. Every caller passes a rank array it owns.
    bool RuleAllows(const int* ranks, int i)
    {
        const TalentRowDef& t = kTalents[i];

        if (ranks[i] >= t.m_maxRank)
        {
            return false;
        }

        if (t.m_kind == kKindCap)
        {
            for (int k = 0; k < kTalentCount; k++)
            {
                if (k != i && IsCap(k) && ranks[k] > 0)
                {
                    return false;
                }
            }
        }

        // a bridge is refused while another bridge joining the same two trees holds a rank (the server's BridgeTaken)
        if (t.m_kind == kKindBridge)
        {
            for (int k = 0; k < kTalentCount; k++)
            {
                const TalentRowDef& o = kTalents[k];

                if (k != i && o.m_kind == kKindBridge && ranks[k] > 0 &&
                    ((o.m_between[0] == t.m_between[0] && o.m_between[1] == t.m_between[1]) ||
                     (o.m_between[0] == t.m_between[1] && o.m_between[1] == t.m_between[0])))
                {
                    return false;
                }
            }
        }

        // a full plain talent closes its ring: its tree-and-ring siblings take no rank (the server's SiblingFull)
        if (t.m_kind == kKindNode)
        {
            for (int k = 0; k < kTalentCount; k++)
            {
                if (k != i && kTalents[k].m_kind == kKindNode && kTalents[k].m_tree == t.m_tree &&
                    kTalents[k].m_ring == t.m_ring && ranks[k] >= kTalents[k].m_maxRank)
                {
                    return false;
                }
            }
        }

        return ParentsOpen(ranks, i);
    }

    // The rank the panel shows: confirmed, or staged if higher.
    int EffRank(int i)
    {
        const int r = s_state.m_want[i] > s_state.m_rank[i] ? s_state.m_want[i] : s_state.m_rank[i];

        return r > kTalents[i].m_maxRank ? kTalents[i].m_maxRank : r;
    }

    // Staged points still to be sent. Confirmed ranks are excluded: they are already paid for.
    int PendingTotal()
    {
        int n = 0;

        for (int i = 0; i < kTalentCount; i++)
        {
            if (s_state.m_want[i] > s_state.m_rank[i])
            {
                n += s_state.m_want[i] - s_state.m_rank[i];
            }
        }

        return n;
    }

    int EffUnspent()
    {
        const int u = static_cast<int>(s_state.m_unspent) - PendingTotal();

        return u < 0 ? 0 : u;
    }

    // A talent can be staged when the rule allows it on the staged ranks (so a whole draft can be built before
    // Confirm) and a point is left to spend.
    bool CanLearn(int i)
    {
        if (EffUnspent() <= 0)
        {
            return false;
        }

        int staged[kTalentCount];

        for (int k = 0; k < kTalentCount; k++)
        {
            staged[k] = EffRank(k);
        }

        return RuleAllows(staged, i);
    }

    // Taking a point back must break the chain ahead of it: a staged point whose parents are no longer open is
    // dropped, and that can orphan the next one, so it repeats until nothing changes. The confirmed ranks the
    // server holds are cascaded by the server (DropOrphans) and arrive in the tally.
    void DropStagedOrphans()
    {
        for (bool changed = true; changed;)
        {
            changed = false;

            int eff[kTalentCount];

            for (int k = 0; k < kTalentCount; k++)
            {
                eff[k] = EffRank(k);
            }

            for (int i = 0; i < kTalentCount; i++)
            {
                if (s_state.m_want[i] > s_state.m_rank[i] && !ParentsOpen(eff, i))
                {
                    s_state.m_want[i] = 0;
                    changed = true;
                    break;
                }
            }
        }
    }

    // The next B message the server can take now: pending entries whose parent is already CONFIRMED, one big talent
    // at most (none may be confirmed), and at most kBatchPairs. A dependent entry waits for the tally that confirms
    // its parent, so no single message needs the server to order its own draft. Returns the count written to `out`.
    int BuildBatch(int* out)
    {
        int confirmed[kTalentCount];
        bool capTaken = false;

        for (int k = 0; k < kTalentCount; k++)
        {
            confirmed[k] = s_state.m_rank[k];
            capTaken = capTaken || (IsCap(k) && confirmed[k] > 0);
        }

        int n = 0;
        bool capStaged = false;

        for (int i = 0; i < kTalentCount && n < kBatchPairs; i++)
        {
            if (s_state.m_want[i] <= s_state.m_rank[i])
            {
                continue;
            }

            if (IsCap(i) && (capTaken || capStaged))
            {
                continue;
            }

            if (!RuleAllows(confirmed, i))
            {
                continue;
            }

            out[n++] = i;
            capStaged = capStaged || IsCap(i);
        }

        return n;
    }

    // --- publishing ---------------------------------------------------------------------------------------

    // Publishes Manastorm_TalentD once per script context. Talents.lua builds its whole frame from this and bails if
    // it is absent.
    void PushTable()
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        // A UI reload creates a fresh lua_State with no Manastorm_TalentD; the flag must not outlive it.
        if (lua != s_state.m_pushedInto)
        {
            s_state.m_pushedInto = lua;
            s_state.m_tablePushed = false;
            s_state.m_dirty = true;
        }

        if (s_state.m_tablePushed)
        {
            return;
        }

        lua_newtable(lua);

        for (int i = 0; i < kTalentCount; i++)
        {
            const TalentRowDef& t = kTalents[i];

            // The tooltip number is the first effect in its own unit. Flags show none, and the two angle
            // thresholds are the client-side talents (clientSide = 1).
            double      step = 0.0;
            const char* unit = "";
            int         clientSide = 0;

            if (t.m_nEffects > 0)
            {
                const char* key = t.m_effects[0].m_key;
                const float per = t.m_effects[0].m_per;

                if (!std::strcmp(key, "climb_threshold") || !std::strcmp(key, "dive_threshold"))
                {
                    step = per;
                    unit = " deg";
                    clientSide = 1;
                }
                else if (!std::strcmp(key, "shallow_pull"))
                {
                    step = per;
                    unit = " deg";
                }
                else if (!std::strcmp(key, "speed_cap"))
                {
                    step = per;
                    unit = " pts";
                }
                else if (!std::strcmp(key, "boost_stacks") || !std::strcmp(key, "start_stacks"))
                {
                    step = per;
                }
                else if (std::strcmp(key, "aftershock") != 0)
                {
                    step = per * 100.0;
                    unit = "%";
                }
            }

            // Lua trees are 1 dive, 2 glide, 3 wings, 0 none. A bridge opens the tree on its other side.
            const int luaTree = t.m_tree >= 0 && t.m_tree < 3 ? t.m_tree + 1 : 0;
            int       opens   = 0;

            if (IsBridge(i))
            {
                const int other = t.m_between[0] == t.m_tree ? t.m_between[1] : t.m_between[0];
                opens = other >= 0 && other < 3 ? other + 1 : 0;
            }

            lua_newtable(lua);

            lua_pushnumber(lua, t.m_id);                 lua_rawseti(lua, -2, 1);
            lua_pushnumber(lua, luaTree);                lua_rawseti(lua, -2, 2);
            lua_pushnumber(lua, t.m_ring);               lua_rawseti(lua, -2, 3);
            lua_pushnumber(lua, t.m_maxRank);            lua_rawseti(lua, -2, 4);
            lua_pushnumber(lua, clientSide);             lua_rawseti(lua, -2, 5);
            lua_pushstring(lua, t.m_name);               lua_rawseti(lua, -2, 6);
            lua_pushstring(lua, t.m_desc);               lua_rawseti(lua, -2, 7);
            lua_pushnumber(lua, step);                   lua_rawseti(lua, -2, 8);
            lua_pushstring(lua, unit);                   lua_rawseti(lua, -2, 9);
            lua_pushnumber(lua, opens);                  lua_rawseti(lua, -2, 10);
            lua_pushnumber(lua, t.m_kind);               lua_rawseti(lua, -2, 11);
            lua_pushnumber(lua, t.m_angle);              lua_rawseti(lua, -2, 12);
            lua_pushnumber(lua, t.m_parents[0]);         lua_rawseti(lua, -2, 13);
            lua_pushnumber(lua, t.m_parents[1]);         lua_rawseti(lua, -2, 14);
            lua_pushnumber(lua, t.m_parents[2]);         lua_rawseti(lua, -2, 15);
            lua_pushnumber(lua, t.m_between[0] >= 0 && t.m_between[0] < 3 ? t.m_between[0] + 1 : 0);
            lua_rawseti(lua, -2, 16);
            lua_pushnumber(lua, t.m_between[1] >= 0 && t.m_between[1] < 3 ? t.m_between[1] + 1 : 0);
            lua_rawseti(lua, -2, 17);

            // 18-19: a ring-5 bridge's two reach talents (0 for every other row)
            for (const BridgeReachDef& r : kBridgeReach)
            {
                if (r.m_bridge == t.m_id)
                {
                    lua_pushnumber(lua, r.m_node[0]);
                    lua_rawseti(lua, -2, 18);
                    lua_pushnumber(lua, r.m_node[1]);
                    lua_rawseti(lua, -2, 19);
                }
            }

            lua_rawseti(lua, -2, i + 1);
        }

        lua_setglobal(lua, "Manastorm_TalentD");

        s_state.m_tablePushed = true;
    }

    // 1 + the summed per-rank bonuses of one effect over the shown ranks: the server's BuildTalents (effects on one
    // key sum, never compound).
    double EffectSum(const char* key)
    {
        double sum = 1.0;

        for (int i = 0; i < kTalentCount; i++)
        {
            const int r = EffRank(i);

            for (int k = 0; r > 0 && k < kTalents[i].m_nEffects; k++)
            {
                if (kTalents[i].m_effects[k].m_key && !std::strcmp(kTalents[i].m_effects[k].m_key, key))
                {
                    sum += kTalents[i].m_effects[k].m_per * r;
                }
            }
        }

        return sum < 0.0 ? 0.0 : sum;
    }

    // The headline speeds in percent of base run speed, as the server's ApplyTalents makes them. Talent bonuses
    // add percentage points to the base (+6% on 200% is 206%): speed_all and the per-speed key are summed, never multiplied.
    // shortcut: the base speeds (320 / 220 / 600 / 1200) and the 1600 cap are the module's defaults, not read from
    // the server; upgrade to a pushed value if the .conf ever changes them.
    void PublishSpeeds()
    {
        const double all = EffectSum("speed_all") - 1.0;
        const double cap = 1600.0 + (EffectSum("speed_cap") - 1.0);
        const auto   speed = [&](double base, const char* key) { return std::min(base + 100.0 * (all + EffectSum(key) - 1.0), cap); };
        const double level = speed(320.0, "level_speed");
        const double climb = speed(220.0, "climb_speed");
        const double dive = speed(600.0, "dive_speed");
        const double boost = std::max(1200.0 + 100.0 * (all + EffectSum("boost_speed") - 1.0), std::max(climb, std::max(level, dive)));

        // Angle bridges 140 Still Air / 141 Tucked Wings, as SpeedFlying::ClimbSin / DiveSin read them.
        const int still = IndexOfId(140), tucked = IndexOfId(141);

        SetGlobalNumber("Manastorm_StatLevel", level);
        SetGlobalNumber("Manastorm_StatClimb", climb);
        SetGlobalNumber("Manastorm_StatDive", dive);
        SetGlobalNumber("Manastorm_StatBoost", boost);
        SetGlobalNumber("Manastorm_StatClimbAngle", still >= 0 && EffRank(still) ? 25.0 : 15.0);
        SetGlobalNumber("Manastorm_StatDiveAngle", tucked >= 0 && EffRank(tucked) ? 65.0 : 75.0);
    }

    // Everything the panel repaints from. Ranks and the can-learn flags are indexed by position in
    // Manastorm_TalentD, not by id.
    void PublishState()
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        SetGlobalNumber("Manastorm_TalentPoints", static_cast<double>(s_state.m_points));
        SetGlobalNumber("Manastorm_TalentSpent", static_cast<double>(s_state.m_spent));
        SetGlobalNumber("Manastorm_TalentUnspent", static_cast<double>(EffUnspent()));
        SetGlobalNumber("Manastorm_TalentMax", static_cast<double>(kMaxPoints));
        SetGlobalNumber("Manastorm_TalentPending", static_cast<double>(PendingTotal()));
        SetGlobalNumber("Manastorm_TalentPartial", static_cast<double>(s_state.m_partialPermille));
        SetGlobalNumber("Manastorm_TalentNeed", static_cast<double>(s_state.m_needYards));
        SetGlobalString("Manastorm_TalentStatus", s_state.m_status);
        PublishSpeeds();

        lua_newtable(lua);

        for (int i = 0; i < kTalentCount; i++)
        {
            lua_pushnumber(lua, EffRank(i));
            lua_rawseti(lua, -2, i + 1);
        }

        lua_setglobal(lua, "Manastorm_TalentRanks");

        lua_newtable(lua);

        for (int i = 0; i < kTalentCount; i++)
        {
            lua_pushnumber(lua, CanLearn(i) ? 1 : 0);
            lua_rawseti(lua, -2, i + 1);
        }

        lua_setglobal(lua, "Manastorm_TalentCan");

        s_state.m_dirty = false;
    }

    // --- the panel's raised intents -----------------------------------------------------------------------

    // Manastorm_TalentPick = { <talent id>, +1 | -1 }. Staging only: nothing is sent until Confirm. The target is
    // absolute, so a click at or below the confirmed rank is a no-op -- a rank the server knows is only removed
    // by its refund path.
    void PollPick()
    {
        lua_State* lua = L();

        lua_getglobal(lua, "Manastorm_TalentPick");

        if (!lua_istable(lua, -1))
        {
            lua_pop(lua, 1);
            return;
        }

        lua_rawgeti(lua, -1, 1);
        const int id = static_cast<int>(static_cast<int64_t>(lua_tonumber(lua, -1)));
        lua_pop(lua, 1);

        lua_rawgeti(lua, -1, 2);
        const int delta = static_cast<int>(static_cast<int64_t>(lua_tonumber(lua, -1)));
        lua_pop(lua, 2);

        ClearGlobal("Manastorm_TalentPick");

        const int i = IndexOfId(id);

        if (i < 0 || delta == 0)
        {
            return;
        }

        const int want = s_state.m_want[i] > s_state.m_rank[i] ? s_state.m_want[i] : s_state.m_rank[i];

        // a confirmed point comes back through the server, which also refunds everything ahead of it that needed it
        if (delta < 0 && want == s_state.m_rank[i] && s_state.m_rank[i] > 0)
        {
            char body[16];
            std::snprintf(body, sizeof(body), "U\t%d", id);
            QueueSend(body);
            return;
        }

        if (want + delta > kTalents[i].m_maxRank || want + delta < s_state.m_rank[i])
        {
            return;
        }

        if (delta > 0 && !CanLearn(i))
        {
            return;
        }

        s_state.m_want[i] = want + delta;

        if (delta < 0)
        {
            DropStagedOrphans();
        }

        s_state.m_status[0] = '\0';
        s_state.m_dirty = true;
    }

    void PollConfirm()
    {
        if (!ReadGlobalFlag("Manastorm_TalentConfirm"))
        {
            return;
        }

        ClearGlobal("Manastorm_TalentConfirm");

        if (PendingTotal() > 0)
        {
            s_state.m_confirming = true;
            s_state.m_confirmSentMs = 0;
            s_state.m_confirmSends = 0;
            s_state.m_status[0] = '\0';
            s_state.m_dirty = true;
        }
    }

    // The panel's Reset All: drop everything staged and ask for the whole tree back.
    void PollReset()
    {
        if (!ReadGlobalFlag("Manastorm_TalentReset"))
        {
            return;
        }

        ClearGlobal("Manastorm_TalentReset");

        std::memset(s_state.m_want, 0, sizeof(s_state.m_want));
        s_state.m_confirming = false;
        s_state.m_resetPending = true;
        s_state.m_resetSentMs = 0;
        s_state.m_resetSends = 0;
        s_state.m_dirty = true;
    }

    // One message for the whole Confirm, every staged talent as an absolute target, resent on the retry clock
    // until the tally shows every want met.
    void TickConfirm(uint32_t nowMs)
    {
        if (!s_state.m_confirming)
        {
            return;
        }

        if (PendingTotal() == 0)
        {
            s_state.m_confirming = false;
            return;
        }

        if (s_state.m_confirmSentMs && nowMs - s_state.m_confirmSentMs <= kResendMs)
        {
            return;
        }

        if (s_state.m_confirmSends >= kMaxSends)
        {
            // Lost or refused for good: drop the batch rather than resend it forever.
            std::memset(s_state.m_want, 0, sizeof(s_state.m_want));
            s_state.m_confirming = false;
            s_state.m_dirty = true;
            return;
        }

        // One round is the entries the server can take now. The tally that confirms them restarts the next round
        // at once (OnRanks), so a long draft goes out a batch at a time and every batch is in dependency order.
        int idx[kBatchPairs];
        const int n = BuildBatch(idx);

        if (n == 0)
        {
            // Staged points nothing confirmed can reach: drop them rather than wait on a tally that never comes.
            std::memset(s_state.m_want, 0, sizeof(s_state.m_want));
            s_state.m_confirming = false;
            s_state.m_dirty = true;
            return;
        }

        char body[256];
        size_t at = static_cast<size_t>(std::snprintf(body, sizeof(body), "B\t"));

        for (int k = 0; k < n; k++)
        {
            at += static_cast<size_t>(std::snprintf(body + at, sizeof(body) - at, "%s%d:%d",
                                                     k ? "," : "", kTalents[idx[k]].m_id, s_state.m_want[idx[k]]));
        }

        if (QueueSend(body))
        {
            s_state.m_confirmSentMs = nowMs ? nowMs : 1;
            s_state.m_confirmSends++;
        }
    }

    // The whole-tree refund is the one destructive op, so it is retried until a tally shows the tree empty.
    void TickReset(uint32_t nowMs)
    {
        if (!s_state.m_resetPending)
        {
            return;
        }

        if (s_state.m_haveTally && s_state.m_spent == 0 && s_state.m_resetSends > 0)
        {
            s_state.m_resetPending = false;
            return;
        }

        if (s_state.m_resetSentMs && nowMs - s_state.m_resetSentMs <= kResendMs)
        {
            return;
        }

        if (s_state.m_resetSends >= kMaxSends)
        {
            s_state.m_resetPending = false;
            return;
        }

        if (QueueSend("R\tA"))
        {
            s_state.m_resetSentMs = nowMs ? nowMs : 1;
            s_state.m_resetSends++;
        }
    }

    // While the panel is open the tally is re-requested on a slow clock, so it recovers a server restart and
    // picks up points earned meanwhile.
    void TickHeartbeat(uint32_t nowMs)
    {
        if (s_state.m_confirming || s_state.m_resetPending || nowMs - s_state.m_lastQueryMs < kHeartbeatMs)
        {
            return;
        }

        if (ReadGlobalFlag("Manastorm_SF_Shown") && QueueSend("Q"))
        {
            s_state.m_lastQueryMs = nowMs;
        }
    }
}

namespace TalentUI
{
    // The three counters of a "T:<points>,<spent>,<unspent>,..." body. A tally replaces the confirmed ranks
    // wholesale: a refund removes pairs, so merging would leave a stale rank behind.
    void OnTally(uint32_t points, uint32_t spent, uint32_t unspent)
    {
        s_state.m_points = points;
        s_state.m_spent = spent;
        s_state.m_unspent = unspent;
        s_state.m_haveTally = true;

        std::memset(s_state.m_rank, 0, sizeof(s_state.m_rank));

        // A refusal stays readable for a moment; the follow-up tally it triggers must not wipe it at once.
        if (s_state.m_nowMs - s_state.m_statusAtMs >= kStatusHoldMs)
        {
            s_state.m_status[0] = '\0';
        }

        s_state.m_dirty = true;
    }

    // The confirmed ranks, from the T body: one character per talent in table order ("0"-"9", then "a"-"z"). A
    // table of another length is a build whose tree differs from this one, so its ranks are dropped, never assigned
    // to the wrong talents.
    void OnRanks(const char* table, int count)
    {
        std::memset(s_state.m_rank, 0, sizeof(s_state.m_rank));

        if (table && count == kTalentCount)
        {
            for (int i = 0; i < kTalentCount && table[i]; i++)
            {
                const char c = table[i];
                int rank = 0;

                if (c >= '0' && c <= '9')
                {
                    rank = c - '0';
                }
                else if (c >= 'a' && c <= 'z')
                {
                    rank = c - 'a' + 10;
                }

                s_state.m_rank[i] = rank > kTalents[i].m_maxRank ? kTalents[i].m_maxRank : rank;
            }
        }

        // Retire the staged ranks the server now agrees with. This is the queue's advance signal.
        for (int i = 0; i < kTalentCount; i++)
        {
            if (s_state.m_want[i] <= s_state.m_rank[i])
            {
                s_state.m_want[i] = 0;
            }
        }

        DropStagedOrphans();

        // A tally answers the last batch, so the next round of a staged draft may go out at once.
        if (s_state.m_confirming)
        {
            s_state.m_confirmSentMs = 0;
            s_state.m_confirmSends = 0;
        }

        s_state.m_dirty = true;
    }

    // The confirmed rank of one talent by its wire id (0 for an id this build does not know). Synced to the server's
    // tally like the panel's ranks, so the angle talents read the server's answer.
    int RankOf(int id)
    {
        const int i = IndexOfId(id);

        return i >= 0 ? s_state.m_rank[i] : 0;
    }

    void OnPartial(uint32_t permille, uint32_t needYards)
    {
        s_state.m_partialPermille = permille > 1000 ? 1000 : permille;
        s_state.m_needYards = needYards;
        s_state.m_dirty = true;
    }

    // A refusal. The server answers a batch that stalled with only this (no tally), so what was staged may be partly
    // applied: the batch is dropped and the tree re-requested rather than guessed at. Known reasons show a plain
    // sentence; any other reason is shown as its own plain words.
    void OnStatus(const char* reason)
    {
        const char* text = nullptr;

        if (reason && !std::strcmp(reason, "locked"))
        {
            text = "Fill the talent next to it first";
        }
        else if (reason && !std::strcmp(reason, "ring-full"))
        {
            text = "A talent on this ring is full: go to the next ring";
        }
        else if (reason && !std::strcmp(reason, "bridge-taken"))
        {
            text = "A bridge between these two trees is already chosen";
        }
        else if (reason && !std::strcmp(reason, "cap-taken"))
        {
            text = "Only one big talent can be chosen";
        }
        else if (reason && !std::strcmp(reason, "max-rank"))
        {
            text = "That talent is already at full rank";
        }
        else if (reason && !std::strcmp(reason, "no-points"))
        {
            text = "No points left to spend";
        }

        if (text)
        {
            std::snprintf(s_state.m_status, sizeof(s_state.m_status), "%s", text);
        }
        else
        {
            // The reason is a wire field: keep only plain words.
            size_t w = 0;

            for (size_t r = 0; reason && reason[r] && w + 1 < sizeof(s_state.m_status); r++)
            {
                const char c = reason[r];

                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
                {
                    s_state.m_status[w++] = c;
                }
            }

            s_state.m_status[w] = '\0';
        }

        s_state.m_statusAtMs = s_state.m_nowMs ? s_state.m_nowMs : 1;

        if (s_state.m_confirming)
        {
            std::memset(s_state.m_want, 0, sizeof(s_state.m_want));
            s_state.m_confirming = false;
            QueueSend("Q");
        }

        s_state.m_resetPending = false;
        s_state.m_dirty = true;
    }

    void Update(uint32_t timeMs)
    {
        if (!L())
        {
            return;
        }

        s_state.m_nowMs = timeMs;

        PushTable();
        PollPick();
        PollConfirm();
        PollReset();
        TickConfirm(timeMs);
        TickReset(timeMs);
        TickHeartbeat(timeMs);

        // The panel repaints on its own OnUpdate from these globals. They are rewritten the moment anything
        // moved (a click must answer at once), and on a slow clock otherwise so a reload recovers.
        if (s_state.m_dirty || timeMs - s_state.m_lastSync >= 1000)
        {
            s_state.m_lastSync = timeMs;
            PublishState();
        }
    }
}
