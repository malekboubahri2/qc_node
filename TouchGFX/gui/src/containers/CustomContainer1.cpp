#include <gui/containers/CustomContainer1.hpp>
#include <touchgfx/Color.hpp>

CustomContainer1::CustomContainer1()
{
}

void CustomContainer1::initialize()
{
    CustomContainer1Base::initialize();
}

void CustomContainer1::setProductIndex(int index)
{
    static const touchgfx::colortype COLORS[3] = {
        touchgfx::Color::getColorFromRGB(245, 232, 220), /* cream — product 0 */
        touchgfx::Color::getColorFromRGB(200, 230, 245), /* light blue — product 1 */
        touchgfx::Color::getColorFromRGB(200, 245, 215), /* light green — product 2 */
    };
    const int idx = (index >= 0 && index < 3) ? index : 0;
    box1.setColor(COLORS[idx]);
    box1.invalidate();
}
