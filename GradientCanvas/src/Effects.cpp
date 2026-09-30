/*---------------------------------------------------------*\
| Effects.cpp                                               |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "Effects.h"
#include <algorithm>
#include <cmath>

static const double TWO_PI = 6.283185307179586;

static const EffectInfo effect_infos[EFFECT_COUNT] =
{
    /* name               scale  angle  centre  p1 label            p1 range/default     p2 label         p2 range/default */
    { "Linear Gradient",  true,  true,  false,  "",                 0, 0, 0,             "",              0, 0, 0   },
    { "Radial",           true,  false, true,   "",                 0, 0, 0,             "",              0, 0, 0   },
    { "Spiral",           true,  false, true,   "Arms",             1, 8, 2,             "Twist",         0, 4, 1   },
    { "Wave",             true,  true,  false,  "Wave depth",       0, 1, 0.85,          "Sharpness",     1, 6, 2   },
    { "Plasma",           true,  false, false,  "Turbulence",       0.2, 3, 1,           "",              0, 0, 0   },
    { "Breathing",        false, false, true,   "Ripple",           0, 4, 0,             "Breaths/loop",  1, 12, 4  },
    { "Colour Cycle",     false, true,  false,  "Spread",           0, 2, 0,             "",              0, 0, 0   },
};

const EffectInfo& GetEffectInfo(int type)
{
    if(type < 0 || type >= EFFECT_COUNT)
    {
        type = 0;
    }
    return effect_infos[type];
}

static inline double Frac(double x)
{
    return x - std::floor(x);
}

/*---------------------------------------------------------*\
| Sample the gradient honouring the mirror (ping-pong) flag |
\*---------------------------------------------------------*/
static inline void SampleRepeat(const Gradient& g, bool mirror, double pos, float& r, float& gr, float& b)
{
    if(mirror)
    {
        double f = Frac(pos * 0.5);
        g.Sample(1.0 - std::fabs(2.0 * f - 1.0), false, r, gr, b);
    }
    else
    {
        g.Sample(pos, true, r, gr, b);
    }
}

void EvaluateEffect(const EffectParams& p, const Gradient& g,
                    double u, double v, double aspect,
                    double phase, double /*time*/,
                    float& r, float& gr, float& b)
{
    const double ph     = p.reverse ? -phase : phase;
    const double scale  = std::max(p.scale, 0.01);
    const double rad    = p.angle * TWO_PI / 360.0;
    const double ca     = std::cos(rad);
    const double sa     = std::sin(rad);

    /*-----------------------------------------------------*\
    | Aspect-corrected coordinates, origin at canvas centre |
    \*-----------------------------------------------------*/
    const double x      = (u - 0.5) * aspect;
    const double y      = (v - 0.5);

    /* Distance along the effect direction */
    const double along  = x * ca + y * sa;

    /* Vector from the effect centre */
    const double dx     = (u - p.cx) * aspect;
    const double dy     = (v - p.cy);
    const double dist   = std::sqrt(dx * dx + dy * dy);

    float  level = 1.0f;

    switch(p.type)
    {
        default:
        case EFFECT_LINEAR_GRADIENT:
            SampleRepeat(g, p.mirror, along / scale - ph, r, gr, b);
            break;

        case EFFECT_RADIAL:
            SampleRepeat(g, p.mirror, dist / scale - ph, r, gr, b);
            break;

        case EFFECT_SPIRAL:
        {
            double arms  = std::max(1.0, std::round(p.param1));
            double theta = std::atan2(dy, dx) / TWO_PI;      /* -0.5 .. 0.5 */
            SampleRepeat(g, p.mirror, theta * arms + (dist / scale) * p.param2 - ph, r, gr, b);
            break;
        }

        case EFFECT_WAVE:
        {
            double s     = along / scale;
            double w     = 0.5 + 0.5 * std::sin(TWO_PI * (s - ph));
            w            = std::pow(w, std::max(1.0, p.param2));
            level        = (float)((1.0 - p.param1) + p.param1 * w);
            SampleRepeat(g, p.mirror, s * 0.25 - ph * 0.1, r, gr, b);
            break;
        }

        case EFFECT_PLASMA:
        {
            double px    = x / scale * 6.0;
            double py    = y / scale * 6.0;
            double t     = ph * TWO_PI;
            double val   = std::sin(px + t)
                         + std::sin(py * 0.8 + t * 1.3)
                         + std::sin((px + py) * 0.6 + t * 0.7)
                         + std::sin(std::sqrt(px * px + py * py + 1.0) * 1.2 - t * 1.7);
            val         /= 4.0;                               /* -1 .. 1 */
            SampleRepeat(g, p.mirror, val * 0.5 * p.param1 + ph * 0.2, r, gr, b);
            break;
        }

        case EFFECT_BREATHING:
        {
            double local = ph - p.param1 * dist;
            double br    = 0.5 - 0.5 * std::cos(TWO_PI * local);
            level        = (float)(br * br);
            SampleRepeat(g, p.mirror, local / std::max(1.0, p.param2), r, gr, b);
            break;
        }

        case EFFECT_COLOR_CYCLE:
            SampleRepeat(g, p.mirror, ph * 0.25 + along * p.param1, r, gr, b);
            break;
    }

    level *= (float)std::clamp(p.brightness, 0.0, 1.0);
    r  *= level;
    gr *= level;
    b  *= level;
}

nlohmann::json EffectParams::ToJson() const
{
    return {
        {"type",        type},
        {"speed",       speed},
        {"scale",       scale},
        {"angle",       angle},
        {"cx",          cx},
        {"cy",          cy},
        {"param1",      param1},
        {"param2",      param2},
        {"brightness",  brightness},
        {"mirror",      mirror},
        {"reverse",     reverse},
    };
}

void EffectParams::FromJson(const nlohmann::json& j)
{
    if(!j.is_object())
    {
        return;
    }
    type        = j.value("type",       type);
    speed       = j.value("speed",      speed);
    scale       = j.value("scale",      scale);
    angle       = j.value("angle",      angle);
    cx          = j.value("cx",         cx);
    cy          = j.value("cy",         cy);
    param1      = j.value("param1",     param1);
    param2      = j.value("param2",     param2);
    brightness  = j.value("brightness", brightness);
    mirror      = j.value("mirror",     mirror);
    reverse     = j.value("reverse",    reverse);

    type        = std::clamp(type, 0, (int)EFFECT_COUNT - 1);
}
