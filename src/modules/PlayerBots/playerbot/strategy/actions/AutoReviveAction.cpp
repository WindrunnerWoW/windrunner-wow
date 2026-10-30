#include "playerbot/playerbot.h"
#include "AutoReviveAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "Objects/Corpse.h"

using namespace ai;

namespace
{
    // All ranks: the lower ones stay in the spellbook, but be safe.
    const uint32 REZ_SPELLS[] =
    {
        2006, 2010, 10880, 10881, 20770,    // Resurrection
        7328, 10322, 10324, 20772, 20773,   // Redemption
        2008, 20609, 20610, 20776, 20777,   // Ancestral Spirit
    };

    // A teleport that "succeeds" but never lands the body next to the master
    // (master moving on, a bad Z) must not loop forever.
    const uint32 MAX_REVIVE_TELEPORTS = 3;
    const float REVIVE_MASTER_RANGE = 10.0f;
}

bool AutoReviveAction::CanResurrectOthers(Player* player)
{
    if (!player)
        return false;

    for (uint32 spellId : REZ_SPELLS)
    {
        if (player->HasSpell(spellId))
            return true;
    }

    return false;
}

bool AutoReviveAction::IsDungeonDeath(Player* dead)
{
    if (!dead)
        return false;

    // A released ghost stands at a graveyard outside; where it died is where
    // its corpse lies. An unreleased body is still on the map it died on.
    if (Corpse* corpse = dead->GetCorpse())
    {
        MapEntry const* entry = sMapStorage.LookupEntry<MapEntry>(corpse->GetMapId());
        return entry && entry->IsDungeon();
    }

    // FindMap, not GetMap: a bot mid-teleport has no map and GetMap throws.
    Map const* map = dead->FindMap();
    return map && map->IsDungeon();
}

bool AutoReviveAction::ShouldWaitForAutoRevive(Player* dead)
{
    if (!sPlayerbotAIConfig.autoReviveWithoutRezzer || !dead || dead->IsAlive() || dead->InBattleGround())
        return false;

    PlayerbotAI* deadAi = GetBotAI(dead);
    if (!deadAi || !deadAi->HasRealPlayerMaster())
        return false;

    Player* master = deadAi->GetMaster();
    if (!master || !dead->GetGroup() || !dead->IsInGroup(master))
        return false;

    return IsDungeonDeath(dead);
}

bool AutoReviveAction::HasLivingRezzer(Player* dead)
{
    Player* master = GetBotAI(dead)->GetMaster();
    Group* group = dead->GetGroup();

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (!member || member == dead || !member->IsInWorld() ||
            member->GetMapId() != master->GetMapId() || member->GetInstanceId() != master->GetInstanceId())
            continue;

        if (member->IsAlive() && CanResurrectOthers(member))
            return true;
    }

    return false;
}

bool AutoReviveAction::WillAutoRevive(Player* dead)
{
    if (!sPlayerbotAIConfig.autoReviveWithoutRezzer || !dead || dead->IsAlive() ||
        !dead->IsInWorld() || dead->IsBeingTeleported())
        return false;

    PlayerbotAI* deadAi = GetBotAI(dead);
    if (!deadAi || !deadAi->HasRealPlayerMaster() || dead->InBattleGround())
        return false;

    Player* master = deadAi->GetMaster();
    if (!master || !master->IsInWorld() || !master->IsAlive() || master->IsBeingTeleported())
        return false;

    if (master->GetMapId() != dead->GetMapId() || master->GetInstanceId() != dead->GetInstanceId())
    {
        Corpse* corpse = dead->GetCorpse();
        if (!dead->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) || !corpse ||
            corpse->GetMapId() != master->GetMapId() || corpse->GetInstanceId() != master->GetInstanceId())
            return false;
    }

    Group* group = dead->GetGroup();
    if (!group || !dead->IsInGroup(master))
        return false;

    // In a dungeon a rezzer only gets the first chance (isUseful doubles the
    // delay); auto revive is the fallback either way.
    if (IsDungeonDeath(dead))
        return true;

    return !HasLivingRezzer(dead);
}

void AutoReviveAction::ResetState()
{
    quietSince = 0;
    teleports = 0;
}

