#ifndef INPUT_TYPEPRESENTER_HPP
#define INPUT_TYPEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class input_typeView;

class input_typePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    input_typePresenter(input_typeView& v);

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

    /* Called by View when the operator taps "Suivant" on the keyboard. */
    void finishPreciser(const char* text);

    virtual ~input_typePresenter() {}

private:
    input_typePresenter();

    input_typeView& view;
};

#endif // INPUT_TYPEPRESENTER_HPP
