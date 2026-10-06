#include "pch.h"
#include "CompassProbe.h"

#include <string>
#include <vector>

namespace
{
	constexpr std::uint32_t kProbeCompassKey = 0x40;  // F6
	constexpr std::size_t kMaxLoggedMembers = 80;

	void LogMembers(std::string_view a_path, const RE::GFxValue& a_object)
	{
		std::size_t logged = 0;
		a_object.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
			if (logged >= kMaxLoggedMembers) {
				return;
			}

			logger::info("Compass probe: {}.{} type={}",
				a_path,
				a_name ? a_name : "<unnamed>",
				static_cast<std::uint32_t>(a_value.GetType()));
			++logged;
		});
		logger::info("Compass probe: logged {} members for {}{}",
			logged,
			a_path,
			logged == kMaxLoggedMembers ? " (limit reached)" : "");
	}

	void LogChildMembers(const RE::GFxValue& a_parent, std::string_view a_parentPath, const char* a_childName)
	{
		RE::GFxValue child;
		if (!a_parent.GetMember(a_childName, &child)) {
			logger::warn("Compass probe: {}.{} could not be read", a_parentPath, a_childName);
			return;
		}

		if (child.IsDisplayObject()) {
			RE::GFxValue::DisplayInfo displayInfo;
			if (child.GetDisplayInfo(&displayInfo)) {
				using Flag = RE::GFxValue::DisplayInfo::Flag;
				logger::info("Compass probe: {}.{} display state visible={} (set={}) x={} (set={}) y={} (set={}) alpha={} (set={})",
					a_parentPath,
					a_childName,
					displayInfo.GetVisible(),
					displayInfo.IsFlagSet(Flag::kVisible),
					displayInfo.GetX(),
					displayInfo.IsFlagSet(Flag::kX),
					displayInfo.GetY(),
					displayInfo.IsFlagSet(Flag::kY),
					displayInfo.GetAlpha(),
					displayInfo.IsFlagSet(Flag::kAlpha));
			} else {
				logger::warn("Compass probe: display state unavailable for {}.{}", a_parentPath, a_childName);
			}
		}

		if (!child.IsObject() && !child.IsDisplayObject()) {
			logger::info("Compass probe: {}.{} is not an inspectable object (type={})",
				a_parentPath,
				a_childName,
				static_cast<std::uint32_t>(child.GetType()));
			return;
		}

		const auto childPath = std::format("{}.{}", a_parentPath, a_childName);
		LogMembers(childPath, child);
	}

	void ProbeCompass()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			logger::warn("Compass probe: UI singleton is null");
			RE::DebugNotification("Compass probe: UI unavailable");
			return;
		}

		auto hud = ui->GetMenu<RE::HUDMenu>();
		if (!hud) {
			logger::warn("Compass probe: HUD Menu not found");
			RE::DebugNotification("Compass probe: HUD Menu not found");
			return;
		}

		auto& root = hud->GetRuntimeData().root;
		if (!root.IsDisplayObject()) {
			logger::warn("Compass probe: HUD root is not a display object");
			RE::DebugNotification("Compass probe: HUD root unavailable");
			return;
		}

		RE::GFxValue holder;
		if (!root.GetMember("CompassShoutMeterHolder", &holder)) {
			logger::warn("Compass probe: CompassShoutMeterHolder not found under HUD root");
			RE::DebugNotification("Compass probe: holder not found");
			return;
		}

		RE::GFxValue compass;
		if (!holder.GetMember("Compass", &compass)) {
			logger::warn("Compass probe: Compass not found under CompassShoutMeterHolder");
			RE::DebugNotification("Compass probe: object not found");
			return;
		}

		if (!compass.IsDisplayObject()) {
			logger::warn("Compass probe: Compass member exists but is not a display object");
			RE::DebugNotification("Compass probe: not a display object");
			return;
		}

		logger::info("Compass probe: Scaleform compass display object found");
		LogMembers("CompassShoutMeterHolder", holder);
		LogMembers("CompassShoutMeterHolder.Compass", compass);
		LogChildMembers(compass, "CompassShoutMeterHolder.Compass", "CompassFrame");
		LogChildMembers(compass, "CompassShoutMeterHolder.Compass", "CompassFrameAlt");
		LogChildMembers(compass, "CompassShoutMeterHolder.Compass", "CompassMask_mc");

		std::vector<std::string> generatedChildren;
		compass.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
			if (a_name && a_value.IsDisplayObject() && std::string_view(a_name).starts_with("instance") && generatedChildren.size() < 16) {
				generatedChildren.emplace_back(a_name);
			}
		});
		for (const auto& childName : generatedChildren) {
			logger::info("Compass probe: inspecting generated display child {}", childName);
			LogChildMembers(compass, "CompassShoutMeterHolder.Compass", childName.c_str());
		}
		RE::DebugNotification("Compass probe: compass found");
	}

	class CompassProbeInputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static CompassProbeInputSink* GetSingleton()
		{
			static CompassProbeInputSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const* a_event,
			RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event || !*a_event || (*a_event)->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto* button = (*a_event)->AsButtonEvent();
			if (button && button->GetIDCode() == kProbeCompassKey && button->IsDown()) {
				ProbeCompass();
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void CompassProbe::Register()
{
	if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
		input->AddEventSink(CompassProbeInputSink::GetSingleton());
		logger::info("Compass probe registered on F6");
	} else {
		logger::warn("Compass probe could not register: input manager unavailable");
	}
}
