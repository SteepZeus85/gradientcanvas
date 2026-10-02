/*---------------------------------------------------------*\
| LayoutModel.cpp                                           |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "LayoutModel.h"
#include <algorithm>
#include <cmath>

static const double DEG_TO_RAD = 3.141592653589793 / 180.0;
static const unsigned int NO_LED = 0xFFFFFFFF;

/*---------------------------------------------------------*\
| ZonePlacement                                             |
\*---------------------------------------------------------*/
bool ZonePlacement::IsMatrix() const
{
    return (type == ZONE_TYPE_MATRIX || type == ZONE_TYPE_MATRIX_LOOP_X || type == ZONE_TYPE_MATRIX_LOOP_Y)
        && matrix_w > 0 && matrix_h > 0;
}

QPointF ZonePlacement::LedLocal(unsigned int led) const
{
    auto it = led_overrides.find(led);
    if(it != led_overrides.end())
    {
        return it->second;
    }

    if(led < default_local.size())
    {
        QPointF p = default_local[led];
        if(reverse && !IsMatrix())
        {
            p.setX(1.0 - p.x());
        }
        return p;
    }

    return QPointF(0.5, 0.5);
}

QPointF ZonePlacement::LocalToWorld(const QPointF& local) const
{
    /*-----------------------------------------------------*\
    | Rotate about the rectangle centre - matches the       |
    | QGraphicsItem transform used by the canvas            |
    \*-----------------------------------------------------*/
    double lx = (local.x() - 0.5) * w;
    double ly = (local.y() - 0.5) * h;
    double c  = std::cos(rotation * DEG_TO_RAD);
    double s  = std::sin(rotation * DEG_TO_RAD);

    return QPointF(x + w * 0.5 + lx * c - ly * s,
                   y + h * 0.5 + lx * s + ly * c);
}

QPointF ZonePlacement::WorldToLocal(const QPointF& world) const
{
    double dx = world.x() - (x + w * 0.5);
    double dy = world.y() - (y + h * 0.5);
    double c  = std::cos(-rotation * DEG_TO_RAD);
    double s  = std::sin(-rotation * DEG_TO_RAD);

    double lx = dx * c - dy * s;
    double ly = dx * s + dy * c;

    return QPointF(w > 0 ? lx / w + 0.5 : 0.5, h > 0 ? ly / h + 0.5 : 0.5);
}

QPointF ZonePlacement::LedWorld(unsigned int led) const
{
    return LocalToWorld(LedLocal(led));
}

void ZonePlacement::RebuildDefaults()
{
    default_local.assign(led_count, QPointF(0.5, 0.5));

    if(led_count == 0)
    {
        return;
    }

    if(IsMatrix() && controller)
    {
        const unsigned int* map = controller->GetZoneMatrixMapData(zone_idx);
        if(map)
        {
            for(unsigned int row = 0; row < matrix_h; row++)
            {
                for(unsigned int col = 0; col < matrix_w; col++)
                {
                    unsigned int v = map[row * matrix_w + col];
                    if(v != NO_LED && v < led_count)
                    {
                        default_local[v] = QPointF((col + 0.5) / matrix_w, (row + 0.5) / matrix_h);
                    }
                }
            }
            return;
        }
    }

    if(type == ZONE_TYPE_SINGLE && led_count == 1)
    {
        return;
    }

    /*-----------------------------------------------------*\
    | Linear / loop / segmented / unknown matrix: a strip   |
    \*-----------------------------------------------------*/
    for(unsigned int i = 0; i < led_count; i++)
    {
        default_local[i] = QPointF((i + 0.5) / led_count, 0.5);
    }

    /*-----------------------------------------------------*\
    | Drop overrides for LEDs that no longer exist (zone    |
    | was resized)                                          |
    \*-----------------------------------------------------*/
    for(auto it = led_overrides.begin(); it != led_overrides.end();)
    {
        if(it->first >= led_count) it = led_overrides.erase(it);
        else                       ++it;
    }
}

void ZonePlacement::ResetDefaultSize()
{
    const double cell = 22.0;

    if(IsMatrix())
    {
        w = std::max(cell, matrix_w * cell);
        h = std::max(cell, matrix_h * cell);
    }
    else if(type == ZONE_TYPE_SINGLE || led_count <= 1)
    {
        w = 40.0;
        h = 40.0;
    }
    else
    {
        w = std::clamp(led_count * 14.0, 60.0, 700.0);
        h = 24.0;
    }
}

/*---------------------------------------------------------*\
| LayoutModel                                               |
\*---------------------------------------------------------*/
std::string LayoutModel::MakeDeviceKey(RGBControllerInterface* controller)
{
    return controller->GetName() + "|" + controller->GetLocation() + "|" + controller->GetSerial();
}

void LayoutModel::Unbind()
{
    for(ZonePlacement& z : zones)
    {
        z.controller = nullptr;
    }
}

