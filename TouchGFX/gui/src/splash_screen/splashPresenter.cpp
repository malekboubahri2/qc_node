#include <gui/splash_screen/splashView.hpp>
#include <gui/splash_screen/splashPresenter.hpp>
#include <gui/model/Model.hpp>

splashPresenter::splashPresenter(splashView& v)
    : view(v)
{

}

bool splashPresenter::isConfigReady() const
{
    return model != nullptr && model->getOperatorCount() > 0;
}

void splashPresenter::activate()
{

}

void splashPresenter::deactivate()
{

}
