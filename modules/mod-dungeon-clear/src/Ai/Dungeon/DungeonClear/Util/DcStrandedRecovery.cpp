/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcStrandedRecovery.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include <cmath>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCombatFlag.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPullPlanner.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStrandedDecision.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonPathFollower.h"

#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "AiObjectContext.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"

namespace
{
    // Progress epsilon (yards): the tank must close on the next anchor by at least
    // this much versus its closest-ever approach to count as progress. Matches the
    // harness livelock net so the two agree on what "closing distance" means.
    constexpr float DC_STRANDED_PROGRESS_EPSILON_YD = 1.0f;

    // Fall guard tuning (see DcStrandedRecovery::FallGuard).
    constexpr uint32 DC_FALL_GUARD_CHECK_MS     = 250;    // per-bot probe cadence
    constexpr float  DC_FALL_GUARD_SAFE_TOL_YD  = 1.0f;   // "standing on ground"
    constexpr float  DC_FALL_GUARD_SUNK_YD      = 3.0f;   // surface this far above = under the floor
    constexpr float  DC_FALL_GUARD_DROP_YD      = 12.0f;  // airborne this far under the last safe Z
    constexpr uint32 DC_FALL_GUARD_SUNK_MS      = 500;    // under-floor must persist this long
    constexpr uint32 DC_FALL_GUARD_COOLDOWN_MS  = 2000;   // between two snap-backs
    constexpr uint32 DC_FALL_GUARD_BURST_MS     = 30000;  // burst window ...
    constexpr uint32 DC_FALL_GUARD_BURST_MAX    = 5;      // ... and its snap-back budget

    // Can this spot be teleported to? The recorded safe position was ground when
    // stamped, but a door/GO can have closed over it since; re-ask the world.
    bool LandingIsGrounded(Map const* map, float x, float y, float z)
    {
        return NavmeshSnap::HasGround(map, x, y, z, 2.0f);
    }

    // Where to put a bot that has to be teleported back onto the floor. The
    // bot's OWN last safe spot first (the fall guard's record: real ground it
    // stood on moments ago, nearby, same instance), then the tall-column mesh
    // snap - but only if real ground agrees with it, because the column happily
    // answers with a phantom deck or the floor of a sealed shaft. False when
    // neither is trustworthy: better to leave the bot than drop it somewhere
    // worse.
    bool PickRescueLanding(Player* p, NavmeshSnap::Result const& column,
                           float& outX, float& outY, float& outZ)
    {
        Map const* map = p->GetMap();
        if (PlayerbotAI* ai = GET_PLAYERBOT_AI(p))
        {
            DcRunState const& s = DcRun::Of(ai);
            if (s.safeValid && s.safeMapId == p->GetMapId() &&
                s.safeInstanceId == p->GetInstanceId() &&
                std::hypot(s.safeX - p->GetPositionX(), s.safeY - p->GetPositionY()) < 60.0f &&
                LandingIsGrounded(map, s.safeX, s.safeY, s.safeZ))
            {
                outX = s.safeX;
                outY = s.safeY;
                outZ = s.safeZ;
                return true;
            }
        }
        if (column.ok && LandingIsGrounded(map, column.x, column.y, column.z))
        {
            outX = column.x;
            outY = column.y;
            outZ = column.z;
            return true;
        }
        return false;
    }

