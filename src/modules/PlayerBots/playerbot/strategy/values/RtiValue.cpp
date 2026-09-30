
#include "playerbot/playerbot.h"
#include "RtiValue.h"

using namespace ai;

RtiValue::RtiValue(PlayerbotAI* ai)
    : ManualSetValue<std::string>(ai, "skull", "rti")
{
}

RtiCcValue::RtiCcValue(PlayerbotAI* ai)
    : ManualSetValue<std::string>(ai, GetDefaultForClass(ai->GetBot()->getClass()), "rti cc")
{
}

std::string RtiCcValue::GetDefaultForClass(uint8 cls)
{
    switch (cls)
    {
        case CLASS_HUNTER:  return "square";
        case CLASS_ROGUE:   return "star";
        case CLASS_WARLOCK: return "diamond";
        case CLASS_PRIEST:
        case CLASS_DRUID:
        case CLASS_PALADIN: return "triangle";
        default:            return "moon";
    }
}

bool RtiCcValue::Load(std::string text)
{
    // "moon" was every class's old default and is what older saves contain.
    // Treat it as "use the class default" so existing bots pick up their mark.
    if (text == "moon")
        text = GetDefaultForClass(bot->getClass());

    value = text;
    return true;
}
