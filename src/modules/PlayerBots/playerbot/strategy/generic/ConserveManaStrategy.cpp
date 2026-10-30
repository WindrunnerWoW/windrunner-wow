
#include "playerbot/playerbot.h"
#include "ConserveManaStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/values/LastSpellCastValue.h"
#include "playerbot/ServerFacade.h"
#include "Group/Group.h"

using namespace ai;

namespace
{
    // Instanced groups: a healer's mana belongs to the party, so damage spells
    // only get what is left above these marks.
    const uint8 kDungeonHealerDpsMana = 70;
    const uint8 kDungeonHealerDpsGroupHealth = 80;
    const uint8 kDungeonOffhealerDpsMana = 60;

    bool IsShapeshiftSpell(const std::string& spell)
    {
        return spell == "bear form" || spell == "dire bear form" || spell == "cat form" ||
               spell == "travel form" || spell == "aquatic form" || spell == "moonkin form";
    }

    bool IsGroupMemberBelow(Player* bot, uint8 healthPct)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || !member->IsInWorld() || member->GetMapId() != bot->GetMapId() || !member->IsAlive())
                continue;

            if (member->GetHealthPercent() < healthPct)
                return true;
        }

        return false;
    }

    bool HasOtherHealer(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member && member != bot && member->IsAlive() && PlayerbotAI::IsHeal(member))
                return true;
        }

        return false;
    }
}

float ConserveManaMultiplier::GetValue(Action* action)
{
    if (action == NULL) return 1.0f;

    uint8 health = AI_VALUE2(uint8, "health", "self target");
    uint8 mana = AI_VALUE2(uint8, "mana", "self target");
    bool hasMana = AI_VALUE2(bool, "has mana", "self target");
    bool mediumMana = hasMana && mana < sPlayerbotAIConfig.mediumMana;
    bool lowMana = hasMana && mana < sPlayerbotAIConfig.lowMana;

    if (health < sPlayerbotAIConfig.lowHealth)
        return 1.0f;

    // Never throttle heals or emergency utility through mana conservation.
    // (HealerAutoSaveManaMultiplier handles inefficient heals separately.)
    if (dynamic_cast<CastHealingSpellAction*>(action))
        return 1.0f;

    CastSpellAction* spellAction = dynamic_cast<CastSpellAction*>(action);
    if (!spellAction)
        return 1.0f;

    std::string spell = spellAction->getName();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!spellInfo || spellInfo->powerType != POWER_MANA)
        return 1.0f;

    // Free spells (wand Shoot) cost nothing to conserve.
    if (spell == "shoot" || (!spellInfo->manaCost && !spellInfo->ManaCostPercentage))
        return 1.0f;

    // Forms are buffs too, but a druid that cannot shift back into bear or cat
    // ends up meleeing in caster form - shifting is never worth saving for.
    if (IsShapeshiftSpell(spell))
        return 1.0f;

    // Skip combat buffs once mana is no longer comfortable.
    if (mediumMana && dynamic_cast<CastBuffSpellAction*>(action))
        return 0.0f;

    bool const isDamage = action->getThreatType() != ActionThreatType::ACTION_THREAT_NONE;
    bool const isHealer = ai->ContainsStrategy(STRATEGY_TYPE_HEAL);

    // Grouped in a dungeon, damage from the healer is a luxury: a wipe two
    // pulls later costs more than the mob dying a few seconds sooner.
    Map* map = bot->GetMap();
    if (isDamage && bot->GetGroup() && map && map->IsDungeon())
    {
        if (isHealer)
        {
            if (hasMana && mana < kDungeonHealerDpsMana)
                return 0.0f;

            if (IsGroupMemberBelow(bot, kDungeonHealerDpsGroupHealth))
                return 0.0f;
        }
        else if (ai->HasStrategy("offheal", BotState::BOT_STATE_COMBAT) && !HasOtherHealer(bot))
        {
            // Off-healer (balance druid, shadow priest...) acting as the
            // group's only healer: keep enough mana for the heals.
            if (hasMana && mana < kDungeonOffhealerDpsMana)
                return 0.0f;
        }
    }

    // Healers: drop off-DPS when mana is low, or when someone actually needs a heal.
    if (isHealer && isDamage)
    {
        if (lowMana)
            return 0.0f;

        Unit* toHeal = AI_VALUE(Unit*, "party member to heal");
        if (toHeal && toHeal->GetHealthPercent() < sPlayerbotAIConfig.mediumHealth)
            return 0.0f;
    }

    Unit* target = AI_VALUE(Unit*, "current target");
    if (action->GetTarget() != target)
        return 1.0f;

    if (target)
    {
        if (((int)target->GetLevel() - (int)bot->GetLevel()) >= 0)
            return 1.0f;
    }

    return 1.0f;
}

