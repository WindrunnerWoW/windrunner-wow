#ifndef MANGOSSERVER_LFTMGR_H
#define MANGOSSERVER_LFTMGR_H

#include <unordered_set>
#include <array>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Common.h"

class Group;
class Player;

// Moved here from LFTQeueue.cpp's anonymous namespace: the bot fill in
// LFTBotFill.cpp needs them too.
enum LFTRoles
{
    LFT_ROLE_TANK   = 0x01,
    LFT_ROLE_HEALER = 0x02,
    LFT_ROLE_DAMAGE = 0x04
};

class LFTManager
{
    // Groups formed (or adopted) by the LFT matcher itself.
    // TakeFromBotOnlyGroup may only raid THESE: a bot-only group can just as
    // well belong to someone else - live, LFT ripped the tank out of a
    // mod-dungeon-clear test party mid-run because the "bot-only means ours"
    // assumption was never actually checked. Ids of long-gone groups linger
    // harmlessly (group ids are not recycled within an uptime).
    std::unordered_set<uint32> m_lftGroupIds;

    public:
        LFTManager();

        bool HandleAddonMessage(Player* player, uint32 type, std::string const& rawMessage);
        void Update(uint32 diff);
        void OnPlayerLogout(ObjectGuid const& guid);

        // Group-listing module API. World-thread only. Acts only for characters a module
        // manages (Script_IsAIControlled): a human's listing and role rows stay written by
        // that human's own client. Validation is the addon path's (one listing per leader,
        // leader or solo, category, role limits) plus stricter checks; packets are the addon
        // path's (CreateListing / ApplyListingSignup / EraseListing + BroadcastGroupsList).
        // Every call that changes nothing sends nothing.

        // Lists the leader's group in the group finder (as C2S_NEW_GROUP). Fails if the
        // leader already has a listing (RemoveGroupListing first). limits = tank, healer,
        // damage wanted; all zero = no roles. category 1-4 as the addon's browse tabs.
        // outListingId, if given, receives the new listing's id, set only on success.
        bool CreateGroupListing(Player* leader, std::string const& title, std::string const& description,
                                uint8 category, std::array<uint8, 3> const& limits, uint32* outListingId = nullptr);
        // Removes the leader's listing (as C2S_DELETE_GROUP). False if it has none.
        bool RemoveGroupListing(Player* leader);
        // Sets a managed party member's role row on the leader's listing (as that member's
        // C2C_ROLECHECK_RESPONSE t/h/d). role = one LFTRoles bit allowed for the member's class.
        // Never invites anybody: the member must already be the leader or in the leader's group.
        bool AcceptListingSignup(Player* leader, ObjectGuid const& memberGuid, uint8 role);
        // Clears an existing role row on the leader's listing (as C2C_ROLECHECK_RESPONSE;0).
        // Only a module-managed member of the leader's party: the member must resolve as a
        // live Player (in world), be the leader or in the leader's group, and be bot-managed;
        // a human's row is changed only by their own client. False if the checks fail or the
        // listing has no row for that member. Stale rows of departed players stay the job of
        // the PruneListingSignups path.
        bool DeclineListingSignup(Player* leader, ObjectGuid const& memberGuid);

        struct ListingSignupInfo
        {
            std::string name;
            std::string className;
            uint32 level = 0;
            uint8 role = 0;            // one LFTRoles bit
        };
        struct GroupListingInfo
        {
            uint32 id = 0;
            ObjectGuid creatorGuid;
            std::string creator;
            std::string className;
            uint32 level = 0;
            uint32 team = 0;
            bool isHardcore = false;
            std::string title;
            std::string description;
            uint8 category = 1;
            std::array<uint8, 3> limit = {{0, 0, 0}};
            std::array<uint8, 3> numConfirmed = {{0, 0, 0}};
            std::vector<ListingSignupInfo> signups;
        };
        // World-thread only. Copy of all listings (rows as last saved; they are pruned on the
        // next change, not here). Not const: loads the stored listings on first use.
        std::vector<GroupListingInfo> GetGroupListings();

