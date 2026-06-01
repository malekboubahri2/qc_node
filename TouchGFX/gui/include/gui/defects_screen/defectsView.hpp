#ifndef DEFECTSVIEW_HPP
#define DEFECTSVIEW_HPP

#include <gui_generated/defects_screen/defectsViewBase.hpp>
#include <gui/defects_screen/defectsPresenter.hpp>

class defectsView : public defectsViewBase
{
public:
    defectsView();
    virtual ~defectsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // DEFECTSVIEW_HPP