void LayoutModel::Sync(const std::vector<RGBControllerInterface*>& controllers)
{
    Unbind();

    for(RGBControllerInterface* controller : controllers)
    {
        if(!controller)
        {
            continue;
        }

        std::string key  = MakeDeviceKey(controller);
        std::string name = controller->GetName();

        for(unsigned int zone_idx = 0; zone_idx < controller->GetZoneCount(); zone_idx++)
        {
            ZonePlacement* match = nullptr;

            /*---------------------------------------------*\
            | 1. Exact key + zone index                     |
            \*---------------------------------------------*/
            for(ZonePlacement& z : zones)
            {
                if(!z.controller && z.device_key == key && z.zone_idx == zone_idx)
                {
                    match = &z;
                    break;
                }
            }

            /*---------------------------------------------*\
            | 2. Same device name + zone (location / port   |
            |    changed since the layout was saved)        |
            \*---------------------------------------------*/
            if(!match)
            {
                for(ZonePlacement& z : zones)
                {
                    if(!z.controller && z.device_name == name && z.zone_idx == zone_idx
                    && z.zone_name == controller->GetZoneName(zone_idx))
                    {
                        match = &z;
                        break;
                    }
                }
            }

            bool is_new = false;
            if(!match)
            {
                zones.emplace_back();
                match  = &zones.back();
                is_new = true;
            }

            match->device_key   = key;
            match->device_name  = name;
            match->zone_idx     = zone_idx;
            match->zone_name    = controller->GetZoneName(zone_idx);
            match->controller   = controller;
            match->start_index  = controller->GetZoneStartIndex(zone_idx);
            match->led_count    = controller->GetZoneLEDsCount(zone_idx);
            match->type         = controller->GetZoneType(zone_idx);
            match->matrix_w     = controller->GetZoneMatrixMapWidth(zone_idx);
            match->matrix_h     = controller->GetZoneMatrixMapHeight(zone_idx);
            match->RebuildDefaults();
            match->preview.resize(match->led_count, QColor(Qt::black));

            if(is_new)
            {
                match->ResetDefaultSize();
                match->x = (canvas_w - match->w) * 0.5;
                match->y = (canvas_h - match->h) * 0.5;
            }
        }
    }
}

void LayoutModel::AutoArrange()
{
    const double margin = 20.0;
    double cx = margin;
    double cy = margin;
    double col_w = 0.0;

    for(ZonePlacement& z : zones)
    {
        if(!z.enabled)
        {
            continue;
        }

        z.rotation = 0.0;

        if(cy + z.h > canvas_h - margin && cy > margin)
        {
            cx   += col_w + margin;
            cy    = margin;
            col_w = 0.0;
        }

        z.x    = cx;
        z.y    = cy;
        cy    += z.h + margin;
        col_w  = std::max(col_w, z.w);
    }
}

nlohmann::json LayoutModel::ToJson() const
{
    nlohmann::json j;
    j["canvas_w"] = canvas_w;
    j["canvas_h"] = canvas_h;

    nlohmann::json arr = nlohmann::json::array();
    for(const ZonePlacement& z : zones)
    {
        nlohmann::json zj;
        zj["device_key"]  = z.device_key;
        zj["device_name"] = z.device_name;
        zj["zone_idx"]    = z.zone_idx;
        zj["zone_name"]   = z.zone_name;
        zj["enabled"]     = z.enabled;
        zj["x"]           = z.x;
        zj["y"]           = z.y;
        zj["w"]           = z.w;
        zj["h"]           = z.h;
        zj["rotation"]    = z.rotation;
        zj["reverse"]     = z.reverse;
        if(!z.calibration.IsIdentity())
        {
            zj["calibration"] = z.calibration.ToJson();
        }

        nlohmann::json leds = nlohmann::json::object();
        for(const auto& kv : z.led_overrides)
        {
            leds[std::to_string(kv.first)] = { kv.second.x(), kv.second.y() };
        }
        zj["leds"] = leds;

        arr.push_back(zj);
    }
    j["zones"] = arr;
    return j;
}

void LayoutModel::FromJson(const nlohmann::json& j)
{
    if(!j.is_object())
    {
        return;
    }

    canvas_w = j.value("canvas_w", 1000.0);
    canvas_h = j.value("canvas_h", 600.0);

    zones.clear();

    if(!j.contains("zones") || !j["zones"].is_array())
    {
        return;
    }

    for(const auto& zj : j["zones"])
    {
        ZonePlacement z;
        z.device_key  = zj.value("device_key",  std::string());
        z.device_name = zj.value("device_name", std::string());
        z.zone_idx    = zj.value("zone_idx",    0u);
        z.zone_name   = zj.value("zone_name",   std::string());
        z.enabled     = zj.value("enabled",     false);
        z.x           = zj.value("x",           0.0);
        z.y           = zj.value("y",           0.0);
        z.w           = zj.value("w",           200.0);
        z.h           = zj.value("h",           30.0);
        z.rotation    = zj.value("rotation",    0.0);
        z.reverse     = zj.value("reverse",     false);
        if(zj.contains("calibration"))
        {
            z.calibration.FromJson(zj["calibration"]);
        }

        if(zj.contains("leds") && zj["leds"].is_object())
        {
            for(auto it = zj["leds"].begin(); it != zj["leds"].end(); ++it)
            {
                if(it.value().is_array() && it.value().size() == 2)
                {
                    z.led_overrides[(unsigned int)std::stoul(it.key())] =
                        QPointF(it.value()[0].get<double>(), it.value()[1].get<double>());
                }
            }
        }

        zones.push_back(z);
    }
}
