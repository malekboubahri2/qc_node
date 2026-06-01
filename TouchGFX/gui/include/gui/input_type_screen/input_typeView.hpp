#ifndef INPUT_TYPEVIEW_HPP
#define INPUT_TYPEVIEW_HPP

#include <gui_generated/input_type_screen/input_typeViewBase.hpp>
#include <gui/input_type_screen/input_typePresenter.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/containers/buttons/AbstractButtonContainer.hpp>
#include <touchgfx/widgets/TextArea.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <stddef.h>

class input_typeView : public input_typeViewBase
{
public:
    input_typeView();
    virtual ~input_typeView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

private:
    typedef touchgfx::TextButtonStyle<
                touchgfx::BoxWithBorderButtonStyle<
                    touchgfx::ClickButtonTrigger>> DoneButtonBase;

    static const size_t BUF_SIZE = 128;
    static const int    KEY_COUNT = 28;

    touchgfx::Unicode::UnicodeChar m_buf[BUF_SIZE];
    size_t m_len;

    touchgfx::TextAreaWithOneWildcard m_inputDisplay;

    struct KeyEntry {
        touchgfx::AbstractButtonContainer* btn;
        char ch;
    };
    KeyEntry m_keys[KEY_COUNT];

    touchgfx::Callback<input_typeView, const touchgfx::AbstractButtonContainer&> m_keyCb;
    touchgfx::Callback<input_typeView, const DoneButtonBase&, const touchgfx::ClickEvent&> m_doneCb;

    void onKeyPressed(const touchgfx::AbstractButtonContainer& src);
    void onDoneClicked(const DoneButtonBase& src, const touchgfx::ClickEvent& evt);

    void appendChar(char c);
};

#endif // INPUT_TYPEVIEW_HPP
