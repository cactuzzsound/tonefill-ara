#pragma once

namespace tonefill::plugin::ara
{
// What the *current host* actually supports, detected once at document-controller init.
// The UI reads this to enable/disable/relabel controls and choose fallback paths.
// Never branch on host *name* — only on capabilities (design §I).
struct CapabilityProfile
{
    bool araActive        = false; // an ARA document controller is present at all
    bool canReadSources   = false; // AudioReaders available for learn material
    bool hasRegionTimeline = false; // PlaybackRegion timeline mapping usable
    bool canRequestRender = false; // host honours plugin-rendered region output

    // Derived UX decisions.
    bool needsManualCapture()  const { return ! canReadSources; }
    bool needsManualDuration() const { return ! hasRegionTimeline; }
    bool exportOnly()          const { return ! canRequestRender; } // fall back to WAV export
};
} // namespace tonefill::plugin::ara
