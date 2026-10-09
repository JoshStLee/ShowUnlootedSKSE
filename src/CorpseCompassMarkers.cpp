#include "pch.h"
#include "CorpseCompassMarkers.h"
#include "CorpseTracker.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace
{
	struct MarkerFrameOffsets
	{
		std::uint32_t quest;
		std::uint32_t questDoor;
		std::uint32_t playerSet;
		std::uint32_t enemy;
		std::uint32_t location;
		std::uint32_t undiscoveredLocation;
	};
	static_assert(sizeof(MarkerFrameOffsets) == 0x18);

	constexpr const char* kKillLootFrameLabel = "KillLoot";

	struct ScaleformMarkerData
	{
		RE::GFxValue heading;
		RE::GFxValue alpha;
		RE::GFxValue icon;
		RE::GFxValue scale;
	};
	static_assert(sizeof(ScaleformMarkerData) == 0x60);

	struct EngineHUDMarkerManager
	{
		ScaleformMarkerData scaleformMarkerData[49];
		RE::NiPoint3 position[48];
		RE::BSTArray<RE::RefHandle> locationRefs;
		float sqRadiusToAddLocation;
		std::uint32_t currentMarkerIndex;
		std::uint32_t unk14C0;
	};
	static_assert(offsetof(EngineHUDMarkerManager, currentMarkerIndex) == 0x14BC);
	static_assert(sizeof(EngineHUDMarkerManager) == 0x14C8);

	EngineHUDMarkerManager* GetMarkerManager()
	{
		REL::Relocation<EngineHUDMarkerManager*> singleton{ RELOCATION_ID(519611, 406154) };
		return singleton.get();
	}

	MarkerFrameOffsets* GetMarkerFrameOffsets()
	{
		REL::Relocation<MarkerFrameOffsets*> singleton{ RELOCATION_ID(519587, 406118) };
		return singleton.get();
	}

	using AddMarkerFn = bool (*)(
		const EngineHUDMarkerManager*,
		ScaleformMarkerData*,
		RE::NiPoint3*,
		const RE::RefHandle&,
		std::int32_t);

	bool AddMarker(
		const EngineHUDMarkerManager* a_manager,
		ScaleformMarkerData* a_markerData,
		RE::NiPoint3* a_position,
		const RE::RefHandle& a_handle,
		std::int32_t a_iconFrame)
	{
		static REL::Relocation<AddMarkerFn> addMarker{ RELOCATION_ID(50851, 51728) };
		return addMarker(a_manager, a_markerData, a_position, a_handle, a_iconFrame);
	}

	using CompassUpdateFn = void (*)(RE::HUDObject*);
	CompassUpdateFn originalCompassUpdate = nullptr;
	bool killLootLabelResolved = false;

	bool InvokeGotoAndStop(RE::GFxValue& a_clip, const RE::GFxValue& a_frame)
	{
		return a_clip.Invoke("gotoAndStop", nullptr, &a_frame, 1);
	}

	bool ReadCurrentFrame(RE::GFxValue& a_clip, double& a_frame)
	{
		RE::GFxValue currentFrame;
		if (!a_clip.GetMember("_currentframe", &currentFrame) || !currentFrame.IsNumber()) {
			return false;
		}

		a_frame = currentFrame.GetNumber();
		return true;
	}

	bool ProbeKillLootLabel(RE::GFxMovieView* a_view)
	{
		if (!a_view) {
			return false;
		}

		RE::GFxValue compassRect;
		constexpr std::array<std::string_view, 3> compassRectPaths{
			"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.DirectionRect",
			"_root.CompassShoutMeterHolder.Compass.DirectionRect",
			"CompassShoutMeterHolder.Compass.DirectionRect"
		};
		for (const auto path : compassRectPaths) {
			if (a_view->GetVariable(&compassRect, path.data()) && compassRect.IsDisplayObject()) {
				break;
			}
		}
		if (!compassRect.IsDisplayObject()) {
			logger::info("HUD KillLoot label probe unavailable; using enemy icon frame");
			return false;
		}

		RE::GFxValue depth;
		if (!compassRect.Invoke("getNextHighestDepth", &depth, nullptr, 0) || !depth.IsNumber()) {
			logger::info("HUD KillLoot label probe unavailable; using enemy icon frame");
			return false;
		}

		RE::GFxValue attachArgs[3];
		attachArgs[0].SetString("Compass Marker");
		attachArgs[1].SetString("ShowUnlootedKillLootProbe");
		attachArgs[2] = depth;
		RE::GFxValue probeClip;
		if (!compassRect.Invoke("attachMovie", &probeClip, attachArgs, 3) || !probeClip.IsDisplayObject()) {
			logger::info("HUD KillLoot label probe unavailable; using enemy icon frame");
			return false;
		}

		RE::GFxValue hidden;
		hidden.SetBoolean(false);
		probeClip.SetMember("_visible", hidden);
		RE::GFxValue zeroAlpha;
		zeroAlpha.SetNumber(0.0);
		probeClip.SetMember("_alpha", zeroAlpha);

		RE::GFxValue frameOne;
		frameOne.SetNumber(1.0);
		RE::GFxValue frameTwo;
		frameTwo.SetNumber(2.0);
		RE::GFxValue frameLabel(kKillLootFrameLabel);
		double resolvedFromOne = 0.0;
		double resolvedFromTwo = 0.0;
		const bool testedFromOne = InvokeGotoAndStop(probeClip, frameOne) &&
			InvokeGotoAndStop(probeClip, frameLabel) &&
			ReadCurrentFrame(probeClip, resolvedFromOne);
		const bool testedFromTwo = InvokeGotoAndStop(probeClip, frameTwo) &&
			InvokeGotoAndStop(probeClip, frameLabel) &&
			ReadCurrentFrame(probeClip, resolvedFromTwo);

		RE::GFxValue ignored;
		probeClip.Invoke("removeMovieClip", &ignored, nullptr, 0);

		const bool resolved = testedFromOne && testedFromTwo && resolvedFromOne == resolvedFromTwo;
		if (resolved) {
			logger::info("HUD KillLoot label '{}' resolved", kKillLootFrameLabel);
		} else {
			logger::info("HUD KillLoot label not resolved; using enemy icon frame");
		}
		return resolved;
	}

	void CompassUpdateHook(RE::HUDObject* a_compass)
	{
		auto* ui = RE::UI::GetSingleton();
		static bool probeAttempted = false;
		if (!probeAttempted && ui) {
			auto hudView = ui->GetMovieView(RE::HUDMenu::MENU_NAME);
			if (hudView) {
				killLootLabelResolved = ProbeKillLootLabel(hudView.get());
				probeAttempted = true;
			}
		}

		const bool journalOpen = ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME);

		if (!journalOpen) {
			CorpseCompassMarkers::AppendTrackedMarkers();
		}
		if (originalCompassUpdate) {
			originalCompassUpdate(a_compass);
		}
	}
}

