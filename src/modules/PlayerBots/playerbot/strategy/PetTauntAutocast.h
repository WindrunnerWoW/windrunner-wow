#pragma once

#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"

// Pet taunts (hunter Growl, Voidwalker Torment/Suffering) on autocast steal
// aggro from the group's tank.  They stay on while the owner is solo (the pet
// is the tank) and are switched off while the owner is in a group.
namespace ai
{
    namespace PetTauntAutocast
    {
        inline bool IsPetTauntSpell(uint32 spellId)
        {
            const SpellEntry* spell = sServerFacade.LookupSpellInfo(spellId);
            if (!spell || spell->SpellName[0].empty())
                return false;

            // Matches every rank; names are the enUS DBC strings.
            const std::string name = spell->SpellName[0];
            return name == "Growl" || name == "Torment" || name == "Suffering";
        }

        inline bool IsAutocastOn(Pet* pet, uint32 spellId)
        {
            for (AutoSpellList::const_iterator i = pet->m_autospells.begin(); i != pet->m_autospells.end(); ++i)
            {
                if (*i == spellId)
                    return true;
            }
            return false;
        }

        inline bool WantAutocast(Player* bot)
        {
            return bot->GetGroup() == nullptr;
        }

        // Returns true if any taunt does not match the desired state.  Pass
        // apply=true to also fix it.  Walks only the pet's own spell map.
        inline bool Sync(Player* bot, bool apply)
        {
            Pet* pet = bot ? bot->GetPet() : nullptr;
            if (!pet || !pet->IsAlive())
                return false;

            const bool want = WantAutocast(bot);
            bool mismatch = false;
            for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
            {
                if (itr->second.state == PETSPELL_REMOVED || IsPassiveSpell(itr->first))
                    continue;

                if (!IsPetTauntSpell(itr->first) || IsAutocastOn(pet, itr->first) == want)
                    continue;

                mismatch = true;
                if (!apply)
                    break;

                pet->ToggleAutocast(itr->first, want);
            }
            return mismatch;
        }
    }
}
