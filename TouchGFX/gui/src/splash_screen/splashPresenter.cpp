#include <gui/splash_screen/splashView.hpp>
#include <gui/splash_screen/splashPresenter.hpp>
#include <gui/model/Model.hpp>

splashPresenter::splashPresenter(splashView& v)
    : view(v)
{

}

bool splashPresenter::isConfigReady() const
{
    // Wait for BOTH the operator list (needed by login) and the product /
    // defect-type config (needed by the defect grid). Releasing on operators
    // alone let the operator reach the grid before the larger products config
    // was parsed, so defect selections resolved against an empty type list and
    // committed nothing. Products and their defect types are applied together.
    return model != nullptr
        && model->getOperatorCount() > 0
        && model->getProductCount() > 0;
}

void splashPresenter::activate()
{

}

void splashPresenter::deactivate()
{

}