void CorpseCompassMarkers::InstallHook()
{
	if (originalCompassUpdate) {
		return;
	}

	REL::Relocation<std::uintptr_t> compassVTable{ RE::VTABLE_Compass[0] };
	originalCompassUpdate = reinterpret_cast<CompassUpdateFn>(compassVTable.write_vfunc(1, &CompassUpdateHook));
	if (originalCompassUpdate) {
		logger::info("Native corpse marker update hook installed");
	} else {
		logger::critical("Native corpse marker update hook installation failed");
	}
}

void CorpseCompassMarkers::AppendTrackedMarkers()
{
	auto* manager = GetMarkerManager();
	auto* frameOffsets = GetMarkerFrameOffsets();
	if (!manager || !frameOffsets) {
		return;
	}

	// Reused across frames so the per-frame compass update stops allocating a
	// fresh vector each call. The copy under the lock is still required for
	// thread safety — the death handler mutates the tracker on another thread.
	static std::vector<TrackedCorpse> corpses;
	CorpseTracker::Get().GetSnapshotInto(corpses);

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	for (const auto& corpse : corpses) {
		// Looted corpses stop being drawn — the "unlooted" half of the feature.
		if (corpse.looted) {
			continue;
		}

		if (corpse.refHandle == 0) {
			continue;
		}

		RE::NiPointer<RE::TESObjectREFR> markerRef;
		if (!RE::LookupReferenceByHandle(corpse.refHandle, markerRef) ||
			!markerRef.get() ||
			markerRef->GetFormID() != corpse.formID ||
			markerRef->IsDeleted() ||
			!markerRef->Is3DLoaded()) {
			continue;
		} 

		// AddMarker advances manager->currentMarkerIndex itself (the reference
		// implementation, Compass Navigation Overhaul, reads
		// `currentMarkerIndex - 1` to find the slot it just added), so reading
		// the live value each iteration gives every corpse its own free slot.
		// Do not hoist this out of the loop.
		const auto markerIndex = manager->currentMarkerIndex;
		if (markerIndex >= std::size(manager->position)) {
			break;
		}

		// Seed the slot with the engine's enemy frame. The final icon uses the
		// KillLoot label when the HUD probe resolves it, and otherwise keeps this
		// enemy frame as the fallback.
		if (!AddMarker(
			manager,
			&manager->scaleformMarkerData[markerIndex],
			&manager->position[markerIndex],
			corpse.refHandle,
			static_cast<std::int32_t>(frameOffsets->enemy))) {
			continue;
		}

		const auto targetPosition = markerRef->GetPosition();
		const auto playerPosition = player->GetPosition();
		// Scaleform needs a numeric heading to create an injected marker. Supply
		// the absolute world bearing; the compass applies the player/camera yaw.
		// Subtracting either yaw here rotates the marker a second time.
		constexpr float twoPi = 6.28318530718F;
		constexpr float radiansToDegrees = 57.2957795131F;
		float worldBearing = std::atan2(
			targetPosition.x - playerPosition.x,
			targetPosition.y - playerPosition.y);
		worldBearing = std::fmod(worldBearing, twoPi);
		if (worldBearing < 0.0F) {
			worldBearing += twoPi;
		}

		auto& markerData = manager->scaleformMarkerData[markerIndex];
		markerData.heading.SetNumber(worldBearing * radiansToDegrees);
		markerData.alpha.SetNumber(100.0);
		if (killLootLabelResolved) {
			markerData.icon.SetString(kKillLootFrameLabel);
		} else {
			markerData.icon.SetNumber(static_cast<double>(frameOffsets->enemy));
		}
		markerData.scale.SetNumber(100.0);
	}
}
