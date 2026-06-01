#include <gui/input_type_screen/input_typeView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>
#include <string.h>

input_typeView::input_typeView()
    : m_len(0),
      m_keyCb(this, &input_typeView::onKeyPressed),
      m_doneCb(this, &input_typeView::onDoneClicked)
{
    m_buf[0] = 0;
}

void input_typeView::setupScreen()
{
    input_typeViewBase::setupScreen();

    m_len = 0;
    m_buf[0] = 0;
    input_other.setVisible(true);
    input_other.invalidate();

    /* Map each keyboard button to its character. Layout: AZERTY rows. */
    m_keys[0]  = { &keyboard_button_,     'a' };
    m_keys[1]  = { &keyboard_button__1,   'z' };
    m_keys[2]  = { &keyboard_button__2,   'e' };
    m_keys[3]  = { &keyboard_button__3,   'r' };
    m_keys[4]  = { &keyboard_button__4,   't' };
    m_keys[5]  = { &keyboard_button__5,   'y' };
    m_keys[6]  = { &keyboard_button__6,   'u' };
    m_keys[7]  = { &keyboard_button__7,   'i' };
    m_keys[8]  = { &keyboard_button__8,   'o' };
    m_keys[9]  = { &keyboard_button__9,   'p' };
    m_keys[10] = { &keyboard_button__10,  'q' };
    m_keys[11] = { &keyboard_button__1_1, 's' };
    m_keys[12] = { &keyboard_button__2_1, 'd' };
    m_keys[13] = { &keyboard_button__3_1, 'f' };
    m_keys[14] = { &keyboard_button__4_1, 'g' };
    m_keys[15] = { &keyboard_button__5_1, 'h' };
    m_keys[16] = { &keyboard_button__6_1, 'j' };
    m_keys[17] = { &keyboard_button__7_1, 'k' };
    m_keys[18] = { &keyboard_button__8_1, 'l' };
    m_keys[19] = { &keyboard_button__9_1, 'm' };
    m_keys[20] = { &keyboard_button__3_1_1, 'w' };
    m_keys[21] = { &keyboard_button__4_1_1, 'x' };
    m_keys[22] = { &keyboard_button__5_1_1, 'c' };
    m_keys[23] = { &keyboard_button__6_1_1, 'v' };
    m_keys[24] = { &keyboard_button__7_1_1, 'b' };
    m_keys[25] = { &keyboard_button__8_1_1, 'n' };
    m_keys[26] = { &keyboard_button__9_1_1, '\'' };
    m_keys[27] = { &keyboard_button__4_1_1_1, ' ' };

    for (int i = 0; i < KEY_COUNT; ++i)
        m_keys[i].btn->setAction(m_keyCb);

    next_button.setClickAction(m_doneCb);

    /* Overlay to show typed text — hidden until first keypress. */
    m_inputDisplay.setTypedText(touchgfx::TypedText(T_INPUT_WILDCARD));
    m_inputDisplay.setWildcard(m_buf);
    m_inputDisplay.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    m_inputDisplay.setPosition(25, 176, 230, 20);
    m_inputDisplay.setVisible(false);
    add(m_inputDisplay);
}

void input_typeView::tearDownScreen()
{
    input_typeViewBase::tearDownScreen();
}

void input_typeView::onKeyPressed(const touchgfx::AbstractButtonContainer& src)
{
    for (int i = 0; i < KEY_COUNT; ++i)
    {
        if (&src == m_keys[i].btn)
        {
            appendChar(m_keys[i].ch);
            break;
        }
    }
}

void input_typeView::appendChar(char c)
{
    if (m_len >= BUF_SIZE - 1)
        return;
    m_buf[m_len++] = static_cast<touchgfx::Unicode::UnicodeChar>(
        static_cast<unsigned char>(c));
    m_buf[m_len] = 0;
    if (m_len == 1)
    {
        input_other.setVisible(false);
        input_other.invalidate();
        m_inputDisplay.setVisible(true);
    }
    m_inputDisplay.setWildcard(m_buf);
    m_inputDisplay.invalidate();
}

void input_typeView::onDoneClicked(const DoneButtonBase& /*src*/,
                                   const touchgfx::ClickEvent& evt)
{
    if (evt.getType() != touchgfx::ClickEvent::RELEASED)
        return;
    /* Convert UnicodeChar buffer back to ASCII for presenter. */
    char ascii[BUF_SIZE];
    for (size_t i = 0; i <= m_len; ++i)
        ascii[i] = static_cast<char>(m_buf[i]);
    presenter->finishPreciser(ascii);
}
