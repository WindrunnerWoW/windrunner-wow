
#include "playerbot/playerbot.h"
#include "DpsTargetValue.h"
#include "LeastHpTargetValue.h"

using namespace ai;


namespace
{
    class CcTargetCheck : public FindNonCcTargetStrategy
    {
    public:
        CcTargetCheck(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai) {}
        void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override {}
        bool IsCc(Unit* attacker) { return IsCcTarget(attacker); }
    };

    bool IsAttacker(const std::list<ObjectGuid>& attackers, Unit* unit)
    {
        return unit && std::find(attackers.begin(), attackers.end(), unit->GetObjectGuid()) != attackers.end();
    }
}

Unit* DpsTargetValue::GetGroupFocusTarget(const std::list<ObjectGuid>& attackers)
{
    Group* group = bot->GetGroup();
    if (!group || attackers.empty())
        return nullptr;

    CcTargetCheck ccCheck(ai);
    auto focusOf = [&](Player* member) -> Unit*
    {
        if (!member || member == bot || !member->IsAlive() || member->FindMap() != bot->FindMap())
            return nullptr;

        Unit* focus = member->GetVictim();
        // Casters rarely have a melee victim; their selection is what they fight.
        if (!focus && !GetBotAI(member) && member->GetSelectionGuid())
            focus = ai->GetUnit(member->GetSelectionGuid());

        // Only ever assist onto something already in the fight - never pull.
        if (!focus || focus->IsPlayer() || !focus->IsAlive() || !IsAttacker(attackers, focus))
            return nullptr;

        if (ccCheck.IsCc(focus))
            return nullptr;

        return focus;
    };

    // Main tank first, then any other tank. Only members on our map are
    // inspected: IsTank reads another bot's strategies, which is only safe on
    // the map thread both of us are updated on.
    Player* mainTank = nullptr;
    std::vector<Player*> tanks;
    ObjectGuid const mainTankGuid = group->GetMainTankGuid();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (!member || member == bot || member->FindMap() != bot->FindMap())
            continue;

        if (mainTankGuid && member->GetObjectGuid() == mainTankGuid)
            mainTank = member;
        else if (PlayerbotAI::IsTank(member))
            tanks.push_back(member);
    }

    if (Unit* focus = focusOf(mainTank))
        return focus;

    for (Player* tank : tanks)
    {
        if (Unit* focus = focusOf(tank))
            return focus;
    }

    Player* master = GetMaster();
    if (master && master->IsInGroup(bot))
    {
        if (Unit* focus = focusOf(master))
            return focus;
    }

    return nullptr;
}

Unit* DpsTargetValue::Calculate()
{
    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");

    // Focus fire with the group instead of each bot picking its own mob.
    if (Unit* focus = GetGroupFocusTarget(attackers))
        return focus;

    // Nobody to assist: stick with the current target while it is still a
    // valid attacker. Re-picking the lowest-HP mob on every tick made DPS
    // swap targets constantly (losing combo points / DoTs, peeling mobs off
    // the tank) whenever another mob dipped lower.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && !current->IsPlayer() && current->IsAlive() && IsAttacker(attackers, current))
    {
        CcTargetCheck ccCheck(ai);
        if (!ccCheck.IsCc(current))
            return current;
    }

    FindLeastHpTargetStrategy strategy(ai);
    return TargetValue::FindTarget(&strategy);
}

class FindMaxHpTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxHpTargetStrategy(PlayerbotAI* ai) : FindTargetStrategy(ai)
    {
        maxHealth = 0;
    }

public:
    virtual void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
    {
        Group* group = ai->GetBot()->GetGroup();
        if (group)
        {
            uint64 guid = group->GetTargetIcon(4);
            if (guid && attacker->GetObjectGuid() == ObjectGuid(guid))
                return;
        }
        if (!result || result->GetHealth() < attacker->GetHealth())
            result = attacker;
    }

protected:
    float maxHealth;
};

Unit* DpsAoeTargetValue::Calculate()
{
    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    FindMaxHpTargetStrategy strategy(ai);
    return TargetValue::FindTarget(&strategy);
}
