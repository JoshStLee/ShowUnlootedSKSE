#include "pch.h"
#include "CorpseCompassMarkers.h"
#include "CorpseTracker.h"
#include "CorpseSerialization.h"
#include "QuickLootIntegration.h"

class DeathHandler : public RE::BSTEventSink<RE::TESDeathEvent>
{
public:
    static DeathHandler* GetSingleton()
    {
        static DeathHandler singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>*) override
    {
        if (!a_event || !a_event->actorDying) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* deadRef = a_event->actorDying.get();
        auto* dead = deadRef ? deadRef->As<RE::Actor>() : nullptr;
        if (!dead) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::Actor* killer = nullptr;
        if (a_event->actorKiller) {
            if (auto* killerRef = a_event->actorKiller.get()) {
                killer = killerRef->As<RE::Actor>();
            }
        }

        const bool counts =
            killer &&
            (killer == player || killer->IsPlayerTeammate());

        if (!counts) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // const char* name = dead->GetDisplayFullName();
        // logger::info("Death: {} (killer: {})",
        //     name ? name : "<no name>",
        //     killer && killer->GetDisplayFullName() ? killer->GetDisplayFullName() : "none");

        // RE::DebugNotification(
        //     std::format("Killed: {}", name ? name : "something").c_str());

        TrackedCorpse entry;
        entry.formID    = dead->GetFormID();
        RE::CreateRefHandle(entry.refHandle, dead);
        entry.position  = dead->GetPosition();
        entry.timestamp = RE::Calendar::GetSingleton()
                              ? RE::Calendar::GetSingleton()->GetHoursPassed()
                              : 0.0f;
        entry.looted    = false;

        if (CorpseTracker::Get().Add(entry)) {
            logger::info(
                "Tracked corpse {:X} (handle {:X}) at ({:.1f}, {:.1f}, {:.1f}) — total {}",
                entry.formID,
                entry.refHandle,
                entry.position.x, entry.position.y, entry.position.z,
                CorpseTracker::Get().Size());
        } else {
            logger::debug("Corpse {:X} already tracked, skipped", entry.formID);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

// Activating a corpse opens its container. Clear that corpse's marker once the
// activation event identifies it as the object being activated. QuickLootIE's
// separate menu-open API is handled by QuickLootIntegration.
class ActivateHandler : public RE::BSTEventSink<RE::TESActivateEvent>
{
    public:
        static ActivateHandler* GetSingleton()
        {
            static ActivateHandler singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESActivateEvent* a_event,
            RE::BSTEventSource<RE::TESActivateEvent>*) override
        {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // objectActivated is the target; actionRef is the actor performing
            // the activation. Only the target should be marked as looted.
            auto* activated = a_event->objectActivated.get();
            if (activated && CorpseTracker::Get().SetLooted(activated->GetFormID())) {
                logger::info("Corpse {:X} activated — marker cleared", activated->GetFormID());
            }

            return RE::BSEventNotifyControl::kContinue;
        }
};

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    switch (message->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        QuickLootIntegration::Register();
        if (auto* source = RE::ScriptEventSourceHolder::GetSingleton()) {
            source->AddEventSink(DeathHandler::GetSingleton());
            source->AddEventSink(ActivateHandler::GetSingleton());
            logger::info("DeathHandler + ActivateHandler registered");
        } 
        break; 
    }
    
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();

    auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(
        a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));

    if (!g_messaging) {
        logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
        return false;
    }

    logger::info("{} v{}"sv, Plugin::NAME, Plugin::VERSION.string());

    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(1 << 10);
    CorpseCompassMarkers::InstallHook(); 
    CorpseSerialization::Register();
    g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

    return true;
}