    // Walk the leader's same-map group into kernel rows and report whether the
    // party is FIGHTING. Recover re-walks the live group itself, so a plain value
    // snapshot is all the kernel needs here.
    //
    // Engagement, NOT the combat flag. This failsafe exists for exactly one
    // situation — a member stuck out of range forever while the tank waits on it —
    // and the raw flag disabled it in the one case that produced that situation
    // most reliably: a hostile area aura flags the whole party in with nothing
    // aggroed, so `partyEngaged` read true every tick, which both re-armed the
    // progress clock and made Decide() early-out. The failsafe could never fire
    // (Arcatraz heroic, tr-20260801-194932-20: followers parked 30yd back for 15
    // minutes, run killed by the no-progress watchdog). A real fight still blocks
    // the teleport and still counts as progress — that is what engagement means.
    void BuildSnapshot(Player* leader, std::vector<DcStrandedDecision::Member>& out,
                       bool& partyEngaged)
    {
        partyEngaged = DcCombatFlag::AnyPartyEngagement(leader);

        Group* group = leader->GetGroup();
        if (!group)
            return;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;

            DcStrandedDecision::Member m;
            m.isBot = GET_PLAYERBOT_AI(member) != nullptr;
            m.isAlive = member->IsAlive();
            m.onMap = member->GetMapId() == leader->GetMapId();
            m.isTank = member == leader;
            m.distToTank = leader->GetDistance(member);
            out.push_back(m);
        }
    }

    // Out-of-combat progress detector, mirroring DcTestRunJob's livelock net but
    // ticked live on the leader: a completed encounter, a new cleared anchor, or
    // the tank closing on the next anchor each re-stamps the clock. Updates the
    // last-seen snapshot on the leader's run state and returns whether progress
    // was observed this tick.
    bool DetectProgress(Player* leader, PlayerbotAI* leaderAI, DcRunState& run)
    {
        AiObjectContext* ctx = leaderAI->GetAiObjectContext();
        bool progressed = false;

        uint32 mask = run.progressMask;
        if (InstanceScript* inst = DcTargeting::GetInstanceScript(leader))
            mask = inst->GetCompletedEncounterMask();
        if (mask != run.progressMask)
        {
            run.progressMask = mask;
            progressed = true;
        }

        std::size_t const anchors =
            ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().size();
        if (static_cast<uint32>(anchors) != run.progressAnchors)
        {
            run.progressAnchors = static_cast<uint32>(anchors);
            progressed = true;
        }

        std::optional<DungeonBossInfo> const next =
            ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
        if (next.has_value() && next->mapId == leader->GetMapId())
        {
            // Re-arm on target change: distance to a NEW anchor is unrelated to the
            // best held for the old one, and would otherwise read as an instant
            // regression that never recovers.
            if (next->entry != run.progressAnchorEntry)
            {
                run.progressAnchorEntry = next->entry;
                run.progressBestDist = -1.0f;
            }
            float const dist = leader->GetDistance(next->x, next->y, next->z);
            if (run.progressBestDist < 0.0f ||
                dist < run.progressBestDist - DC_STRANDED_PROGRESS_EPSILON_YD)
            {
                run.progressBestDist = dist;
                progressed = true;
            }
        }

        return progressed;
    }
}

namespace DcStrandedRecovery
{
    bool Enabled(Player* bot)
    {
        return DcSettings::GetBool(bot, "StrandedRecovery");
    }

    bool Evaluate(Player* bot)
    {
        if (!bot || bot->isDead())
            return false;
        PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(bot);
        if (!leaderAI)
            return false;
        // Leader-only: the failsafe teleports members TO the tank, so the elected
        // leader is the sole clock owner. No-op on every other bot.
        if (!DcLeaderSignal::IsDungeonClearLeader(bot))
            return false;

        DcRunState& run = DcRun::Of(leaderAI);
        if (!run.enabled || !Enabled(bot))
            return false;

        uint32 const now = getMSTime();

        // A pause is an intentional hold, not a stall: keep the clock fresh so a
        // long Wait-at-Boss / door pause never makes the run look frozen on resume.
        if (run.paused)
        {
            run.progressMs = now ? now : 1;
            return false;
        }

        std::vector<DcStrandedDecision::Member> members;
        bool partyEngaged = false;
        BuildSnapshot(bot, members, partyEngaged);

        // A FIGHT re-arms the clock wholesale — a fight is progress, so neither a
        // long boss fight nor a between-pulls skirmish ever burns the budget. A
        // bare combat FLAG with nothing fighting is not progress and must not
        // re-arm it; see BuildSnapshot. Otherwise fall to the closing-distance /
        // encounter detector.
        bool const progressed = partyEngaged || DetectProgress(bot, leaderAI, run);
        if (progressed || run.progressMs == 0)
            run.progressMs = now ? now : 1;

        DcStrandedDecision::Inputs in;
        in.enabled = true;
        in.nowMs = now;
        in.lastProgressMs = run.progressMs;
        in.noProgressTimeoutMs = DcSettings::GetUInt(bot, "StrandedRecoveryNoProgressSecs") * 1000;
        in.partyEngaged = partyEngaged;
        in.maxSpread = DcSettings::GetFloat(bot, "PartyMaxSpread");

        return DcStrandedDecision::Decide(in, members).recover;
    }

