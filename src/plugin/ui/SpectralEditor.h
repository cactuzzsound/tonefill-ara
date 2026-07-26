#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace tonefill::plugin::ui
{
// Data bridge for the spectral band editor, so the same UI works over the VST/ARA SessionState and the
// AAX ASShared. The editor reads the analysed source (for the spectrogram) and the band count, and
// reads/writes the explicit band-edge frequencies (Hz).
struct SpectralEditorHost
{
    using Buffer = std::vector<std::vector<float>>; // [channel][sample]
    virtual ~SpectralEditorHost() = default;

    virtual std::shared_ptr<const Buffer> source (double& sampleRate) = 0; // raw analysed source, or null
    virtual int  sourceGen() = 0;   // changes whenever the source changes (triggers a spectrogram rebuild)
    virtual int  bandCount() = 0;   // 3..12
    virtual std::vector<float> edges() = 0;         // current band edges (Hz)
    virtual void setEdges (std::vector<float>) = 0; // commit edges + trigger a re-render
};

// The full editor: a spectrogram view (STFT, log-frequency, colour-mapped) with draggable band-edge
// lines, plus a toolbar (H/V zoom, Fit, pan Speed, Bright). Host-agnostic.
class SpectralEditorComponent : public juce::Component
{
public:
    explicit SpectralEditorComponent (SpectralEditorHost& host);
    ~SpectralEditorComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class View; // spectrogram + edges (defined in the .cpp)
    std::unique_ptr<View> view_;
    juce::TextButton hInBtn_, hOutBtn_, vInBtn_, vOutBtn_, fitBtn_;
    juce::Slider speed_, bright_;
    juce::Label speedLbl_, brightLbl_, hint_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralEditorComponent)
};
} // namespace tonefill::plugin::ui
