#pragma once

#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/GroupCcTargetReservation.h"
#include "Group/Group.h"

// Pull-safety helpers for grouped bots on dungeon/raid maps.  Kept local to the
// strategy layer (header-only) so actions, triggers and values can share them
// without touching PlayerbotAI.  Note PlayerbotAI::IsInRaid() is true on any
// instance map, so "cc <spec> raid" strategies also run in 5-mans.
namespace ai
{
    namespace InstanceSafety
    {
        // Margin added to an effect radius when looking for idle mobs that a
        // fleeing target or an AoE could drag into the fight.
        constexpr float IDLE_PACK_MARGIN = 5.0f;
        constexpr float TANK_RESCUE_RANGE = 30.0f;
        constexpr float FEAR_IDLE_PACK_RANGE = 40.0f;

        inline bool IsGroupedOnInstanceMap(Player* bot)
        {
            if (!bot || !bot->GetGroup())
                return false;

            const Map* map = bot->FindMap();
            return map && (map->IsDungeon() || map->IsRaid());
        }

        inline bool IsActive(Player* bot)
        {
            return sPlayerbotAIConfig.instanceSafeCc && IsGroupedOnInstanceMap(bot);
        }

        inline bool AppliesAura(const SpellEntry* spell, AuraType aura)
        {
            if (!spell)
                return false;

            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if ((spell->Effect[i] == SPELL_EFFECT_APPLY_AURA || spell->Effect[i] == SPELL_EFFECT_APPLY_AREA_AURA_PARTY) &&
                    spell->EffectApplyAuraName[i] == aura)
                    return true;
            }
            return false;
        }

        // Death Coil's horror, Psychic Scream, Howl of Terror, Intimidating
        // Shout, Scare Beast and Turn Undead all use SPELL_AURA_MOD_FEAR in 1.12.
        inline bool IsFearSpell(const SpellEntry* spell)
        {
            return AppliesAura(spell, SPELL_AURA_MOD_FEAR);
        }

        // Nature's Grasp only carries a proc trigger, so match it by name.
        inline bool IsRootSpell(const SpellEntry* spell, const std::string& spellName)
        {
            return AppliesAura(spell, SPELL_AURA_MOD_ROOT) || spellName == "nature's grasp";
        }

        inline float GetEffectRadius(const SpellEntry* spell)
        {
            float radius = 0.0f;
            if (!spell)
                return radius;

            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                radius = std::max(radius, Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i])));
            return radius;
        }

        inline bool IsVictimTank(PlayerbotAI* ai, Unit* unit)
        {
            Player* victim = unit ? dynamic_cast<Player*>(unit->GetVictim()) : nullptr;
            return victim && ai->IsTank(victim);
        }

        inline bool HasAliveTankNear(Player* bot, float range)
        {
            Group* group = bot->GetGroup();
            if (!group)
                return false;

            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                    continue;

                if (PlayerbotAI::IsTank(member) && sServerFacade.GetDistance2d(bot, member) <= range)
                    return true;
            }
            return false;
        }

        // Any attackable, not-yet-engaged hostile near (x, y).  Uses the bot's
        // cached "possible targets no los" list, so it costs one list walk.
        inline bool HasIdleHostileNear(PlayerbotAI* ai, float x, float y, float range)
        {
            Player* bot = ai->GetBot();
            std::list<ObjectGuid> possible = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible targets no los")->Get();
            for (const ObjectGuid& guid : possible)
            {
                Unit* unit = ai->GetUnit(guid);
                if (!unit || !unit->IsAlive() || unit->IsInCombat() || unit->IsPlayer())
                    continue;

                if (Creature* creature = unit->ToCreature())
                {
                    if (creature->IsCritter() || creature->IsTotem())
                        continue;
                }

                if (sServerFacade.IsFriendlyTo(bot, unit))
                    continue;

                if (sServerFacade.GetDistance2d(unit, x, y) <= range)
                    return true;
            }
            return false;
        }

        // Fears are forbidden in instances unless the bot is about to die with
        // nobody to save it and the target cannot run into another pack.
        inline bool IsFearAllowed(PlayerbotAI* ai, Unit* target)
        {
            Player* bot = ai->GetBot();
            if (!target || target == bot)
                return false;

            if (bot->GetHealthPercent() >= sPlayerbotAIConfig.criticalHealth)
                return false;

            if (target->GetVictim() != bot)
                return false;

            if (HasAliveTankNear(bot, TANK_RESCUE_RANGE))
                return false;

            return !HasIdleHostileNear(ai, target->GetPositionX(), target->GetPositionY(), FEAR_IDLE_PACK_RANGE);
        }

        // Roots only on this bot's own CC assignment, or on a mob that is
        // beating on a non-tank bot.  Never on whatever the tank is holding.
        inline bool IsRootAllowedOn(PlayerbotAI* ai, Unit* unit)
        {
            Player* bot = ai->GetBot();
            if (!unit || unit == bot)
                return false;

            if (IsVictimTank(ai, unit))
                return false;

            AiObjectContext* context = ai->GetAiObjectContext();
            if (unit == AI_VALUE(Unit*, "rti cc target") ||
                GroupCcTargetReservation::IsOwnedBy(bot, unit->GetObjectGuid()))
                return true;

            return unit->GetVictim() == bot && !ai->IsTank(bot);
        }

        inline bool IsRootAllowed(PlayerbotAI* ai, const SpellEntry* spell, Unit* target)
        {
            Player* bot = ai->GetBot();

            // Self-centred roots (Frost Nova, Nature's Grasp procs) hit every
            // nearby mob, so each attacker in reach must be a legal root target.
            float radius = GetEffectRadius(spell);
            if (radius > 0.0f && (target == bot || bot->IsWithinDist(target, radius)))
            {
                AiObjectContext* context = ai->GetAiObjectContext();
                std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
                for (const ObjectGuid& guid : attackers)
                {
                    Unit* unit = ai->GetUnit(guid);
                    if (unit && unit->IsAlive() && bot->IsWithinDist(unit, radius) && IsVictimTank(ai, unit))
                        return false;
                }
            }

            if (target == bot)
                return !ai->IsTank(bot) && !bot->getAttackers().empty();

            return IsRootAllowedOn(ai, target);
        }

        // Central gate used by CastSpellAction::isUseful.
        inline bool IsCcSafe(PlayerbotAI* ai, uint32 spellId, const std::string& spellName, Unit* target)
        {
            if (!spellId || !IsActive(ai->GetBot()))
                return true;

            const SpellEntry* spell = sServerFacade.LookupSpellInfo(spellId);
            if (!spell)
                return true;

            if (IsFearSpell(spell))
                return IsFearAllowed(ai, target);

            if (IsRootSpell(spell, spellName))
                return IsRootAllowed(ai, spell, target);

            return true;
        }
    }
}
