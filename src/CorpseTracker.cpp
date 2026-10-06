#include "pch.h"
#include "CorpseTracker.h"

#include <algorithm>

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
        [&](const TrackedCorpse& c) { return c.formID == entry.formID; });

    if (already != _corpses.end()) {
        return false;
    }

    _corpses.push_back(entry);
    return true;
}

std::vector<TrackedCorpse> CorpseTracker::GetSnapshot() const
{
    std::lock_guard lock(_mutex);
    return _corpses;
}

std::size_t CorpseTracker::Size() const
{
    std::lock_guard lock(_mutex);
    return _corpses.size();
}