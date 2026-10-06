#pragma once

#include "RE/Skyrim.h"

#include <mutex>
#include <vector>

struct TrackedCorpse
{
    RE::FormID   formID{ 0 };
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

    std::vector<TrackedCorpse> GetSnapshot() const;
    std::size_t Size() const;

private:
    CorpseTracker() = default;

    mutable std::mutex         _mutex;
    std::vector<TrackedCorpse> _corpses;
};