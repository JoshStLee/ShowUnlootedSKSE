#include "pch.h"
#include "CorpseCompassMarkers.h"
#include "CorpseTracker.h"

#include <cstddef>
#include <cmath>
#include <unordered_set>

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

	void CompassUpdateHook(RE::HUDObject* a_compass)
	{
		CorpseCompassMarkers::AppendTrackedMarkers();
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

CorpseCompassMarkers::InsertResult CorpseCompassMarkers::AppendTrackedMarkers()
{
	InsertResult result;
	auto* manager = GetMarkerManager();
	auto* frameOffsets = GetMarkerFrameOffsets();
	if (!manager || !frameOffsets) {
		logger::warn("Native corpse marker probe: manager or frame offsets unavailable");
		++result.failed;
		return result;
	}

	const auto corpses = CorpseTracker::Get().GetSnapshot();
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		++result.failed;
		return result;
	}
	for (const auto& corpse : corpses) {
		if (corpse.refHandle == 0) {
			++result.unresolved;
			continue;
		}

		RE::NiPointer<RE::TESObjectREFR> markerRef;
		if (!RE::LookupReferenceByHandle(corpse.refHandle, markerRef) ||
			!markerRef.get() || markerRef->GetFormID() != corpse.formID) {
			++result.unresolved;
			continue;
		}

		const auto markerIndex = manager->currentMarkerIndex;
		if (markerIndex >= std::size(manager->position)) {
			++result.full;
			break;
		}

		const bool added = AddMarker(
			manager,
			&manager->scaleformMarkerData[markerIndex],
			&manager->position[markerIndex],
			corpse.refHandle,
			static_cast<std::int32_t>(frameOffsets->enemy));

		if (added) {
			const auto targetPosition = markerRef->GetPosition();
			const auto playerPosition = player->GetPosition();
			// Scaleform needs a numeric heading to create an injected marker.  Supply
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
			markerData.icon.SetNumber(frameOffsets->enemy);
			markerData.scale.SetNumber(100.0);

			++result.added;
			static std::unordered_set<RE::FormID> loggedMarkers;
			if (loggedMarkers.insert(corpse.formID).second) {
				auto* parentCell = player->GetParentCell();
				const auto northRotation = parentCell ? parentCell->GetNorthRotation() : 0.0F;
				logger::info("Native corpse marker {:X}: player=({}, {}), corpse=({}, {}), actorYawRad={}, cellNorthRad={}",
					corpse.formID,
					player->GetPosition().x,
					player->GetPosition().y,
					markerRef->GetPosition().x,
					markerRef->GetPosition().y,
					player->GetAngleZ(),
					northRotation);
			}
		} else {
			++result.failed;
		}
	}

	return result;
}
