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
    /* 3 seconds at 60 Hz */
    static constexpr uint16_t ADVANCE_TICKS = 3u * 60u;
};

#endif // SPLASHVIEW_HPP
