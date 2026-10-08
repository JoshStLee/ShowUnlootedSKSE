#include "pch.h"
#include "CorpseTracker.h"

#include <algorithm>

namespace
{
    // Two entries identify the same corpse when they share a live reference
    // handle. Runtime FormIDs can be recycled onto a different reference, so a
    // matching FormID alone is not proof of a duplicate; only fall back to the
    // FormID comparison when a handle is unavailable (e.g. an entry restored
    // from a save before its reference could be resolved).
    bool SameCorpse(const TrackedCorpse& a_lhs, const TrackedCorpse& a_rhs)
    {
        if (a_lhs.refHandle != 0 && a_rhs.refHandle != 0) {
            return a_lhs.refHandle == a_rhs.refHandle;
        }

        return a_lhs.formID == a_rhs.formID;
    }
}

CorpseTracker& CorpseTracker::Get()
{
    static CorpseTracker singleton;
    return singleton;
}

bool CorpseTracker::Add(const TrackedCorpse& entry)
{
    std::lock_guard lock(_mutex);

    const auto already = std::find_if(
        _corpses.begin(),
        _corpses.end(),
        [&](const TrackedCorpse& c) { return SameCorpse(c, entry); });

    if (already != _corpses.end()) {
        return false;
    }

    _corpses.push_back(entry);
    return true;
}

bool CorpseTracker::SetLooted(RE::FormID a_formID)
{
    std::lock_guard lock(_mutex);

    const auto corpse = std::find_if(
        _corpses.begin(),
        _corpses.end(),
        [&](const TrackedCorpse& c) { return c.formID == a_formID; });

    if (corpse == _corpses.end()) {
        return false;
    }

    _corpses.erase(corpse);
    return true;
}

std::vector<TrackedCorpse> CorpseTracker::GetSnapshot() const
{
    std::lock_guard lock(_mutex);
    return _corpses;
}

void CorpseTracker::GetSnapshotInto(std::vector<TrackedCorpse>& a_out) const
{
    std::lock_guard lock(_mutex);
    // vector::operator= reuses a_out's existing storage when it is already
    // large enough, so a caller that keeps one buffer across frames stops
    // allocating after the first few calls.
    a_out = _corpses;
}

std::size_t CorpseTracker::Size() const
{
    std::lock_guard lock(_mutex);
    return _corpses.size();
}

void CorpseTracker::Clear()
{
    std::lock_guard lock(_mutex);
    _corpses.clear();
}
