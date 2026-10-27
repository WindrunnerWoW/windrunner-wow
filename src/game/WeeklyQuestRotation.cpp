#include "WeeklyQuestRotation.h"

#include "Camera.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WeeklyQuestLedger.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <list>

namespace
{
    struct Rotation
    {
        WeeklyQuestRotation::Definition definition;
        uint64 period = 0;
        std::vector<uint32> current;
    };

    // World thread only, like Update(). A list keeps the entries in place while modules register.
    std::list<Rotation>& Rotations()
    {
        static std::list<Rotation> rotations;
        return rotations;
    }

    bool Offered(QuestRelationsMap& relations, uint32 giver, uint32 quest)
    {
        auto const range = relations.equal_range(giver);
        for (auto it = range.first; it != range.second; ++it)
            if (it->second == quest)
                return true;
        return false;
    }

    void Rotate(Rotation& rotation, uint64 period)
    {
        WeeklyQuestRotation::Definition const& definition = rotation.definition;
        QuestRelationsMap& relations = sObjectMgr.GetCreatureQuestRelationsMap();
        bool restore = false;
        if (period == rotation.period)
        {
            // A GM reload of creature_questrelation drops the runtime rows: this week's go back in.
            for (uint32 giver : definition.givers)
                for (uint32 quest : rotation.current)
                    restore = restore || (quest && !Offered(relations, giver, quest));
            if (!restore)
                return;
        }
        else
            rotation.current = definition.draw(WeeklyRotation::WeekIndex(period));
        for (uint32 giver : definition.givers)
        {
            auto range = relations.equal_range(giver);
            for (auto it = range.first; it != range.second; )
                if (definition.owns(it->second))
                    it = relations.erase(it);
                else
                    ++it;
            for (uint32 quest : rotation.current)
                if (quest)
                    relations.emplace(giver, quest);
        }
        rotation.period = period;
        for (auto const& session : sWorld.GetAllSessions())
            if (Player* player = session.second->GetPlayer())
                if (player->IsInWorld())
                    player->GetCamera().UpdateVisibilityForOwner();
        std::string quests;
        for (uint32 quest : rotation.current)
            quests += (quests.empty() ? "" : " / ") + std::to_string(quest);
        sLog.outString("Weekly quest rotation %s: %s (period %llu)%s", definition.name.c_str(), quests.c_str(),
            static_cast<unsigned long long>(period), restore ? ", restored after a reload" : "");
    }
}

void WeeklyQuestRotation::Register(Definition definition)
{
    std::list<Rotation>& rotations = Rotations();
    auto const known = std::find_if(rotations.begin(), rotations.end(),
        [&definition](Rotation const& rotation) { return rotation.definition.name == definition.name; });
    if (known != rotations.end())
        *known = Rotation{ std::move(definition) };
    else
        rotations.push_back(Rotation{ std::move(definition) });
}

void WeeklyQuestRotation::Update()
{
    uint64 const period = WeeklyQuestLedger::GetCurrentPeriod(sWorld.GetGameTime());
    if (!period)
        return;
    for (Rotation& rotation : Rotations())
        Rotate(rotation, period);
}

std::vector<uint32> WeeklyQuestRotation::Current(std::string const& name)
{
    for (Rotation const& rotation : Rotations())
        if (rotation.definition.name == name)
            return rotation.current;
    return {};
}
