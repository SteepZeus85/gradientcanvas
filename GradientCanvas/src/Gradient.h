/*---------------------------------------------------------*\
| Gradient.h                                                |
|                                                           |
|   Multi-stop colour gradient with perceptual (OKLab)      |
|   interpolation, presets and JSON serialisation           |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <vector>
#include <nlohmann/json.hpp>

struct GradientStop
{
    double  pos;    /* 0.0 - 1.0 */
    QColor  color;
};

class Gradient
{
public:
    Gradient();

    /*-----------------------------------------------------*\
    | Sample the gradient.                                  |
    |   t       - position, any real number                 |
    |   cyclic  - true: wraps last stop back to first so    |
    |             scrolling gradients have no seam          |
    |   Returns 0-1 floats in r,g,b                         |
    \*-----------------------------------------------------*/
    void            Sample(double t, bool cyclic, float& r, float& g, float& b) const;

    const std::vector<GradientStop>& Stops() const { return stops; }
    void            SetStops(const std::vector<GradientStop>& new_stops);
    void            AddStop(double pos, const QColor& color);
    void            RemoveStop(int index);
    void            MoveStop(int index, double pos);
    void            SetStopColor(int index, const QColor& color);

    nlohmann::json  ToJson() const;
    void            FromJson(const nlohmann::json& j);

    static QStringList  PresetNames();
    static Gradient     Preset(const QString& name);

private:
    void            Sort();
    void            RebuildLUT();

    std::vector<GradientStop>   stops;

    /*-----------------------------------------------------*\
    | Pre-computed lookup tables (linear / cyclic) so the   |
    | per-LED cost is a table read rather than OKLab maths  |
    \*-----------------------------------------------------*/
    static const int            LUT_SIZE = 1024;
    std::vector<float>          lut_linear;
    std::vector<float>          lut_cyclic;
};
