#pragma once

#include "AAX_CEffectParameters.h"

#include "ASShared.h"

// Parameter / GUI side of the AudioSuite plug-in. The actual processing is done by the
// HostProcessor; this just declares the controls Pro Tools shows. It also OWNS the shared state
// bridging the processor and the GUI (both reach it via GetEffectParameters()).
class ToneFillAS_Parameters : public AAX_CEffectParameters
{
public:
    static AAX_CEffectParameters* AAX_CALLBACK Create();
    AAX_Result EffectInit() override;

    tonefill_aax::ASShared& shared() { return mShared; }

private:
    tonefill_aax::ASShared mShared;
};
