#include "playerbot/playerbot.h"
#include "BgFillerMgr.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "RandomPlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "ServerFacade.h"
#include "ChatHelper.h"
#include "Battlegrounds/BattleGroundMgr.h"
#include "ObjectAccessor.h"

#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <vector>

namespace
{
    uint32 TeamIndex(Team team) { return team == ALLIANCE ? 0 : 1; }
    Team TeamOf(uint32 index) { return index == 0 ? ALLIANCE : HORDE; }

    uint32 TargetPerTeam(BattleGround const* bg)
    {
        uint32 target = sPlayerbotAIConfig.bgFillerFillToMax ? bg->GetMaxPlayersPerTeam() : bg->GetMinPlayersPerTeam();
        if (sPlayerbotAIConfig.bgFillerMaxPerTeam && target > sPlayerbotAIConfig.bgFillerMaxPerTeam)
            target = sPlayerbotAIConfig.bgFillerMaxPerTeam;
        return std::max(target, bg->GetMinPlayersPerTeam());
    }

    // Class for the role, from classes this faction can actually play.
    uint8 PickClass(Team team, bool healer, uint8& race)
    {
        static const uint8 healers[] = { CLASS_PRIEST, CLASS_DRUID, CLASS_PALADIN, CLASS_SHAMAN };
        static const uint8 damage[] = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_PRIEST,
                                        CLASS_SHAMAN, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID };

        std::vector<uint8> pool;
        if (healer)
            pool.assign(std::begin(healers), std::end(healers));
        else
            pool.assign(std::begin(damage), std::end(damage));

        RandomPlayerbotFactory factory(0);
        while (!pool.empty())
        {
            uint32 const idx = urand(0, uint32(pool.size() - 1));
            uint8 const cls = pool[idx];
            pool.erase(pool.begin() + idx);

            if (!RandomPlayerbotFactory::isAvailableRole(cls, healer ? BOT_ROLE_HEALER : BOT_ROLE_DPS))
                continue;

            uint8 const candidate = factory.GetRandomRace(cls, team);
            if (candidate && Player::TeamForRace(candidate) == team)
            {
                race = candidate;
                return cls;
            }
        }

        return 0;
    }
}

bool BgFillerMgr::Enabled()
{
    return sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.windrunnerCompanionMode && sPlayerbotAIConfig.bgFillerEnabled;
}

void BgFillerMgr::OnStartup()
{
    // Fillers never survive a restart: delete whatever a crash left behind.
    std::vector<uint32> stale;
    if (auto result = CharacterDatabase.PQuery(
            "SELECT DISTINCT bot FROM ai_playerbot_random_bots WHERE owner = 0 AND event = '%s'", Marker()))
    {
        do
        {
            stale.push_back(result->Fetch()[0].GetUInt32());
        } while (result->NextRow());
    }

    for (uint32 guid : stale)
        DeleteFiller(guid);

    if (!stale.empty())
        sLog.outString("BgFiller: removed %u leftover battleground filler bots", uint32(stale.size()));
}

void BgFillerMgr::Update(uint32 diff)
{
    if (!Enabled() && fillers.empty())
        return;

    updateTimer += diff;
    if (updateTimer < 1000)
        return;

    scanTimer += updateTimer;
    updateTimer = 0;

    if (scanTimer >= 5000)
    {
        scanTimer = 0;
        ScanDemand();
        if (Enabled())
            CreateMissing();
    }

    UpdateFillers();
}

bool BgFillerMgr::HasDemand(QueueKey const& key) const
{
    auto itr = demand.find(key);
    return itr != demand.end() && itr->second.humans > 0;
}

