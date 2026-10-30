#pragma once
#include "playerbot/strategy/Action.h"

namespace ai
{
    // A companion that died where nobody left standing can resurrect it (a
    // warrior/rogue/druid-only party, or everyone but the player went down)
    // used to lie there until the player typed "corpse run". Once the fight is
    // over and the master is up, it gets back on its feet.
    //
    // Inside a dungeon/raid (where it died, not where its ghost stands) a bot
    // with a real-player master never releases or corpse-runs. Once the party
    // is quiet it is moved to its master and raised there on a later tick (a
    // teleport only completes on its ack): after the delay without a living
    // rezzer, after twice the delay with one - the rezzer gets the first
    // chance, auto revive is the fallback. Outside dungeons it revives in place
    // only when there is no rezzer, and corpse runs are left alone.
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

        // Did `dead` die on a dungeon/raid map? Uses the corpse's map for a
        // released ghost. Battlegrounds are not dungeons.
        static bool IsDungeonDeath(Player* dead);

        // True for a dead bot of a real player, grouped with it, that died in
        // a dungeon. The dead-state engine uses it to suppress the auto
        // release, the corpse run and the spirit healer regardless of where
        // the master is: the bot waits as a corpse for a rezzer or auto
        // revive. Explicit "release" / "corpse run" commands still work.
        static bool ShouldWaitForAutoRevive(Player* dead);

        // True when auto revive will bring `dead` back once the party is
        // quiet: companion of a living real player on the same map/instance,
        // and - outside dungeons only - no living rezzer there. In a dungeon a
        // rezzer only delays it (2x). Shared with DungeonClear so its rez
        // recovery holds the run instead of ending it.
        static bool WillAutoRevive(Player* dead);

    private:
        // A living group member on the master's map/instance that can rez.
        // Only valid after WillAutoRevive / ShouldWaitForAutoRevive passed.
        static bool HasLivingRezzer(Player* dead);
        void ResetState();

        time_t quietSince = 0;
        // Teleports to the master issued for the pending revive; non-zero
        // while one may still be in flight.
        uint32 teleports = 0;
    };
}
