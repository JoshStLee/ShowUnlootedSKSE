#include "pch.h"
#include "CorpseCompassMarkers.h"
#include "CorpseTracker.h"

#include <RE/M/MessageBoxMenu.h>
#include <RE/S/StatsMenu.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

// Diagnostic build: log the first AddMarker slot snapshot per corpse, then at
// most once every two seconds while it remains tracked.
#define SHOW_UNLOOTED_MARKER_DIAGNOSTICS 1

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

    // Distances are in Skyrim world units. Shrink corpse markers with distance,
    // then omit them beyond the display range without fading their alpha.
    constexpr float kLootMarkerScaleStartDistance = 1536.0F;
    constexpr float kLootMarkerMinimumScaleDistance = 3072.0F;
    constexpr float kLootMarkerMaxDistance = 4096.0F;
    constexpr double kLootMarkerMinimumScale = 75.0;
    constexpr double kLootMarkerMaximumScale = 100.0;

    struct ScaleformMarkerData
    {
        RE::GFxValue heading;
        RE::GFxValue alpha;
        RE::GFxValue icon;
        RE::GFxValue scale;
    };
    static_assert(sizeof(ScaleformMarkerData) == 0x60);

#if SHOW_UNLOOTED_MARKER_DIAGNOSTICS
    struct EngineHUDMarkerManager;

    // Logging-only byte copy. Never reconstruct a GFxValue from this snapshot
    // or release any pointer stored in it; ownership remains with the source.
    struct RawGFxValueSnapshot
    {
        std::uintptr_t objectInterface;
        std::uint32_t type;
        std::uint32_t padding;
        std::uint64_t value;
    };
    static_assert(sizeof(RawGFxValueSnapshot) == sizeof(RE::GFxValue));

    RawGFxValueSnapshot SnapshotGFxValue(const RE::GFxValue& a_value)
    {
        RawGFxValueSnapshot snapshot{};
        std::memcpy(&snapshot, &a_value, sizeof(snapshot));
        return snapshot;
    }

    struct MarkerDiagnosticTrace
    {
        RE::FormID formID;
        RE::RefHandle refHandle;
        std::chrono::steady_clock::time_point lastLogged;
    };

    bool ShouldLogMarkerDiagnostic(
        RE::FormID a_formID,
        RE::RefHandle a_refHandle,
        std::vector<MarkerDiagnosticTrace>& a_traces,
        std::mutex& a_mutex)
    {
        std::lock_guard lock(a_mutex);
        const auto now = std::chrono::steady_clock::now();
        const auto existing = std::find_if(
            a_traces.begin(),
            a_traces.end(),
            [&](const MarkerDiagnosticTrace& a_trace) {
                return a_trace.formID == a_formID && a_trace.refHandle == a_refHandle;
            });
        if (existing == a_traces.end()) {
            a_traces.push_back({ a_formID, a_refHandle, now });
            return true;
        }

        constexpr auto kMarkerDiagnosticInterval = std::chrono::seconds(2);
        if (now - existing->lastLogged < kMarkerDiagnosticInterval) {
            return false;
        }

        existing->lastLogged = now;
        return true;
    }

    void LogMarkerAttemptSnapshot(
        RE::FormID a_formID,
        RE::RefHandle a_refHandle,
        RE::FormID a_cellFormID,
        std::uint32_t a_markerIndex,
        float a_distance,
        double a_markerScale,
        double a_corpseIconFrame,
        std::int32_t a_passedIconFrame,
        const EngineHUDMarkerManager* a_manager,
        const ScaleformMarkerData* a_markerData,
        const RE::NiPoint3* a_markerPosition,
        const RE::NiPoint3& a_targetPosition)
    {
        const auto managerAddress = reinterpret_cast<std::uintptr_t>(a_manager);
        const auto markerDataAddress = reinterpret_cast<std::uintptr_t>(a_markerData);
        const auto markerPositionAddress = reinterpret_cast<std::uintptr_t>(a_markerPosition);
        const auto markerDataOffset = markerDataAddress - managerAddress;
        const auto markerPositionOffset = markerPositionAddress - managerAddress;
        const auto expectedMarkerDataOffset = static_cast<std::uintptr_t>(a_markerIndex) * sizeof(ScaleformMarkerData);
        const auto expectedMarkerPositionOffset =
            sizeof(ScaleformMarkerData) * 49 + static_cast<std::uintptr_t>(a_markerIndex) * sizeof(RE::NiPoint3);
        const auto heading = SnapshotGFxValue(a_markerData->heading);
        const auto alpha = SnapshotGFxValue(a_markerData->alpha);
        const auto icon = SnapshotGFxValue(a_markerData->icon);
        const auto scale = SnapshotGFxValue(a_markerData->scale);
        logger::info(
            "AddMarker sample: corpse={:X} handle={:X} cell={:X} index={} "
            "distance={:.0f} scale={:.1f} corpseFrame={:.0f} passedFrame={} "
            "manager={:p} dataSlot={:p} dataOffset=0x{:X}/expected=0x{:X} "
            "positionSlot={:p} positionOffset=0x{:X}/expected=0x{:X} "
            "target=({:.1f},{:.1f},{:.1f}) "
            "markerPos=({:.1f},{:.1f},{:.1f}) "
            "GFx heading={:p}/{:08X}/{:016X} alpha={:p}/{:08X}/{:016X} "
            "icon={:p}/{:08X}/{:016X} scale={:p}/{:08X}/{:016X}",
            a_formID,
            a_refHandle,
            a_cellFormID,
            a_markerIndex,
            a_distance,
            a_markerScale,
            a_corpseIconFrame,
            a_passedIconFrame,
            static_cast<const void*>(a_manager),
            static_cast<const void*>(a_markerData),
            markerDataOffset,
            expectedMarkerDataOffset,
            static_cast<const void*>(a_markerPosition),
            markerPositionOffset,
            expectedMarkerPositionOffset,
            a_targetPosition.x,
            a_targetPosition.y,
            a_targetPosition.z,
            a_markerPosition->x,
            a_markerPosition->y,
            a_markerPosition->z,
            reinterpret_cast<const void*>(heading.objectInterface), heading.type, heading.value,
            reinterpret_cast<const void*>(alpha.objectInterface), alpha.type, alpha.value,
            reinterpret_cast<const void*>(icon.objectInterface), icon.type, icon.value,
            reinterpret_cast<const void*>(scale.objectInterface), scale.type, scale.value);
    }
