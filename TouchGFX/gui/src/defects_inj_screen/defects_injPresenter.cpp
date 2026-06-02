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

void defects_injPresenter::logDefectInspection(int buttonIndex, bool isOther, const char* note)
{
    const int productId = model->getCurrentProductId();
    int count = 0;
    const defect_type_t* types =
        model->getDefectTypes(productId, DEFECT_CONFIG_CATEGORY_INJ, &count);
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

void defects_injPresenter::logOkInspection()
{
    /* Enqueue inspection event via Model → mqtt_task
     * outcome="OK", no defect_type_id */
    model->enqueueInspection(
        model->getCurrentProductId(),
        model->getOperator(model->getCurrentOperatorIdx()).id,
        "OK", -1, "");
}
