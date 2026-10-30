
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "CombatActions.h"

using namespace ai;

bool SwitchToMeleeAction::isUseful()
{
    return ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT);
}

bool SwitchToMeleeAction::Execute(Event &event)
{
    if (Unit* target = AI_VALUE(Unit*, "current target"))
    {
        // Player::MeleeAttackStart is a no-op compat stub; Attack() with
        // meleeAttack=true also upgrades an existing ranged engagement.
        bot->Attack(target, true);
        return ChangeCombatStrategyAction::Execute(event);
    }
    return false;
}

bool SwitchToRangedAction::isUseful()
{

    return ai->HasStrategy("close", BotState::BOT_STATE_COMBAT);
}

bool SwitchToRangedAction::Execute(Event &event)
{
    if (Unit* target = AI_VALUE(Unit*, "current target"))
    {
        // Player::MeleeAttackStop is a no-op compat stub: stop only the
        // swing and keep the attack target, as CreatureAI::SetMeleeAttack does.
        if (bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING))
        {
            bot->ClearUnitState(UNIT_STAT_MELEE_ATTACKING);
            if (Unit* victim = bot->GetVictim())
                bot->SendMeleeAttackStop(victim);
        }
        return ChangeCombatStrategyAction::Execute(event);
    }
    return false;
}
