#ifndef PRODUCTREFPRESENTER_HPP
#define PRODUCTREFPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class productRefView;

class productRefPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    productRefPresenter(productRefView& v);

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

    virtual ~productRefPresenter() {}

private:
    productRefPresenter();

    productRefView& view;
};

#endif // PRODUCTREFPRESENTER_HPP
