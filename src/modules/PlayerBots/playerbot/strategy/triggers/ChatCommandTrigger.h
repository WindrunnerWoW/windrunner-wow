#pragma once

#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class ChatCommandTrigger : public Trigger {
    public:
        ChatCommandTrigger(PlayerbotAI* ai, std::string command) : Trigger(ai, command) {}

        virtual void ExternalEvent(std::string param, Player* owner = NULL) override
        {
            this->param = param;
            this->owner = owner;
            triggered = true;
        }

        virtual Event Check() override
        {
            if (!triggered)
                return Event();

            Event event(getName(), param, owner);
            event.SetOwnerCommand(true);
            return event;
        }

        virtual void Reset() override
        {
            triggered = false;
        }
    };
}
