#ifndef LOGINPRESENTER_HPP
#define LOGINPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class loginView;

class loginPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    loginPresenter(loginView& v);

    virtual void activate();
    virtual void deactivate();
    virtual ~loginPresenter() {}

    /* Called by the View when a digit key is tapped. */
    void digitPressed(int digit);

private:
    loginPresenter();

    loginView& view;

    char m_pin[Model::PIN_MAX_LEN + 1];
    int  m_digit_count;

    void submitPin();
    void clearPin();
};

#endif // LOGINPRESENTER_HPP
