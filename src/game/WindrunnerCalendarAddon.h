#ifndef WRC_CALENDAR_ADDON_H
#define WRC_CALENDAR_ADDON_H

// Include this from ChatHandler.cpp after its normal server headers.
#include "GameEventMgr.h"
#include "Chat.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "WindrunnerCalendarSchedule.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace WindrunnerCalendar
{
static const char* const Prefix = "WRCALENDAR";
static const size_t MaxPayloadBytes = 240;
static const size_t MaxRows = 512;

struct CalendarRow
{
    uint32_t id;
    uint32_t holidayId;
    char category;
    int64_t start;
    int64_t end;
    std::string title;
};

inline bool IsCalendarHolidayEvent(uint32_t id)
{
    // Player-facing seasonal events. Internal quest, spawn, announcement,
    // and child events are intentionally omitted.
    switch (id)
    {
        case 1: case 2: case 7: case 8: case 9: case 10: case 11: case 12:
        case 15: case 28:
            return true;
        default:
            return false;
    }
}

inline char CalendarCategory(uint32_t id, uint32_t holidayId)
{
    if (holidayId >= 283 && holidayId <= 287)
        return 'P';
    // Any scheduled holiday exposed by the realm belongs on the calendar,
    // including custom holiday entries added after this addon was installed.
    // Hardcoded controllers and disabled/phase-gated rows are filtered below.
    if (holidayId != 0)
        return 'H';
    if (IsCalendarHolidayEvent(id))
        return 'H';
    switch (id)
    {
        case 16: case 29: case 30: case 31: case 32: case 34:
            return 'W';
        default:
            return 0;
    }
}

inline std::tm LocalTm(int64_t epoch)
{
    const time_t value = static_cast<time_t>(epoch);
    std::tm* valueTm = std::localtime(&value);
    return valueTm ? *valueTm : std::tm();
}

inline int64_t LocalMidnight(std::tm civil)
{
    civil.tm_hour = 0;
    civil.tm_min = 0;
    civil.tm_sec = 0;
    civil.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&civil));
}

inline std::string FormatCivil(int64_t epoch)
{
    const std::tm civil = LocalTm(epoch);
    std::ostringstream out;
    out << (civil.tm_year + 1900) << '|'
        << (civil.tm_mon + 1) << '|'
        << civil.tm_mday << '|'
        << civil.tm_hour << '|'
        << civil.tm_min;
    return out.str();
}

inline std::string MakeEntryPayload(const CalendarRow& row, uint32_t requestId)
{
    size_t titleLimit = 128;
    for (;;)
    {
        const std::string title = SanitizeTitle(row.title, titleLimit);
        std::ostringstream out;
        out << "E|" << requestId << '|' << row.id << '|' << row.holidayId << '|'
            << row.category << '|' << FormatCivil(row.start) << '|'
            << FormatCivil(row.end) << '|' << title;
        const std::string payload = out.str();
        if (payload.size() <= MaxPayloadBytes || titleLimit == 0)
            return payload;
        --titleLimit;
    }
}

inline void AddScheduledRows(std::vector<CalendarRow>& rows,
                             GameEventMgr::GameEventDataMap const& events,
                             int64_t rangeStart, int64_t rangeEnd)
{
    for (uint32_t id = 1; id < events.size(); ++id)
    {
        const GameEventData& event = events[id];
        const char category = CalendarCategory(id, static_cast<uint32_t>(event.holiday_id));
        if (!category || event.hardcoded || event.disabled || !event.isValid() || event.start <= 0 || event.end <= 0)
            continue;
        if (id == 4 || id == 5 || id == 23 || id == 24)
            continue; // Darkmoon is expanded by its hardcoded controller below.

        const std::vector<Span> spans = ScheduledSpans(
            static_cast<int64_t>(event.start), static_cast<int64_t>(event.end),
            event.occurence, event.length, event.leapDays, rangeStart, rangeEnd);
        for (std::vector<Span>::const_iterator span = spans.begin(); span != spans.end(); ++span)
        {
            CalendarRow row;
            row.id = id;
            row.holidayId = static_cast<uint32_t>(event.holiday_id);
            row.category = category;
            row.start = span->start;
            row.end = span->end;
            row.title = event.description;
            rows.push_back(row);
        }
    }
}

inline bool HasValidEvent(GameEventMgr::GameEventDataMap const& events, uint32_t id)
{
    return id < events.size() && events[id].isValid();
}

