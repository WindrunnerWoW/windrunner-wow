#pragma once
#include "playerbot/strategy/Action.h"

namespace ai
{
    // A companion that died where nobody left standing can resurrect it (a
    // warrior/rogue/druid-only party, or everyone but the player went down)
    // used to lie there until the player typed "corpse run". Once the fight is
    // over and the master is up, get back on its feet in place instead.
    // Soulstones / Reincarnation are left to "self resurrect".
    class AutoReviveAction : public Action
    {
    public:
        AutoReviveAction(PlayerbotAI* ai) : Action(ai, "auto revive") {}

        bool isUseful() override;
        bool Execute(Event& event) override;

        // Does this group member have an out-of-combat resurrection?
        // (Druid Rebirth does not count: 30 min cooldown and a reagent.)
        static bool CanResurrectOthers(Player* player);

        // True when auto revive would bring `dead` back once the party is
        // quiet: companion of a living real player, and no living rezzer on
        // the map. Shared with DungeonClear so its rez recovery holds the run
        // instead of ending it.
        static bool WillAutoRevive(Player* dead);

    private:
        time_t quietSince = 0;
    };
}
