
#include "playerbot/playerbot.h"
#include "TankTargetValue.h"
#include "PossibleAttackTargetsValue.h"

using namespace ai;

class FindTargetForTankStrategy : public FindNonCcTargetStrategy
{
public:
    FindTargetForTankStrategy(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai)
    {
        minThreat = 0;
        looseScore = -1;
        looseThreat = 0;
        loose = nullptr;
    }

public:
    virtual void CheckAttacker(Unit* creature, ThreatManager* threatManager) override
    {
        Player* bot = ai->GetBot();
        AiObjectContext* context = ai->GetAiObjectContext();

        if (IsCcTarget(creature)) return;

        if (!PossibleAttackTargetsValue::IsValid(creature, bot))
        {
            std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
            if (std::find(attackers.begin(), attackers.end(), creature->GetObjectGuid()) == attackers.end())
                return;
        }

        float threat = threatManager->getThreat(bot);
        if (!result || (minThreat - threat) > 0.1f)
        {
            minThreat = threat;
            result = creature;
        }

        // A mob that is beating on someone other than a tank is the tank's
        // first job, whatever the threat numbers say. Healers first.
        Unit* victim = creature->GetVictim();
        if (!victim || victim == bot)
            return;

        Player* victimPlayer = victim->IsPlayer() ? static_cast<Player*>(victim) : nullptr;
        if (!victimPlayer && victim->GetOwner() && victim->GetOwner()->IsPlayer())
            return; // pets can hold their own

        if (!victimPlayer || !victimPlayer->IsInGroup(bot) || ai->IsTank(victimPlayer))
            return;

        int score = ai->IsHeal(victimPlayer) ? 2 : 1;
        if (score > looseScore || (score == looseScore && threat < looseThreat))
        {
            looseScore = score;
            looseThreat = threat;
            loose = creature;
        }
    }

    Unit* GetLooseTarget() const { return loose; }

protected:
    float minThreat;
    int looseScore;
    float looseThreat;
    Unit* loose;
};


Unit* TankTargetValue::Calculate()
{
    FindTargetForTankStrategy strategy(ai);
    Unit* lowestThreat = FindTarget(&strategy);

    // 1. Pick up mobs that are loose on the group.
    if (Unit* loose = strategy.GetLooseTarget())
        return loose;

    // 2. Skull, but for a player-led group only once it is in the fight:
    //    the next pack's mark must not be pulled early.
    Unit* rti = RtiTargetValue::Calculate();
    if (rti)
    {
        if (!ai->HasActivePlayerMaster())
            return rti;

        std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        if (std::find(attackers.begin(), attackers.end(), rti->GetObjectGuid()) != attackers.end())
            return rti;
    }

    // 3. Otherwise build threat where it is lowest.
    return lowestThreat;
}
