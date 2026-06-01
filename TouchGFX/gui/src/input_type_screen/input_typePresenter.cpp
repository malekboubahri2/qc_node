#include <gui/input_type_screen/input_typeView.hpp>
#include <gui/input_type_screen/input_typePresenter.hpp>
#include <gui/model/Model.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

input_typePresenter::input_typePresenter(input_typeView& v)
    : view(v)
{
}

void input_typePresenter::activate()
{
}

void input_typePresenter::deactivate()
{
}

void input_typePresenter::finishPreciser(const char* text)
{
    model->setPreciserPendingText(text);

    auto* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
    switch (model->getPreciserOrigin())
    {
        case Model::PreciserOrigin::PMP_DEFECTS:
            app->gotodefects_pmpScreenNoTransition();
            break;
        case Model::PreciserOrigin::INJ_DEFECTS:
            app->gotodefects_injScreenNoTransition();
            break;
        default:
            app->gotoproductRefScreenNoTransition();
            break;
    }
}