    void Recover(Player* leader)
    {
        if (!leader || leader->isDead())
            return;
        PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
        if (!leaderAI || !DcLeaderSignal::IsDungeonClearLeader(leader))
            return;
        Group* group = leader->GetGroup();
        if (!group)
            return;

        float const maxSpread = DcSettings::GetFloat(leader, "PartyMaxSpread");
        float const lx = leader->GetPositionX();
        float const ly = leader->GetPositionY();
        float const lz = leader->GetPositionZ();

        // Altitude sanity BEFORE anything else: if the leader himself is
        // parked in the air - live, stock movement repeatedly grounded bots
        // on the OVERWORLD height over the Deadmines entrance (Z 130..216
        // with the real floor at ~60) - every rescue below would COPY that
        // height onto the strays it teleports in. Column-snap him back onto
        // the mesh and let the next tick work from real positions.
        {
            NavmeshSnap::Result const column =
                NavmeshSnap::SnapColumn(leader->GetMap(), lx, ly, lz);
            float rx = 0.0f, ry = 0.0f, rz = 0.0f;
            if (column.ok && std::fabs(column.z - lz) > 25.0f &&
                PickRescueLanding(leader, column, rx, ry, rz))
            {
                leader->GetMotionMaster()->Clear();
                leader->SetFallInformation(0, 0.0f);
                leader->NearTeleportTo(rx, ry, rz, leader->GetOrientation(),
                                       /*casting*/ false, /*vehicle*/ false,
                                       /*withPet*/ true);
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] altitude sanity: {:.0f}y off the mesh -> back onto grounded "
                         "({:.1f},{:.1f},{:.1f})",
                         leader->GetName(), std::fabs(column.z - lz), rx, ry, rz);
                return;
            }
        }

        uint32 moved = 0;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == leader)
                continue;
            if (!member->IsInWorld() || !member->IsAlive())
                continue;                       // dead members are the rez recovery's job
            if (member->GetMapId() != leader->GetMapId())
                continue;
            if (!GET_PLAYERBOT_AI(member))       // bots only, never a human
                continue;
            // Same altitude sanity for a member floating over the mesh -
            // his Z poisons GetDistance and any rescue that copies it.
            {
                float const mz = member->GetPositionZ();
                NavmeshSnap::Result const column = NavmeshSnap::SnapColumn(
                    member->GetMap(), member->GetPositionX(), member->GetPositionY(), mz);
                float rx = 0.0f, ry = 0.0f, rz = 0.0f;
                if (column.ok && std::fabs(column.z - mz) > 25.0f &&
                    PickRescueLanding(member, column, rx, ry, rz))
                {
                    member->GetMotionMaster()->Clear();
                    member->SetFallInformation(0, 0.0f);
                    member->NearTeleportTo(rx, ry, rz, member->GetOrientation(),
                                           /*casting*/ false, /*vehicle*/ false,
                                           /*withPet*/ true);
                    LOG_INFO("playerbots.dungeonclear",
                             "[DC:{}] altitude sanity: {} {:.0f}y off the mesh -> back onto "
                             "grounded ({:.1f},{:.1f},{:.1f})",
                             leader->GetName(), member->GetName(), std::fabs(column.z - mz),
                             rx, ry, rz);
                    continue;
                }
            }

            float const strandedDist = leader->GetDistance(member);
            if (strandedDist <= maxSpread)
                continue;                       // in range — not stranded

            // Fan the strays out a little around the tank so they don't stack on
            // one point, and drop any stale follow spline that would otherwise drag
            // them straight back toward wherever they were stuck.
            float const angle = leader->GetOrientation() + static_cast<float>(moved) * 0.7f;
            float const off = 1.5f + 0.5f * static_cast<float>(moved);
            float tx = lx + std::cos(angle) * off;
            float ty = ly + std::sin(angle) * off;
            float tz = lz;

            // The fanned spot carries the leader's Z only on flat ground: next
            // to a ledge or a stair it is air (or another floor). Put it on the
            // mesh with real ground under it, else just aim at the leader.
            {
                NavmeshSnap::Result const spot =
                    NavmeshSnap::SnapGrounded(leader->GetMap(), tx, ty, lz, 3.0f);
                if (spot.ok && std::fabs(spot.z - lz) < 3.0f)
                {
                    tx = spot.x;
                    ty = spot.y;
                    tz = spot.z;
                }
                else
                {
                    tx = lx;
                    ty = ly;
                }
            }

            // Run there, never teleport (see DcRouteRecorder's teleport
            // filter and the no-shortcuts rule): a rescue that moves a bot
            // through walls both skips content and poisons every route the
            // recorder captures on that leg.
            member->GetMotionMaster()->Clear();
            member->GetMotionMaster()->MovePoint(0, tx, ty, tz, MOVE_PATHFINDING | MOVE_RUN_MODE,
                                                 0.0f, -10.0f, /*generatePath*/ true, false);
            ++moved;

            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] stranded-recovery: no progress past the timeout with {} out of "
                     "range ({:.0f}yd) -> sent running to the tank",
                     leader->GetName(), member->GetName(), strandedDist);
        }

        if (moved == 0)
            return;

        // Make the rescue STICK. In pull mode the followers are pinned by
        // hold-at-camp, so the moment they land next to the tank they walk right
        // back to the camp — and if that camp is what stranded them (stale behind a
        // driver that isn't advance, or on ground they can't path back onto) the
        // teleport is undone within seconds and the same freeze repeats every
        // timeout, forever. Observed live in heroic Old Hillsbrad: five rescues, all
        // reversed. Re-anchor the camp onto walked ground behind the tank, unless a
        // pull maneuver is in flight (there the camp is the drag destination and
        // must not move under the tank's feet).
        //
        // "In flight" means a HOLDING phase (Forming/Advancing/Returning) — the legs
        // where the party is pinned passive and the tank is hauling a pack home. It
        // does NOT mean Engage: by then the drag is over and the party has been
        // released, so the camp is free to move. The first cut of this guard tested
        // `phase == Idle`, which skipped the re-anchor for the whole Engage phase —
        // and a phase orphaned at Engage is exactly the state that strands a party
        // (heroic Old Hillsbrad tr-20260801-174432-3: nine rescues, every one of
        // them reversed because this block never ran). A failsafe must not stand
        // down in the state it exists for.
        AiObjectContext* ctx = leaderAI->GetAiObjectContext();
        DcPullContext& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
        if (!DcLeaderSignal::IsPullPhaseHolding(static_cast<uint32>(pull.phase)) &&
            pull.HasCamp())
        {
            float const setback = DcSettings::GetFloat(leader, "PullSetback");
            float const maxDrag = DcSettings::GetFloat(leader, "PullMaxDrag");
            std::optional<Position> const trail =
                DcPullPlanner::ComputeTrailCamp(leaderAI, setback, maxDrag);
            pull.camp = trail ? *trail : leader->GetPosition();
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] stranded-recovery: camp re-anchored to ({:.1f},{:.1f},{:.1f}) so "
                     "the rescued members aren't sent back to the old one",
                     leader->GetName(), pull.camp.GetPositionX(), pull.camp.GetPositionY(),
                     pull.camp.GetPositionZ());
        }

        // Re-arm the clock: the strays are back at the tank but nothing else has
        // changed, so without this the very next tick would read as stale again and
        // re-fire. Give the run a fresh window for follow-tank + advance to resume
        // real progress. Re-seed the closing-distance mark from the tank's current
        // spot too, so a re-measure isn't fooled by the old best.
        DcRunState& run = DcRun::Of(leaderAI);
        run.progressMs = getMSTime();
        run.progressBestDist = -1.0f;

        DcStatusPublisher::SendAddonMessage(
            leaderAI,
            "CHAT\tRescued " + std::to_string(moved) +
                (moved == 1 ? " stuck party member." : " stuck party members."));
    }

    void FallGuard(Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;
        PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
        if (!ai)
            return;
        Map* map = bot->GetMap();
        if (!map || !map->IsDungeon())
            return;

        DcRunState& self = DcRun::Of(ai);
        uint32 const now = getMSTime();
        if (self.fallGuardCheckMs && getMSTimeDiff(self.fallGuardCheckMs, now) < DC_FALL_GUARD_CHECK_MS)
            return;
        self.fallGuardCheckMs = now ? now : 1;

        // Only bots in a live run: the guard teleports, and outside a clear the
        // bot's movement is someone else's business.
        if (!DcLeaderSignal::FindRunOwner(bot))
            return;

        if (!bot->IsAlive() || bot->IsTaxiFlying() || bot->GetTransport() || bot->IsBeingTeleported())
        {
            self.fallGuardSunkSince = 0;
            return;
        }

        float const x = bot->GetPositionX();
        float const y = bot->GetPositionY();
        float const z = bot->GetPositionZ();

        // 1. Standing on real ground: this is the new safe spot.
        if (NavmeshSnap::HasGround(map, x, y, z, DC_FALL_GUARD_SAFE_TOL_YD))
        {
            self.safeValid      = true;
            self.safeMapId      = bot->GetMapId();
            self.safeInstanceId = bot->GetInstanceId();
            self.safeX = x;
            self.safeY = y;
            self.safeZ = z;
            self.fallGuardSunkSince = 0;
            return;
        }

        // Swimming bots have no floor at their feet by design, and a planned
        // drop / knockback is airborne on purpose - neither is judged.
        if (bot->IsInWater() || map->IsInWater(x, y, z))
        {
            self.fallGuardSunkSince = 0;
            return;
        }
        if (ai->IsJumping() || DcLeaderSignal::IsScriptedDropActive(bot))
            return;
        MovementGeneratorType const mt = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        if (mt == EFFECT_MOTION_TYPE || mt == CHARGE_MOTION_TYPE)
            return;

        // 2. Classify. Void: no surface anywhere below (fell out of the world).
        //    Sunk: a floor a few yards ABOVE and nothing at the feet (clipped
        //    through it). Dropped: airborne well under the last safe spot.
        float const below = map->GetHeight(x, y, z + 0.5f);
        float const above = map->GetHeight(x, y, z + 8.0f, true, 8.0f);
        bool const inVoid = below <= INVALID_HEIGHT;
        bool const sunk = above > INVALID_HEIGHT && above > z + DC_FALL_GUARD_SUNK_YD;
        bool const sameInstance = self.safeValid && self.safeMapId == bot->GetMapId() &&
                                  self.safeInstanceId == bot->GetInstanceId();
        bool const dropped = sameInstance && z < self.safeZ - DC_FALL_GUARD_DROP_YD;

        if (sunk && !inVoid && !dropped)
        {
            // A floor overhead with nothing at the feet is also a bot passing
            // under a low ledge mid-fall; only act once it persists.
            if (!self.fallGuardSunkSince)
            {
                self.fallGuardSunkSince = now ? now : 1;
                return;
            }
            if (getMSTimeDiff(self.fallGuardSunkSince, now) < DC_FALL_GUARD_SUNK_MS)
                return;
        }
        else if (!inVoid && !dropped)
        {
            self.fallGuardSunkSince = 0;
            return;   // airborne but nothing alarming (a short hop, a step off a crate)
        }

        // 3. Rate limit: one snap-back per cooldown, and a capped burst so a
        //    spot that keeps swallowing the bot cannot become a teleport loop.
        if (self.fallGuardRescueMs && getMSTimeDiff(self.fallGuardRescueMs, now) < DC_FALL_GUARD_COOLDOWN_MS)
            return;
        if (self.fallGuardRescueMs && getMSTimeDiff(self.fallGuardRescueMs, now) > DC_FALL_GUARD_BURST_MS)
            self.fallGuardRescues = 0;
        if (self.fallGuardRescues >= DC_FALL_GUARD_BURST_MAX)
            return;

        // 4. Landing: the recorded safe spot, re-checked against the world.
        //    Without one (first seconds of a run), the run owner's feet if he
        //    is standing on ground on this instance.
        float lx = 0.0f, ly = 0.0f, lz = 0.0f;
        bool landing = false;
        if (sameInstance && LandingIsGrounded(map, self.safeX, self.safeY, self.safeZ))
        {
            lx = self.safeX;
            ly = self.safeY;
            lz = self.safeZ;
            landing = true;
        }
        else if (Player* owner = DcLeaderSignal::FindRunOwner(bot))
        {
            if (owner != bot && owner->IsAlive() && owner->GetMap() == map &&
                LandingIsGrounded(map, owner->GetPositionX(), owner->GetPositionY(),
                                  owner->GetPositionZ()))
            {
                lx = owner->GetPositionX();
                ly = owner->GetPositionY();
                lz = owner->GetPositionZ();
                landing = true;
            }
        }
        if (!landing)
        {
            LOG_DEBUG("playerbots.dungeonclear",
                      "[DC:{}] fall guard: {} at ({:.1f},{:.1f},{:.1f}) but no grounded landing to "
                      "return to", bot->GetName(),
                      inVoid ? "in the void" : (dropped ? "dropped" : "under the floor"), x, y, z);
            return;
        }

        // 5. Snap back: stop everything, clear the fall (stock and bot-side
        //    sim both), teleport, and drop the DC follower state so the next
        //    tick re-snaps onto the route from the safe spot.
        bot->GetMotionMaster()->Clear();
        bot->StopMoving();
        ai->SetJumpTime(0);
        ai->ResetJumpDestination();
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR);
        bot->SetFallInformation(0, 0.0f);
        bot->NearTeleportTo(lx, ly, lz + 0.5f, bot->GetOrientation(),
                            /*casting*/ false, /*vehicle*/ false, /*withPet*/ true);
        ai->GetAiObjectContext()->GetValue<DungeonFollowerState&>(DcKey::FollowerState)->Get() =
            DungeonFollowerState{};

        self.fallGuardRescueMs = now ? now : 1;
        ++self.fallGuardRescues;
        self.fallGuardSunkSince = 0;

        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] fall guard: {} at ({:.1f},{:.1f},{:.1f}), {:.1f}y under the last safe "
                 "spot -> snapped back to ({:.1f},{:.1f},{:.1f}) [#{} this burst]",
                 bot->GetName(),
                 inVoid ? "in the void" : (dropped ? "dropped" : "under the floor"),
                 x, y, z, sameInstance ? self.safeZ - z : 0.0f, lx, ly, lz,
                 self.fallGuardRescues);
    }
}
