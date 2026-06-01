#ifndef SUMMARYPRESENTER_HPP
#define SUMMARYPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class summaryView;

class summaryPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    summaryPresenter(summaryView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();
    virtual void deactivate();

    virtual ~summaryPresenter() {}

private:
    summaryPresenter();

    summaryView& view;
};

#endif // SUMMARYPRESENTER_HPP
