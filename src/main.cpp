#include "pch.h"
#include "CorpseTracker.h"

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

        const char* name = dead->GetDisplayFullName();
        logger::info("Death: {} (killer: {})",
            name ? name : "<no name>",
            killer && killer->GetDisplayFullName() ? killer->GetDisplayFullName() : "none");

        RE::DebugNotification(
            std::format("Killed: {}", name ? name : "something").c_str());

        TrackedCorpse entry;
        entry.formID    = dead->GetFormID();
        entry.position  = dead->GetPosition();
        entry.timestamp = RE::Calendar::GetSingleton()
                              ? RE::Calendar::GetSingleton()->GetHoursPassed()
                              : 0.0f;
        entry.looted    = false;

        if (CorpseTracker::Get().Add(entry)) {
            logger::info(
                "Tracked corpse {:X} at ({:.1f}, {:.1f}, {:.1f}) — total {}",
                entry.formID,
                entry.position.x, entry.position.y, entry.position.z,
                CorpseTracker::Get().Size());
        } else {
            logger::debug("Corpse {:X} already tracked, skipped", entry.formID);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    switch (message->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        if (auto* source = RE::ScriptEventSourceHolder::GetSingleton()) {
            source->AddEventSink(DeathHandler::GetSingleton());
            logger::info("DeathHandler registered");
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

    g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

    return true;
}