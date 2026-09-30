/*---------------------------------------------------------*\
| LayoutModel.h                                             |
|                                                           |
|   The layout map: where every device zone (and, when      |
|   customised, every individual LED) sits on a shared 2D   |
|   canvas.                                                 |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QColor>
#include <QPointF>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "RGBControllerInterface.h"

struct ZonePlacement
{
    /*-----------------------------------------------------*\
    | Identity (persisted)                                  |
    \*-----------------------------------------------------*/
    std::string     device_key;         /* name|location|serial             */
    std::string     device_name;
    unsigned int    zone_idx    = 0;
    std::string     zone_name;

    /*-----------------------------------------------------*\
    | Placement on canvas (persisted). x,y is the top-left  |
    | of the un-rotated rectangle, rotation is about its    |
    | centre, in degrees.                                   |
    \*-----------------------------------------------------*/
    bool            enabled     = false;
    double          x           = 0.0;
    double          y           = 0.0;
    double          w           = 200.0;
    double          h           = 30.0;
    double          rotation    = 0.0;
    bool            reverse     = false;

    /*-----------------------------------------------------*\
    | Per-LED overrides, zone-local LED index -> position   |
    | normalised to the zone rectangle (0-1, 0-1)           |
    \*-----------------------------------------------------*/
    std::map<unsigned int, QPointF> led_overrides;

    /*-----------------------------------------------------*\
    | Cached zone description (refreshed on bind)           |
    \*-----------------------------------------------------*/
    unsigned int                led_count   = 0;
    zone_type                   type        = ZONE_TYPE_LINEAR;
    unsigned int                matrix_w    = 0;
    unsigned int                matrix_h    = 0;
    std::vector<QPointF>        default_local;  /* per LED, 0-1 in rect */

    /*-----------------------------------------------------*\
    | Runtime binding (never persisted)                     |
    \*-----------------------------------------------------*/
    RGBControllerInterface*     controller  = nullptr;
    unsigned int                start_index = 0;
    std::vector<QColor>         preview;        /* last rendered colours */

    bool        IsMatrix() const;
    QPointF     LedLocal(unsigned int led) const;
    QPointF     LedWorld(unsigned int led) const;
    QPointF     LocalToWorld(const QPointF& local) const;
    QPointF     WorldToLocal(const QPointF& world) const;
    void        RebuildDefaults();
    void        ResetDefaultSize();
};

class LayoutModel
{
public:
    double                      canvas_w    = 1000.0;
    double                      canvas_h    = 600.0;
    std::vector<ZonePlacement>  zones;

    /*-----------------------------------------------------*\
    | Bind placements to the live controller list. New      |
    | zones are appended (disabled); zones of unplugged     |
    | devices are kept so their layout survives.            |
    \*-----------------------------------------------------*/
    void            Sync(const std::vector<RGBControllerInterface*>& controllers);
    void            Unbind();

    /*-----------------------------------------------------*\
    | Arrange all enabled zones in a tidy stack             |
    \*-----------------------------------------------------*/
    void            AutoArrange();

    nlohmann::json  ToJson() const;
    void            FromJson(const nlohmann::json& j);

    static std::string  MakeDeviceKey(RGBControllerInterface* controller);
};
