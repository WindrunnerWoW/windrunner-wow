
#include "playerbot/playerbot.h"
#include "UseFoodStrategy.h"

using namespace ai;

void UseFoodStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    // "low health" only covers 20-50%: a bot left below 20% after a fight
    // used to never eat at all, and one at 55% walked into the next pull.
    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("food", 6.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("food", 6.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "medium health",
        NextAction::array(0, new NextAction("food", 5.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "high mana",
        NextAction::array(0, new NextAction("drink", 6.0f), NULL)));
}
