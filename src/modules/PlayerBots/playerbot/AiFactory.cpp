#include "playerbot/playerbot.h"
#include "playerbot/AiFactory.h"
#include "strategy/AiObjectContext.h"
#include "strategy/ReactionEngine.h"

#include "strategy/priest/PriestAiObjectContext.h"
#include "strategy/mage/MageAiObjectContext.h"
#include "strategy/warlock/WarlockAiObjectContext.h"
#include "strategy/warrior/WarriorAiObjectContext.h"
#include "strategy/shaman/ShamanAiObjectContext.h"
#include "strategy/paladin/PaladinAiObjectContext.h"
#include "strategy/druid/DruidAiObjectContext.h"
#include "strategy/hunter/HunterAiObjectContext.h"
#include "strategy/rogue/RogueAiObjectContext.h"
#include "strategy/deathknight/DKAiObjectContext.h"
#include "Objects/Player.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "Battlegrounds/BattleGroundMgr.h"
#include <sstream>

namespace
{
// Classic mana budgets are tight. Healers should only off-DPS outdoors.
bool AllowHealerOffdps(Player* player)
{
#ifdef MANGOSBOT_ZERO
    if (!player)
        return false;
    if (player->InBattleGround())
        return false;

    Map* map = player->GetMap();
    if (map && (map->IsDungeon() || map->IsRaid()))
        return false;
#endif
    return true;
}

// The configured default strategy lists must not pick the role: talents do,
// above. Two sibling groups made the shipped "+dps,+dps assist,..." default
// quietly rewrite roles:
//  - "dps"/"heal"/"tank" are class spec aliases (a priest's "dps" IS the
//    shadow placeholder) and spec strategies are siblings, so every
//    Holy/Discipline priest was turned into Shadow and the party lost its
//    healer;
//  - "dps assist"/"tank assist"/"dps aoe" are siblings too, so every tank bot
//    lost "tank assist" and picked targets like a DPS (no loose-mob pickup).
// Chat commands ("co +dps") still switch deliberately; only defaults are
// filtered.
std::string WithoutSpecAliases(const std::string& strategies)
{
    std::string result;
    std::stringstream ss(strategies);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        std::string name = token;
        while (!name.empty() && (name[0] == '+' || name[0] == '-' || name[0] == '~' || name[0] == ' '))
            name.erase(0, 1);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();

        if (name == "dps" || name == "heal" || name == "tank" ||
            name == "dps assist" || name == "tank assist" || name == "dps aoe")
            continue;

        if (!result.empty())
            result += ",";
        result += token;
    }

    return result;
}

// A player's companions stay with the group: whatever the configured default
// lists say, no grinding, fishing trips, crafting RPG or free wandering.
void StripCompanionRoaming(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine)
{
    if (!sPlayerbotAIConfig.windrunnerCompanionMode || !facade || !facade->HasRealPlayerMaster() || player->InBattleGround())
        return;

    for (const char* name : { "grind", "tfish", "rpg", "rpg craft", "travel", "gather", "emote" })
        nonCombatEngine->removeStrategy(name);

    if (!sPlayerbotAIConfig.useWanderAsDefaultFollowStrategy && nonCombatEngine->HasStrategy("wander"))
    {
        nonCombatEngine->removeStrategy("wander");
        nonCombatEngine->addStrategy("follow");
    }
}
}

AiObjectContext* AiFactory::createAiObjectContext(Player* player, PlayerbotAI* ai)
{
    switch (player->getClass())
    {
        case CLASS_PRIEST:
        {
            return new PriestAiObjectContext(ai);
            break;
        }

        case CLASS_MAGE:
        {
            return new MageAiObjectContext(ai);
            break;
        }

        case CLASS_WARLOCK:
        {
            return new WarlockAiObjectContext(ai);
            break;
        }

        case CLASS_WARRIOR:
        {
            return new WarriorAiObjectContext(ai);
            break;
        }

        case CLASS_SHAMAN:
        {
            return new ShamanAiObjectContext(ai);
            break;
        }

        case CLASS_PALADIN:
        {
            return new PaladinAiObjectContext(ai);
            break;
        }

        case CLASS_DRUID:
        {
            return new DruidAiObjectContext(ai);
            break;
        }

        case CLASS_HUNTER:
        {
            return new HunterAiObjectContext(ai);
            break;
        }

        case CLASS_ROGUE:
        {
            return new RogueAiObjectContext(ai);
            break;
        }

#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT:
        {
            return new DKAiObjectContext(ai);
            break;
        }
#endif
    }
    return new AiObjectContext(ai);
}

BotRoles AiFactory::GetForcedRoleBits(uint8 forcedRole)
{
    // Same reading as PlayerbotAI::ResetStrategies: 1 tank, 2 heal, anything
    // else set means dps.
    if (!forcedRole)
        return BOT_ROLE_NONE;
    if (forcedRole == BOT_ROLE_TANK)
        return BOT_ROLE_TANK;
    if (forcedRole == BOT_ROLE_HEALER)
        return BOT_ROLE_HEALER;
    return BOT_ROLE_DPS;
}

int AiFactory::GetPlayerSpecTab(const Player* bot)
{
    PlayerbotAI* ai = GetBotAI(bot);
    return GetTalentSpecTab(bot, ai ? ai->GetForcedRole() : 0);
}