#endif

    double CalculateMarkerScale(float a_distance)
    {
        if (a_distance <= kLootMarkerMinimumScaleDistance) {
            const float scaleProgress = std::clamp(
                (a_distance - kLootMarkerScaleStartDistance) /
                    (kLootMarkerMinimumScaleDistance - kLootMarkerScaleStartDistance),
                0.0F,
                1.0F);
            return kLootMarkerMaximumScale -
                static_cast<double>(scaleProgress) *
                    (kLootMarkerMaximumScale - kLootMarkerMinimumScale);
        }

        const float scaleProgress = std::clamp(
            (a_distance - kLootMarkerMinimumScaleDistance) /
                (kLootMarkerMaxDistance - kLootMarkerMinimumScaleDistance),
            0.0F,
            1.0F);
        return kLootMarkerMinimumScale * static_cast<double>(1.0F - scaleProgress);
    }

    float CalculateWorldBearingDegrees(const RE::NiPoint3& a_target, const RE::NiPoint3& a_player)
    {
        constexpr float twoPi = 6.28318530718F;
        constexpr float radiansToDegrees = 57.2957795131F;
        float worldBearing = std::atan2(a_target.x - a_player.x, a_target.y - a_player.y);
        worldBearing = std::fmod(worldBearing, twoPi);
        if (worldBearing < 0.0F) {
            worldBearing += twoPi;
        }
        return worldBearing * radiansToDegrees;
    }

    struct EngineHUDMarkerManager
    {
        // These counts mirror the engine layout. AddMarker needs a position
        // and marker-data slot, so the 48 positions cap usable marker indices;
        // the extra marker-data entry does not make index 48 valid.
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
    std::recursive_mutex compassUpdateMutex;

    std::optional<double> killLootFrame;

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

    std::optional<double> ProbeKillLootLabel(RE::GFxMovieView* a_view)
    {
        if (!a_view) {
            return std::nullopt;
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
            logger::info("HUD KillLoot probe: DirectionRect not found; using enemy icon frame");
            return std::nullopt;
        }

        RE::GFxValue depth;
        if (!compassRect.Invoke("getNextHighestDepth", &depth, nullptr, 0) || !depth.IsNumber()) {
            logger::info("HUD KillLoot probe: getNextHighestDepth failed; using enemy icon frame");
            return std::nullopt;
        }

        RE::GFxValue attachArgs[3];
        attachArgs[0].SetString("Compass Marker");
        attachArgs[1].SetString("ShowUnlootedKillLootProbe");
        attachArgs[2] = depth;
        RE::GFxValue probeClip;
        if (!compassRect.Invoke("attachMovie", &probeClip, attachArgs, 3) || !probeClip.IsDisplayObject()) {
            logger::info("HUD KillLoot probe: attachMovie('Compass Marker') failed; using enemy icon frame");
            return std::nullopt;
        }

        RE::GFxValue hidden;
        hidden.SetBoolean(false);
        probeClip.SetMember("_visible", hidden);
        RE::GFxValue zeroAlpha;
        zeroAlpha.SetNumber(0.0);
        probeClip.SetMember("_alpha", zeroAlpha);

        RE::GFxValue totalFrames;
        double total = -1.0;
        if (probeClip.GetMember("_totalframes", &totalFrames) && totalFrames.IsNumber()) {
            total = totalFrames.GetNumber();
        }

        const auto* movieDef = a_view->GetMovieDef();
        const char* movieURL = movieDef ? movieDef->GetFileURL() : nullptr;
        RE::GFxValue clipURLValue;
        const char* clipURL = probeClip.GetMember("_url", &clipURLValue) && clipURLValue.IsString()
            ? clipURLValue.GetString()
            : nullptr;
        logger::info(
            "HUD Scaleform source: movie='{}', Compass Marker clip='{}', totalFrames={}",
            movieURL ? movieURL : "<unknown>",
            clipURL ? clipURL : "<unknown>",
            total);

        double frameAfterFive = 0.0;
        RE::GFxValue frameFive;
        frameFive.SetNumber(5.0);
        const bool jumpedToFive = InvokeGotoAndStop(probeClip, frameFive) &&
            ReadCurrentFrame(probeClip, frameAfterFive);

        RE::GFxValue frameOne;
        frameOne.SetNumber(1.0);
        RE::GFxValue frameTwo;
        frameTwo.SetNumber(2.0);
        RE::GFxValue frameLabel;
        a_view->CreateString(&frameLabel, kKillLootFrameLabel);

        double resolvedFromOne = 0.0;
        double resolvedFromTwo = 0.0;
        const bool testedFromOne = InvokeGotoAndStop(probeClip, frameOne) &&
            InvokeGotoAndStop(probeClip, frameLabel) &&
            ReadCurrentFrame(probeClip, resolvedFromOne);
        const bool testedFromTwo = InvokeGotoAndStop(probeClip, frameTwo) &&
            InvokeGotoAndStop(probeClip, frameLabel) &&
            ReadCurrentFrame(probeClip, resolvedFromTwo);

        logger::info(
            "HUD KillLoot probe: total={} goto5={} frameAfter5={} invoke1={} invoke2={} frameFrom1={} frameFrom2={}",
            total,
            jumpedToFive,
            frameAfterFive,
            testedFromOne,
            testedFromTwo,
            resolvedFromOne,
            resolvedFromTwo);

        RE::GFxValue ignored;
        probeClip.Invoke("removeMovieClip", &ignored, nullptr, 0);

        const bool resolved = testedFromOne && testedFromTwo &&
            resolvedFromOne == resolvedFromTwo &&
            resolvedFromOne > 0.0;
        if (resolved) {
            logger::info("HUD KillLoot label '{}' resolved to frame {}", kKillLootFrameLabel, resolvedFromOne);
            return resolvedFromOne;
        } else {
            logger::info("HUD KillLoot probe: gotoAndStop('{}') did not resolve a stable frame; using enemy icon frame", kKillLootFrameLabel);
            return std::nullopt;
        }
    }

    bool IsCompassHUDVisible(RE::HUDObject* a_compass, RE::UI* a_ui)
    {
        if (!a_compass || !a_compass->view || !a_ui ||
            a_ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) ||
            a_ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) ||
            a_ui->IsMenuOpen(RE::StatsMenu::MENU_NAME) ||
            a_ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME)) {
            return false;
        }

        constexpr std::array<const char*, 3> compassPaths{
            "_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass",
            "_root.CompassShoutMeterHolder.Compass",
            "CompassShoutMeterHolder.Compass"
        };

        for (const auto* path : compassPaths) {
            RE::GFxValue compassClip;
            if (!a_compass->view->GetVariable(&compassClip, path) || !compassClip.IsDisplayObject()) {
                continue;
            }

            RE::GFxValue visible;
            if (!compassClip.GetMember("_visible", &visible) || !visible.IsBool() || !visible.GetBool()) {
                return false;
            }

            RE::GFxValue alpha;
            if (compassClip.GetMember("_alpha", &alpha) && alpha.IsNumber() && alpha.GetNumber() <= 0.0) {
                return false;
            }

            return true;
        }

        return false;
    }

    void CompassUpdateHook(RE::HUDObject* a_compass)
    {
        // Crash logs show HUD updates reaching this hook from job/task
        // threads. Serialize access to the marker manager and Scaleform data
        // across our injection and the original compass update. This lock also
        // protects the one-shot probe state and cached frame below.
        std::lock_guard updateLock(compassUpdateMutex);

        auto* ui = RE::UI::GetSingleton();
        const bool compassVisible = IsCompassHUDVisible(a_compass, ui);
        // Access is serialized by compassUpdateMutex above; keep the flag and
        // cached optional frame under that same lock.
        static bool probeAttempted = false;
        if (!probeAttempted && compassVisible && ui) {
            auto hudView = ui->GetMovieView(RE::HUDMenu::MENU_NAME);
            if (hudView) {
                killLootFrame = ProbeKillLootLabel(hudView.get());
                probeAttempted = true;
            }
        }

        if (compassVisible) {
            CorpseCompassMarkers::AppendTrackedMarkers();
        }

        // Append markers before Skyrim finishes the compass update so its HUD
        // refresh can consume the updated marker list.
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
#if SHOW_UNLOOTED_MARKER_DIAGNOSTICS
	static std::vector<MarkerDiagnosticTrace> markerDiagnosticTraces;
	static std::mutex markerDiagnosticMutex;
	const auto shouldLogDiagnostic = [&](RE::FormID formID, RE::RefHandle refHandle) {
		return ShouldLogMarkerDiagnostic(formID, refHandle, markerDiagnosticTraces, markerDiagnosticMutex);
	};
#endif

	auto* manager = GetMarkerManager();
	auto* frameOffsets = GetMarkerFrameOffsets();
	if (!manager || !frameOffsets) {
		return;
	}

	// This hook can be reached from different HUD update threads. Keep the
	// snapshot storage local so overlapping calls cannot overwrite each other's
	// corpse list.
	auto corpses = CorpseTracker::Get().GetSnapshot();
#if SHOW_UNLOOTED_MARKER_DIAGNOSTICS
	{
		std::lock_guard lock(markerDiagnosticMutex);
		const auto staleBefore = std::chrono::steady_clock::now() - std::chrono::minutes(1);
		markerDiagnosticTraces.erase(
			std::remove_if(
				markerDiagnosticTraces.begin(),
				markerDiagnosticTraces.end(),
				[&](const MarkerDiagnosticTrace& trace) { return trace.lastLogged < staleBefore; }),
			markerDiagnosticTraces.end());
	}
#endif
	std::vector<TrackedCorpse> invalidCorpses;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	const auto enemyIconFrame = static_cast<std::int32_t>(frameOffsets->enemy);
	const double corpseIconFrame = killLootFrame.value_or(static_cast<double>(enemyIconFrame));

	for (const auto& corpse : corpses) {
		if (corpse.looted) {
			continue;
		}

		if (corpse.refHandle == 0) {
			continue;
		}

		RE::NiPointer<RE::TESObjectREFR> markerRef;
		if (!RE::LookupReferenceByHandle(corpse.refHandle, markerRef)) {
			// Handle gone — drop so we stop trying every frame.
			invalidCorpses.push_back(corpse);
			continue;
		}

		auto* reference = markerRef.get();
		if (!reference || reference->GetFormID() != corpse.formID || reference->IsDeleted()) {
			invalidCorpses.push_back(corpse);
			continue;
		}

		// A reference without a parent cell cannot be passed to the native marker routine.
		if (!reference->GetParentCell()) {
			invalidCorpses.push_back(corpse);
			continue;
		}

		auto* actor = reference->As<RE::Actor>();
		if (!actor || !actor->IsDead()) {
			invalidCorpses.push_back(corpse);
			continue;
		}

		if (!reference->Is3DLoaded()) {
			continue;
		}

		const auto targetPosition = reference->GetPosition();
		const auto playerPosition = player->GetPosition();
		const float dx = targetPosition.x - playerPosition.x;
		const float dy = targetPosition.y - playerPosition.y;
		const float dz = targetPosition.z - playerPosition.z;
		const float distanceSquared = dx * dx + dy * dy + dz * dz;
		constexpr float maxDistanceSquared = kLootMarkerMaxDistance * kLootMarkerMaxDistance;
		if (!std::isfinite(distanceSquared) || distanceSquared >= maxDistanceSquared) {
			// Leave the corpse tracked so its icon can return if the player comes back.
			continue;
		}

		const float distance = std::sqrt(distanceSquared);
		const double markerScale = CalculateMarkerScale(distance);

		const auto markerIndex = manager->currentMarkerIndex;
		if (markerIndex >= std::size(manager->position)) {
			break;
		}

		auto* markerDataSlot = &manager->scaleformMarkerData[markerIndex];
		auto* markerPosition = &manager->position[markerIndex];
#if SHOW_UNLOOTED_MARKER_DIAGNOSTICS
		const bool logDiagnostic = shouldLogDiagnostic(corpse.formID, corpse.refHandle);
		if (logDiagnostic) {
			LogMarkerAttemptSnapshot(
				corpse.formID,
				corpse.refHandle,
				reference->GetParentCell()->GetFormID(),
				markerIndex,
				distance,
				markerScale,
				corpseIconFrame,
				enemyIconFrame,
				manager,
				markerDataSlot,
				markerPosition,
				targetPosition);
		}
#endif
		const bool markerAdded = AddMarker(
			manager,
			markerDataSlot,
			markerPosition,
			corpse.refHandle,
			enemyIconFrame);

#if SHOW_UNLOOTED_MARKER_DIAGNOSTICS
		if (logDiagnostic && !markerAdded) {
			logger::warn(
				"AddMarker returned false: corpse={:X} handle={:X} index={} nextIndex={}",
				corpse.formID,
				corpse.refHandle,
				markerIndex,
				manager->currentMarkerIndex);
		}
#endif

		if (!markerAdded) {
			continue;
		}

		auto& markerData = manager->scaleformMarkerData[markerIndex];
		markerData.heading.SetNumber(CalculateWorldBearingDegrees(targetPosition, playerPosition));
		markerData.alpha.SetNumber(100.0);
		markerData.icon.SetNumber(corpseIconFrame);
		markerData.scale.SetNumber(markerScale);
	}

	for (const auto& corpse : invalidCorpses) {
		CorpseTracker::Get().Forget(corpse.formID, corpse.refHandle);
	}
}
