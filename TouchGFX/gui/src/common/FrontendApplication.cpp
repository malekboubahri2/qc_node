#include <gui/common/FrontendApplication.hpp>
#include <gui/common/FrontendHeap.hpp>
#include <gui/defects_pmp_screen/defects_pmpView.hpp>
#include <gui/defects_pmp_screen/defects_pmpPresenter.hpp>
#include <gui/input_type_screen/input_typeView.hpp>
#include <gui/input_type_screen/input_typePresenter.hpp>
#include <touchgfx/transitions/NoTransition.hpp>

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap)
{
}

void FrontendApplication::gotodefects_pmpScreenNoTransition()
{
    m_defects_pmpCallback = touchgfx::Callback<FrontendApplication>(
        this, &FrontendApplication::gotodefects_pmpScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &m_defects_pmpCallback;
}

void FrontendApplication::gotodefects_pmpScreenNoTransitionImpl()
{
    touchgfx::makeTransition<defects_pmpView, defects_pmpPresenter,
                             touchgfx::NoTransition, Model>(
        &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoinput_typeScreenNoTransition()
{
    m_input_typeCallback = touchgfx::Callback<FrontendApplication>(
        this, &FrontendApplication::gotoinput_typeScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &m_input_typeCallback;
}

void FrontendApplication::gotoinput_typeScreenNoTransitionImpl()
{
    touchgfx::makeTransition<input_typeView, input_typePresenter,
                             touchgfx::NoTransition, Model>(
        &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}
