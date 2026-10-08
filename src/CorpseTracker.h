#pragma once

#include "RE/Skyrim.h"

#include <mutex>
#include <vector>

struct TrackedCorpse
{
    RE::FormID   formID{ 0 };
    RE::RefHandle refHandle{ 0 };
    RE::NiPoint3 position{};
    float        timestamp{ 0.0f };
    bool         looted{ false };
};

class CorpseTracker
{
public:
    static CorpseTracker& Get();

    // Returns true if newly added, false if already tracked
    bool Add(const TrackedCorpse& entry);

    // Removes the tracked corpse with the given FormID when it is looted.
    // Returns true if a matching entry was removed.
    bool SetLooted(RE::FormID a_formID);

    std::vector<TrackedCorpse> GetSnapshot() const;

    // Fills a caller-owned buffer instead of returning a new vector, so hot
    // per-frame callers can reuse one buffer and avoid reallocating each time.
    void GetSnapshotInto(std::vector<TrackedCorpse>& a_out) const;

    std::size_t Size() const;
    void Clear();
private:
    CorpseTracker() = default;

    mutable std::mutex         _mutex;
    std::vector<TrackedCorpse> _corpses;
};
