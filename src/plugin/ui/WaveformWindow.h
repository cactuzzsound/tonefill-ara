#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"

#include <functional>
#include <memory>

namespace tonefill::plugin { struct SessionState; }

namespace tonefill::plugin::ui
{
// Large, resizable waveform view: timecode ruler, horizontal + vertical zoom, scroll, and
// drag-to-select room-tone regions. Selections are shared with the main view through
// SessionState (the single source of truth), so edits here and there stay in sync.
class WaveformWindow : public juce::DocumentWindow
{
public:
    explicit WaveformWindow (SessionState& state);
    ~WaveformWindow() override;

    void closeButtonPressed() override;
    std::function<void()> onClose;

private:
    ToneFillLookAndFeel lnf_;
    std::unique_ptr<juce::Component> content_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformWindow)
};
} // namespace tonefill::plugin::ui
