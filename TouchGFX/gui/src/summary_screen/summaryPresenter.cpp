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

    /* Read the part's defect count before publishing (publish clears it). */
    view.setDisplayData(model->getInspectionDefectCount(), name);

    /* Publish the full part inspection (PMP + INJ) exactly once. */
    model->publishInspection();
}

void summaryPresenter::deactivate()
{
}