int AiFactory::GetTalentSpecTab(const Player* bot, uint8 forcedRole)
{
    std::map<uint32, int32> tabs = GetPlayerSpecTabs(bot);
    BotRoles const role = GetForcedRoleBits(forcedRole);

    if (bot->GetLevel() >= 10 && ((tabs[0] + tabs[1] + tabs[2]) > 0))
    {
        int tab = -1, max = 0;
        for (uint32 i = 0; i < uint32(3); i++)
        {
            if (tab == -1 || max < tabs[i])
            {
                tab = i;
                max = tabs[i];
            }
        }

        // A tie used to go to the lowest tab, whatever the bot was there for.
        // With a role assigned, the tied tree that fills it wins instead.
        if (role != BOT_ROLE_NONE && !(GetPlayerRoles(bot->getClass(), tab) & role))
        {
            for (uint32 i = 0; i < uint32(3); i++)
            {
                if (tabs[i] == max && (GetPlayerRoles(bot->getClass(), i) & role))
                    return i;
            }
        }

        return tab;
    }
    else
    {
        int tab = 0;

        switch (bot->getClass())
        {
        case CLASS_MAGE:
            tab = 1;
            break;
        case CLASS_PALADIN:
            tab = 2;
            break;
        case CLASS_PRIEST:
            tab = 1;
            break;
        case CLASS_WARRIOR:
            tab = 2;
            break;
        case CLASS_SHAMAN:
            tab = 1;
            break;
        }

        // No talents at all is a three-way tie. The class default above sent
        // every talentless druid to Balance - wrath spam on the tank slot.
        if (role != BOT_ROLE_NONE && !(GetPlayerRoles(bot->getClass(), tab) & role))
        {
            for (uint32 i = 0; i < uint32(3); i++)
            {
                if (GetPlayerRoles(bot->getClass(), i) & role)
                    return i;
            }
        }

        return tab;
    }
}

int AiFactory::GetRoleSpecTab(const Player* player, uint8 forcedRole)
{
    int const tab = GetTalentSpecTab(player, forcedRole);
    BotRoles const role = GetForcedRoleBits(forcedRole);
    if (role == BOT_ROLE_NONE)
        return tab;

    uint8 const cls = player->getClass();
    if (GetPlayerRoles(cls, tab) & role)
        return tab;

    // Feral fills dps as well as tank: the strategy code below picks cat or
    // bear from the role, so a feral druid sent as dps stays feral.
    if (cls == CLASS_DRUID && tab == 1 && role == BOT_ROLE_DPS)
        return tab;

    // The role outranks the talents for strategy selection: a balance druid
    // put on the heal slot heals, a holy paladin on the tank slot tanks. A
    // class that cannot fill the role at all keeps its own tree.
    for (uint32 i = 0; i < uint32(3); i++)
    {
        if (GetPlayerRoles(cls, i) & role)
            return i;
    }

    return tab;
}

std::map<uint32, int32> AiFactory::GetPlayerSpecTabs(const Player* bot)
{
    std::map<uint32, int32> tabs;
    for (uint32 i = 0; i < uint32(3); i++)
        tabs[i] = 0;

    uint32 classMask = bot->getClassMask();
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const *talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo)
            continue;

        TalentTabEntry const *talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo)
            continue;

        if ((classMask & talentTabInfo->ClassMask) == 0)
            continue;

        int maxRank = 0;
        for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
        {
            if (!talentInfo->RankID[rank])
                continue;

            uint32 spellid = talentInfo->RankID[rank];
            if (spellid && bot->HasSpell(spellid))
                maxRank = rank + 1;

        }
        tabs[talentTabInfo->tabpage] += maxRank;
    }

    return tabs;
}

