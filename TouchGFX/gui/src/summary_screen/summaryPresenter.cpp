#include <gui/summary_screen/summaryView.hpp>
#include <gui/summary_screen/summaryPresenter.hpp>
#include <gui/model/Model.hpp>

summaryPresenter::summaryPresenter(summaryView& v)
    : view(v)
{
}

void summaryPresenter::activate()
{
    int idx = model->getCurrentOperatorIdx();
    const char* name = (idx >= 0) ? model->getOperator(idx).name : "---";

    /* Publish the full part inspection (PMP + INJ) exactly once; this also
     * bumps the session parts-inspected counter. */
    model->publishInspection();

    /* Show how many parts have been inspected since login. */
    view.setDisplayData(model->getSessionInspectedCount(), name);
}

void summaryPresenter::deactivate()
{
}
