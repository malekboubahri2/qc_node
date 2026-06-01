#include <gui/defects_pmp_screen/defects_pmpView.hpp>
#include <gui/common/pmp_colors.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>
#include <touchgfx/Unicode.hpp>
#include <string.h>

defects_pmpView::defects_pmpView()
    : m_autre_selected(false),
      m_defectCb(this, &defects_pmpView::onDefectClicked),
      m_nextCb(this, &defects_pmpView::onNextClicked),
      m_preciserCb(this, &defects_pmpView::onPreciserClicked)
{
    for (int i = 0; i < DEFECT_COUNT; ++i)
        m_selected[i] = false;
    m_preciserText[0] = '\0';
    m_preciserBuf[0]  = 0;
}

void defects_pmpView::setupScreen()
{
    /* Do NOT call defects_pmpViewBase::setupScreen() — the base's transitionBegins()
     * would immediately navigate to summary before the operator acts. */

    for (int i = 0; i < DEFECT_COUNT; ++i)
        m_selected[i] = false;
    m_autre_selected  = false;
    m_preciserText[0] = '\0';
    m_preciserBuf[0]  = 0;

    DefectButton* btns[DEFECT_COUNT] = {
        &defect_4, &defect_5, &defect_6, &defect_7,
        &defect_8, &defect_9, &defect_10, &defect_other
    };
    for (int i = 0; i < DEFECT_COUNT; ++i)
        btns[i]->setClickAction(m_defectCb);

    next_button.setClickAction(m_nextCb);

    m_titleLabel.setTypedText(touchgfx::TypedText(T_DEFECTS_PMP_TITLE));
    m_titleLabel.setColor(touchgfx::Color::getColorFromRGB(0xFA, 0xEE, 0xE3));
    m_titleLabel.setPosition(10, 5, 252, 23);
    add(m_titleLabel);

    /* Préciser hidden until Autre is selected */
    input_other.setVisible(false);
    input_other.invalidate();
    input_underline_ui.setVisible(false);
    input_underline_ui.invalidate();
    input_other.setClickAction(m_preciserCb);

    /* Overlay to display typed préciser text (on top of static input_other label). */
    m_preciserDisplay.setTypedText(touchgfx::TypedText(T_INPUT_WILDCARD));
    m_preciserDisplay.setWildcard(m_preciserBuf);
    m_preciserDisplay.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    m_preciserDisplay.setPosition(25, 362, 230, 20);
    m_preciserDisplay.setVisible(false);
    add(m_preciserDisplay);

    updateActionButton();
}

void defects_pmpView::tearDownScreen()
{
    defects_pmpViewBase::tearDownScreen();
}

void defects_pmpView::updateDefectButton(DefectButton& btn, bool selected)
{
    if (selected)
    {
        btn.setBoxWithBorderColors(
            pmp::colors::accent_gold(), pmp::colors::dark_pressed(),
            pmp::colors::brand_teal(), pmp::colors::brand_teal());
        btn.setTextColors(pmp::colors::brand_teal(), pmp::colors::brand_teal());
    }
    else
    {
        btn.setBoxWithBorderColors(
            pmp::colors::brand_teal(), pmp::colors::accent_gold(),
            touchgfx::Color::getColorFromRGB(0, 51, 102),
            touchgfx::Color::getColorFromRGB(51, 102, 153));
        btn.setTextColors(pmp::colors::cream(), touchgfx::Color::getColorFromRGB(10, 10, 10));
    }
    btn.invalidate();
}

void defects_pmpView::updateActionButton()
{
    bool anySelected = false;
    for (int i = 0; i < DEFECT_COUNT; ++i)
        if (m_selected[i]) { anySelected = true; break; }

    if (anySelected)
    {
        next_button.setBoxWithBorderColors(
            pmp::colors::accent_gold(), pmp::colors::dark_pressed(),
            pmp::colors::brand_teal(), pmp::colors::brand_teal());
        next_button.setTextColors(pmp::colors::brand_teal(), pmp::colors::brand_teal());
        next_button.setText(TypedText(T___SINGLEUSE_FQOE)); /* "Suivant" */
    }
    else
    {
        next_button.setBoxWithBorderColors(
            pmp::colors::success_green(), pmp::colors::dark_pressed(),
            pmp::colors::cream(), pmp::colors::cream());
        next_button.setTextColors(pmp::colors::cream(), pmp::colors::cream());
        next_button.setText(TypedText(T_T_PIECE_OK)); /* "Pièce OK" */
    }
    next_button.invalidate();
}