BotRoles AiFactory::GetPlayerRoles(uint8 cls, uint8 tab)
{
    BotRoles role = BOT_ROLE_NONE;
    switch (cls)
    {
        case CLASS_PRIEST: {
            if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }
            else
            {
                role = BOT_ROLE_HEALER;
            }

            break;
        }

        case CLASS_SHAMAN: {
            if (tab == 2)
            {
                role = BOT_ROLE_HEALER;
            }
            else
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_WARRIOR: {
            if (tab == 2)
            {
                role = BOT_ROLE_TANK;
            }
            else
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_PALADIN: {
            if (tab == 1)
            {
                role = BOT_ROLE_TANK;
            }
            else if (tab == 0)
            {
                role = BOT_ROLE_HEALER;
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_DRUID: {
            if (tab == 0)
            {
                role = BOT_ROLE_DPS;
            }
            else if (tab == 1)
            {
                // Feral counts as tank here. The dungeon finder can put a druid
                // on the tank slot, and this mapping decides which premade
                // talent build it gets - with DPS it would be handed a balance
                // build and try to tank in caster form.
                role = BOT_ROLE_TANK;
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_HEALER;
            }

            break;
        }

#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT: {
            if (tab == 0)
            {
                role = BOT_ROLE_TANK;
            }
            else if (tab == 1)
            {
                role = (BotRoles)(BOT_ROLE_TANK | BOT_ROLE_DPS);
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }
#endif
        default: {
            role = BOT_ROLE_DPS;
            break;
        }
    }

    return role;
}

BotRoles AiFactory::GetPlayerRoles(const Player* player)
{
    // This used to work out a role from what the character was doing right then
    // - defensive stance, righteous fury, bear form - and then return the talent
    // based answer anyway, throwing that work away. Restoring it would have made
    // things worse, not better: the switch only ever set the tank bit, so a fury
    // warrior who happened to stand in defensive stance would have been handed
    // the tank slot with a fury build, while a feral druid was already
    // recognised through its talents. The aura check is gone. The tree decides.
    //
    // The tree alone, too: no forced-role tie-breaking here. Role assignment asks
    // this to find out whether the talents contradict the role it is handing out.
    return GetPlayerRoles(player->getClass(), GetTalentSpecTab(player, 0));
}

void AiFactory::AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* combatEngine)
{
    uint8 const forcedRole = facade->GetForcedRole();
    int tab = GetRoleSpecTab(player, forcedRole);

    combatEngine->addStrategies("mount", NULL);
    combatEngine->addStrategy("avoid mobs");
    combatEngine->addStrategy("dungeon");

    // A battleground is precisely where "pvp" is needed: it is the strategy
    // whose "enemy player near" trigger lets the bot take an enemy player as a
    // target at all, and ChooseTargetAction asks for it by name. Excluding it
    // here left bots standing on their spawn with no way to acquire anyone, and
    // took "default" - the baseline combat behaviour - with it. Measured before
    // this change: ten bots held identical coordinates for six minutes inside a
    // running match, and no [BATTLEGROUND] honor had been recorded since 08-10.
    // "duel" is the one that genuinely has no business in a battleground.
    if (player->InBattleGround())
    {
        combatEngine->addStrategies("racials", "default", "pvp", NULL);
    }
    else
    {
        combatEngine->addStrategies("racials", "default", "duel", "pvp", NULL);
    }

    switch (player->getClass())
    {
        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("discipline");
                if (sPlayerbotAIConfig.enableOffSpecStrategies && AllowHealerOffdps(player))
                    combatEngine->addStrategy("offdps");
            }
            else if (tab == 1)
            {
                combatEngine->addStrategy("holy");
                if (sPlayerbotAIConfig.enableOffSpecStrategies && AllowHealerOffdps(player))
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                combatEngine->addStrategy("shadow");
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("dps assist", "flee", "cure", "ranged", "cc", "buff", "aoe", "boost", NULL);
            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                combatEngine->addStrategy("fire");
            }
            else
            {
                combatEngine->addStrategy("frost");
            }

            combatEngine->addStrategies("dps assist", "flee", "cure", "ranged", "cc", "buff", "aoe", "boost", NULL);
            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                combatEngine->addStrategies("protection", "tank assist", "pull", "pull back", "close", NULL);
            }
            else if (player->GetLevel() < 30 || tab == 0)
            {
                combatEngine->addStrategies("arms", "dps assist", "behind", NULL);
            }
            else
            {
                combatEngine->addStrategies("fury", "dps assist", "behind", NULL);
            }

            combatEngine->addStrategies("aoe", "cc", "buff", "boost", NULL);
            break;
        }

        case CLASS_SHAMAN:
        {
            if (tab == 0)
            {
                combatEngine->addStrategies("elemental", "aoe", "cc", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }
            else if (tab == 2)
            {
                combatEngine->addStrategies("restoration", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies && AllowHealerOffdps(player))
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                combatEngine->addStrategies("enhancement", "aoe", "cc", "close", "behind", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("dps assist", "cure", "totems", "buff", "boost", NULL);
            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                combatEngine->addStrategies("protection", "tank assist", "pull", "pull back", "close", NULL);
			}
            else if(tab == 0)
            {
                combatEngine->addStrategies("holy", "dps assist", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies && AllowHealerOffdps(player))
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                combatEngine->addStrategies("retribution", "dps assist", "close", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("cure", "aoe", "cc", "buff", "boost", "aura", "blessing", NULL);
            break;
        }

        case CLASS_DRUID:
        {
            // Below level 10, no shapeshift forms exist yet (Bear at 10, Cat at 20), so
            // neither Feral spec is usable. Dedicated leveling kit instead: burst Wrath/
            // Moonfire at range, melee once the mob closes, self-heal as needed. Deliberately
            // no "flee"/"ranged" here - see the CLASS_DRUID guard in OutNumberedTrigger.
            if (player->GetLevel() < 10)
            {
                combatEngine->addStrategies("leveling", "dps assist", "cure", NULL);
                break;
            }

            if (tab == 1)
            {
                // Bear or cat. An assigned role decides it outright: a druid the
                // dungeon finder put on the tank slot should be a bear whether or
                // not it has reached Primal Fury, and one sent as dps should stay
                // a cat even if it has.
                //
                // Primal Fury - 16958 and 16961 - remains the fallback for bots
                // nobody assigned anything to, which is every druid simply
                // wandering the world. It is only a proxy for having gone down
                // the bear side of the tree, and a poor one below the level that
                // reaches it.
                BotRoles const role = GetForcedRoleBits(forcedRole);

                bool const tanking = role ? (role & BOT_ROLE_TANK) != 0
                                          : (player->HasSpell(16961) || player->HasSpell(16958));

                if (tanking)
                {
                    combatEngine->addStrategies("tank feral", "tank assist", "pull", "pull back", "close", NULL);
                }
                else
                {
                    combatEngine->addStrategies("dps feral", "dps assist", "close", "behind", NULL);
                    if (sPlayerbotAIConfig.enableOffSpecStrategies)
                        combatEngine->addStrategy("offheal");
                }
            }
            else if (tab == 2)
            {
                combatEngine->addStrategies("restoration", "dps assist", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies && AllowHealerOffdps(player))
                    combatEngine->addStrategy("offdps");
            }
            else
            {
                combatEngine->addStrategies("balance", "dps assist", "flee", "ranged", NULL);
                if (sPlayerbotAIConfig.enableOffSpecStrategies)
                    combatEngine->addStrategy("offheal");
            }

            combatEngine->addStrategies("cure", "aoe", "cc", "buff", "boost", NULL);
            break;
        }

        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                combatEngine->addStrategy("survival");
            }
            else
            {
                combatEngine->addStrategy("marksmanship");
            }

            combatEngine->addStrategies("dps assist", "ranged", "cc", "aoe", "buff", "boost", "aspect", "sting", "pet", NULL);
            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                combatEngine->addStrategy("subtlety");
            }
            else
            {
                combatEngine->addStrategy("combat");
            }

            combatEngine->addStrategies("dps assist", "aoe", "close", "cc", "behind", "stealth", "poisons", "buff", "boost", NULL);
            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                combatEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                combatEngine->addStrategy("demonology");
            }
            else
            {
                combatEngine->addStrategy("destruction");
            }

            combatEngine->addStrategies("dps assist", "flee", "ranged", "cc", "pet", "aoe", "buff", "boost", "curse", NULL);
            break;
        }

#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT:
        {
            if (tab == 0)
            {
                combatEngine->addStrategies("blood", "tank assist", "pull", "pull back", NULL);
            }
            else if (tab == 1)
            {
                combatEngine->addStrategies("frost", "frost aoe", "dps assist", NULL);
            }
            else
            {
                combatEngine->addStrategies("unholy", "unholy aoe", "dps assist", NULL);
            }

            combatEngine->addStrategies("dksquest", "dps assist", "flee", "close", "cc", NULL);
            break;
        }
#endif
    }

    if (facade->IsRealPlayer() || sRandomPlayerbotMgr.IsFreeBot(player))
	{
        combatEngine->addStrategy("roll");

        if (!player->GetGroup())
        {
            combatEngine->addStrategy("flee");
            combatEngine->addStrategy("boost");
            
            if (player->getClass() == CLASS_DRUID && tab == 2)
            {
                combatEngine->addStrategies("balance", "ranged", NULL);
                combatEngine->removeStrategy("close");
            }

            if (player->getClass() == CLASS_DRUID && tab == 1 && !forcedRole && urand(0, 100) > 50 && player->GetLevel() >= 20)
            {
                if (player->HasSpell(16961) || player->HasSpell(16958))
                {
                    combatEngine->addStrategies("dps feral", "close", "stealth", "behind", NULL);
                    combatEngine->removeStrategy("tank feral");
                    combatEngine->removeStrategy("tank assist");
                }
                else
                {
                    combatEngine->addStrategies("tank feral", "tank assist", "close", NULL);
                    combatEngine->removeStrategy("dps feral");
                    combatEngine->removeStrategy("dps assist");
                    combatEngine->removeStrategy("behind");
                }
            }

            if (player->getClass() == CLASS_PRIEST && tab < 2 && AllowHealerOffdps(player))
            {
                combatEngine->addStrategy("offdps");
            }

            if (player->getClass() == CLASS_SHAMAN && tab == 2)
            {
                combatEngine->addStrategies("elemental", "aoe", "cc", NULL);
            }

            if (player->getClass() == CLASS_PALADIN && tab == 0)
            {
                combatEngine->addStrategies("retribution", "close", NULL);
                combatEngine->removeStrategy("ranged");
            }
        }

        if ((facade->HasRealPlayerMaster() && sPlayerbotAIConfig.jumpWithPlayer) ||
            (player->InBattleGround() && sPlayerbotAIConfig.jumpInBg) ||
            (!player->InBattleGround() && !facade->HasRealPlayerMaster()))
        {
            if (combatEngine->HasStrategy("close") && frand(0.0f, 1.0f) < sPlayerbotAIConfig.jumpMeleeInCombatChance)
            {
                combatEngine->addStrategy("rpg jump");
            }
        }

        if (sPlayerbotAIConfig.jumpChase)
            combatEngine->addStrategy("chase jump");

        // remove threat for now
        //engine->removeStrategy("threat");

        combatEngine->ChangeStrategy(WithoutSpecAliases(sPlayerbotAIConfig.randomBotCombatStrategies));
    }
    else
    {
        combatEngine->ChangeStrategy(WithoutSpecAliases(sPlayerbotAIConfig.combatStrategies));
    }

    // High-impact combat defaults. Applied after ChangeStrategy so historical
    // conf entries like `-threat` cannot silently strip them.
    AddRoleStrategies(player, facade, combatEngine);

