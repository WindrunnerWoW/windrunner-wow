/*
 * Copyright (C) 2026 Turtle WoW
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef MANGOS_WEEKLY_QUEST_ROTATION_H
#define MANGOS_WEEKLY_QUEST_ROTATION_H

#include "Common.h"
#include "WeeklyRotationDraw.h"

#include <functional>
#include <string>
#include <vector>

/// Runtime rotation of weekly quests over quest givers. A module registers a rotation (its quest givers, which quests
/// it owns, and a function drawing the quests of a week); once per weekly period the server replaces the owned quests
/// in the creature quest relations of those givers with the new draw. The relations are put back after a GM reload of
/// creature_questrelation, and players in the world see the change at once.
///
/// Update() runs from a WorldScript after the map update has joined its workers (see WeeklyResetRuntime.cpp); modules
/// do not call it. Use WeeklyRotation::DrawSequence / Random from WeeklyRotationDraw.h to write the draw function.
class WeeklyQuestRotation
{
public:
    struct Definition
    {
        std::string name;                                   // unique; registering a name again replaces it
        std::vector<uint32> givers;                         // creature entries that offer the quests
        std::function<bool(uint32)> owns;                   // true for every quest this rotation may offer
        std::function<std::vector<uint32>(uint64)> draw;    // quest ids of a week, see WeeklyRotation::WeekIndex
    };

    static void Register(Definition definition);
    static void Update();

    // The quests currently offered by the named rotation (empty before its first period or if unknown).
    static std::vector<uint32> Current(std::string const& name);
};

#endif
