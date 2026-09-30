#pragma once
#include "playerbot/strategy/Value.h"
#include "RtiTargetValue.h"
#include "TargetValue.h"

namespace ai
{
    class DpsTargetValue : public RtiTargetValue
	{
	public:
        DpsTargetValue(PlayerbotAI* ai, std::string type = "rti", std::string name = "dps target") : RtiTargetValue(ai, type, name) {}

    public:
        Unit* Calculate() override;

    private:
        // What the group is actually fighting: the tank's (or else the master's)
        // target, provided it is already one of our attackers.
        Unit* GetGroupFocusTarget(const std::list<ObjectGuid>& attackers);
        // Skull, then cross - only once they are in the fight.
        Unit* GetMarkedTarget(const std::list<ObjectGuid>& attackers);
    };

    class DpsAoeTargetValue : public RtiTargetValue
    {
    public:
        DpsAoeTargetValue(PlayerbotAI* ai, std::string type = "rti", std::string name = "dps aoe target") : RtiTargetValue(ai, type, name) {}

    public:
        Unit* Calculate() override;
    };
}
