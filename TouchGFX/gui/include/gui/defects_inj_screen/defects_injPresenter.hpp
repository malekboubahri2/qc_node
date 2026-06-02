#ifndef DEFECTS_INJPRESENTER_HPP
#define DEFECTS_INJPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class defects_injView;

class defects_injPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    defects_injPresenter(defects_injView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    /* Called by the View when the operator confirms. buttonIndex is the 0-based
     * grid position; isOther marks the "Autre — préciser" button. The real
     * server-side defect_type_id is resolved from the product's INJECTION config. */
    void logDefectInspection(int buttonIndex, bool isOther, const char* note);
    void logOkInspection();

    /* Préciser keyboard round-trip. */
    void openKeyboardForPreciser();
    void checkPendingPreciserText();

    virtual ~defects_injPresenter() {}

private:
    defects_injPresenter();

    defects_injView& view;
};

#endif // DEFECTS_INJPRESENTER_HPP
