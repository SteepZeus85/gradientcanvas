/*---------------------------------------------------------*\
| Effects.h                                                 |
|                                                           |
|   Spatial effect functions. Every effect is evaluated at  |
|   a point on the layout canvas, so gradients flow across  |
|   all devices according to where they sit on the map.    |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "Gradient.h"
#include <QString>
#include <nlohmann/json.hpp>

enum EffectType
{
    EFFECT_LINEAR_GRADIENT  = 0,
    EFFECT_RADIAL           = 1,
    EFFECT_SPIRAL           = 2,
    EFFECT_WAVE             = 3,
    EFFECT_PLASMA           = 4,
    EFFECT_BREATHING        = 5,
    EFFECT_COLOR_CYCLE      = 6,
    EFFECT_COUNT
};

/*---------------------------------------------------------*\
| Which controls an effect uses, and what they're called    |
\*---------------------------------------------------------*/
struct EffectInfo
{
    QString name;
    bool    uses_scale;
    bool    uses_angle;
    bool    uses_center;
    QString param1_label;   /* empty = unused */
    double  param1_min, param1_max, param1_default;
    QString param2_label;   /* empty = unused */
    double  param2_min, param2_max, param2_default;
};

const EffectInfo& GetEffectInfo(int type);

struct EffectParams
{
    int     type        = EFFECT_LINEAR_GRADIENT;
    double  speed       = 0.25;     /* cycles per second                    */
    double  scale       = 1.0;      /* size of one gradient repeat (canvas  */
                                    /* heights)                             */
    double  angle       = 0.0;      /* degrees, 0 = left->right             */
    double  cx          = 0.5;      /* centre, normalised 0-1               */
    double  cy          = 0.5;
    double  param1      = 3.0;
    double  param2      = 1.0;
    double  brightness  = 1.0;      /* 0-1                                  */
    bool    mirror      = false;    /* ping-pong instead of wrap            */
    bool    reverse     = false;

    nlohmann::json  ToJson() const;
    void            FromJson(const nlohmann::json& j);
};

/*---------------------------------------------------------*\
| Evaluate an effect at canvas point (u, v).                |
|   u, v    - normalised 0-1 canvas coordinates             |
|   aspect  - canvas width / height (keeps circles round)   |
|   phase   - accumulated animation phase (speed-integrated)|
|   time    - wall-clock seconds (for secondary motion)     |
|   Output r,g,b in 0-1                                     |
\*---------------------------------------------------------*/
void EvaluateEffect(const EffectParams& p, const Gradient& g,
                    double u, double v, double aspect,
                    double phase, double time,
                    float& r, float& gr, float& b);
