/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NavmeshSnap.h"

#include <algorithm>
#include <cmath>

#include "DetourExtended.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "Map.h"
#include "MapCollisionData.h"
#include "MapDefines.h"
#include "Player.h"

namespace
{
    constexpr float SNAP_VERT_EXTENT = 10.0f;
}

NavmeshSnap::Result NavmeshSnap::Snap(Map const* map, float x, float y, float z, float maxRadius)
{
    Result result;
    if (!map)
        return result;

    // GetMMapData()/GetNavMeshQuery() is non-const because the query is lazily
    // built on first access; const_cast is the same pattern PathGenerator uses
    // when constructed from a const WorldObject. The query itself is then
    // accessed read-only.
    // By value: this core hands out a snapshot handle, not a live member.
    Map::MapCollisionData coll = map->GetMapCollisionData();
    dtNavMesh const* navMesh = coll.GetMMapData().GetNavMesh();
    dtNavMeshQuery const* query = coll.GetMMapData().GetNavMeshQuery();
    if (!navMesh || !query)
        return result;

    // Simple ground/water filter — matches the player branch of
    // PathGenerator::CreateFilter so we snap to polys a player can stand on.
    dtQueryFilterExt filter;
    filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA);
    filter.setExcludeFlags(0);

    // World (x, y, z) maps to detour (y, z, x).
    float const point[3] = { y, z, x };

    // Two passes: requested radius, then double. Two attempts is enough — if
    // a point is more than 60yd from any walkable poly it's truly off-mesh.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        float const radius = (attempt == 0) ? maxRadius : maxRadius * 2.0f;
        float const extents[3] = { radius, SNAP_VERT_EXTENT, radius };
        dtPolyRef polyRef = 0;
        float closest[3] = { 0.0f, 0.0f, 0.0f };
        if (dtStatusSucceed(query->findNearestPoly(point, extents, &filter, &polyRef, closest)) &&
            polyRef != 0)
        {
            result.ok = true;
            result.x = closest[2];
            result.y = closest[0];
            result.z = closest[1];
            float const dx = result.x - x;
            float const dy = result.y - y;
            float const dz = result.z - z;
            result.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            return result;
        }
    }

    return result;
}

NavmeshSnap::Result NavmeshSnap::Snap(Player const* bot, float x, float y, float z, float maxRadius)
{
    if (!bot)
        return {};
    return Snap(bot->FindMap(), x, y, z, maxRadius);
}

NavmeshSnap::Result NavmeshSnap::SnapColumn(Map const* map, float x, float y, float z,
                                            float halfHeight, float radius)
{
    Result result;
    if (!map)
        return result;

    Map::MapCollisionData coll = map->GetMapCollisionData();
    dtNavMeshQuery const* query = coll.GetMMapData().GetNavMeshQuery();
    if (!query)
        return result;

    dtQueryFilterExt filter;
    filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA);
    filter.setExcludeFlags(0);

    float const point[3] = { y, z, x };
    float const extents[3] = { radius, halfHeight, radius };
    dtPolyRef ref = 0;
    float nearest[3] = { 0.0f, 0.0f, 0.0f };
    if (dtStatusFailed(const_cast<dtNavMeshQuery*>(query)->findNearestPoly(point, extents, &filter, &ref, nearest)) || !ref)
        return result;

    result.ok = true;
    result.x = nearest[2];
    result.y = nearest[0];
    result.z = nearest[1];
    float const dx = result.x - x, dy = result.y - y, dz = result.z - z;
    result.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return result;
}

bool NavmeshSnap::HasGround(Map const* map, float x, float y, float z,
                            float tolerance, float* outZ)
{
    if (!map || !MaNGOS::IsValidMapCoord(x, y, z))
        return false;

    // GetHeight looks down from probeZ + 2, so start `tolerance` above the
    // point to also accept a floor that sits a little over it (a snapped poly
    // is often a hair under the render mesh).
    float const probeZ = z + tolerance;
    float const ground = map->GetHeight(x, y, probeZ, true, tolerance * 2.0f + 2.0f);
    if (ground <= INVALID_HEIGHT)
        return false;
    if (std::fabs(ground - z) > tolerance)
        return false;
    if (outZ)
        *outZ = ground;
    return true;
}

NavmeshSnap::Result NavmeshSnap::SnapGrounded(Map const* map, float x, float y, float z,
                                              float maxRadius, float groundTolerance)
{
    Result snapped = Snap(map, x, y, z, maxRadius);
    if (!snapped.ok)
        return snapped;
    float groundZ = snapped.z;
    if (!HasGround(map, snapped.x, snapped.y, snapped.z, groundTolerance, &groundZ))
        return Result{};
    // Land on the collision floor, not the mesh approximation of it, so the
    // arrival never starts inside the floor.
    snapped.z = std::max(snapped.z, groundZ) + 0.1f;
    return snapped;
}
