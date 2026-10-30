
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
        currentIsCandidate = false;
        currentTarget = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
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

        if (creature == currentTarget)
            currentIsCandidate = true;

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
    Unit* GetCurrentCandidate() const { return currentIsCandidate ? currentTarget : nullptr; }

protected:
    float minThreat;
    bool currentIsCandidate;
    Unit* currentTarget;
    int looseScore;
    float looseThreat;
    Unit* loose;
};


namespace
{
    // Tank "owns" a mob once it is the mob's victim and leads every other
    // player on its threat list by a clear margin, so a swap will not lose it.
    bool HasSecureThreat(Player* bot, Unit* creature)
    {
        if (creature->GetVictim() != bot)
            return false;

        ThreatManager& threatManager = creature->getThreatManager();
        const float own = threatManager.getThreat(bot);
        if (own <= 0.0f)
            return false;

        float other = 0.0f;
        for (HostileReference* ref : threatManager.getThreatList())
        {
            Unit* unit = ref ? ref->GetVictim() : nullptr;
            if (unit && unit != bot)
                other = std::max(other, ref->getThreat());
        }
        return own >= other * 1.3f + 100.0f;
    }
}

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

    // 3. Otherwise build threat where it is lowest, but finish securing the
    //    current mob first: re-picking the lowest-threat mob every tick made
    //    the tank ping-pong between adds without holding any of them.
    Unit* current = strategy.GetCurrentCandidate();
    if (current && current != lowestThreat && !HasSecureThreat(bot, current))
        return current;

    return lowestThreat;
}
