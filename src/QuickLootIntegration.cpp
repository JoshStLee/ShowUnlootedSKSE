#include "pch.h"
#include "QuickLootIntegration.h"

#include "CorpseTracker.h"
#include "QuickLootAPI.h"

namespace
{
    void OnOpenLootMenu(QuickLoot::API::OpenLootMenuEvent* a_event)
    {
        if (!a_event) {
            return;
        }

        auto container = a_event->container.get();
        auto* reference = container.get();
        if (!reference) {
            logger::warn("QuickLootIE reported an opened menu with an invalid container handle");
            return;
        }

        const auto formID = reference->GetFormID();
        const bool trackedCorpse = CorpseTracker::Get().SetLooted(formID);
        logger::info(
            "QuickLootIE opened container {:X}; tracked corpse match={}",
            formID,
            trackedCorpse);
        if (trackedCorpse) {
            logger::info("Corpse {:X} opened through QuickLootIE — marker cleared", formID);
        }
    }
}

void QuickLootIntegration::Register()
{
    if (!QuickLoot::API::QuickLootAPI::Init("ShowUnlootedSKSE")) {
        logger::info("QuickLootIE API unavailable; vanilla activation tracking remains enabled");
        return;
    }

    QuickLoot::API::QuickLootAPI::RegisterOpenLootMenuHandler(OnOpenLootMenu);
    logger::info("QuickLootIE open-menu handler registered");
}
