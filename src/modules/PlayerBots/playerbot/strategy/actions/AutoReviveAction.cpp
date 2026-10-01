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

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (!member || member == dead || !member->IsInWorld() ||
            member->GetMapId() != master->GetMapId() || member->GetInstanceId() != master->GetInstanceId())
            continue;

        if (member->IsAlive() && CanResurrectOthers(member))
            return false;
    }

    return true;
}

bool AutoReviveAction::isUseful()
{
    if (!sServerFacade.UnitIsDead(bot) || !WillAutoRevive(bot))
    {
        quietSince = 0;
        return false;
    }

    // A soulstone / Ankh is a better answer and has its own action.
    if (bot->GetUInt32Value(PLAYER_SELF_RES_SPELL))
    {
        quietSince = 0;
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

    uint32 const delay = sPlayerbotAIConfig.windrunnerCompanionMode ?
        std::max<uint32>(30u, sPlayerbotAIConfig.autoReviveDelay) : sPlayerbotAIConfig.autoReviveDelay;
    return now - quietSince >= time_t(delay);
}

bool AutoReviveAction::Execute(Event& event)
{
    // Recheck at execution: another group member may have entered combat
    // after this action was queued.
    if (!isUseful())
        return false;

    Player* master = GetMaster();
    bool const wasGhost = bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);

    bot->ResurrectPlayer(0.5f);
    if (!bot->IsAlive())
        return false;
    bot->SpawnCorpseBones();
    ai->OnResurrected();

    // A released ghost stands at the graveyard or halfway back: rejoin the
    // master instead of reviving out there.
    if (wasGhost && master)
    {
        uint32 const options = master->GetMapId() == bot->GetMapId() &&
            master->GetInstanceId() != bot->GetInstanceId() ? TELE_TO_FORCE_MAP_CHANGE : 0;
        bot->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(),
            master->GetPositionZ(), master->GetOrientation(), options);
    }

    quietSince = 0;

    if (master)
        ai->TellPlayerNoFacing(master, "Nobody can resurrect me - getting back up.");

    sLog.outDetail("Bot %s auto revived (no rezzer in group)", bot->GetName());
    return true;
}
