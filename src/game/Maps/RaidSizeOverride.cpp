#include "RaidSizeOverride.h"

#include "Creature.h"
#include "Map.h"

#include <algorithm>

RaidSizeOverride& RaidSizeOverride::Instance()
{
    static RaidSizeOverride instance;
    return instance;
}

void RaidSizeOverride::Register(uint32 mapId, uint32 originalMaxPlayers, uint32 maxPlayers, float healthMultiplier)
{
    m_entries[mapId] = Entry{ originalMaxPlayers, maxPlayers, healthMultiplier };
}

void RaidSizeOverride::Clear()
{
    m_entries.clear();
}

RaidSizeOverride::Entry const* RaidSizeOverride::Find(uint32 mapId) const
{
    if (m_entries.empty())
        return nullptr;

    auto itr = m_entries.find(mapId);
    return itr != m_entries.end() ? &itr->second : nullptr;
}

bool RaidSizeOverride::IsReduced(Map const* map) const
{
    return map && IsReduced(map->GetId());
}

uint32 RaidSizeOverride::ScaleCreatureHealth(Creature const* creature, uint32 health) const
{
    if (m_entries.empty() || !creature)
        return health;

    // Player pets and totems keep their own health.
    if (creature->IsPet() || creature->IsTotem())
        return health;

    Map const* map = creature->FindMap();
    if (!map)
        return health;

    Entry const* entry = Find(map->GetId());
    if (!entry)
        return health;

    return std::max<uint32>(1, static_cast<uint32>(health * entry->healthMultiplier));
}
