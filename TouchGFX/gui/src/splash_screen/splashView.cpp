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
    ++m_tick_count;

    // Hold on the splash until the operator config has been fetched, so login
    // never appears empty. A minimum keeps the brand visible briefly; a maximum
    // guarantees we advance even when offline with no cached config.
    const bool minElapsed  = m_tick_count >= MIN_TICKS;
    const bool configReady = presenter->isConfigReady();
    const bool timedOut    = m_tick_count >= MAX_TICKS;

    if ((minElapsed && configReady) || timedOut)
    {
        m_tick_count = 0;
        application().gotologinScreenNoTransition();
    }
}
