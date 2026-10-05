
#include "playerbot/playerbot.h"
#include "AreaTriggerAction.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

bool ReachAreaTriggerAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    uint32 triggerId;

    if (ai->IsRealPlayer()) //Do not trigger own area trigger.
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    p >> triggerId;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    // Only teleport triggers are worth walking to. The compat shim maps
    // cmangos' sObjectMgr.GetAreaTrigger() onto the plain trigger geometry,
    // which exists for EVERY trigger, so it cannot tell a portal from a
    // scripted/exploration trigger; the teleport table can. Without this the
    // bots dropped follow, froze their AI for the walk to whatever trigger
    // the master crossed (dungeons are full of them), or answered "too far
    // away" when the trigger's centre was beyond sight distance.
    AreaTriggerTeleport const* at = sObjectMgr.GetAreaTriggerTeleport(triggerId);
    if (!at)
    {
        WorldPacket p1(CMSG_AREATRIGGER);
        p1 << triggerId;
        p1.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p1);

        return true;
    }

    // The master's packet is relayed after the master has already been
    // ported, so by the time a bot handles it the bot may be on the other
    // side as well (summoned, or teleported along by a module such as the
    // companion recruiter). Nothing left to follow then.
    if (bot->GetMapId() != atEntry->mapid && bot->GetMapId() == at->destination.mapId)
        return false;

    if (bot->GetMapId() != atEntry->mapid || bot->GetDistance(atEntry->x, atEntry->y, atEntry->z) > sPlayerbotAIConfig.sightDistance)
    {
        ai->TellError(requester, "I won't follow: too far away");
        return true;
    }

    MotionMaster &mm = *bot->GetMotionMaster();
	mm.MovePoint(atEntry->mapid, atEntry->x, atEntry->y, atEntry->z, FORCED_MOVEMENT_RUN);
    const float distance = sqrt(bot->GetDistance(atEntry->x, atEntry->y, atEntry->z, DIST_CALC_NONE));
    const float duration = 1000.0f * distance / bot->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.reactDelay;
    ai->TellError(requester, "Wait for me");
    SetDuration(duration);
    context->GetValue<LastMovement&>("last area trigger")->Get().lastAreaTrigger = triggerId;

    return true;
}



bool AreaTriggerAction::Execute(Event& event)
{
    LastMovement& movement = context->GetValue<LastMovement&>("last area trigger")->Get();

    uint32 triggerId = movement.lastAreaTrigger;
    movement.lastAreaTrigger = 0;

    // Module gate: while an exit-sensitive run owns this bot, a teleport
    // trigger underfoot must NOT be relayed (the consume above still clears
    // it so the trigger cannot fire later either). Non-teleport triggers are
    // not this action's business anyway - it returns before the relay for
    // those below.
    if (ai->IsAreaTriggerRelaySuppressed())
        return false;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTriggerTeleport const* at = sObjectMgr.GetAreaTriggerTeleport(triggerId);
    if (!at)
        return true;

    WorldPacket p(CMSG_AREATRIGGER);
    p << triggerId;
    p.rpos(0);
    bot->GetSession()->HandleAreaTriggerOpcode(p);
    return true;
}
