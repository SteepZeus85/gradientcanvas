/*---------------------------------------------------------*\
| Calibration.cpp                                           |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "Calibration.h"
#include <algorithm>
#include <cmath>

bool ColorCalibration::IsIdentity() const
{
    return brightness == 1.0 && red == 1.0 && green == 1.0 && blue == 1.0
        && gamma == 1.0 && saturation == 1.0 && order == ORDER_RGB;
}

void ColorCalibration::Apply(float& r, float& g, float& b) const
{
    /*-----------------------------------------------------*\
    | 1. Saturation around Rec.709 luma                     |
    \*-----------------------------------------------------*/
    if(saturation != 1.0)
    {
        float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        float s    = (float)saturation;
        r = luma + (r - luma) * s;
        g = luma + (g - luma) * s;
        b = luma + (b - luma) * s;
    }

    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);

    /*-----------------------------------------------------*\
    | 2. Gamma. >1 darkens mid-tones (for LEDs that look    |
    |    washed out / too bright at low values), <1 lifts   |
    \*-----------------------------------------------------*/
    if(gamma != 1.0)
    {
        float gm = (float)gamma;
        r = std::pow(r, gm);
        g = std::pow(g, gm);
        b = std::pow(b, gm);
    }

    /*-----------------------------------------------------*\
    | 3 + 4. White balance and brightness                   |
    \*-----------------------------------------------------*/
    r *= (float)(red   * brightness);
    g *= (float)(green * brightness);
    b *= (float)(blue  * brightness);

    /*-----------------------------------------------------*\
    | 5. Channel order. Pick whichever makes the Red test   |
    |    show red, Green green and Blue blue.               |
    \*-----------------------------------------------------*/
    float in[3] = { r, g, b };
    switch(order)
    {
        case ORDER_RBG: r = in[0]; g = in[2]; b = in[1]; break;
        case ORDER_GRB: r = in[1]; g = in[0]; b = in[2]; break;
        case ORDER_GBR: r = in[1]; g = in[2]; b = in[0]; break;
        case ORDER_BRG: r = in[2]; g = in[0]; b = in[1]; break;
        case ORDER_BGR: r = in[2]; g = in[1]; b = in[0]; break;
        default: break;
    }
}

nlohmann::json ColorCalibration::ToJson() const
{
    return {
        {"brightness", brightness},
        {"red",        red},
        {"green",      green},
        {"blue",       blue},
        {"gamma",      gamma},
        {"saturation", saturation},
        {"order",      order},
    };
}

void ColorCalibration::FromJson(const nlohmann::json& j)
{
    if(!j.is_object())
    {
        return;
    }
    brightness = std::clamp(j.value("brightness", 1.0), 0.0, 1.0);
    red        = std::clamp(j.value("red",        1.0), 0.0, 1.0);
    green      = std::clamp(j.value("green",      1.0), 0.0, 1.0);
    blue       = std::clamp(j.value("blue",       1.0), 0.0, 1.0);
    gamma      = std::clamp(j.value("gamma",      1.0), 0.3, 3.0);
    saturation = std::clamp(j.value("saturation", 1.0), 0.0, 2.0);
    order      = std::clamp(j.value("order",      0),   0, (int)ORDER_COUNT - 1);
}

QStringList ColorCalibration::OrderNames()
{
    return { "RGB (normal)", "RBG", "GRB", "GBR", "BRG", "BGR" };
}
