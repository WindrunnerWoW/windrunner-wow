/*
 * Raid size override registry.
 *
 * Lets a module (windrunner-20playerraids) shrink selected raid maps to a smaller roster.
 * The module registers maps at startup; the core and encounter scripts query
 * this registry for the entry cap, creature health scaling, autoscaler bypass
 * and per-encounter target counts. With nothing registered every query is a
 * no-op, so the core behaves exactly as before.
 */

#ifndef _RAIDSIZEOVERRIDE_H
#define _RAIDSIZEOVERRIDE_H

#include "Common.h"

#include <unordered_map>

class Map;
class Creature;

class RaidSizeOverride
{
    public:
        struct Entry
        {
            uint32 originalMaxPlayers;
            uint32 maxPlayers;
            float healthMultiplier;
        };

        static RaidSizeOverride& Instance();

        // Registration is only safe at startup, before any instance map exists.
        void Register(uint32 mapId, uint32 originalMaxPlayers, uint32 maxPlayers, float healthMultiplier);
        void Clear();

        Entry const* Find(uint32 mapId) const;
        bool IsReduced(uint32 mapId) const { return Find(mapId) != nullptr; }
        bool IsReduced(Map const* map) const;

        // Health a freshly initialised creature on mapId should get.
        uint32 ScaleCreatureHealth(Creature const* creature, uint32 health) const;

        // Encounter helper: reducedValue on a reduced raid, originalValue otherwise.
        template <class T>
        T Pick(Map const* map, T originalValue, T reducedValue) const
        {
            return IsReduced(map) ? reducedValue : originalValue;
        }

    private:
        RaidSizeOverride() = default;
        std::unordered_map<uint32, Entry> m_entries;
};

#define sRaidSizeOverride RaidSizeOverride::Instance()

#endif
