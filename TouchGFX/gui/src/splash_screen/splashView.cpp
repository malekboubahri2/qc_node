#include <gui/splash_screen/splashView.hpp>

splashView::splashView()
    : m_tick_count(0)
{
}

void splashView::setupScreen()
{
    splashViewBase::setupScreen();
    m_tick_count = 0;
}

void splashView::tearDownScreen()
{
    splashViewBase::tearDownScreen();
}

void splashView::handleTickEvent()
{
    if (++m_tick_count >= ADVANCE_TICKS)
    {
        m_tick_count = 0;
        application().gotologinScreenNoTransition();
    }
}