float SaveManaMultiplier::GetValue(Action* action)
{
    if (action == NULL)
        return 1.0f;

    if (dynamic_cast<CastHealingSpellAction*>(action))
        return 1.0f;

    if (action->GetTarget() != AI_VALUE(Unit*, "current target"))
        return 1.0f;

    double saveLevel = AI_VALUE(double, "mana save level");
    if (saveLevel <= 1.0)
        return 1.0f;

    CastSpellAction* spellAction = dynamic_cast<CastSpellAction*>(action);
    if (!spellAction)
        return 1.0f;

    std::string spell = spellAction->getName();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!spellInfo || spellInfo->powerType != POWER_MANA)
        return 1.0f;

    int32 cost = spellInfo->manaCost;
    if (!cost)
        return 1.0f;

    time_t lastCastTime = AI_VALUE2(time_t, "last spell cast time", spell);
    if (!lastCastTime)
        return 1.0f;

    time_t elapsed = time(0) - lastCastTime;
    if ((double)elapsed < 10 * saveLevel)
        return 0.0f;

    return 1.0f;
}

float HealerAutoSaveManaMultiplier::GetValue(Action* action)
{
    if (!action || !ai->ContainsStrategy(STRATEGY_TYPE_HEAL))
        return 1.0f;

    if (!AI_VALUE2(bool, "has mana", "self target"))
        return 1.0f;

    // Out of combat there is time to drink afterwards: topping people up is
    // never worth refusing.
    if (!bot->IsInCombat())
        return 1.0f;

    uint8 mana = AI_VALUE2(uint8, "mana", "self target");
    if (mana > sPlayerbotAIConfig.saveManaThreshold)
        return 1.0f;

    CastHealingSpellAction* healingAction = dynamic_cast<CastHealingSpellAction*>(action);
    if (!healingAction)
        return 1.0f;

    Unit* target = healingAction->GetActionTarget();
    if (!target)
        return 1.0f;

    bool isTank = target->IsPlayer() && ai->IsTank((Player*)target);
    uint8 health = target->GetHealthPercent();
    HealingManaEfficiency manaEfficiency = healingAction->manaEfficiency;
    uint8 estAmount = healingAction->estAmount;
    uint8 lossAmount = health >= 100 ? 0 : (100 - health);

    // Below low health anything goes: the fast, inefficient heals exist for
    // exactly this. The non-tank branch used to refuse them at any health,
    // so a dying DPS waited for a slow heal or got nothing.
    if (health < sPlayerbotAIConfig.lowHealth)
        return 1.0f;

    // Tanks have larger health pools; treat estimated heal as smaller relative hit.
    if (isTank)
        estAmount = uint8(float(estAmount) / 1.5f);

    // Medium to full: only efficient heals that will not mostly overheal.
    if (health >= sPlayerbotAIConfig.mediumHealth &&
        (lossAmount < estAmount || manaEfficiency <= HealingManaEfficiency::MEDIUM))
        return 0.0f;

    // Low to medium: drop only the least efficient heals and clear overheals.
    if (lossAmount < estAmount || manaEfficiency <= HealingManaEfficiency::LOW)
        return 0.0f;

    return 1.0f;
}

void ConserveManaStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ConserveManaMultiplier(ai));
    multipliers.push_back(new SaveManaMultiplier(ai));
}

void HealerAutoSaveManaStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new HealerAutoSaveManaMultiplier(ai));
}

void HealerAutoSaveManaStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new HealerAutoSaveManaMultiplier(ai));
}