#ifdef MANGOSBOT_ZERO
    // Conf or leftover strategies can still enable healer off-DPS; strip it in
    // instances where Classic mana cannot support it.
    if (!AllowHealerOffdps(player))
        combatEngine->removeStrategy("offdps");
#endif

    // Battleground switch
    if (player->InBattleGround())
    {
        BattleGroundTypeId bgType = player->GetBattleGroundTypeId();

#ifdef MANGOSBOT_TWO
        if (bgType == BATTLEGROUND_RB)
        {
            bgType = player->GetBattleGround()->GetTypeId(true);
        }

        if (bgType == BATTLEGROUND_IC)
        {
            combatEngine->addStrategy("isle");
        }
#endif

        if (bgType == BATTLEGROUND_WS)
        {
            combatEngine->addStrategy("warsong");
        }

        if (bgType == BATTLEGROUND_AB)
        {
            combatEngine->addStrategy("arathi");
        }

        if(bgType == BATTLEGROUND_AV)
        {
            combatEngine->addStrategy("alterac");
        }

#ifndef MANGOSBOT_ZERO
        if (bgType == BATTLEGROUND_EY)
        {
            combatEngine->addStrategy("eye");
        }
#endif

#ifndef MANGOSBOT_ZERO
        if (!player->IsBeingTeleported() && player->InArena())
        {
            combatEngine->addStrategy("arena");
        }
#endif
        combatEngine->addStrategies("boost", "racials", "default", "aoe", "dps assist", "pvp", NULL);
        combatEngine->removeStrategy("custom::say");
        combatEngine->removeStrategy("flee");
        combatEngine->removeStrategy("threat");
        combatEngine->removeStrategy("tank face");
        combatEngine->removeStrategy("follow");
        combatEngine->removeStrategy("wander");
        combatEngine->removeStrategy("conserve mana");
        combatEngine->removeStrategy("cast time");
        combatEngine->removeStrategy("avoid aoe");
        combatEngine->removeStrategy("preheal");
        combatEngine->removeStrategy("wait for attack");

        if (player->getClass() == CLASS_SHAMAN && tab == 2)
        {
            combatEngine->addStrategies("elemental", "aoe", "cc", NULL);
        }

        if (player->getClass() == CLASS_DRUID && tab == 2)
        {
            combatEngine->addStrategies("balance", NULL);
        }

        if (player->getClass() == CLASS_DRUID && tab == 1)
        {
            if (player->HasSpell(16961) || player->HasSpell(16958))
            {
                combatEngine->addStrategies("tank feral", "close", NULL);
            }
            else
            {
                combatEngine->addStrategies("behind", "dps feral", "stealth", NULL);
            }
        }
        
        if (player->getClass() == CLASS_ROGUE)
        {
            combatEngine->addStrategies("behind", "stealth", "poisons", "buff", NULL);
        }
    }

}

