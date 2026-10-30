#pragma once
#include "playerbot/strategy/Value.h"
#include "PartyMemberValue.h"

namespace ai
{
    class PartyMemberToHeal : public PartyMemberValue
	{
	public:
        PartyMemberToHeal(PlayerbotAI* ai, std::string name = "party member to heal") :
          PartyMemberValue(ai, name) {}
    
    protected:
        virtual Unit* Calculate() override;
        bool CanHealPet(Pet* pet);
        virtual bool Check(Unit* player);
        bool Check(Unit* player, float maxDist);

        // Range at which the heal can actually be cast from where the bot stands.
        float GetHealDistance();
        // Wider radius for group members the healer should walk over to: the
        // heal action's reach prerequisite closes the gap, the cast itself is
        // still limited to spell range.
        float GetReachDistance();

    private:
        std::vector<Player*> GetPartyMembers();
	};

    class PartyMemberToProtect : public PartyMemberValue
    {
    public:
        PartyMemberToProtect(PlayerbotAI* ai, std::string name = "party member to protect") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    class PartyMemberToRemoveRoots : public PartyMemberValue
    {
    public:
        PartyMemberToRemoveRoots(PlayerbotAI* ai, std::string name = "party member to remove roots") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };
}
