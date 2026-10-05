#include "pch.h"

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

        // Prefer killer from the event if the struct has it
        RE::Actor* killer = nullptr;
        if (a_event->actorKiller) {
            if (auto* killerRef = a_event->actorKiller.get()) {
                killer = killerRef->As<RE::Actor>();
            }
        }

        // Fallback: only care that something died for now if killer is unavailable
        const bool killedByPlayer = (killer == player);

        if (killedByPlayer || !killer) {  // remove "|| !killer" later if you only want player kills
            const char* name = dead->GetDisplayFullName();
            logger::info("Death: {} (killer: {})",
                name ? name : "<no name>",
                killer ? (killer->GetDisplayFullName() ? killer->GetDisplayFullName() : "unknown") : "none");

            RE::DebugNotification(
                std::format("Killed: {}", name ? name : "something").c_str());
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