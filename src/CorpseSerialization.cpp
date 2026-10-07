#include "pch.h"
#include "CorpseSerialization.h"
#include "CorpseTracker.h"

namespace
{
    // Unique ID for this plugin's whole co-save section. Change this only if
    // it collides with another plugin you ship alongside — otherwise never
    // change it, or old saves lose this data on load.
    constexpr std::uint32_t kSerializationID = 0x43545241;  // 'CTRA'

    // Record type for the corpse list specifically, scoped within this
    // plugin's section. Bump kCorpseRecordVersion (not this) if you ever
    // change what fields get written per entry.
    constexpr std::uint32_t kCorpseRecordType    = 0x43525053;  // 'CRPS'
    constexpr std::uint32_t kCorpseRecordVersion = 1;

    void SaveCallback(SKSE::SerializationInterface* a_intfc)
    {
        const auto corpses = CorpseTracker::Get().GetSnapshot();

        if (!a_intfc->OpenRecord(kCorpseRecordType, kCorpseRecordVersion)) {
            logger::error("Failed to open corpse tracker record for save");
            return;
        }

        const auto count = static_cast<std::uint32_t>(corpses.size());
        a_intfc->WriteRecordData(&count, sizeof(count));

        for (const auto& corpse : corpses) {
            a_intfc->WriteRecordData(&corpse.formID, sizeof(corpse.formID));
            a_intfc->WriteRecordData(&corpse.position, sizeof(corpse.position));
            a_intfc->WriteRecordData(&corpse.timestamp, sizeof(corpse.timestamp));
            a_intfc->WriteRecordData(&corpse.looted, sizeof(corpse.looted));
            // refHandle is deliberately NOT written — handles are a runtime-only
            // concept and are meaningless once the process restarts. We rebuild
            // it on load instead, from the resolved form.
        }

        logger::info("Corpse tracker: saved {} entries", count);
    }

    void LoadCallback(SKSE::SerializationInterface* a_intfc)
    {
        CorpseTracker::Get().Clear();

        std::uint32_t type, version, length;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type != kCorpseRecordType) {
                logger::warn("Corpse tracker: skipping unknown record type {:X}", type);
                continue;
            }
            if (version != kCorpseRecordVersion) {
                logger::warn("Corpse tracker: unexpected record version {} (expected {}), skipping", version, kCorpseRecordVersion);
                continue;
            }

            std::uint32_t count = 0;
            a_intfc->ReadRecordData(&count, sizeof(count));

            // Guard against a truncated or corrupt record: the declared entry
            // count cannot exceed what the record's byte length can physically
            // hold, so clamp it before trusting it to drive the read loop.
            constexpr std::size_t kEntrySize =
                sizeof(RE::FormID) + sizeof(RE::NiPoint3) + sizeof(float) + sizeof(bool);
            const std::uint32_t maxCount = length >= sizeof(std::uint32_t)
                ? static_cast<std::uint32_t>((length - sizeof(std::uint32_t)) / kEntrySize)
                : 0;
            if (count > maxCount) {
                logger::warn(
                    "Corpse tracker: record claims {} entries but its {} bytes fit only {} — clamping",
                    count, length, maxCount);
                count = maxCount;
            }

            std::uint32_t restored = 0;
            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID oldFormID = 0;
                RE::NiPoint3 position{};
                float timestamp = 0.0f;
                bool looted = false;

                a_intfc->ReadRecordData(&oldFormID, sizeof(oldFormID));
                a_intfc->ReadRecordData(&position, sizeof(position));
                a_intfc->ReadRecordData(&timestamp, sizeof(timestamp));
                a_intfc->ReadRecordData(&looted, sizeof(looted));

                RE::FormID newFormID = 0;
                if (!a_intfc->ResolveFormID(oldFormID, newFormID)) {
                    // Common and expected: the cell reset, or the ref was a
                    // temporary form that no longer exists. Just drop it.
                    logger::debug("Corpse tracker: could not resolve formID {:X}, dropping", oldFormID);
                    continue;
                }

                TrackedCorpse entry;
                entry.formID    = newFormID;
                entry.position  = position;
                entry.timestamp = timestamp;
                entry.looted    = looted;
                entry.refHandle = 0;

                if (auto* form = RE::TESForm::LookupByID(newFormID)) {
                    if (auto* ref = form->As<RE::TESObjectREFR>()) {
                        RE::CreateRefHandle(entry.refHandle, ref);
                    }
                }

                if (CorpseTracker::Get().Add(entry)) {
                    ++restored;
                }
            }

            logger::info("Corpse tracker: restored {}/{} entries from save", restored, count);
        }
    }

    void RevertCallback(SKSE::SerializationInterface*)
    {
        CorpseTracker::Get().Clear();
        logger::info("Corpse tracker: reverted (new game or pre-load)");
    }
}

void CorpseSerialization::Register()
{
    auto* serde = SKSE::GetSerializationInterface();
    if (!serde) {
        logger::critical("Failed to get SKSE serialization interface");
        return;
    }

    serde->SetUniqueID(kSerializationID);
    serde->SetSaveCallback(SaveCallback);
    serde->SetLoadCallback(LoadCallback);
    serde->SetRevertCallback(RevertCallback);

    logger::info("Corpse tracker serialization registered");
}
