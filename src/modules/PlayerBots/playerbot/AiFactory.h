#pragma once

#include "PlayerbotAI.h"

class Player;

namespace ai
{
    class AiObjectContext;
    class ReactionEngine;
    class Engine;
}

using namespace ai;

class AiFactory
{
public:
    static AiObjectContext* createAiObjectContext(Player* player, PlayerbotAI* ai);
	static Engine* createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
	static Engine* createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static Engine* createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static ReactionEngine* createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static void AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine);
    static void AddDefaultDeadStrategies(Player* player, PlayerbotAI* const facade, Engine* deadEngine);
    static void AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* engine);
    static void AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine);
    // Role-dependent combat extras (heal interrupt/conserve mana/preheal for
    // healers, tank face for tanks, threat/wait for attack for dps). Decided
    // from the combat engine's own strategies, so it works while the facade's
    // engines are still being built, and again after a forced role is applied.
    static void AddRoleStrategies(Player* player, PlayerbotAI* const facade, Engine* combatEngine,
                                  Engine* nonCombatEngine = nullptr, bool reapply = false);

public:
    // Talent tab; ties (and no talents at all) go to the bot's forced role.
    static int GetPlayerSpecTab(const Player* player);
    // Talent tab with ties resolved for the given forced role (0 = none).
    static int GetTalentSpecTab(const Player* player, uint8 forcedRole);
    // Tab used to pick strategies: the forced role wins over the talents for
    // classes that can fill it.
    static int GetRoleSpecTab(const Player* player, uint8 forcedRole);
    // Forced role value (1 tank, 2 heal, other non-zero dps) as role bits.
    static BotRoles GetForcedRoleBits(uint8 forcedRole);
    static std::map<uint32, int32> GetPlayerSpecTabs(const Player* player);
    static BotRoles GetPlayerRoles(uint8 cls, uint8 specTab);
    static BotRoles GetPlayerRoles(const Player* player);
};
