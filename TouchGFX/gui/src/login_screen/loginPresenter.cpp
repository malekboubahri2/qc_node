#include <gui/login_screen/loginView.hpp>
#include <gui/login_screen/loginPresenter.hpp>
#include <string.h>

loginPresenter::loginPresenter(loginView& v)
    : view(v), m_digit_count(0)
{
    memset(m_pin, 0, sizeof(m_pin));
}

void loginPresenter::activate()
{
    clearPin();
}

void loginPresenter::deactivate()
{
}

void loginPresenter::digitPressed(int digit)
{
    if (m_digit_count >= Model::PIN_MAX_LEN)
        return;

    m_pin[m_digit_count] = '0' + digit;
    ++m_digit_count;
    m_pin[m_digit_count] = '\0';

    view.setDigitCount(m_digit_count);

    if (m_digit_count == Model::PIN_MAX_LEN)
        submitPin();
}

void loginPresenter::submitPin()
{
    int op_idx = -1;
    if (model->validatePin(m_pin, &op_idx))
    {
        model->setCurrentOperatorIdx(op_idx);
        model->resetSessionDefectCount();
        clearPin();
        view.gotoProductRefScreen();
    }
    else
    {
        clearPin();
        view.showError();
    }
}

void loginPresenter::clearPin()
{
    m_digit_count = 0;
    memset(m_pin, 0, sizeof(m_pin));
    view.setDigitCount(0);
}
