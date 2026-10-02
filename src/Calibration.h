/*---------------------------------------------------------*\
| Calibration.h                                             |
|                                                           |
|   Per-zone colour correction so different hardware shows  |
|   the same colour for the same value. Applied only to the |
|   colours sent to devices - the on-screen preview always  |
|   shows the intended colour.                              |
|                                                           |
|   Order of operations:                                    |
|     1. saturation   (fix washed-out / over-vivid LEDs)    |
|     2. gamma        (match brightness curves)             |
|     3. R/G/B gain   (white balance)                       |
|     4. brightness   (match overall intensity)             |
|     5. channel order (fix strips wired as GRB, BRG, ...)  |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QStringList>
#include <nlohmann/json.hpp>

enum ChannelOrder
{
    ORDER_RGB = 0,
    ORDER_RBG,
    ORDER_GRB,
    ORDER_GBR,
    ORDER_BRG,
    ORDER_BGR,
    ORDER_COUNT
};

struct ColorCalibration
{
    double  brightness  = 1.0;  /* 0 - 1                        */
    double  red         = 1.0;  /* 0 - 1 gain per channel       */
    double  green       = 1.0;
    double  blue        = 1.0;
    double  gamma       = 1.0;  /* 0.3 - 3, 1 = unchanged       */
    double  saturation  = 1.0;  /* 0 - 2, 1 = unchanged         */
    int     order       = ORDER_RGB;

    bool            IsIdentity() const;
    void            Apply(float& r, float& g, float& b) const;

    nlohmann::json  ToJson() const;
    void            FromJson(const nlohmann::json& j);

    static QStringList  OrderNames();
};
