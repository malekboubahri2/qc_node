#include <gui/defects_pmp_screen/defects_pmpView.hpp>
#include <gui/defects_pmp_screen/defects_pmpPresenter.hpp>
#include <gui/model/Model.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

defects_pmpPresenter::defects_pmpPresenter(defects_pmpView& v)
    : view(v)
{
}

void defects_pmpPresenter::activate()
{
    checkPendingPreciserText();
}

void defects_pmpPresenter::deactivate()
{
}

void defects_pmpPresenter::openKeyboardForPreciser()
{
    model->setPreciserOrigin(Model::PreciserOrigin::PMP_DEFECTS);
    model->setPreciserPendingText("");
    static_cast<FrontendApplication*>(touchgfx::Application::getInstance())
        ->gotoinput_typeScreenNoTransition();
}

void defects_pmpPresenter::checkPendingPreciserText()
{
    if (model->getPreciserOrigin() == Model::PreciserOrigin::PMP_DEFECTS)
    {
        view.receivePreciserText(model->getPreciserPendingText());
        model->clearPreciser();
    }
}

void defects_pmpPresenter::logDefectInspection(int defectTypeId, const char* note)
{
    /* Enqueue inspection event via Model → mqtt_task
     * outcome="DEFECT", defect_type_id=defectTypeId, note=note */
    model->enqueueInspection(
        model->getCurrentProductId(),
        model->getOperator(model->getCurrentOperatorIdx()).id,
        "DEFECT", defectTypeId, note);
}

void defects_pmpPresenter::logOkInspection()
{
    /* Enqueue inspection event via Model → mqtt_task
     * outcome="OK", no defect_type_id */
    model->enqueueInspection(
        model->getCurrentProductId(),
        model->getOperator(model->getCurrentOperatorIdx()).id,
        "OK", -1, "");
}
