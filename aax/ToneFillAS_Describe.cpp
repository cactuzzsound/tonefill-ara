#include "ToneFillAS_Defs.h"
#include "ToneFillAS_Parameters.h"
#include "ToneFillAS_HostProcessor.h"

#include "AAX_ICollection.h"
#include "AAX_IEffectDescriptor.h"
#include "AAX_IComponentDescriptor.h"
#include "AAX_IPropertyMap.h"
#include "AAX_Errors.h"
#include "AAX_Enums.h"
#include "AAX_Properties.h"

using namespace tonefill_aax;

static AAX_Result DescribeEffect (AAX_IEffectDescriptor* outDescriptor)
{
    AAX_IPropertyMap* properties = outDescriptor->NewPropertyMap();
    if (! properties) return AAX_ERROR_NULL_OBJECT;

    properties->AddProperty (AAX_eProperty_ManufacturerID, kManufactureID);
    properties->AddProperty (AAX_eProperty_ProductID, kProductID);
    properties->AddProperty (AAX_eProperty_PlugInID_AudioSuite, kPlugInID_AS);
    properties->AddProperty (AAX_eProperty_NumberOfInputs, AAX_eMaxAudioSuiteTracks);
    properties->AddProperty (AAX_eProperty_NumberOfOutputs, AAX_eMaxAudioSuiteTracks);
    // Random access -> enables the native WHOLE FILE button and GetAudio() over the whole source.
    properties->AddProperty (AAX_eProperty_UsesRandomAccess, true);

    outDescriptor->AddName ("ToneFill");
    outDescriptor->AddName ("ToneFill AS");
    outDescriptor->AddName ("TnFil");
    outDescriptor->AddName ("TnFl");
    outDescriptor->AddCategory (AAX_ePlugInCategory_NoiseReduction);

    outDescriptor->AddProcPtr (reinterpret_cast<void*> (ToneFillAS_Parameters::Create),    kAAX_ProcPtrID_Create_EffectParameters);
    outDescriptor->AddProcPtr (reinterpret_cast<void*> (ToneFillAS_HostProcessor::Create), kAAX_ProcPtrID_Create_HostProcessor);

    return outDescriptor->SetProperties (properties);
}

AAX_Result GetEffectDescriptions (AAX_ICollection* outCollection)
{
    AAX_IEffectDescriptor* descriptor = outCollection->NewDescriptor();
    if (! descriptor) return AAX_ERROR_NULL_OBJECT;

    AAX_Result err = DescribeEffect (descriptor);
    if (err == AAX_SUCCESS)
        err = outCollection->AddEffect ("com.cactuzzsound.tonefill.audiosuite", descriptor);

    outCollection->SetManufacturerName ("Cactuzz Sound");
    outCollection->AddPackageName ("ToneFill");
    outCollection->AddPackageName ("ToneFill AudioSuite");
    outCollection->AddPackageName ("TnFil");
    outCollection->SetPackageVersion (1);
    return err;
}