bool AutoReviveAction::isUseful()
{
    if (!sServerFacade.UnitIsDead(bot))
    {
        ResetState();
        return false;
    }

    // Our own move to the master is in flight. WillAutoRevive is false while
    // teleporting; that must not restart the quiet period - the resurrect
    // happens on the first tick after the bot lands.
    if (teleports && bot->IsBeingTeleported())
        return false;

    if (!WillAutoRevive(bot))
    {
        ResetState();
        return false;
    }

    // A soulstone / Ankh is a better answer and has its own action.
    if (bot->GetUInt32Value(PLAYER_SELF_RES_SPELL))
    {
        ResetState();
        return false;
    }

    // Only once the fight is over for everybody still standing.
    Group* group = bot->GetGroup();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (member && member->IsInCombat())
        {
            quietSince = 0;
            return false;
        }
    }

    time_t const now = time(0);
    if (!quietSince)
        quietSince = now;

    uint32 delay = sPlayerbotAIConfig.windrunnerCompanionMode ?
        std::max<uint32>(30u, sPlayerbotAIConfig.autoReviveDelay) : sPlayerbotAIConfig.autoReviveDelay;

    // A living rezzer gets its chance first; the bot only gets up on its own
    // if nobody has raised it after twice the usual wait.
    if (IsDungeonDeath(bot) && HasLivingRezzer(bot))
        delay *= 2;

    return now - quietSince >= time_t(delay);
}

bool AutoReviveAction::Execute(Event& event)
{
    // Recheck at execution: another group member may have entered combat
    // after this action was queued.
    if (!isUseful())
        return false;

    Player* master = GetMaster();
    bool const dungeon = IsDungeonDeath(bot);
    bool const wasGhost = bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);

    // In a dungeon the body goes to the player FIRST and is raised there on a
    // later tick. A near teleport only completes on the (bot-driven) teleport
    // ack, so resurrecting in the same call would raise it where it fell and
    // move it a moment later.
    bool const elsewhere = master && (
        bot->GetMapId() != master->GetMapId() ||
        bot->GetInstanceId() != master->GetInstanceId() ||
        bot->GetDistance(master) > REVIVE_MASTER_RANGE);

    if (dungeon && elsewhere)
    {
        if (teleports < MAX_REVIVE_TELEPORTS)
        {
            ++teleports;
            uint32 const options = master->GetMapId() == bot->GetMapId() &&
                master->GetInstanceId() != bot->GetInstanceId() ? TELE_TO_FORCE_MAP_CHANGE : 0;
            if (bot->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(),
                master->GetPositionZ(), master->GetOrientation(), options))
            {
                sLog.outDetail("Bot %s auto revive: moving to master before getting up (%u/%u)",
                    bot->GetName(), teleports, MAX_REVIVE_TELEPORTS);
                return true;
            }

            sLog.outError("Bot %s auto revive: teleport to master failed, reviving in place", bot->GetName());
        }
        else
            sLog.outError("Bot %s auto revive: did not reach master after %u teleports, reviving in place",
                bot->GetName(), MAX_REVIVE_TELEPORTS);
    }

    bool const passedOver = dungeon && HasLivingRezzer(bot);

    bot->ResurrectPlayer(0.5f);
    if (!bot->IsAlive())
        return false;
    bot->SpawnCorpseBones();
    ai->OnResurrected();

    // A released ghost stands at the graveyard or halfway back: rejoin the
    // master instead of reviving out there. In a dungeon this is only reached
    // when the move above failed, so it is the living bot's second try.
    if (master && (dungeon ? elsewhere : wasGhost))
    {
        uint32 const options = master->GetMapId() == bot->GetMapId() &&
            master->GetInstanceId() != bot->GetInstanceId() ? TELE_TO_FORCE_MAP_CHANGE : 0;
        bot->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(),
            master->GetPositionZ(), master->GetOrientation(), options);
    }

    ResetState();

    if (master)
        ai->TellPlayerNoFacing(master, passedOver ? "Nobody resurrected me - getting back up." :
            "Nobody can resurrect me - getting back up.");

    sLog.outDetail("Bot %s auto revived (%s)", bot->GetName(),
        passedOver ? "rezzer did not act" : "no rezzer in group");
    return true;
}
