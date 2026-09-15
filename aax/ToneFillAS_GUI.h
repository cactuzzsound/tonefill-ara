#pragma once

#include "AAX_CEffectGUI.h"

#include <juce_events/juce_events.h>

#include <memory>

namespace tonefill_aax { class ASView; }

// AAX GUI object: hosts a JUCE ASView inside Pro Tools' native view (NSView on macOS, HWND on
// Windows). This is a NON-JUCE AAX client, so it manages JUCE's GUI subsystem itself via
// ScopedJuceInitialiser_GUI. Parameters are read/written through GetEffectParameters().
class ToneFillAS_GUI : public AAX_CEffectGUI
{
public:
    static AAX_IEffectGUI* AAX_CALLBACK Create();
    ToneFillAS_GUI();
    ~ToneFillAS_GUI() override;

    void       CreateViewContents () override;
    void       CreateViewContainer () override;
    void       DeleteViewContainer () override;
    AAX_Result GetViewSize (AAX_Point* oViewSize) const override;
    AAX_Result ParameterUpdated (AAX_CParamID iParameterID) override;

private:
    juce::ScopedJuceInitialiser_GUI mJuceInit; // must outlive the component
    std::unique_ptr<tonefill_aax::ASView> mView;
};
