#include "plugin/ui/SpectralEditorWindow.h"
#include "plugin/ui/SpectralEditor.h"
#include "plugin/SessionState.h"

namespace tonefill::plugin::ui
{
// Adapts the VST/ARA SessionState to the host-agnostic spectral editor.
struct SpectralEditorWindow::SessionHost : SpectralEditorHost
{
    SessionHost (SessionState& s, std::function<void (int)> setBands)
        : state (s), setBandsParam (std::move (setBands)) {}

    std::shared_ptr<const Buffer> source (double& sr) override { return state.getSourcePreview (sr); }
    int  sourceGen() override { return state.sourceSamples.load(); }
    int  bandCount() override { return state.spectralBands.load(); }
    std::vector<float> edges() override { return state.getSpectralEdges(); }
    void setEdges (std::vector<float> e) override { state.setSpectralEdges (std::move (e)); state.generation.fetch_add (1); }
    int  edgesGen() override { return state.spectralEdgesGen.load(); }
    void setBandCount (int n) override { if (setBandsParam) setBandsParam (juce::jlimit (3, 12, n)); }
    void requestAutoBands() override { state.autoBandsRequest.fetch_add (1); }

    SessionState& state;
    std::function<void (int)> setBandsParam;
};

//==============================================================================
SpectralEditorWindow::SpectralEditorWindow (SessionState& state, std::function<void (int)> setBandCount)
    : juce::DocumentWindow ("ToneFill - Spectral Band Editor", ToneFillLookAndFeel::bg(), juce::DocumentWindow::allButtons)
{
    setLookAndFeel (&lnf_);
    host_    = std::make_unique<SessionHost> (state, std::move (setBandCount));
    content_ = std::make_unique<SpectralEditorComponent> (*host_);
    setContentNonOwned (content_.get(), true);
    setResizable (true, true);
    setUsingNativeTitleBar (true);
    centreWithSize (980, 560);
    setVisible (true);
}

SpectralEditorWindow::~SpectralEditorWindow()
{
    setLookAndFeel (nullptr);
    clearContentComponent();
}

void SpectralEditorWindow::closeButtonPressed() { if (onClose) onClose(); }
} // namespace tonefill::plugin::ui
