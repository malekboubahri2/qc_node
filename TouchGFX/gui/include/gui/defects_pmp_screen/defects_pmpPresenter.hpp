#ifndef DEFECTS_PMPPRESENTER_HPP
#define DEFECTS_PMPPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class defects_pmpView;

class defects_pmpPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    defects_pmpPresenter(defects_pmpView& v);

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

    /* Called by the View when the operator confirms. */
    void logDefectInspection(int defectTypeId, const char* note);
    void logOkInspection();

    /* Préciser keyboard round-trip. */
    void openKeyboardForPreciser();
    void checkPendingPreciserText();

    virtual ~defects_pmpPresenter() {}

private:
    defects_pmpPresenter();

    defects_pmpView& view;
};

#endif // DEFECTS_PMPPRESENTER_HPP
