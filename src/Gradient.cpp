/*---------------------------------------------------------*\
| Gradient.cpp                                              |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "Gradient.h"
#include <algorithm>
#include <cmath>

/*---------------------------------------------------------*\
| OKLab helpers - interpolating in OKLab keeps blends vivid |
| (no muddy grey midpoint between complementary colours)    |
\*---------------------------------------------------------*/
namespace
{
    struct Lab { float L, a, b; };

    float SrgbToLinear(float c)
    {
        return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    float LinearToSrgb(float c)
    {
        c = std::clamp(c, 0.0f, 1.0f);
        return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    Lab ToOklab(const QColor& col)
    {
        float r = SrgbToLinear((float)col.redF());
        float g = SrgbToLinear((float)col.greenF());
        float b = SrgbToLinear((float)col.blueF());

        float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
        float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
        float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);

        return {
            0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s
        };
    }

    void FromOklab(const Lab& c, float& r, float& g, float& b)
    {
        float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
        float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
        float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;

        l = l * l * l;
        m = m * m * m;
        s = s * s * s;

        r = LinearToSrgb( 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
        g = LinearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
        b = LinearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    }

    void BuildTable(const std::vector<GradientStop>& stops, bool cyclic, int size, std::vector<float>& out)
    {
        out.assign(size * 3, 0.0f);

        if(stops.empty())
        {
            return;
        }

        /*-------------------------------------------------*\
        | Build an extended stop list. For cyclic tables    |
        | add a copy of the first stop at pos+1 and last    |
        | stop at pos-1 so interpolation wraps cleanly      |
        \*-------------------------------------------------*/
        std::vector<std::pair<double, Lab>> ext;
        if(cyclic)
        {
            ext.push_back({stops.back().pos - 1.0, ToOklab(stops.back().color)});
        }
        for(const GradientStop& s : stops)
        {
            ext.push_back({s.pos, ToOklab(s.color)});
        }
        if(cyclic)
        {
            ext.push_back({stops.front().pos + 1.0, ToOklab(stops.front().color)});
        }

        for(int i = 0; i < size; i++)
        {
            double t = (double)i / (double)(cyclic ? size : (size - 1));
            Lab    c;

            if(t <= ext.front().first)
            {
                c = ext.front().second;
            }
            else if(t >= ext.back().first)
            {
                c = ext.back().second;
            }
            else
            {
                std::size_t k = 1;
                while(k < ext.size() && ext[k].first < t) k++;

                const auto& a = ext[k - 1];
                const auto& b = ext[k];
                double span = b.first - a.first;
                float  f    = (span > 1e-9) ? (float)((t - a.first) / span) : 0.0f;

                c.L = a.second.L + (b.second.L - a.second.L) * f;
                c.a = a.second.a + (b.second.a - a.second.a) * f;
                c.b = a.second.b + (b.second.b - a.second.b) * f;
            }

            FromOklab(c, out[i * 3 + 0], out[i * 3 + 1], out[i * 3 + 2]);
        }
    }
}

Gradient::Gradient()
{
    stops = {
        {0.00, QColor(255,   0, 128)},
        {0.33, QColor(255, 140,   0)},
        {0.66, QColor(  0, 200, 255)},
    };
    RebuildLUT();
}

void Gradient::Sample(double t, bool cyclic, float& r, float& g, float& b) const
{
    if(cyclic)
    {
        t = t - std::floor(t);
    }
    else
    {
        t = std::clamp(t, 0.0, 1.0);
    }

    const std::vector<float>& lut = cyclic ? lut_cyclic : lut_linear;

    double fi  = t * (cyclic ? LUT_SIZE : (LUT_SIZE - 1));
    int    i0  = (int)fi;
    float  f   = (float)(fi - i0);
    int    i1  = cyclic ? (i0 + 1) % LUT_SIZE : std::min(i0 + 1, LUT_SIZE - 1);
    i0         = cyclic ? i0 % LUT_SIZE : std::min(i0, LUT_SIZE - 1);

    r = lut[i0 * 3 + 0] + (lut[i1 * 3 + 0] - lut[i0 * 3 + 0]) * f;
    g = lut[i0 * 3 + 1] + (lut[i1 * 3 + 1] - lut[i0 * 3 + 1]) * f;
    b = lut[i0 * 3 + 2] + (lut[i1 * 3 + 2] - lut[i0 * 3 + 2]) * f;
}

void Gradient::SetStops(const std::vector<GradientStop>& new_stops)
{
    stops = new_stops;
    if(stops.empty())
    {
        stops.push_back({0.0, Qt::white});
    }
    Sort();
    RebuildLUT();
}

void Gradient::AddStop(double pos, const QColor& color)
{
    stops.push_back({std::clamp(pos, 0.0, 1.0), color});
    Sort();
    RebuildLUT();
}

void Gradient::RemoveStop(int index)
{
    if(stops.size() > 1 && index >= 0 && index < (int)stops.size())
    {
        stops.erase(stops.begin() + index);
        RebuildLUT();
    }
}

void Gradient::MoveStop(int index, double pos)
{
    if(index >= 0 && index < (int)stops.size())
    {
        stops[index].pos = std::clamp(pos, 0.0, 1.0);
        RebuildLUT();   /* no sort - caller keeps its index while dragging */
    }
}

void Gradient::SetStopColor(int index, const QColor& color)
{
    if(index >= 0 && index < (int)stops.size())
    {
        stops[index].color = color;
        RebuildLUT();
    }
}

void Gradient::Sort()
{
    std::stable_sort(stops.begin(), stops.end(),
        [](const GradientStop& a, const GradientStop& b){ return a.pos < b.pos; });
}

void Gradient::RebuildLUT()
{
    std::vector<GradientStop> sorted = stops;
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const GradientStop& a, const GradientStop& b){ return a.pos < b.pos; });