void BgFillerMgr::ScanDemand()
{
    demand.clear();

    std::vector<ObjectGuid> online;
    {
        std::shared_lock<HashMapHolder<Player>::LockType> guard(HashMapHolder<Player>::GetLock());
        for (auto const& itr : sObjectAccessor.GetPlayers())
            online.push_back(itr.first);
    }

    for (ObjectGuid const& guid : online)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->IsInWorld() || !player->InBattleGroundQueue())
            continue;

        // Fillers are counted from our own bookkeeping, not here.
        if (fillers.find(player->GetGUIDLow()) != fillers.end())
            continue;

        bool const human = !GetBotAI(player);

        for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
        {
            BattleGroundQueueTypeId const queueTypeId = player->GetBattleGroundQueueTypeId(slot);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            BattleGroundTypeId const bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
            BattleGroundBracketId const bracketId = sBattleGroundMgr.GetBattleGroundBracketIdFromLevel(bgTypeId, player->GetLevel());
            if (bracketId == BG_BRACKET_ID_NONE)
                continue;

            QueueKey key;
            key.queueTypeId = queueTypeId;
            key.bracketId = bracketId;

            Demand& d = demand[key];
            d.bgTypeId = bgTypeId;
            d.members[TeamIndex(player->GetTeam())]++;
            if (human)
            {
                d.humans++;
                d.referenceLevel = std::max(d.referenceLevel, player->GetLevel());
            }
        }
    }
}

void BgFillerMgr::CreateMissing()
{
    uint32 budget = sPlayerbotAIConfig.bgFillerCreatePerScan;

    for (auto const& entry : demand)
    {
        QueueKey const& key = entry.first;
        Demand const& d = entry.second;
        if (!d.humans)
            continue;

        BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(BattleGroundTypeId(d.bgTypeId));
        if (!bg)
            continue;

        uint32 const target = TargetPerTeam(bg);

        for (uint32 team = 0; team < 2 && budget; ++team)
        {
            uint32 have = d.members[team];
            uint32 healers = 0;
            uint32 ours = 0;
            for (auto const& f : fillers)
            {
                if (f.second.key.queueTypeId == key.queueTypeId && f.second.key.bracketId == key.bracketId &&
                    f.second.teamIndex == team)
                {
                    ++have;
                    ++ours;
                    if (f.second.healer)
                        ++healers;
                }
            }

            while (have < target && budget && fillers.size() < sPlayerbotAIConfig.bgFillerMaxTotal)
            {
                // Roughly one healer in five.
                bool const healer = healers * 5 < ours + 1;
                if (!CreateFiller(key, d, team, healer))
                {
                    budget = 0;
                    break;
                }

                ++have;
                ++ours;
                if (healer)
                    ++healers;
                --budget;
            }
        }

        if (!budget)
            break;
    }
}

bool BgFillerMgr::CreateFiller(QueueKey const& key, Demand const& d, uint32 teamIndex, bool healer)
{
    BattleGroundTypeId const bgTypeId = BattleGroundTypeId(d.bgTypeId);
    BattleGroundBracketId const bracketId = BattleGroundBracketId(key.bracketId);
    uint32 const minLevel = Player::GetMinLevelForBattleGroundBracketId(bracketId, bgTypeId);
    uint32 const maxLevel = std::max(minLevel, Player::GetMaxLevelForBattleGroundBracketId(bracketId, bgTypeId));

    // Close to the player who queued, inside the bracket.
    uint32 const ref = d.referenceLevel ? d.referenceLevel : maxLevel;
    uint32 lo = ref > 2 ? ref - 2 : 1;
    uint32 hi = ref + 2;
    lo = std::max(lo, minLevel);
    hi = std::min(hi, maxLevel);
    if (lo > hi)
        lo = hi = std::min(std::max(ref, minLevel), maxLevel);
    uint32 const level = urand(lo, hi);

    Team const team = TeamOf(teamIndex);
    uint8 race = 0;
    uint8 const cls = PickClass(team, healer, race);
    if (!cls)
        return false;

    BotRoles const role = healer ? BOT_ROLE_HEALER : BOT_ROLE_DPS;

    std::ostringstream params;
    params << "level=" << level
           << " class=" << ChatHelper::formatClass(cls)
           << " role=" << ChatHelper::formatRole(role)
           << " race=" << uint32(race)
           << " login=false";

    std::list<std::string> messages;
    ObjectGuid botGuid;
    sRandomPlayerbotMgr.CreateBot(nullptr, params.str(), messages, botGuid, Marker());
    if (!botGuid)
    {
        sLog.outError("BgFiller: could not create a filler bot (%s)", messages.empty() ? "unknown error" : messages.back().c_str());
        return false;
    }

    uint32 const guid = botGuid.GetCounter();

    Filler filler;
    filler.guid = guid;
    filler.key = key;
    filler.bgTypeId = d.bgTypeId;
    filler.teamIndex = teamIndex;
    filler.healer = healer;
    filler.createdAt = Clock::now();
    fillers[guid] = filler;

    sRandomPlayerbotMgr.SetExternallyManaged(guid, true);
    sRandomPlayerbotMgr.AddPlayerBot(guid, 0);

    sLog.outDetail("BgFiller: created %s level %u %s for queue %u bracket %u",
        team == ALLIANCE ? "alliance" : "horde", level, healer ? "healer" : "dps", key.queueTypeId, key.bracketId);
    return true;
}

