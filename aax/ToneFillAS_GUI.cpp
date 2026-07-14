#include "ToneFillAS_GUI.h"
#include "ASView.h"

#include "AAX_IEffectParameters.h"
#include "AAX_GUITypes.h"

using namespace tonefill_aax;

AAX_IEffectGUI* AAX_CALLBACK ToneFillAS_GUI::Create() { return new ToneFillAS_GUI(); }

ToneFillAS_GUI::ToneFillAS_GUI() = default;
ToneFillAS_GUI::~ToneFillAS_GUI() { DeleteViewContainer(); }

void ToneFillAS_GUI::CreateViewContents()
{
    if (mView != nullptr) return;

    ASView::Bridge bridge;
    bridge.getNorm = [this] (const char* id) -> double
    {
        double v = 0.0;
        if (auto* p = GetEffectParameters()) p->GetParameterNormalizedValue (id, &v);
        return v;
    };
    bridge.setNorm = [this] (const char* id, double v)
    {
        if (auto* p = GetEffectParameters()) p->SetParameterNormalizedValue (id, v);
    };
    mView = std::make_unique<ASView> (std::move (bridge));
}

void ToneFillAS_GUI::CreateViewContainer()
{
    CreateViewContents();
    if (mView == nullptr) return;

    if (void* native = GetViewContainerPtr())
    {
       #if JUCE_MAC
        if (GetViewContainerType() == AAX_eViewContainer_Type_NSView)
       #else
        if (GetViewContainerType() == AAX_eViewContainer_Type_HWND)
       #endif
        {
            mView->setVisible (true);
            mView->addToDesktop (0, native); // host the JUCE component in PT's native view
        }
    }
}

void ToneFillAS_GUI::DeleteViewContainer()
{
    if (mView != nullptr)
    {
        JUCE_AUTORELEASEPOOL
        {
            mView->removeFromDesktop();
            mView.reset();
        }
    }
}

AAX_Result ToneFillAS_GUI::GetViewSize (AAX_Point* oViewSize) const
{
    if (mView == nullptr || oViewSize == nullptr) return AAX_ERROR_NULL_OBJECT;
    oViewSize->vert = (float) mView->getHeight();
    oViewSize->horz = (float) mView->getWidth();
    return AAX_SUCCESS;
}

AAX_Result ToneFillAS_GUI::ParameterUpdated (AAX_CParamID)
{
    // ASView polls the parameters on a timer, so nothing to push here.
    return AAX_SUCCESS;
}
