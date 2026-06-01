#ifndef DEFECTS_INJVIEW_HPP
#define DEFECTS_INJVIEW_HPP

#include <gui_generated/defects_inj_screen/defects_injViewBase.hpp>
#include <gui/defects_inj_screen/defects_injPresenter.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/containers/buttons/BoxWithBorderButtonStyle.hpp>
#include <touchgfx/containers/buttons/ClickButtonTrigger.hpp>
#include <touchgfx/containers/buttons/TextButtonStyle.hpp>
#include <touchgfx/mixins/ClickListener.hpp>
#include <touchgfx/widgets/TextArea.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>

class defects_injView : public defects_injViewBase
{
public:
    defects_injView();
    virtual ~defects_injView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

private:
    typedef touchgfx::TextButtonStyle<
                touchgfx::BoxWithBorderButtonStyle<
                    touchgfx::ClickButtonTrigger>> ButtonBase;
    typedef touchgfx::ClickListener<ButtonBase> DefectButton;

    static constexpr int DEFECT_COUNT = 11;

    bool m_selected[DEFECT_COUNT];
    bool m_autre_selected;

    touchgfx::TextArea m_titleLabel;

    char m_preciserText[128];
    touchgfx::Unicode::UnicodeChar m_preciserBuf[128];
    touchgfx::TextAreaWithOneWildcard m_preciserDisplay;

    void updateDefectButton(DefectButton& btn, bool selected);
    void updateActionButton();

    void onDefectClicked(const ButtonBase& src, const touchgfx::ClickEvent& evt);
    void onNextClicked(const ButtonBase& src, const touchgfx::ClickEvent& evt);
    void onPreciserClicked(const touchgfx::TextArea& src, const touchgfx::ClickEvent& evt);

    touchgfx::Callback<defects_injView, const ButtonBase&, const touchgfx::ClickEvent&> m_defectCb;
    touchgfx::Callback<defects_injView, const ButtonBase&, const touchgfx::ClickEvent&> m_nextCb;
    touchgfx::Callback<defects_injView, const touchgfx::TextArea&, const touchgfx::ClickEvent&> m_preciserCb;

public:
    void receivePreciserText(const char* text);
};

#endif // DEFECTS_INJVIEW_HPP
