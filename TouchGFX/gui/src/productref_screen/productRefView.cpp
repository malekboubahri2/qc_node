#include <gui/productref_screen/productRefView.hpp>

productRefView::productRefView()
    : m_nextCb(this, &productRefView::onNextClicked)
{
}

void productRefView::setupScreen()
{
    productRefViewBase::setupScreen();

    /* 3 dummy products; real list comes from MQTT config later. */
    scrollList1.setNumberOfItems(3);

    /* Override base navigation: go to PMP defect grid, not INJ. */
    next_button.setAction(m_nextCb);
}

void productRefView::tearDownScreen()
{
    productRefViewBase::tearDownScreen();
}

void productRefView::scrollList1UpdateItem(CustomContainer1& item, int16_t itemIndex)
{
    item.setProductIndex(static_cast<int>(itemIndex));
}

void productRefView::onNextClicked(const touchgfx::AbstractButtonContainer& /*src*/)
{
    application().gotodefects_pmpScreenNoTransition();
}
