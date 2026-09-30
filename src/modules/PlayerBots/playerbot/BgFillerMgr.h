#pragma once

#include "Common.h"
#include <chrono>
#include <map>
#include <string>

class Player;

// Battleground filler for companion mode.
//
// With WindrunnerCompanionMode the random-bot pool is offline, so nothing
// ever joins a battleground queue next to the player. When a real player
// queues, this creates temporary bots of the right level bracket for BOTH
// factions, prepares them (spells, talents, gear), queues them and deletes
// them again once their battleground is over or the player left the queue.
// They use the random-bot account pool and are marked "bg_filler" so a crash
// or restart cleans them up on the next start.
class BgFillerMgr
{
public:
    static BgFillerMgr& instance()
    {
        static BgFillerMgr mgr;
        return mgr;
    }

    static bool Enabled();
    static const char* Marker() { return "bg_filler"; }

    void OnStartup();
    void Update(uint32 diff);

    // Login gate for companion mode: only fillers this manager created.
    bool AllowsLogin(uint32 guidLow) const { return fillers.find(guidLow) != fillers.end(); }

private:
    using Clock = std::chrono::steady_clock;

    struct QueueKey
    {
        uint32 queueTypeId = 0;
        uint32 bracketId = 0;
        bool operator<(QueueKey const& o) const
        {
            return queueTypeId != o.queueTypeId ? queueTypeId < o.queueTypeId : bracketId < o.bracketId;
        }
    };

    struct Demand
    {
        uint32 bgTypeId = 0;
        uint32 humans = 0;           // real players queued / inside
        uint32 members[2] = {0, 0};  // everyone but fillers, per team (A, H)
        uint32 referenceLevel = 0;   // a queued player's level
    };

    struct Filler
    {
        uint32 guid = 0;
        QueueKey key;
        uint32 bgTypeId = 0;
        uint32 teamIndex = 0;        // 0 alliance, 1 horde
        bool healer = false;
        Clock::time_point createdAt;
        Clock::time_point queuedAt;
        bool prepared = false;
        bool queued = false;
        bool entered = false;
    };

    void ScanDemand();
    void CreateMissing();
    void UpdateFillers();
    bool CreateFiller(QueueKey const& key, Demand const& demand, uint32 teamIndex, bool healer);
    void DeleteFiller(uint32 guid);
    bool HasDemand(QueueKey const& key) const;

    std::map<QueueKey, Demand> demand;
    std::map<uint32, Filler> fillers;
    uint32 updateTimer = 0;
    uint32 scanTimer = 0;
};

#define sBgFillerMgr BgFillerMgr::instance()
