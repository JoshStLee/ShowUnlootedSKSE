#pragma once

// Minimal declarations for the public QuickLootIE API v2.0 surface used here.
// The interface method order and signatures must stay aligned with
// QuickLootIE's MIT-licensed include/QuickLootAPI.h.
#include "RE/Skyrim.h"
#include "REX/W32/KERNEL32.h"

namespace QuickLoot::API
{
    struct OpenLootMenuEvent
    {
        RE::ObjectRefHandle container;
    };

    using OpenLootMenuHandler = void (*)(OpenLootMenuEvent* a_event);
    using PlaceholderHandler = void (*)(void*);

    class QuickLootAPI
    {
    public:
        static constexpr const char* SERVER_PLUGIN_NAME = "QuickLootIE";

        static bool Init(const char* a_pluginName)
        {
            _pluginName = a_pluginName;

            const auto module = REX::W32::GetModuleHandleA(SERVER_PLUGIN_NAME);
            if (!module) {
                return false;
            }

            using GetInterfaceV20 = InterfaceV20* (*)();
            const auto getInterface = reinterpret_cast<GetInterfaceV20>(
                REX::W32::GetProcAddress(module, "GetQuickLootInterfaceV20"));
            if (!getInterface) {
                return false;
            }

            _interface = getInterface();
            return _interface != nullptr;
        }

        static void RegisterOpenLootMenuHandler(OpenLootMenuHandler a_handler)
        {
            if (_interface) {
                _interface->RegisterOpenLootMenuHandler(_pluginName, a_handler);
            }
        }

    private:
        struct InterfaceV20
        {
            virtual void DisableLootMenu(const char*);
            virtual void EnableLootMenu(const char*);
            virtual void RegisterTakingItemHandler(const char*, PlaceholderHandler);
            virtual void RegisterTakeItemHandler(const char*, PlaceholderHandler);
            virtual void RegisterSelectItemHandler(const char*, PlaceholderHandler);
            virtual void RegisterOpeningLootMenuHandler(const char*, PlaceholderHandler);
            virtual void RegisterOpenLootMenuHandler(const char*, OpenLootMenuHandler);
            virtual void RegisterCloseLootMenuHandler(const char*, PlaceholderHandler);
            virtual void RegisterInvalidateLootMenuHandler(const char*, PlaceholderHandler);
            virtual void RegisterModifyInventoryHandler(const char*, PlaceholderHandler);
            virtual void RegisterPopulateInfoBarHandler(const char*, PlaceholderHandler);
            virtual void RegisterPopulateButtonBarHandler(const char*, PlaceholderHandler);
            virtual void ForceCurrentContainer(const char*, RE::ObjectRefHandle);
            virtual void ClearForcedContainer(const char*);
            virtual void CloseLootMenu(const char*);
            virtual void RefreshLootMenu(const char*);
        };

        inline static const char* _pluginName{ nullptr };
        inline static InterfaceV20* _interface{ nullptr };
    };
}
