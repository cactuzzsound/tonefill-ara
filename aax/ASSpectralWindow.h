#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/SpectralEditor.h"
#include "plugin/ui/ToneFillLookAndFeel.h"
#include "ASShared.h"
#include "ToneFillAS_Defs.h"

#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace tonefill_aax
{
// AAX spectral band editor window: hosts the shared SpectralEditorComponent, bridged to ASShared.
// The processor publishes the source into ASShared.sourcePreview; edits write ASShared.spectralEdges,
// which RenderAudio folds into the render signature so the AudioSuite fill updates.
class ASSpectralWindow : public juce::DocumentWindow
{
public:
    ASSpectralWindow (ASShared& sh, std::function<double (const char*)> getNorm)
        : juce::DocumentWindow ("ToneFill - Spectral Band Editor",
                                tonefill::plugin::ui::ToneFillLookAndFeel::bg(),
                                juce::DocumentWindow::allButtons)
    {
        setLookAndFeel (&lnf_);
        host_    = std::make_unique<Host> (sh, std::move (getNorm));
        content_ = std::make_unique<tonefill::plugin::ui::SpectralEditorComponent> (*host_);
        setContentNonOwned (content_.get(), true);
        setResizable (true, true);
        setUsingNativeTitleBar (true);
        centreWithSize (980, 560);
        setVisible (true);
    }

    ~ASSpectralWindow() override { setLookAndFeel (nullptr); clearContentComponent(); }

    void closeButtonPressed() override { if (onClose) onClose(); }
    std::function<void()> onClose;

private:
    struct Host : tonefill::plugin::ui::SpectralEditorHost
    {
        Host (ASShared& s, std::function<double (const char*)> g) : sh (s), getNorm (std::move (g)) {}

        std::shared_ptr<const Buffer> source (double& sr) override
        { const juce::SpinLock::ScopedLockType l (sh.lock); sr = sh.sourceSr; return sh.sourcePreview; }
        int sourceGen() override { const juce::SpinLock::ScopedLockType l (sh.lock); return sh.sourceGen; }
        int bandCount() override { return (int) std::lround (3.0 + getNorm (kParamBands) * 9.0); }
        std::vector<float> edges() override
        { const juce::SpinLock::ScopedLockType l (sh.lock); return sh.spectralEdges; }
        void setEdges (std::vector<float> e) override
        { const juce::SpinLock::ScopedLockType l (sh.lock); sh.spectralEdges = std::move (e); sh.spectralEdgesGen++; }

        ASShared& sh;
        std::function<double (const char*)> getNorm;
    };

    tonefill::plugin::ui::ToneFillLookAndFeel lnf_;
    std::unique_ptr<Host> host_;
    std::unique_ptr<juce::Component> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ASSpectralWindow)
};
} // namespace tonefill_aax
