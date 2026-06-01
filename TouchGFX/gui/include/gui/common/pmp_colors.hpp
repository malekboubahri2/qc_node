#ifndef PMP_COLORS_HPP
#define PMP_COLORS_HPP

#include <touchgfx/Color.hpp>

namespace pmp::colors {
    inline touchgfx::colortype brand_teal()    { return touchgfx::Color::getColorFromRGB(0x1A, 0x55, 0x60); }
    inline touchgfx::colortype accent_gold()   { return touchgfx::Color::getColorFromRGB(0xD4, 0xB7, 0x65); }
    inline touchgfx::colortype success_green() { return touchgfx::Color::getColorFromRGB(0x2E, 0x7D, 0x5B); }
    inline touchgfx::colortype cream()         { return touchgfx::Color::getColorFromRGB(0xFA, 0xEE, 0xE3); }
    inline touchgfx::colortype dark_pressed()  { return touchgfx::Color::getColorFromRGB(0x14, 0x40, 0x48); }
}

#endif // PMP_COLORS_HPP