void AiFactory::AddRoleStrategies(Player* player, PlayerbotAI* const facade, Engine* combatEngine, Engine* nonCombatEngine, bool reapply)
{
    // Grouped bots only: solo grind should keep full aggression.
    if (player->InBattleGround() || !(player->GetGroup() || facade->HasRealPlayerMaster()))
        return;

    // Asked of the engine itself, not the facade: while the bot is being built
    // the facade has no engines yet, so every bot - healers and tanks too - used
    // to get the dps set. When ResetStrategies runs this again after applying
    // a forced role, clear whatever the talent-based pass picked first.
    if (reapply)
    {
        for (const char* name : { "heal interrupt", "conserve mana", "save mana", "preheal",
                                  "tank face", "threat", "cast time", "wait for attack" })
        {
            combatEngine->removeStrategy(name);
        }
    }

    combatEngine->addStrategy("avoid aoe");
    combatEngine->addStrategy("potions");

    if (combatEngine->ContainsStrategy(STRATEGY_TYPE_HEAL))
    {
        combatEngine->addStrategy("heal interrupt");
        combatEngine->addStrategy("conserve mana");
        if (sPlayerbotAIConfig.autoSaveMana)
            combatEngine->addStrategy("save mana");
        combatEngine->addStrategy("preheal");

        // "dps assist" is a dps-type strategy: it had grouped healers counted
        // as dps by IsDps() and chasing assist targets between heals. Kept only
        // where the healer is allowed to off-dps, which needs a target.
        if (!combatEngine->HasStrategy("offdps") || !AllowHealerOffdps(player))
        {
            combatEngine->removeStrategy("dps assist");
            if (nonCombatEngine)
                nonCombatEngine->removeStrategy("dps assist");
        }
    }
    else if (combatEngine->ContainsStrategy(STRATEGY_TYPE_TANK))
    {
        combatEngine->addStrategy("tank face");
    }
    else
    {
        combatEngine->addStrategy("threat");
        combatEngine->addStrategy("cast time");
        // Let the tank grab initial threat before DPS opens.
        combatEngine->addStrategy("wait for attack");
        if (player->GetPower(POWER_MANA) > 0 || player->GetMaxPower(POWER_MANA) > 0)
            combatEngine->addStrategy("conserve mana");
    }
}

Engine* AiFactory::createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
	Engine* engine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_COMBAT);
    AddDefaultCombatStrategies(player, facade, engine);
    return engine;
}