void defects_pmpView::onDefectClicked(const ButtonBase& src, const touchgfx::ClickEvent& evt)
{
    if (evt.getType() != touchgfx::ClickEvent::RELEASED)
        return;

    DefectButton* btns[DEFECT_COUNT] = {
        &defect_4, &defect_5, &defect_6, &defect_7,
        &defect_8, &defect_9, &defect_10, &defect_other
    };

    for (int i = 0; i < DEFECT_COUNT; ++i)
    {
        if (&src == static_cast<const ButtonBase*>(btns[i]))
        {
            m_selected[i] = !m_selected[i];
            updateDefectButton(*btns[i], m_selected[i]);

            /* Index 7 is Autre — drive the Préciser area visibility */
            if (i == DEFECT_COUNT - 1)
            {
                m_autre_selected = m_selected[i];
                input_underline_ui.setVisible(m_autre_selected);
                input_underline_ui.invalidate();
                if (m_autre_selected)
                {
                    /* Show typed text if available, otherwise show static label. */
                    if (m_preciserBuf[0] != 0)
                    {
                        m_preciserDisplay.setVisible(true);
                        m_preciserDisplay.invalidate();
                        input_other.setVisible(false);
                    }
                    else
                    {
                        input_other.setVisible(true);
                        m_preciserDisplay.setVisible(false);
                    }
                    input_other.invalidate();
                }
                else
                {
                    input_other.setVisible(false);
                    input_other.invalidate();
                    m_preciserDisplay.setVisible(false);
                    m_preciserDisplay.invalidate();
                }
            }
            break;
        }
    }

    updateActionButton();
}

void defects_pmpView::onNextClicked(const ButtonBase& /*src*/, const touchgfx::ClickEvent& evt)
{
    if (evt.getType() != touchgfx::ClickEvent::RELEASED)
        return;

    bool anySelected = false;
    for (int i = 0; i < DEFECT_COUNT; ++i)
        if (m_selected[i]) { anySelected = true; break; }

    if (anySelected)
    {
        for (int i = 0; i < DEFECT_COUNT; ++i)
        {
            if (m_selected[i])
                presenter->logDefectInspection(i + 1,
                    (i == DEFECT_COUNT - 1 && m_autre_selected) ? m_preciserText : "");
        }
    }
    else
    {
        presenter->logOkInspection();
    }

    application().gotodefects_injScreenNoTransition();
}

void defects_pmpView::onPreciserClicked(const touchgfx::TextArea& /*src*/,
                                        const touchgfx::ClickEvent& evt)
{
    if (evt.getType() != touchgfx::ClickEvent::RELEASED)
        return;
    if (m_autre_selected)
        presenter->openKeyboardForPreciser();
}

void defects_pmpView::receivePreciserText(const char* text)
{
    strncpy(m_preciserText, text, 127);
    m_preciserText[127] = '\0';

    /* Convert to UnicodeChar for the display overlay. */
    size_t len = 0;
    while (text[len] && len < 127)
    {
        m_preciserBuf[len] = static_cast<touchgfx::Unicode::UnicodeChar>(
            static_cast<unsigned char>(text[len]));
        ++len;
    }
    m_preciserBuf[len] = 0;

    if (m_preciserBuf[0] != 0)
    {
        /* Auto-select Autre and show the typed préciser text. */
        m_autre_selected = true;
        m_selected[DEFECT_COUNT - 1] = true;
        updateDefectButton(defect_other, true);

        input_other.setVisible(false);
        input_other.invalidate();
        m_preciserDisplay.setWildcard(m_preciserBuf);
        m_preciserDisplay.setVisible(true);
        m_preciserDisplay.invalidate();
        input_underline_ui.setVisible(true);
        input_underline_ui.invalidate();

        updateActionButton();
    }
}