    private:
        struct ListingSignup
        {
            std::string name;
            std::string className;
            uint32 level = 0;
            bool confirmed = false;
        };

        struct Listing
        {
            uint32 id = 0;
            ObjectGuid creatorGuid;
            std::string creator;
            std::string className;
            uint32 level = 0;
            uint32 team = 0;
            bool isHardcore = false;
            std::string title;
            std::string description;
            uint8 category = 1;
            std::array<uint8, 3> limit = {{0, 0, 0}};
            std::array<uint8, 3> numConfirmed = {{0, 0, 0}};
            std::array<std::vector<ListingSignup>, 3> signups;
        };

        struct QueuedPlayer
        {
            ObjectGuid guid;
            ObjectGuid queueLeaderGuid;
            std::string name;
            std::string className;
            uint32 level = 0;
            uint32 team = 0;
            bool isHardcore = false;
            time_t joinTime = 0;
            uint64 queueOrder = 0;
            std::vector<std::string> instances;
            uint8 roleMask = 0;
            uint8 assignedRole = 0;
        };

        struct PendingRolecheck
        {
            ObjectGuid leaderGuid;
            std::vector<ObjectGuid> members;
            std::vector<std::string> instances;
            std::map<ObjectGuid, uint8> responses;
            uint32 timer = 0;
        };

        struct Offer
        {
            uint32 id = 0;
            std::string instance;
            std::map<ObjectGuid, uint8> roles;
            std::set<ObjectGuid> accepted;
            uint32 timer = 0;
        };

        typedef std::map<uint32, Listing> ListingsMap;
        typedef std::map<ObjectGuid, QueuedPlayer> QueueMap;
        typedef std::map<ObjectGuid, PendingRolecheck> RolecheckMap;
        typedef std::map<uint32, Offer> OffersMap;

        void HandleGetGroupsStatus(Player* player);
        void HandleGetGroupsList(Player* player);
        void HandleGetGroupDetails(Player* player, std::vector<std::string> const& fields);
        void HandleNewGroup(Player* player, std::vector<std::string> const& fields);
        void HandleUpdateGroup(Player* player, std::vector<std::string> const& fields);
        void HandleDeleteGroup(Player* player, std::vector<std::string> const& fields);
        void HandleSignup(Player* player, std::vector<std::string> const& fields);
        void HandleQueueJoin(Player* player, std::vector<std::string> const& fields);
        void HandleQueueLeave(Player* player);
        void HandleGetQueueStatus(Player* player);
        void HandleRolecheckResponse(Player* player, std::vector<std::string> const& fields);
        void HandleOfferAccept(Player* player);
        void HandleManualRolecheckResponse(Player* player, std::vector<std::string> const& fields);

        void BroadcastGroupsList();
        void SendGroupsList(Player* player) const;
        void SendGroupDetails(Player* player, Listing const& listing) const;
        void SendQueueStatus(Player* player) const;
        void SendQueuedStatus(Player* player, QueuedPlayer const& queued) const;
        void SendQueueJoined(Player* player, QueuedPlayer const& queued) const;
        void SendQueueLeft(ObjectGuid const& guid, std::string const& name);
        void Send(Player* player, std::string const& message) const;

        void StartRolecheck(Player* leader, std::vector<std::string> const& instances);
        void EnqueuePlayer(Player* player, ObjectGuid const& leaderGuid, std::vector<std::string> const& instances, uint8 roleMask);
        void EnqueueRolecheck(PendingRolecheck const& rolecheck);
        void CancelRolecheck(RolecheckMap::iterator itr);
        void CancelOffer(uint32 offerId, bool requeueAccepted, ObjectGuid const& removeGuid = ObjectGuid(), bool requeueOthers = false);
        void TryMakeOffers();
        bool TryBuildOfferForInstance(std::string const& instance);
        bool CompleteOffer(uint32 offerId);
        bool AddPlayerToGroup(Group*& group, ObjectGuid const& leaderGuid, ObjectGuid const& memberGuid);

