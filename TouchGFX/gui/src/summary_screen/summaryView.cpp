#include <gui/summary_screen/summaryView.hpp>
#include <touchgfx/Color.hpp>
#include <touchgfx/Unicode.hpp>

summaryView::summaryView()
    : m_signoutCb(this, &summaryView::onSignoutClicked),
      m_nextPieceCb(this, &summaryView::onNextPieceClicked),
      m_changeProductCb(this, &summaryView::onChangeProductClicked)
{
    m_countBuf[0] = 0;
    m_nameBuf[0]  = 0;

    count.setWildcard(m_countBuf);
    username.setWildcard(m_nameBuf);

    logout.setClickAction(m_signoutCb);
    next_product.setClickAction(m_nextPieceCb);
    change_product.setClickAction(m_changeProductCb);
}

void summaryView::setupScreen()
{
    summaryViewBase::setupScreen();
}

void summaryView::tearDownScreen()
{
    summaryViewBase::tearDownScreen();
}

void summaryView::setDisplayData(int defectCount, const char* operatorName)
{
    touchgfx::Unicode::snprintf(m_countBuf, COUNT_BUF_SIZE, "%d", defectCount);

    size_t i = 0;
    while (operatorName[i] && i < NAME_BUF_SIZE - 1)
    {
        m_nameBuf[i] = static_cast<touchgfx::Unicode::UnicodeChar>(
            static_cast<unsigned char>(operatorName[i]));
        ++i;
    }
    m_nameBuf[i] = 0;

    count.invalidate();
    username.invalidate();
}

void summaryView::onSignoutClicked(const NavBtnBase& /*src*/, const touchgfx::ClickEvent& evt)
{
    if (evt.getType() != touchgfx::ClickEvent::RELEASED)
        return;
    application().gotologinScreenNoTransition();
}

void summaryView::onNextPieceClicked(const NavBtnBase& /*src*/, const touchgfx::ClickEvent& evt)
{
    if (evt.getType() != touchgfx::ClickEvent::RELEASED)
        return;
    application().gotodefects_pmpScreenNoTransition();
}

void summaryView::onChangeProductClicked(const NavBtnBase& /*src*/, const touchgfx::ClickEvent& evt)
{
    if (evt.getType() != touchgfx::ClickEvent::RELEASED)
        return;
    application().gotoproductRefScreenNoTransition();
}
