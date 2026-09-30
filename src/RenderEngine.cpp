/*---------------------------------------------------------*\
| RenderEngine.cpp                                          |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "RenderEngine.h"
#include <algorithm>
#include <cmath>

RenderEngine::RenderEngine(QObject* parent) : QObject(parent)
{
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &RenderEngine::OnTick);
    clock.start();
    timer.start(1000 / fps);    /* timer always runs; 'playing' gates animation */
}

void RenderEngine::SetPlaying(bool new_playing)
{
    playing = new_playing;
    last_ns = clock.nsecsElapsed();

    if(playing)
    {
        std::lock_guard<std::mutex> lock(device_mutex);
        custom_mode_set.clear();    /* re-assert Direct mode on start */
    }
}

void RenderEngine::SetOutputEnabled(bool enabled)
{
    output_enabled = enabled;

    std::lock_guard<std::mutex> lock(device_mutex);
    custom_mode_set.clear();
}

void RenderEngine::SetFPS(int new_fps)
{
    fps = std::clamp(new_fps, 1, 120);
    timer.start(std::max(1, 1000 / fps));
}

void RenderEngine::ReleaseDevices()
{
    std::lock_guard<std::mutex> lock(device_mutex);
    devices_valid   = false;
    devices_blocked = true;
    layout.Unbind();
    custom_mode_set.clear();
}

void RenderEngine::DeviceListReady()
{
    std::lock_guard<std::mutex> lock(device_mutex);
    devices_blocked = false;
}

bool RenderEngine::BindDevices(const std::function<std::vector<RGBControllerInterface*>()>& get_controllers)
{
    /*-----------------------------------------------------*    | Fetch the list while holding the lock so a rescan     |
    | starting on another thread (ReleaseDevices) waits for |
    | us rather than freeing controllers mid-bind           |
    \*-----------------------------------------------------*/
    std::lock_guard<std::mutex> lock(device_mutex);
    if(devices_blocked)
    {
        return false;
    }
    layout.Sync(get_controllers());
    devices_valid = true;
    custom_mode_set.clear();
    return true;
}

void RenderEngine::EnsureCustomModes()
{
    /*-----------------------------------------------------*\
    | Switch every device we drive into its Direct/Custom   |
    | mode once. Called with device_mutex held.             |
    \*-----------------------------------------------------*/
    for(ZonePlacement& z : layout.zones)
    {
        if(z.enabled && z.controller && custom_mode_set.find(z.controller) == custom_mode_set.end())
        {
            z.controller->SetCustomMode();
            custom_mode_set.insert(z.controller);
        }
    }
}

void RenderEngine::OnTick()
{
    qint64 now = clock.nsecsElapsed();
    double dt  = (now - last_ns) / 1e9;
    last_ns    = now;

    if(!playing)
    {
        return;
    }

    dt      = std::clamp(dt, 0.0, 0.25);    /* avoid jumps after stalls */
    phase  += dt * params.speed;
    time_s += dt;

    RenderFrame();
}

void RenderEngine::RenderFrame()
{
    std::lock_guard<std::mutex> lock(device_mutex);

    const double cw     = std::max(1.0, layout.canvas_w);
    const double ch     = std::max(1.0, layout.canvas_h);
    const double aspect = cw / ch;
    const bool   send   = playing && output_enabled && devices_valid;

    if(send)
    {
        EnsureCustomModes();
    }

    std::set<RGBControllerInterface*> touched;

    for(ZonePlacement& z : layout.zones)
    {
        if(!z.enabled)
        {
            continue;
        }

        if(z.preview.size() != z.led_count)
        {
            z.preview.resize(z.led_count);
        }

        for(unsigned int i = 0; i < z.led_count; i++)
        {
            QPointF world = z.LedWorld(i);
            float r, g, b;

            EvaluateEffect(params, gradient, world.x() / cw, world.y() / ch, aspect, phase, time_s, r, g, b);

            int ri = std::clamp((int)std::lround(r * 255.0f), 0, 255);
            int gi = std::clamp((int)std::lround(g * 255.0f), 0, 255);
            int bi = std::clamp((int)std::lround(b * 255.0f), 0, 255);

            z.preview[i] = QColor(ri, gi, bi);

            if(send && z.controller)
            {
                z.controller->SetColor(z.start_index + i, ToRGBColor(ri, gi, bi));
            }
        }

        if(send && z.controller)
        {
            touched.insert(z.controller);
        }
    }

    /*-----------------------------------------------------*\
    | UpdateLEDs() is asynchronous in OpenRGB 1.0 - it just |
    | flags the controller's own update thread              |
    \*-----------------------------------------------------*/
    for(RGBControllerInterface* c : touched)
    {
        c->UpdateLEDs();
    }

    emit FrameRendered();
}

nlohmann::json RenderEngine::ToJson() const
{
    return {
        {"layout",   layout.ToJson()},
        {"effect",   params.ToJson()},
        {"gradient", gradient.ToJson()},
        {"fps",      fps},
        {"playing",  playing},
        {"output",   output_enabled},
    };
}

void RenderEngine::FromJson(const nlohmann::json& j)
{
    if(!j.is_object())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(device_mutex);
        if(j.contains("layout"))
        {
            layout.FromJson(j["layout"]);
            devices_valid = false;
        }
    }

    if(j.contains("effect"))   params.FromJson(j["effect"]);
    if(j.contains("gradient")) gradient.FromJson(j["gradient"]);

    SetFPS(j.value("fps", fps));
    SetOutputEnabled(j.value("output", output_enabled));
    SetPlaying(j.value("playing", playing));
}
