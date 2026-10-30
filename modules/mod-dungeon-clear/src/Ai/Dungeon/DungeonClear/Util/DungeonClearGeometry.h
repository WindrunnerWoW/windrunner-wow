/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARGEOMETRY_H
#define _PLAYERBOT_DUNGEONCLEARGEOMETRY_H

#include <cstddef>
#include <vector>

#include "G3D/Vector3.h"

class Player;
class dtQueryFilterExt;

// Shared geometry / line-of-sight primitives for the dungeon-clear route
// producers and the corridor-centering pass. These were previously copy-pasted
// byte-for-byte across StridedPathfinder, LongRangePathfinder and CorridorCenter
// (with comments admitting "keep the two in sync"); this is the single source of
// truth so a tuning change can't silently desync the producers.
namespace DungeonClearGeometry
{
    // Plain Euclidean distance helpers (no engine state).
    float Dist2D(float ax, float ay, float bx, float by);
    float Dist3D(float ax, float ay, float az, float bx, float by, float bz);

    // True iff the bot has a clear static-VMAP straight line between two world
    // points. Every chord is cast, however short. A null map fails open (clear)
    // so the check never blocks routing when geometry can't be queried. The
    // raycast is bumped to ~eye height so floor-touching points don't false-fail
    // on slope/stair transitions.
    bool ChordClear(Player* bot, G3D::Vector3 const& a, G3D::Vector3 const& b);

    // Walks the smoothed polyline's consecutive chords with ChordClear and
    // returns how many *leading* points form a usable corridor (counting from
    // index 0, inclusive). Returns pts.size() when the whole corridor is clean
    // (the common case).
    //
    // On a sustained obstruction it returns the length of the clean prefix so
    // the caller can still use the verified part and re-probe from its end.
    // A single blocked chord (a corner graze on a sharp convex bend) is bridged;
    // a clear chord resets the run. Two in a row truncate. A chord whose END
    // vertex has no real ground under it (Map::GetHeight, incl. GO models,
    // within ~2yd; swim vertices exempt) counts as blocked too.
    //
    // Static-VMAP-only (no game-object checks): doors and dynamic obstacles are
    // handled elsewhere (DungeonClearBlockingDoorValue / engage triggers), and
    // including them here would reject good corridors with a transient door.
    std::size_t LosCleanPrefixCount(Player* bot, std::vector<G3D::Vector3> const& pts);

    // Unit::UpdateAllowedPositionZ, but trusting the navmesh Z it was handed:
    // the static height probe looks DOWN, so under a bridge, balcony or stacked
    // floor it answers with the level below (or under the map). Keep the
    // snapped Z only when it moved the point by at most `maxDelta` yards.
    void ClampedAllowedZ(Player* bot, G3D::Vector3& p, float maxDelta = 1.5f);

    // Apply the liquid-avoidance Detour area-cost multipliers (WaterPathCost,
    // MagmaPathCost) to a freshly-built filter so the A* corridor search and the
    // string-pulled smooth path both prefer land. The water/magma polys remain in
    // the include flags, so a route still crosses liquid when no cheaper-enough
    // land path exists (water caves, mandatory swims). Costs are server-only conf
    // values, so this is safe to call from the off-map-thread route producers.
    void ApplyLiquidAreaCosts(dtQueryFilterExt& filter);
}

#endif