inline void AddDarkmoonRows(std::vector<CalendarRow>& rows,
                            GameEventMgr::GameEventDataMap const& events,
                            int64_t rangeStart, int64_t rangeEnd)
{
    // The Darkmoon script only runs while event 4 is enabled. It chooses one
    // of events 4, 5, 23, and 24 for each server-local date. Event validity is
    // the same gate used by DarkmoonFaire::Update().
    if (events.size() <= 4 || !DarkmoonControllerEnabled(events[4].disabled))
        return;

    const int64_t scanStart = [&]()
    {
        std::tm civil = LocalTm(rangeStart);
        civil.tm_mday -= 8;
        return LocalMidnight(civil);
    }();
    std::tm scanEndCivil = LocalTm(rangeEnd);
    scanEndCivil.tm_mday += 8;
    const int64_t scanEnd = LocalMidnight(scanEndCivil);

    std::tm day = LocalTm(scanStart);
    day.tm_hour = 12;
    day.tm_min = 0;
    day.tm_sec = 0;
    day.tm_isdst = -1;
    std::vector<CalendarRow> spans;
    for (int count = 0; count < 60; ++count)
    {
        const int64_t dayStart = LocalMidnight(day);
        std::tm nextDay = day;
        nextDay.tm_mday += 1;
        const int64_t dayEnd = LocalMidnight(nextDay);
        if (dayStart >= scanEnd)
            break;

        const std::tm weekday = LocalTm(dayStart + 12 * 60 * 60);
        const uint32_t selectedId = DarkmoonEventForDate(weekday);
        const bool isHordeWeek = selectedId == 5 || selectedId == 24;

        if (HasValidEvent(events, selectedId))
        {
            std::string title = isHordeWeek ? "Darkmoon Faire (Mulgore)" : "Darkmoon Faire (Elwynn)";
            if (selectedId == 23 || selectedId == 24)
                title = isHordeWeek ? "Darkmoon Faire setup (Mulgore)" : "Darkmoon Faire setup (Elwynn)";
            if (!spans.empty() && spans.back().id == selectedId && spans.back().end == dayStart)
                spans.back().end = dayEnd;
            else
            {
                CalendarRow row;
                row.id = selectedId;
                row.holidayId = isHordeWeek ? 375 : 374;
                row.category = 'H';
                row.start = dayStart;
                row.end = dayEnd;
                row.title = title;
                spans.push_back(row);
            }
        }

        day.tm_mday += 1;
        day.tm_hour = 12;
        day.tm_min = 0;
        day.tm_sec = 0;
        day.tm_isdst = -1;
        if (count == 59)
            break;
    }
    for (std::vector<CalendarRow>::const_iterator span = spans.begin(); span != spans.end(); ++span)
        if (Overlaps(span->start, span->end, rangeStart, rangeEnd))
            rows.push_back(*span);
}

inline void AppendCivilFields(std::ostringstream& out, int64_t epoch)
{
    const std::tm civil = LocalTm(epoch);
    out << '|' << (civil.tm_year + 1900) << '|' << (civil.tm_mon + 1) << '|'
        << civil.tm_mday << '|' << civil.tm_hour << '|' << civil.tm_min << '|' << civil.tm_sec;
}

inline std::vector<std::string> SplitRequestFields(const std::string& message)
{
    std::vector<std::string> fields;
    const std::string prefix = std::string(Prefix) + "\t";
    if (message.compare(0, prefix.size(), prefix) != 0)
        return fields;
    std::string body = message.substr(prefix.size());
    // Vanilla rejects bare pipes in SendAddonMessage. Accept colon requests
    // as well as the original pipe form for compatibility.
    const char delimiter = body.find(':') != std::string::npos ? ':' : '|';
    size_t begin = 0;
    for (;;)
    {
        const size_t separator = body.find(delimiter, begin);
        fields.push_back(body.substr(begin, separator == std::string::npos ? separator : separator - begin));
        if (separator == std::string::npos)
            break;
        begin = separator + 1;
    }
    return fields;
}

inline bool ParseCalendarRequest(const std::string& message, uint32_t& requestId, uint32_t& year, uint32_t& month)
{
    const std::vector<std::string> fields = SplitRequestFields(message);
    return fields.size() == 4 && fields[0] == "Q" &&
        ParseUnsigned(fields[1], requestId) && ParseUnsigned(fields[2], year) && ParseUnsigned(fields[3], month);
}

inline bool AllowRequest(uint32_t playerGuidLow)
{
    typedef std::chrono::steady_clock Clock;
    static std::map<uint32_t, Clock::time_point> lastRequest;
    const Clock::time_point now = Clock::now();
    for (std::map<uint32_t, Clock::time_point>::iterator it = lastRequest.begin(); it != lastRequest.end();)
    {
        if (now - it->second > std::chrono::minutes(2))
            it = lastRequest.erase(it);
        else
            ++it;
    }
    std::map<uint32_t, Clock::time_point>::iterator found = lastRequest.find(playerGuidLow);
    if (found != lastRequest.end() && now - found->second < std::chrono::seconds(2))
        return false;
    if (lastRequest.size() >= 4096)
    {
        std::map<uint32_t, Clock::time_point>::iterator oldest = lastRequest.begin();
        for (std::map<uint32_t, Clock::time_point>::iterator it = lastRequest.begin(); it != lastRequest.end(); ++it)
            if (it->second < oldest->second)
                oldest = it;
        lastRequest.erase(oldest);
    }
    lastRequest[playerGuidLow] = now;
    return true;
}
}

