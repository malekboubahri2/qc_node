#include <gui/defects_inj_screen/defects_injView.hpp>
#include <gui/defects_inj_screen/defects_injPresenter.hpp>
#include <gui/model/Model.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

defects_injPresenter::defects_injPresenter(defects_injView& v)
    : view(v)
{
}

void defects_injPresenter::activate()
{
    checkPendingPreciserText();
}

void defects_injPresenter::deactivate()
{
}

void defects_injPresenter::openKeyboardForPreciser()
{
    model->setPreciserOrigin(Model::PreciserOrigin::INJ_DEFECTS);
    model->setPreciserPendingText("");
    static_cast<FrontendApplication*>(touchgfx::Application::getInstance())
        ->gotoinput_typeScreenNoTransition();
}

void defects_injPresenter::checkPendingPreciserText()
{
    if (model->getPreciserOrigin() == Model::PreciserOrigin::INJ_DEFECTS)
    {
        view.receivePreciserText(model->getPreciserPendingText());
        model->clearPreciser();
    }
}

void defects_injPresenter::logDefectInspection(int defectTypeId, const char* note)
{
    /* Enqueue inspection event via Model → mqtt_task
     * outcome="DEFECT", defect_type_id=defectTypeId, note=note */
    model->enqueueInspection(
        model->getCurrentProductId(),
        model->getOperator(model->getCurrentOperatorIdx()).id,
        "DEFECT", defectTypeId, note);
}

void defects_injPresenter::logOkInspection()
{
    /* Enqueue inspection event via Model → mqtt_task
     * outcome="OK", no defect_type_id */
    model->enqueueInspection(
        model->getCurrentProductId(),
        model->getOperator(model->getCurrentOperatorIdx()).id,
        "OK", -1, "");
}