    BuildTable(sorted, false, LUT_SIZE, lut_linear);
    BuildTable(sorted, true,  LUT_SIZE, lut_cyclic);
}

nlohmann::json Gradient::ToJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for(const GradientStop& s : stops)
    {
        arr.push_back({{"pos", s.pos}, {"color", s.color.name().toStdString()}});
    }
    return arr;
}

void Gradient::FromJson(const nlohmann::json& j)
{
    if(!j.is_array() || j.empty())
    {
        return;
    }

    std::vector<GradientStop> new_stops;
    for(const auto& s : j)
    {
        if(s.contains("pos") && s.contains("color"))
        {
            new_stops.push_back({s["pos"].get<double>(),
                                 QColor(QString::fromStdString(s["color"].get<std::string>()))});
        }
    }
    SetStops(new_stops);
}

/*---------------------------------------------------------*\
| Presets                                                   |
\*---------------------------------------------------------*/
QStringList Gradient::PresetNames()
{
    return { "Rainbow", "Neon Sunset", "Ocean", "Cyberpunk", "Fire", "Aurora",
             "Vaporwave", "Forest", "Ice", "Lava Lamp", "Mono White" };
}

Gradient Gradient::Preset(const QString& name)
{
    Gradient g;
    std::vector<GradientStop> s;

    if(name == "Rainbow")
    {
        s = { {0.0, QColor(255,0,0)}, {1.0/6, QColor(255,255,0)}, {2.0/6, QColor(0,255,0)},
              {3.0/6, QColor(0,255,255)}, {4.0/6, QColor(0,0,255)}, {5.0/6, QColor(255,0,255)} };
    }
    else if(name == "Neon Sunset")
    {
        s = { {0.0, QColor(255,0,128)}, {0.33, QColor(255,140,0)}, {0.66, QColor(0,200,255)} };
    }
    else if(name == "Ocean")
    {
        s = { {0.0, QColor(0,20,120)}, {0.35, QColor(0,120,255)}, {0.65, QColor(0,255,200)}, {0.85, QColor(0,80,200)} };
    }
    else if(name == "Cyberpunk")
    {
        s = { {0.0, QColor(255,0,200)}, {0.5, QColor(0,255,255)}, {0.75, QColor(120,0,255)} };
    }
    else if(name == "Fire")
    {
        s = { {0.0, QColor(255,0,0)}, {0.4, QColor(255,90,0)}, {0.6, QColor(255,200,0)}, {0.8, QColor(255,40,0)} };
    }
    else if(name == "Aurora")
    {
        s = { {0.0, QColor(0,255,120)}, {0.3, QColor(0,180,255)}, {0.6, QColor(160,0,255)}, {0.85, QColor(0,255,200)} };
    }
    else if(name == "Vaporwave")
    {
        s = { {0.0, QColor(255,113,206)}, {0.33, QColor(1,205,254)}, {0.66, QColor(185,103,255)} };
    }
    else if(name == "Forest")
    {
        s = { {0.0, QColor(0,120,0)}, {0.4, QColor(120,255,0)}, {0.7, QColor(0,200,80)} };
    }
    else if(name == "Ice")
    {
        s = { {0.0, QColor(255,255,255)}, {0.4, QColor(120,200,255)}, {0.7, QColor(0,80,255)} };
    }
    else if(name == "Lava Lamp")
    {
        s = { {0.0, QColor(255,0,60)}, {0.5, QColor(255,120,0)}, {0.8, QColor(140,0,255)} };
    }
    else
    {
        s = { {0.0, QColor(255,255,255)}, {0.5, QColor(40,40,40)} };
    }

    g.SetStops(s);
    return g;
}
