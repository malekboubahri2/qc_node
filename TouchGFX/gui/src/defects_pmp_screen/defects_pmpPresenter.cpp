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

void defects_pmpPresenter::commitSelection(const bool* selected, int count,
                                           bool autreSelected, const char* note)
{
    int typesCount = 0;
    const defect_type_t* types =
        model->getDefectTypes(model->getCurrentProductId(),
                              DEFECT_CONFIG_CATEGORY_PMP, &typesCount);

    /* Resolve each selected grid button to its real defect_type_id. Regular
     * (non-fallback) types fill the first buttons in display order; the last
     * button is the "Autre" fallback. */
    int ids[INSPECTION_MAX_DEFECTS];
    int n = 0;
    for (int i = 0; i < count && n < INSPECTION_MAX_DEFECTS; ++i)
    {
        if (!selected[i] || types == nullptr || typesCount <= 0)
            continue;

        int id = -1;
        if (i == count - 1) /* "Autre" button */
        {
            for (int k = 0; k < typesCount; ++k)
                if (types[k].is_other) { id = types[k].id; break; }
        }
        else
        {
            int regular = 0;
            for (int k = 0; k < typesCount; ++k)
            {
                if (types[k].is_other) continue;
                if (regular == i) { id = types[k].id; break; }
                ++regular;
            }
        }
        if (id >= 0)
            ids[n++] = id;
    }

    model->setCategoryDefects(DEFECT_CONFIG_CATEGORY_PMP, ids, n,
                              autreSelected ? note : "");
}