void AiFactory::AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine)
{
    uint8 const forcedRole = facade->GetForcedRole();
    const int tab = GetRoleSpecTab(player, forcedRole);
    switch (player->getClass())
    {
        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("discipline");
            }
            else if (tab == 1)
            {
                nonCombatEngine->addStrategy("holy");
            }
            else
            {
                nonCombatEngine->addStrategy("shadow");
            }

            nonCombatEngine->addStrategies("dps assist", "cure", "buff", "boost", NULL);
            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                nonCombatEngine->addStrategies("protection", "tank assist", NULL);
            }
            else if (tab == 0)
            {
                nonCombatEngine->addStrategies("holy", "dps assist", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("retribution", "dps assist", NULL);
            }

            nonCombatEngine->addStrategies("cure", "buff", "boost", "blessing", "aura", NULL);
            break;
        }

        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategy("survival");
            }
            else
            {
                nonCombatEngine->addStrategy("marksmanship");
            }

            nonCombatEngine->addStrategies("dps assist", "cc", "aoe", "buff", "boost", "aspect", "pet", NULL);
            break;
        }

        case CLASS_SHAMAN:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategies("elemental", "aoe", "cc", NULL);
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategies("restoration", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("enhancement", "aoe", "cc", NULL);
            }

            nonCombatEngine->addStrategies("dps assist", "cure", "totems", "buff", "boost", NULL);
            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategy("subtlety");
            }
            else
            {
                nonCombatEngine->addStrategy("combat");
            }

            nonCombatEngine->addStrategies("dps assist", "poisons", "stealth", "buff", "boost", NULL);
            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                nonCombatEngine->addStrategy("demonology");
            }
            else
            {
                nonCombatEngine->addStrategy("destruction");
            }

            nonCombatEngine->addStrategies("dps assist", "pet", "buff", "boost", NULL);
            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                nonCombatEngine->addStrategy("fire");
            }
            else
            {
                nonCombatEngine->addStrategy("frost");
            }

            nonCombatEngine->addStrategies("dps assist", "cure", "buff", "boost", NULL);
            break;
        }

        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                nonCombatEngine->addStrategies("leveling", "dps assist", "cure", NULL);
                break;
            }

            if (tab == 1)
            {
                // Same choice as the combat engine: an assigned role decides,
                // Primal Fury is only the fallback.
                BotRoles const role = GetForcedRoleBits(forcedRole);
                bool const tanking = role ? (role & BOT_ROLE_TANK) != 0
                                          : (player->HasSpell(16961) || player->HasSpell(16958));

                if (tanking)
                {
                    nonCombatEngine->addStrategies("tank feral", "tank assist", NULL);
                }
                else
                {
                    nonCombatEngine->addStrategies("dps feral", "dps assist", NULL);
                }
            }
            else if (tab == 2)
            {
                nonCombatEngine->addStrategies("restoration", "dps assist", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("balance", "dps assist", NULL);
            }

            nonCombatEngine->addStrategies("cure", "aoe", "cc", "buff", "boost", NULL);
            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                nonCombatEngine->addStrategies("protection", "tank assist", NULL);
            }
            else if(player->GetLevel() < 30 || tab == 0)
            {
                nonCombatEngine->addStrategies("arms", "dps assist", NULL);
            }
            else
            {
                nonCombatEngine->addStrategies("fury", "dps assist", NULL);
            }

            nonCombatEngine->addStrategies("buff", "boost", NULL);
            break;
        }

#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT:
        {
            if (tab == 0)
            {
                nonCombatEngine->addStrategy("tank assist");
            }
            else
            {
                nonCombatEngine->addStrategy("dps assist");
            }

            nonCombatEngine->addStrategy("dksquest");

            break;
        }
