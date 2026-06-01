#ifndef LOGINVIEW_HPP
#define LOGINVIEW_HPP

#include <gui_generated/login_screen/loginViewBase.hpp>
#include <gui/login_screen/loginPresenter.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/containers/buttons/BoxWithBorderButtonStyle.hpp>
#include <touchgfx/containers/buttons/ClickButtonTrigger.hpp>
#include <touchgfx/containers/buttons/TextButtonStyle.hpp>

class loginView : public loginViewBase
{
public:
    loginView();
    virtual ~loginView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

    /* Called by Presenter. */
    void setDigitCount(int n);
    void showError();
    void gotoProductRefScreen();

private:
    typedef touchgfx::TextButtonStyle<
        touchgfx::BoxWithBorderButtonStyle<touchgfx::ClickButtonTrigger>> KeypadButton;

    void onKeyClicked(const KeypadButton& src, const touchgfx::ClickEvent& evt); // fires on PRESSED+RELEASED; handler filters to RELEASED only

    touchgfx::Callback<loginView, const KeypadButton&, const touchgfx::ClickEvent&> m_keyCallback;
    int m_error_ticks;
};

#endif // LOGINVIEW_HPP