inline bool HandleWindrunnerCalendarAddonMessage(WorldSession* session, Player* player, uint32 type, std::string const& message)
{
    if (!session || !player || type != CHAT_MSG_GUILD)
        return false;

    const std::string prefix = std::string(WindrunnerCalendar::Prefix) + "\t";
    if (message.compare(0, prefix.size(), prefix) != 0)
        return false;

    if (message.size() > 256)
    {
        const std::vector<std::string> fields = WindrunnerCalendar::SplitRequestFields(message);
        uint32_t oversizedRequestId = 0;
        if (fields.size() > 1)
            WindrunnerCalendar::ParseUnsigned(fields[1], oversizedRequestId);
        player->SendAddonMessage(WindrunnerCalendar::Prefix, "X|" + std::to_string(oversizedRequestId) + "|BAD_REQUEST");
        return true;
    }

    uint32_t requestId = 0, year = 0, month = 0;
    if (!WindrunnerCalendar::ParseCalendarRequest(message, requestId, year, month))
    {
        const std::vector<std::string> fields = WindrunnerCalendar::SplitRequestFields(message);
        if (fields.size() > 1)
            WindrunnerCalendar::ParseUnsigned(fields[1], requestId);
        player->SendAddonMessage(WindrunnerCalendar::Prefix, "X|" + std::to_string(requestId) + "|BAD_REQUEST");
        return true;
    }
    if (!WindrunnerCalendar::IsValidRequestId(requestId))
    {
        player->SendAddonMessage(WindrunnerCalendar::Prefix, "X|" + std::to_string(requestId) + "|BAD_REQUEST");
        return true;
    }

    const time_t now = std::time(NULL);
    const std::tm currentCivil = WindrunnerCalendar::LocalTm(static_cast<int64_t>(now));
    const uint32_t currentYear = static_cast<uint32_t>(currentCivil.tm_year + 1900);
    if (month < 1 || month > 12 || year < currentYear - 1 || year > currentYear + 2)
    {
        player->SendAddonMessage(WindrunnerCalendar::Prefix, "X|" + std::to_string(requestId) + "|OUT_OF_RANGE");
        return true;
    }
    if (!WindrunnerCalendar::AllowRequest(player->GetGUIDLow()))
    {
        player->SendAddonMessage(WindrunnerCalendar::Prefix, "X|" + std::to_string(requestId) + "|THROTTLED");
        return true;
    }

    std::tm firstOfMonth = currentCivil;
    firstOfMonth.tm_year = static_cast<int>(year) - 1900;
    firstOfMonth.tm_mon = static_cast<int>(month) - 1;
    firstOfMonth.tm_mday = 1;
    firstOfMonth.tm_hour = 12;
    firstOfMonth.tm_min = 0;
    firstOfMonth.tm_sec = 0;
    firstOfMonth.tm_isdst = -1;
    std::tm gridStartCivil = WindrunnerCalendar::LocalTm(WindrunnerCalendar::LocalMidnight(firstOfMonth));
    gridStartCivil.tm_mday -= gridStartCivil.tm_wday;
    const int64_t rangeStart = WindrunnerCalendar::LocalMidnight(gridStartCivil);
    std::tm gridEndCivil = gridStartCivil;
    gridEndCivil.tm_mday += 42;
    const int64_t rangeEnd = WindrunnerCalendar::LocalMidnight(gridEndCivil);

    const GameEventMgr::GameEventDataMap& events = sGameEventMgr.GetEventMap();
    std::vector<WindrunnerCalendar::CalendarRow> rows;
    WindrunnerCalendar::AddScheduledRows(rows, events, rangeStart, rangeEnd);
    WindrunnerCalendar::AddDarkmoonRows(rows, events, rangeStart, rangeEnd);
    std::sort(rows.begin(), rows.end(), [](const WindrunnerCalendar::CalendarRow& left, const WindrunnerCalendar::CalendarRow& right)
    {
        if (left.start != right.start)
            return left.start < right.start;
        return left.id < right.id;
    });

    if (rows.size() > WindrunnerCalendar::MaxRows)
    {
        player->SendAddonMessage(WindrunnerCalendar::Prefix, "X|" + std::to_string(requestId) + "|TOO_MANY_EVENTS");
        return true;
    }

    std::ostringstream begin;
    begin << "B|" << requestId << '|' << static_cast<int64_t>(now) << '|' << year << '|' << month;
    WindrunnerCalendar::AppendCivilFields(begin, static_cast<int64_t>(now));
    player->SendAddonMessage(WindrunnerCalendar::Prefix, begin.str());

    uint32_t count = 0;
    for (std::vector<WindrunnerCalendar::CalendarRow>::const_iterator row = rows.begin(); row != rows.end(); ++row)
    {
        const std::string payload = WindrunnerCalendar::MakeEntryPayload(*row, requestId);
        if (payload.size() > WindrunnerCalendar::MaxPayloadBytes)
            continue;
        player->SendAddonMessage(WindrunnerCalendar::Prefix, payload);
        ++count;
    }
    player->SendAddonMessage(WindrunnerCalendar::Prefix, "Z|" + std::to_string(requestId) + "|" + std::to_string(count));
    return true;
}

#endif
