#include "pch.h"
#include "CorpseCompassMarkers.h"
#include "CorpseTracker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

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
            logger::info("HUD KillLoot probe: DirectionRect not found; using enemy icon frame");
            return false;
        }

        RE::GFxValue depth;
        if (!compassRect.Invoke("getNextHighestDepth", &depth, nullptr, 0) || !depth.IsNumber()) {
            logger::info("HUD KillLoot probe: getNextHighestDepth failed; using enemy icon frame");
            return false;
        }

        RE::GFxValue attachArgs[3];
        attachArgs[0].SetString("Compass Marker");
        attachArgs[1].SetString("ShowUnlootedKillLootProbe");
        attachArgs[2] = depth;
        RE::GFxValue probeClip;
        if (!compassRect.Invoke("attachMovie", &probeClip, attachArgs, 3) || !probeClip.IsDisplayObject()) {
            logger::info("HUD KillLoot probe: attachMovie('Compass Marker') failed; using enemy icon frame");
            return false;
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
        } else {
            logger::info("HUD KillLoot probe: gotoAndStop('{}') did not resolve a stable frame; using enemy icon frame", kKillLootFrameLabel);
        }
        return resolved;
    }

    bool IsCompassHUDVisible(RE::HUDObject* a_compass, RE::UI* a_ui)
    {
        if (!a_compass || !a_compass->view || !a_ui ||
            a_ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) ||
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
        auto* ui = RE::UI::GetSingleton();
        const bool compassVisible = IsCompassHUDVisible(a_compass, ui);
        static bool probeAttempted = false;
        if (!probeAttempted && compassVisible && ui) {
            auto hudView = ui->GetMovieView(RE::HUDMenu::MENU_NAME);
            if (hudView) {
                killLootLabelResolved = ProbeKillLootLabel(hudView.get());
                probeAttempted = true;
            }
        }

        if (compassVisible) {
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
	struct MarkerTraceKey
	{
		std::uint32_t markerIndex;
		RE::FormID formID;
		RE::RefHandle refHandle;
	};
	static std::vector<MarkerTraceKey> loggedMarkerAttempts;
	static std::mutex markerTraceMutex;
	const auto shouldLogMarkerOnce = [&](std::uint32_t markerIndex, RE::FormID formID, RE::RefHandle refHandle) {
		std::lock_guard lock(markerTraceMutex);
		const auto alreadyLogged = std::find_if(
			loggedMarkerAttempts.begin(),
			loggedMarkerAttempts.end(),
			[&](const MarkerTraceKey& key) {
				return key.markerIndex == markerIndex && key.formID == formID && key.refHandle == refHandle;
			});
		if (alreadyLogged != loggedMarkerAttempts.end()) {
			return false;
		}
		loggedMarkerAttempts.push_back({ markerIndex, formID, refHandle });
		return true;
	};

	auto* manager = GetMarkerManager();
	auto* frameOffsets = GetMarkerFrameOffsets();
	if (!manager || !frameOffsets) {
		return;
	}

	static std::vector<TrackedCorpse> corpses;
	CorpseTracker::Get().GetSnapshotInto(corpses);
	{
		std::lock_guard lock(markerTraceMutex);
		loggedMarkerAttempts.erase(
			std::remove_if(
				loggedMarkerAttempts.begin(),
				loggedMarkerAttempts.end(),
				[&](const MarkerTraceKey& key) {
					return std::none_of(corpses.begin(), corpses.end(), [&](const TrackedCorpse& corpse) {
						return corpse.formID == key.formID && corpse.refHandle == key.refHandle;
					});
				}),
			loggedMarkerAttempts.end());
	}
	std::vector<TrackedCorpse> invalidCorpses;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	const auto enemyIconFrame = static_cast<std::int32_t>(frameOffsets->enemy);

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

		// CTD path: Orc FF002D76 had ParentCell --- after QuickLoot re-track.
		// Native AddMarker dec's through garbage when the ref is detached.
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

		const auto markerIndex = manager->currentMarkerIndex;
		if (markerIndex >= std::size(manager->position)) {
			if (shouldLogMarkerOnce(markerIndex, corpse.formID, corpse.refHandle)) {
				logger::warn(
					"Skipping corpse marker {:X} (handle {:X}): marker index {} exceeds capacity {}",
					corpse.formID,
					corpse.refHandle,
					markerIndex,
					std::size(manager->position));
			}
			break;
		}

		const bool traceAttempt = shouldLogMarkerOnce(markerIndex, corpse.formID, corpse.refHandle);
		if (traceAttempt) {
			logger::info(
				"Native marker attempt: corpse={:X} handle={:X} markerIndex={} cell={:X} reference={:p}",
				corpse.formID,
				corpse.refHandle,
				markerIndex,
				reference->GetParentCell()->GetFormID(),
				static_cast<const void*>(reference));
		}

		const bool markerAdded = AddMarker(
			manager,
			&manager->scaleformMarkerData[markerIndex],
			&manager->position[markerIndex],
			corpse.refHandle,
			enemyIconFrame);

		if (traceAttempt) {
			logger::info(
				"Native marker result: corpse={:X} handle={:X} markerIndex={} added={} nextMarkerIndex={}",
				corpse.formID,
				corpse.refHandle,
				markerIndex,
				markerAdded,
				manager->currentMarkerIndex);
		}

		if (!markerAdded) {
			continue;
		}

		const auto targetPosition = markerRef->GetPosition();
		const auto playerPosition = player->GetPosition();
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
			markerData.icon.SetNumber(static_cast<double>(enemyIconFrame));
		}
		markerData.scale.SetNumber(100.0);
	}

	for (const auto& corpse : invalidCorpses) {
		CorpseTracker::Get().Forget(corpse.formID, corpse.refHandle);
		logger::info(
			"Dropped invalid tracked corpse {:X} (handle {:X})",
			corpse.formID,
			corpse.refHandle);
	}
}
