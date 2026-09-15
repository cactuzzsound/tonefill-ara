#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"

#include <functional>
#include <memory>

namespace tonefill::plugin { struct SessionState; }

namespace tonefill::plugin::ui
{
// Spectral band editor: a large, resizable window that shows a spectrogram of the source (RX / SpectraLayers
// style, log-frequency, colour-mapped) with draggable horizontal band-edge lines. The number of edges
// follows the Bands knob; dragging an edge sets that band's frequency boundary. Edges are shared with the
// worker through SessionState (spectralEdges) so the Spectral render updates live.
class SpectralEditorWindow : public juce::DocumentWindow
{
public:
    // setBandCount pushes the auto/edge-derived band count back onto the plugin's Bands parameter.
    explicit SpectralEditorWindow (SessionState& state, std::function<void (int)> setBandCount = {});
    ~SpectralEditorWindow() override;

    void closeButtonPressed() override;
    std::function<void()> onClose;

private:
    struct SessionHost;
    ToneFillLookAndFeel lnf_;
    std::unique_ptr<SessionHost>     host_;
    std::unique_ptr<juce::Component> content_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralEditorWindow)
};
} // namespace tonefill::plugin::ui
