#ifndef SUMMARYVIEW_HPP
#define SUMMARYVIEW_HPP

#include <gui_generated/summary_screen/summaryViewBase.hpp>
#include <gui/summary_screen/summaryPresenter.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/Unicode.hpp>
#include <stddef.h>

class summaryView : public summaryViewBase
{
public:
    summaryView();
    virtual ~summaryView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void transitionBegins() {}
    void setDisplayData(int defectCount, const char* operatorName);

private:
    static const size_t COUNT_BUF_SIZE = 16;
    static const size_t NAME_BUF_SIZE  = 32;

    touchgfx::Unicode::UnicodeChar m_countBuf[COUNT_BUF_SIZE];
    touchgfx::Unicode::UnicodeChar m_nameBuf[NAME_BUF_SIZE];

    void onSignoutClicked(const NavBtnBase& src, const touchgfx::ClickEvent& evt);
    void onNextPieceClicked(const NavBtnBase& src, const touchgfx::ClickEvent& evt);
    void onChangeProductClicked(const NavBtnBase& src, const touchgfx::ClickEvent& evt);

    touchgfx::Callback<summaryView, const NavBtnBase&, const touchgfx::ClickEvent&> m_signoutCb;
    touchgfx::Callback<summaryView, const NavBtnBase&, const touchgfx::ClickEvent&> m_nextPieceCb;
    touchgfx::Callback<summaryView, const NavBtnBase&, const touchgfx::ClickEvent&> m_changeProductCb;
};

#endif // SUMMARYVIEW_HPP
