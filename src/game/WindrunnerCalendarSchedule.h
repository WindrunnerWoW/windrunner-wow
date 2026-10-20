#ifndef WRC_CALENDAR_SCHEDULE_H
#define WRC_CALENDAR_SCHEDULE_H

// Standard-library-only pieces shared by the bridge and its portable tests.
#include <cstdint>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

namespace WindrunnerCalendar
{
struct Span
{
    int64_t start;
    int64_t end; // exclusive
};

inline bool ParseUnsigned(const std::string& value, uint32_t& result)
{
    if (value.empty())
        return false;
    uint64_t parsed = 0;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
    {
        if (*it < '0' || *it > '9')
            return false;
        parsed = parsed * 10 + static_cast<unsigned>(*it - '0');
        if (parsed > std::numeric_limits<uint32_t>::max())
            return false;
    }
    result = static_cast<uint32_t>(parsed);
    return true;
}

inline bool IsValidRequestId(uint32_t requestId)
{
    return requestId > 0 && requestId <= 999999;
}

inline bool Overlaps(int64_t start, int64_t end, int64_t rangeStart, int64_t rangeEnd)
{
    return start < rangeEnd && rangeStart < end;
}

inline uint32_t DarkmoonEventForDate(const std::tm& localDate)
{
    const int weekOfYear = (((localDate.tm_yday - localDate.tm_wday + 7) / 7)) + 1;
    const bool isHordeWeek = (weekOfYear % 2) == 0;
    if (localDate.tm_wday == 3)
        return isHordeWeek ? 24 : 23;
    return isHordeWeek ? 5 : 4;
}

inline bool DarkmoonControllerEnabled(bool event4Disabled)
{
    return !event4Disabled;
}

// Mirrors GameEventMgr::CheckOneGameEvent: recurrence is anchored to
// start + leapDays * 86400, while the configured [start,end) bounds remain
// absolute. The server's signed remainder makes the event continuously active
// from start until that adjusted anchor when the anchor is still in the future.
// Returned spans are clipped only to the configured event lifetime.
inline std::vector<Span> ScheduledSpans(int64_t start, int64_t end,
                                       uint32_t occurenceMinutes, uint32_t lengthMinutes,
                                       uint8_t leapDays, int64_t rangeStart, int64_t rangeEnd)
{
    std::vector<Span> spans;
    if (start >= end || !occurenceMinutes || !lengthMinutes || rangeStart >= rangeEnd)
        return spans;

    const uint64_t periodUnsigned = static_cast<uint64_t>(occurenceMinutes) * 60;
    const uint64_t lengthUnsigned = static_cast<uint64_t>(lengthMinutes) * 60;
    if (periodUnsigned > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        lengthUnsigned > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return spans;
    const int64_t period = static_cast<int64_t>(periodUnsigned);
    const int64_t length = static_cast<int64_t>(lengthUnsigned);
    if (length >= period)
    {
        const int64_t clippedStart = start;
        const int64_t clippedEnd = end;
        if (Overlaps(clippedStart, clippedEnd, rangeStart, rangeEnd))
            spans.push_back(Span{clippedStart, clippedEnd});
        return spans;
    }

    const int64_t leapOffset = static_cast<int64_t>(leapDays) * 86400;
    if ((leapOffset > 0 && start > std::numeric_limits<int64_t>::max() - leapOffset) ||
        (leapOffset < 0 && start < std::numeric_limits<int64_t>::min() - leapOffset))
        return spans;
    const int64_t anchor = start + leapOffset;

    if (anchor > start)
    {
        const int64_t preAnchorEnd = anchor < end ? anchor : end;
        if (Overlaps(start, preAnchorEnd, rangeStart, rangeEnd))
            spans.push_back(Span{start, preAnchorEnd});
    }

    // Find the occurrence at or immediately before rangeStart. Recurrences
    // before the adjusted anchor are covered by the signed-remainder segment
    // above, so their index is clamped to zero.
    if ((anchor > 0 && rangeStart < std::numeric_limits<int64_t>::min() + anchor) ||
        (anchor < 0 && rangeStart > std::numeric_limits<int64_t>::max() + anchor))
        return spans;
    const int64_t delta = rangeStart - anchor;
    int64_t quotient = delta / period;
    if (delta < 0 && delta % period)
        --quotient;
    if (quotient < 0)
        quotient = 0;
    if (quotient > std::numeric_limits<int64_t>::max() / period)
        return spans;
    const int64_t step = quotient * period;
    if (anchor > 0 && step > std::numeric_limits<int64_t>::max() - anchor)
        return spans;
    int64_t occurrence = anchor + step;

    // At most one prior occurrence can overlap because length < period.
    while (occurrence < end && occurrence < rangeEnd)
    {
        if (occurrence <= std::numeric_limits<int64_t>::max() - length)
        {
            const int64_t occurrenceEnd = occurrence + length;
            const int64_t clippedStart = occurrence < start ? start : occurrence;
            const int64_t clippedEnd = occurrenceEnd > end ? end : occurrenceEnd;
            if (clippedStart < clippedEnd && Overlaps(clippedStart, clippedEnd, rangeStart, rangeEnd))
            {
                if (!spans.empty() && spans.back().end >= clippedStart)
                    spans.back().end = spans.back().end > clippedEnd ? spans.back().end : clippedEnd;
                else
                    spans.push_back(Span{clippedStart, clippedEnd});
            }
        }
        if (occurrence > std::numeric_limits<int64_t>::max() - period)
            break;
        occurrence += period;
    }
    return spans;
}

inline std::string SanitizeTitle(const std::string& title, size_t maxBytes)
{
    std::string clean;
    clean.reserve(title.size() < maxBytes ? title.size() : maxBytes);
    for (size_t pos = 0; pos < title.size();)
    {
        const unsigned char ch = static_cast<unsigned char>(title[pos]);
        if (ch < 0x80)
        {
            if (clean.size() == maxBytes)
                break;
            clean.push_back((ch < 0x20 || ch == 0x7f || ch == '|' || ch == '\\') ? ' ' : static_cast<char>(ch));
            ++pos;
            continue;
        }

        const size_t sequenceLength = (ch & 0xe0) == 0xc0 ? 2 : (ch & 0xf0) == 0xe0 ? 3 : (ch & 0xf8) == 0xf0 ? 4 : 0;
        bool validSequence = sequenceLength != 0 && pos + sequenceLength <= title.size();
        for (size_t offset = 1; validSequence && offset < sequenceLength; ++offset)
            validSequence = (static_cast<unsigned char>(title[pos + offset]) & 0xc0) == 0x80;
        if (!validSequence)
        {
            if (clean.size() == maxBytes)
                break;
            clean.push_back(' ');
            ++pos;
            continue;
        }
        if (clean.size() + sequenceLength > maxBytes)
            break;
        clean.append(title, pos, sequenceLength);
        pos += sequenceLength;
    }
    while (!clean.empty() && clean[clean.size() - 1] == ' ')
        clean.erase(clean.size() - 1);
    return clean;
}
}

#endif