#endif

        default:
        {
            nonCombatEngine->addStrategy("dps assist");
            break;
        }
    }

    nonCombatEngine->addStrategies("wbuff", NULL);
    nonCombatEngine->addStrategy("avoid mobs");
    nonCombatEngine->addStrategy("dungeon");

    if (!player->InBattleGround())
        nonCombatEngine->addStrategy("force rebuff");

    if (sPlayerbotAIConfig.autoSaveMana && PlayerbotAI::IsHeal(player))
        nonCombatEngine->addStrategy("save mana");

    // Grouped healers: no "dps assist" (see AddRoleStrategies). Out of combat
    // it is what picks the next target after a kill, so solo healers and those
    // allowed to off-dps keep it.
    if ((GetPlayerRoles(player->getClass(), tab) & BOT_ROLE_HEALER) && player->GetLevel() >= 10 &&
        !player->InBattleGround() && (player->GetGroup() || facade->HasRealPlayerMaster()) &&
        (!sPlayerbotAIConfig.enableOffSpecStrategies || !AllowHealerOffdps(player)))
    {
        nonCombatEngine->removeStrategy("dps assist");
    }

    if(sPlayerbotAIConfig.llmEnabled == 2)
        nonCombatEngine->addStrategy("ai chat");

    if (!player->InBattleGround())
    {
        PlayerbotAI* ai = GetBotAI(player);
        Player* master = ai ? ai->GetMaster() : nullptr;

        if (master && !GetBotAI(master))
        {
            // Companions of a real player are there for the group: no
            // "gather" (runs off to herb/ore nodes mid-follow) and no "emote"
            // (random emotes and "suggest what to do" chatter).
            const char* wanderFollow = sPlayerbotAIConfig.useWanderAsDefaultFollowStrategy ? "wander" : "follow";
            nonCombatEngine->addStrategies("racials", "nc", "food", wanderFollow, "default", "quest", "loot", "duel", "buff", "mount", NULL);
        }
        else
        {
            const char* wanderFollow = sPlayerbotAIConfig.useWanderAsDefaultFollowStrategy ? "wander" : "follow";
            nonCombatEngine->addStrategies("racials", "nc", "food", wanderFollow, "default", "quest", "loot", "gather", "duel", "emote", "buff", "mount", NULL);
        }
    }

    if ((facade->HasRealPlayerMaster() && sPlayerbotAIConfig.jumpWithPlayer) ||
        (player->InBattleGround() && sPlayerbotAIConfig.jumpInBg) ||
        (!player->InBattleGround() && !facade->HasRealPlayerMaster()))
    {
        if (frand(0.0f, 1.0f) < sPlayerbotAIConfig.jumpNoCombatChance)
        {
            nonCombatEngine->addStrategy("rpg jump");
        }
    }

    if (!player->InBattleGround() && sPlayerbotAIConfig.jumpFollow)
        nonCombatEngine->addStrategy("follow jump");

    if (!sPlayerbotAIConfig.windrunnerCompanionMode &&
        (facade->IsRealPlayer() || sRandomPlayerbotMgr.IsFreeBot(player)) && !player->InBattleGround())
    {   
        Player* master = facade->GetMaster();

        nonCombatEngine->addStrategy("roll");
#ifdef MANGOSBOT_TWO
        nonCombatEngine->addStrategy("glyph");
#endif

        // let 25% of free bots start duels.
        if (!urand(0, 3))
        {
            nonCombatEngine->addStrategy("start duel");
        }

        if (!sPlayerbotAIConfig.windrunnerCompanionMode && sPlayerbotAIConfig.randomBotJoinLfg)
        {
            nonCombatEngine->addStrategy("lfg");
        }

        if (!player->GetGroup() || facade->IsGroupLeader())
        {
            // let 25% of random not grouped (or group leader) bots help other players
            if (!urand(0, 3))
            {
                nonCombatEngine->addStrategy("attack tagged");
            }

            nonCombatEngine->addStrategy("collision");
            nonCombatEngine->addStrategy("grind");            
            nonCombatEngine->addStrategy("group");
            nonCombatEngine->addStrategy("guild");

            if (sPlayerbotAIConfig.autoDoQuests)
            {
                nonCombatEngine->addStrategy("travel");
                nonCombatEngine->addStrategy("tfish");
                nonCombatEngine->addStrategy("rpg");                
            }

            if (!sPlayerbotAIConfig.windrunnerCompanionMode && sPlayerbotAIConfig.randomBotJoinBG)
            {
                nonCombatEngine->addStrategy("bg");
            }

            if(!master || GetBotAI(master))
            {
                nonCombatEngine->addStrategy("maintenance");
            }

            nonCombatEngine->ChangeStrategy(WithoutSpecAliases(sPlayerbotAIConfig.randomBotNonCombatStrategies));
        }
        else 
        {
            if (facade)
            {
                if (master)
                {
                    if (GetBotAI(master) || sRandomPlayerbotMgr.IsFreeBot(player))
                    {
                        nonCombatEngine->addStrategy("collision");
                        nonCombatEngine->addStrategy("grind");
                        nonCombatEngine->addStrategy("group");
                        nonCombatEngine->addStrategy("guild");

                        if (sPlayerbotAIConfig.autoDoQuests)
                        {
                            nonCombatEngine->addStrategy("travel");
                            nonCombatEngine->addStrategy("tfish");
                            nonCombatEngine->addStrategy("rpg");
                        }

                        if (!master || GetBotAI(master))
                        {
                            nonCombatEngine->addStrategy("maintenance");
                        }

                        nonCombatEngine->ChangeStrategy(WithoutSpecAliases(sPlayerbotAIConfig.randomBotNonCombatStrategies));
                    }
                    else
                    {
                        nonCombatEngine->ChangeStrategy(WithoutSpecAliases(sPlayerbotAIConfig.nonCombatStrategies));
                    }
                }
            }
        }
    }
    else
    {
        nonCombatEngine->ChangeStrategy(WithoutSpecAliases(sPlayerbotAIConfig.nonCombatStrategies));
    }

    StripCompanionRoaming(player, facade, nonCombatEngine);

    // Battleground switch
    if (player->InBattleGround())
    {
        nonCombatEngine->addStrategies("racials", "nc", "default", "buff", "food", "mount", "collision", "dps assist", "attack tagged", "emote", NULL);
        nonCombatEngine->removeStrategy("custom::say");
        nonCombatEngine->removeStrategy("travel");
        nonCombatEngine->removeStrategy("tfish");
        nonCombatEngine->removeStrategy("rpg");
        nonCombatEngine->removeStrategy("follow");
        nonCombatEngine->removeStrategy("wander");

        nonCombatEngine->removeStrategy("grind");

        BattleGroundTypeId bgType = player->GetBattleGroundTypeId();
#ifdef MANGOSBOT_TWO
        if (bgType == BATTLEGROUND_RB)
        {
            bgType = player->GetBattleGround()->GetTypeId(true);
        }
#endif

        bool isArena = false;

#ifndef MANGOSBOT_ZERO
        if (!player->IsBeingTeleported() && player->InArena())
        {
            isArena = true;
        }
#endif
        if (isArena)
        {
            nonCombatEngine->addStrategy("arena");
            nonCombatEngine->removeStrategy("mount");
        }
        else
        {
#ifndef MANGOSBOT_ZERO
#ifdef MANGOSBOT_TWO
            if (bgType <= BATTLEGROUND_EY || bgType == BATTLEGROUND_IC) // do not add for not supported bg
            {
                nonCombatEngine->addStrategy("battleground");
            }
#else
            if (bgType <= BATTLEGROUND_EY) // do not add for not supported bg
            {
                nonCombatEngine->addStrategy("battleground");
            }
#endif
#else
            if (bgType <= BATTLEGROUND_AB) // do not add for not supported bg
            {
                nonCombatEngine->addStrategy("battleground");
            }
#endif

            if (bgType == BATTLEGROUND_WS)
            {
                nonCombatEngine->addStrategy("warsong");
            }

            if (bgType == BATTLEGROUND_AV)
            {
                nonCombatEngine->addStrategy("alterac");
            }

            if (bgType == BATTLEGROUND_AB)
            {
                nonCombatEngine->addStrategy("arathi");
            }

#ifndef MANGOSBOT_ZERO
            if (bgType == BATTLEGROUND_EY)
            {
                nonCombatEngine->addStrategy("eye");
            }
#endif
#ifdef MANGOSBOT_TWO
            if (bgType == BATTLEGROUND_IC)
            {
                nonCombatEngine->addStrategy("isle");
            }
#endif
        }
    }

}

Engine* AiFactory::createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext) 
{
	Engine* nonCombatEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_NON_COMBAT);
    AddDefaultNonCombatStrategies(player, facade, nonCombatEngine);
	return nonCombatEngine;
}

