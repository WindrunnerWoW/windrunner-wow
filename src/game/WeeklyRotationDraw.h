/*
 * Copyright (C) 2026 Turtle WoW
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef MANGOS_WEEKLY_ROTATION_DRAW_H
#define MANGOS_WEEKLY_ROTATION_DRAW_H

// The pure part of the weekly quest rotation: no core, database or world dependency, so a module can unit test its
// draw policy with nothing but this header.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <vector>

namespace WeeklyRotation
{
    // SplitMix64: a deterministic stream, so every server and every restart draws the same quests for a given week.
    class Random
    {
    public:
        explicit Random(std::uint64_t seed) : m_state(seed) {}

        std::uint64_t Next()
        {
            m_state += 0x9e3779b97f4a7c15ull;
            std::uint64_t z = m_state;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }

        // A value in [0, count). count must not be 0.
        std::size_t Below(std::size_t count)
        {
            return static_cast<std::size_t>(Next() % count);
        }

    private:
        std::uint64_t m_state;
    };

    // Number of the local calendar week (counted from the 1970-01-01 epoch) that holds a period boundary timestamp,
    // as returned by WeeklyQuestLedger::GetCurrentPeriod. Counting local civil days survives DST's 167/169-hour weeks.
    inline std::uint64_t WeekIndex(std::uint64_t boundary)
    {
        time_t timestamp = static_cast<time_t>(boundary);
        std::tm date{};
#ifdef _WIN32
        localtime_s(&date, &timestamp);
#else
        localtime_r(&timestamp, &date);
#endif
        int year = date.tm_year + 1900;
        unsigned const month = static_cast<unsigned>(date.tm_mon + 1);
        year -= month <= 2;
        int const era = (year >= 0 ? year : year - 399) / 400;
        unsigned const yoe = static_cast<unsigned>(year - era * 400);
        unsigned const doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + date.tm_mday - 1;
        int const days = era * 146097 + static_cast<int>(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
        return static_cast<std::uint64_t>(std::max(0, days) / 7);
    }

    // The quests of every week, as N quest ids per week. A week's draw may depend on the one before it (to avoid
    // repeats), so the weeks are generated in order from week 0 once and every later call is a lookup. Give each
    // rotation its own seed: two rotations with one seed would draw in lockstep.
    template <std::size_t N>
    class DrawSequence
    {
    public:
        using Draw = std::array<std::uint32_t, N>;
        // Draws one week from the previous one (all zero for week 0), taking its randomness from the shared stream.
        using Step = std::function<Draw(Draw const& previous, Random& random)>;

        DrawSequence(std::uint64_t seed, Step step) : m_random(seed), m_step(std::move(step)) {}

        Draw Get(std::uint64_t week)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (m_draws.size() <= week)
                m_draws.push_back(m_step(m_draws.empty() ? Draw{} : m_draws.back(), m_random));
            return m_draws[static_cast<std::size_t>(week)];
        }

    private:
        std::mutex m_mutex;
        Random m_random;
        Step m_step;
        std::vector<Draw> m_draws;
    };
}

#endif
