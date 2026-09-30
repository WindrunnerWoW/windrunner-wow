#pragma once
#include "playerbot/strategy/Value.h"

namespace ai
{
    class RtiValue : public ManualSetValue<std::string>
	{
	public:
        RtiValue(PlayerbotAI* ai);
        virtual std::string Save() override { return value; }
        virtual bool Load(std::string text) override { value = text; return true; }
    };

    class RtiCcValue : public ManualSetValue<std::string>
    {
    public:
        RtiCcValue(PlayerbotAI* ai);

        virtual std::string Save() override { return value; }
        virtual bool Load(std::string text) override;

        // Conventional crowd-control mark for the bot's class, so a hunter
        // does not try to trap the mage's sheep target and vice versa:
        // moon = polymorph, square = freezing trap, star = sap,
        // diamond = banish/fear, triangle = shackle/hibernate/turn undead.
        static std::string GetDefaultForClass(uint8 cls);
    };
}