void AiFactory::AddDefaultDeadStrategies(Player* player, PlayerbotAI* const facade, Engine* deadEngine)
{
    deadEngine->addStrategies("dead", "stay", "default", "follow", "group", NULL);
    if (sRandomPlayerbotMgr.IsFreeBot(player) && !player->GetGroup())
    {
        deadEngine->removeStrategy("follow");
    }

    const int tab = GetPlayerSpecTab(player);
    switch (player->getClass())
    {
        case CLASS_SHAMAN:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("elemental");
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("restoration");
            }
            else
            {
                deadEngine->addStrategy("enhancement");
            }

            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("subtlety");
            }
            else
            {
                deadEngine->addStrategy("combat");
            }

            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                deadEngine->addStrategy("demonology");
            }
            else
            {
                deadEngine->addStrategy("destruction");
            }

            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                deadEngine->addStrategy("fire");
            }
            else
            {
                deadEngine->addStrategy("frost");
            }

            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                deadEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                deadEngine->addStrategy("holy");
            }
            else
            {
                deadEngine->addStrategy("retribution");
            }

            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                deadEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                deadEngine->addStrategy("arms");
            }
            else
            {
                deadEngine->addStrategy("fury");
            }

            break;
        }

        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                deadEngine->addStrategy("leveling");
                break;
            }

            if (tab == 1)
            {
                if (player->HasSpell(16961) || player->HasSpell(16958))
                {
                    deadEngine->addStrategy("tank feral");
                }
                else
                {
                    deadEngine->addStrategy("dps feral");
                }
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("restoration");
            }
            else
            {
                deadEngine->addStrategy("balance");
            }

            break;
        }
        
        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                deadEngine->addStrategy("survival");
            }
            else
            {
                deadEngine->addStrategy("marksmanship");
            }

            break;
        }

        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                deadEngine->addStrategy("discipline");
            }
            else if (tab == 1)
            {
                deadEngine->addStrategy("holy");
            }
            else
            {
                deadEngine->addStrategy("shadow");
            }

            break;
        }
    }

    if (facade->IsRealPlayer() || sRandomPlayerbotMgr.IsFreeBot(player))
    {
        deadEngine->ChangeStrategy(sPlayerbotAIConfig.randomBotDeadStrategies);
    }
    else
    {
        deadEngine->ChangeStrategy(sPlayerbotAIConfig.deadStrategies);
    }
}

Engine* AiFactory::createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* deadEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_DEAD);
    AddDefaultDeadStrategies(player, facade, deadEngine);
    return deadEngine;
}

void AiFactory::AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine)
{
    reactionEngine->addStrategies("react", "chat", "avoid aoe", "potions", NULL);

    const int tab = GetPlayerSpecTab(player);
    switch (player->getClass())
    {
        case CLASS_SHAMAN:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("elemental");
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("restoration");
            }
            else
            {
                reactionEngine->addStrategy("enhancement");
            }

            break;
        }

        case CLASS_ROGUE:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("assassination");
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("subtlety");
            }
            else
            {
                reactionEngine->addStrategy("combat");
            }

            break;
        }

        case CLASS_WARLOCK:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("affliction");
            }
            else if (tab == 1)
            {
                reactionEngine->addStrategy("demonology");
            }
            else
            {
                reactionEngine->addStrategy("destruction");
            }

            break;
        }

        case CLASS_MAGE:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("arcane");
            }
            else if (tab == 1)
            {
                reactionEngine->addStrategy("fire");
            }
            else
            {
                reactionEngine->addStrategy("frost");
            }

            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                reactionEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                reactionEngine->addStrategy("holy");
            }
            else
            {
                reactionEngine->addStrategy("retribution");
            }

            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2)
            {
                reactionEngine->addStrategy("protection");
            }
            else if (tab == 0)
            {
                reactionEngine->addStrategy("arms");
            }
            else
            {
                reactionEngine->addStrategy("fury");
            }

            break;
        }

        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                reactionEngine->addStrategy("leveling");
                break;
            }

            if (tab == 1)
            {
                if (player->HasSpell(16961) || player->HasSpell(16958))
                {
                    reactionEngine->addStrategy("tank feral");
                }
                else
                {
                    reactionEngine->addStrategy("dps feral");
                }
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("restoration");
            }
            else
            {
                reactionEngine->addStrategy("balance");
            }

            break;
        }

        case CLASS_HUNTER:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("beast mastery");
            }
            else if (tab == 2)
            {
                reactionEngine->addStrategy("survival");
            }
            else
            {
                reactionEngine->addStrategy("marksmanship");
            }

            break;
        }

        case CLASS_PRIEST:
        {
            if (tab == 0)
            {
                reactionEngine->addStrategy("discipline");
            }
            else if (tab == 1)
            {
                reactionEngine->addStrategy("holy");
            }
            else
            {
                reactionEngine->addStrategy("shadow");
            }

            break;
        }
    }

    if (facade->IsRealPlayer() || sRandomPlayerbotMgr.IsFreeBot(player))
    {
        reactionEngine->ChangeStrategy(sPlayerbotAIConfig.randomBotReactStrategies);
    }
    else
    {
        reactionEngine->ChangeStrategy(sPlayerbotAIConfig.reactStrategies);
    }
}

ReactionEngine* AiFactory::createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    ReactionEngine* reactionEngine = new ReactionEngine(facade, AiObjectContext, BotState::BOT_STATE_REACTION);
    AddDefaultReactionStrategies(player, facade, reactionEngine);
    return reactionEngine;
}
