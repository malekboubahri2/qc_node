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

void defects_pmpPresenter::logDefectInspection(int buttonIndex, bool isOther, const char* note)
{
    const int productId = model->getCurrentProductId();
    int count = 0;
    const defect_type_t* types =
        model->getDefectTypes(productId, DEFECT_CONFIG_CATEGORY_PMP, &count);
    if (types == nullptr || count <= 0)
        return;

    /* Map the grid position to the real server-side defect_type_id. Regular
     * (non-fallback) types fill the first buttons in display order; the last
     * button is the "Autre" fallback. */
    int defectTypeId = -1;
    if (isOther)
    {
        for (int i = 0; i < count; ++i)
            if (types[i].is_other) { defectTypeId = types[i].id; break; }
    }
    else
    {
        int regular = 0;
        for (int i = 0; i < count; ++i)
        {
            if (types[i].is_other) continue;
            if (regular == buttonIndex) { defectTypeId = types[i].id; break; }
            ++regular;
        }
    }

    if (defectTypeId < 0)
        return; /* no defect type configured for this button */

    model->enqueueInspection(
        productId,
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
