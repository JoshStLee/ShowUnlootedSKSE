# ShowUnlootedSKSE plugin

This is a plugin made by SKSE and CommonlibSSE NG with the purpose of showing Actor NPCs killed by the player and his/her teammates which haven't been looted yet. The code references Compass Navigation Overhaul repository (https://github.com/alexsylex/CompassNavigationOverhaul/tree/main) to develop hooks that interact with the compass UI.

## TODOs 
__1. `looted` is never set to `true` anywhere. The field exists, is serialized, defaults false — but nothing ever flips it. Your plugin is literally named *looted*, and the "unlooted" half isn't implemented yet. When you get to it, the natural trigger is a `TESContainerChangedEvent`/ `TESActivateEvent` or the actor's container being opened; whatever you pick needs to write back through a `SetLooted(formID)` on the tracker. That's the core feature gap, not a bug.

__2. Make UI overhauls of HUDMenu (oh bother)

