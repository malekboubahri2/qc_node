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
    view.setDisplayData(model->getSessionDefectCount(), name);
}

void summaryPresenter::deactivate()
{
}
