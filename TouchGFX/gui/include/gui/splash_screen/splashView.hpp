#ifndef SPLASHVIEW_HPP
#define SPLASHVIEW_HPP

#include <gui_generated/splash_screen/splashViewBase.hpp>
#include <gui/splash_screen/splashPresenter.hpp>

class splashView : public splashViewBase
{
public:
    splashView();
    virtual ~splashView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

private:
    uint16_t m_tick_count;
    /* Show the splash at least this long for branding, then advance as soon as
     * the operator config has arrived. (~1 s at 60 Hz) */
    static constexpr uint16_t MIN_TICKS = 1u * 60u;
    /* Fall back to login even without config so a first boot with no cached
     * config or no network never hangs on the splash. (~20 s at 60 Hz) */
    static constexpr uint16_t MAX_TICKS = 20u * 60u;
};

#endif // SPLASHVIEW_HPP
