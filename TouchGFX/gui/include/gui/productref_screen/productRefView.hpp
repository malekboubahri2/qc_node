#ifndef PRODUCTREFVIEW_HPP
#define PRODUCTREFVIEW_HPP

#include <gui_generated/productref_screen/productRefViewBase.hpp>
#include <gui/productref_screen/productRefPresenter.hpp>
#include <gui/containers/CustomContainer1.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/containers/buttons/AbstractButtonContainer.hpp>

class productRefView : public productRefViewBase
{
public:
    productRefView();
    virtual ~productRefView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    virtual void scrollList1UpdateItem(CustomContainer1& item, int16_t itemIndex);

protected:
private:
    touchgfx::Callback<productRefView, const touchgfx::AbstractButtonContainer&> m_nextCb;

    void onNextClicked(const touchgfx::AbstractButtonContainer& src);
};

#endif // PRODUCTREFVIEW_HPP
