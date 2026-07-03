#pragma once

#include "AAX_CEffectParameters.h"

// Parameter / GUI side of the AudioSuite plug-in. The actual processing is done by the
// HostProcessor; this just declares the controls Pro Tools shows.
class ToneFillAS_Parameters : public AAX_CEffectParameters
{
public:
    static AAX_CEffectParameters* AAX_CALLBACK Create();
    AAX_Result EffectInit() override;
};