void BgFillerMgr::UpdateFillers()
{
    Clock::time_point const now = Clock::now();
    uint32 prepareBudget = 2;
    std::vector<uint32> finished;

    for (auto& entry : fillers)
    {
        Filler& f = entry.second;
        Player* bot = sRandomPlayerbotMgr.GetPlayerBot(f.guid);

        if (!bot)
        {
            if (now - f.createdAt > std::chrono::seconds(120))
                finished.push_back(f.guid);
            continue;
        }

        PlayerbotAI* ai = GetBotAI(bot);
        if (!ai || !bot->IsInWorld())
            continue;

        if (!f.prepared)
        {
            if (!prepareBudget)
                continue;
            --prepareBudget;

            ai->SetForcedRole(uint8(f.healer ? BOT_ROLE_HEALER : BOT_ROLE_DPS));
            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.InitializeAtCurrentLevel();
            sRandomPlayerbotMgr.SetValue(f.guid, "create levelup", 0);
            sRandomPlayerbotMgr.SetValue(f.guid, "create gear", 0);
            sRandomPlayerbotMgr.SetValue(f.guid, "create group", 0);
            ai->ResetStrategies();
            f.prepared = true;
            continue;
        }

        if (bot->InBattleGround())
        {
            f.entered = true;
            continue;
        }

        if (bot->IsBeingTeleported())
            continue;

        // Its battleground is over and it is back outside.
        if (f.entered)
        {
            finished.push_back(f.guid);
            continue;
        }

        BattleGroundQueueTypeId const queueTypeId = BattleGroundQueueTypeId(f.key.queueTypeId);

        if (!f.queued)
        {
            // The player gave up before we were ready.
            if (!HasDemand(f.key))
            {
                finished.push_back(f.guid);
                continue;
            }

            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(BattleGroundTypeId(f.bgTypeId));
            if (!bg)
            {
                finished.push_back(f.guid);
                continue;
            }

            // 1337 is the core's "queued by command" battlemaster guid: no
            // battlemaster NPC needed.
            WorldPacket packet(CMSG_BATTLEMASTER_JOIN, 20);
            packet << ObjectGuid(uint64(1337)) << uint32(bg->GetMapId()) << uint32(0) << uint8(0);
            bot->GetSession()->HandleBattlemasterJoinOpcode(packet);

            f.queued = true;
            f.queuedAt = now;
            continue;
        }

        bool const invited = bot->IsInvitedForBattleGroundQueueType(queueTypeId);
        if (invited)
            continue;

        if (!bot->InBattleGroundQueueForBattleGroundQueueType(queueTypeId) || !HasDemand(f.key) ||
            now - f.queuedAt > std::chrono::seconds(sPlayerbotAIConfig.bgFillerQueueTimeout))
            finished.push_back(f.guid);
    }

    for (uint32 guid : finished)
    {
        fillers.erase(guid);
        DeleteFiller(guid);
    }
}

void BgFillerMgr::DeleteFiller(uint32 guid)
{
    sRandomPlayerbotMgr.SetExternallyManaged(guid, false);
    sRandomPlayerbotMgr.DeleteBot(ObjectGuid(HIGHGUID_PLAYER, guid));
    CharacterDatabase.PExecute("DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", guid);
}