        void EnsureListingsLoaded();
        void LoadListingsFromDB();
        void SaveListingToDB(Listing const& listing);
        void DeleteListingFromDB(uint32 id);
        Listing* GetListingForGroupMember(Player* player);
        void RemoveListingSignup(Listing& listing, std::string const& name);
        void SetListingSignupRole(Listing& listing, Player* player, uint8 roleIdx);
        void PruneListingSignups(Listing& listing);
        bool IsListingLeader(Player const* player) const;
        Listing* GetListingByCreator(ObjectGuid const& creatorGuid);
        ListingsMap::iterator EraseListing(ListingsMap::iterator itr);
        bool RemoveListingsByCreator(ObjectGuid const& creatorGuid);
        uint32 CreateListing(Player* leader, std::string const& title, std::string const& description,
                             uint8 category, std::array<uint8, 3> const& limits);
        void ApplyListingSignup(Listing& listing, std::string const& memberName, Player* member, uint8 roleIdx);
        std::string SerializeSignups(std::vector<ListingSignup> const& signups) const;
        std::vector<ListingSignup> DeserializeSignups(std::string const& value) const;
        bool CanPlayerSeeListing(Player const* player, Listing const& listing) const;
        bool CanPlayersGroup(Player const* leader, Player const* member) const;
        bool CanQueuedPlayersGroup(QueuedPlayer const& seed, QueuedPlayer const& candidate) const;

        Listing* GetListing(uint32 id);
        Player* GetPlayer(ObjectGuid const& guid) const;
        std::vector<ObjectGuid> GetPartyMembers(Player* leader) const;
        std::vector<ObjectGuid> GetQueueOrder() const;
        std::vector<std::string> GetSharedInstances(std::vector<std::string> const& requested) const;
        uint8 ParseRoleMask(std::string const& roles) const;
        std::array<uint8, 3> ParseRoleCounts(std::string const& roles) const;
        std::string BuildListingJson(Listing const& listing) const;
        std::string BuildSignupJson(ListingSignup const& signup) const;
        std::string JoinStrings(std::vector<std::string> const& values, char delimiter) const;
        std::string ClassName(Player const* player) const;
        uint8 AllowedRoleMask(Player const* player) const;
        void RecountListing(Listing& listing) const;
        void CleanupPlayer(ObjectGuid const& guid);

        // Bot fill - see LFTBotFill.cpp
        void UpdateBotFill(uint32 diff);
        void DropUnneededFillBots();
        void FillInstanceWithBots(std::string const& instance, QueuedPlayer const& waiter);
        void SeedBotOnlyQueue();

        Player* TakeFromBotOnlyGroup(uint8 wanted, QueuedPlayer const& waiter,
                                     uint32 below, uint32 above);

        Player* TakeBotAndRespecFor(uint8 wanted, QueuedPlayer const& waiter,
                                    uint32 below, uint32 above);
        void TeleportBotGroupToInstance(Offer const& offer);
        void AcceptOffersForFillBots();
        void ForgetFillBot(ObjectGuid const& guid);
        bool IsFillBot(ObjectGuid const& guid) const;
        bool RealPlayerWaitsFor(std::string const& instance, time_t& oldestJoin) const;

        ListingsMap m_listings;
        QueueMap m_queue;
        RolecheckMap m_rolechecks;
        OffersMap m_offers;
        std::map<ObjectGuid, uint32> m_playerOffers;
        uint32 m_nextListingId;
        uint32 m_nextOfferId;
        uint64 m_nextQueueOrder;
        bool m_listingsLoaded;

        // Queue entries we created ourselves to fill a real player's group.
        std::set<ObjectGuid> m_fillBots;
        uint32 m_botFillTimer;
};

extern LFTManager sLFTMgr;

#endif
