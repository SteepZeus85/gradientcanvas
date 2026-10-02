/*---------------------------------------------------------*\
| RenderEngine.cpp                                          |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "RenderEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>

RenderEngine::RenderEngine(QObject* parent) : QObject(parent)
{
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &RenderEngine::OnTick);
    clock.start();
    timer.start(1000 / fps);    /* timer always runs; 'playing' gates animation */

    output_thread = std::thread(&RenderEngine::OutputThreadFunction, this);
}

RenderEngine::~RenderEngine()
{
    timer.stop();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        output_running = false;
        pending_frame.reset();
    }
    queue_cv.notify_all();
    if(output_thread.joinable())
    {
        output_thread.join();
    }
}

void RenderEngine::SetPlaying(bool new_playing)
{
    playing = new_playing;
    last_ns = clock.nsecsElapsed();

    if(playing)
    {
        reset_modes = true;     /* re-assert Direct mode on start */
    }
}

void RenderEngine::SetOutputEnabled(bool enabled)
{
    output_enabled = enabled;
    reset_modes    = true;
}

void RenderEngine::SetFPS(int new_fps)
{
    fps = std::clamp(new_fps, 1, 120);
    timer.start(std::max(1, 1000 / fps));
}

/*---------------------------------------------------------*\
| Device lifetime                                           |
\*---------------------------------------------------------*/
void RenderEngine::ReleaseDevices()
{
    {
        std::lock_guard<std::mutex> lock(bind_mutex);
        devices_valid   = false;
        devices_blocked = true;
        generation++;               /* invalidates any frame already queued */
        layout.Unbind();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        pending_frame.reset();
    }

    /*-----------------------------------------------------*\
    | Wait for a frame that's mid-write to finish. After    |
    | this the output thread re-checks the generation and   |
    | will not touch the old controllers again.             |
    \*-----------------------------------------------------*/
    std::lock_guard<std::mutex> wait(output_mutex);
}

void RenderEngine::DeviceListReady()
{
    std::lock_guard<std::mutex> lock(bind_mutex);
    devices_blocked = false;
}

bool RenderEngine::BindDevices(const std::function<std::vector<RGBControllerInterface*>()>& get_controllers)
{
    /*-----------------------------------------------------*\
    | Fetch the list while holding the lock so a rescan     |
    | starting on another thread (ReleaseDevices) waits for |
    | us rather than freeing controllers mid-bind           |
    \*-----------------------------------------------------*/
    std::lock_guard<std::mutex> lock(bind_mutex);
    if(devices_blocked)
    {
        return false;
    }
    generation++;
    layout.Sync(get_controllers());
    devices_valid = true;
    reset_modes   = true;
    return true;
}

/*---------------------------------------------------------*\
| GUI thread: timer + effect                                |
\*---------------------------------------------------------*/
void RenderEngine::OnTick()
{
    qint64 now = clock.nsecsElapsed();
    double dt  = (now - last_ns) / 1e9;
    last_ns    = now;

    if(!playing)
    {
        if(test_active)
        {
            RenderFrame();      /* keep re-sending so replugged devices update too */
        }
        return;
    }

    dt      = std::clamp(dt, 0.0, 0.25);    /* avoid jumps after stalls */
    phase  += dt * params.speed;
    time_s += dt;

    RenderFrame();
}

void RenderEngine::RenderFrame()
{
    const double cw     = std::max(1.0, layout.canvas_w);
    const double ch     = std::max(1.0, layout.canvas_h);
    const double aspect = cw / ch;
    const bool   wanted = (playing || test_active || force_send_once) && output_enabled && !suspended;
    force_send_once     = false;

    std::unique_ptr<OutFrame> frame;

    /*-----------------------------------------------------*\
    | Take a snapshot of the controller bindings. The lock  |
    | is only held while copying pointers - never while     |
    | talking to hardware.                                  |
    \*-----------------------------------------------------*/
    std::vector<RGBControllerInterface*> bound(layout.zones.size(), nullptr);
    {
        std::lock_guard<std::mutex> lock(bind_mutex);
        if(wanted && devices_valid)
        {
            frame             = std::make_unique<OutFrame>();
            frame->generation = generation;
            for(std::size_t zi = 0; zi < layout.zones.size(); zi++)
            {
                bound[zi] = layout.zones[zi].controller;
            }
        }
    }

    for(std::size_t zi = 0; zi < layout.zones.size(); zi++)
    {
        ZonePlacement& z = layout.zones[zi];
        if(!z.enabled)
        {
            continue;
        }

        if(z.preview.size() != z.led_count)
        {
            z.preview.resize(z.led_count);
        }

        OutZone* out = nullptr;
        if(frame && bound[zi])
        {
            frame->zones.push_back({bound[zi], z.start_index, std::vector<RGBColor>(z.led_count)});
            out = &frame->zones.back();
        }

        const bool lit = !test_active || test_only_zones.empty() || test_only_zones.count((int)zi);

        for(unsigned int i = 0; i < z.led_count; i++)
        {
            float r, g, b;

            if(test_active)
            {
                r = lit ? (float)test_color.redF()   : 0.0f;
                g = lit ? (float)test_color.greenF() : 0.0f;
                b = lit ? (float)test_color.blueF()  : 0.0f;
            }
            else
            {
                QPointF world = z.LedWorld(i);
                EvaluateEffect(params, gradient, world.x() / cw, world.y() / ch, aspect, phase, time_s, r, g, b);
            }

            /*---------------------------------------------*\
            | Preview shows the intended colour...          |
            \*---------------------------------------------*/
            z.preview[i] = QColor(std::clamp((int)std::lround(r * 255.0f), 0, 255),
                                  std::clamp((int)std::lround(g * 255.0f), 0, 255),
                                  std::clamp((int)std::lround(b * 255.0f), 0, 255));

            /*---------------------------------------------*\
            | ...devices get the calibrated one             |
            \*---------------------------------------------*/
            if(out)
            {
                if(!z.calibration.IsIdentity())
                {
                    z.calibration.Apply(r, g, b);
                }
                int ri = std::clamp((int)std::lround(r * 255.0f), 0, 255);
                int gi = std::clamp((int)std::lround(g * 255.0f), 0, 255);
                int bi = std::clamp((int)std::lround(b * 255.0f), 0, 255);
                out->colors[i] = ToRGBColor(ri, gi, bi);
            }
        }
    }

    /*-----------------------------------------------------*\
    | Hand over to the output thread. Latest frame wins: if |
    | the hardware is slower than the frame rate, older     |
    | frames are dropped instead of queueing up (which is   |
    | what made changes take seconds to appear).            |
    \*-----------------------------------------------------*/
    if(frame && !frame->zones.empty())
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if(pending_frame)
            {
                dropped_frames++;
            }
            pending_frame = std::move(frame);
        }
        queue_cv.notify_one();
    }

    emit FrameRendered();
}

/*---------------------------------------------------------*\
| Output thread                                             |
\*---------------------------------------------------------*/
void RenderEngine::OutputThreadFunction()
{
    while(true)
    {
        std::unique_ptr<OutFrame> frame;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this]() { return !output_running || pending_frame; });
            if(!output_running)
            {
                return;
            }
            frame = std::move(pending_frame);
        }

        std::lock_guard<std::mutex> out_lock(output_mutex);

        /*-------------------------------------------------*\
        | Re-check after taking output_mutex: if a rescan   |
        | started, the generation changed and these         |
        | controller pointers may already be freed          |
        \*-------------------------------------------------*/
        if(frame->generation != generation.load() || suspended)
        {
            continue;
        }

        WriteFrame(*frame);
    }
}

void RenderEngine::WriteFrame(OutFrame& frame)
{
    auto t0 = std::chrono::steady_clock::now();

    /* consume the reset flag unconditionally (no short-circuit) */
    bool reset = reset_modes.exchange(false);
    if(reset || frame.generation != output_generation)
    {
        output_generation = frame.generation;
        custom_mode_set.clear();
        last_sent.clear();
    }

    /*-----------------------------------------------------*\
    | Group zones by controller                             |
    \*-----------------------------------------------------*/
    std::map<RGBControllerInterface*, std::vector<OutZone*>> by_controller;
    for(OutZone& oz : frame.zones)
    {
        by_controller[oz.controller].push_back(&oz);
    }

    for(auto& entry : by_controller)
    {
        RGBControllerInterface* c = entry.first;

        if(custom_mode_set.insert(c).second)
        {
            c->SetCustomMode();
        }

        /*-------------------------------------------------*\
        | Skip devices whose colours haven't changed - each |
        | UpdateLEDs() costs a hardware write and makes     |
        | OpenRGB repaint that device's view                |
        \*-------------------------------------------------*/
        unsigned int led_count = c->GetLEDCount();
        std::vector<RGBColor>& prev = last_sent[c];
        if(prev.size() != led_count)
        {
            prev.assign(led_count, 0xFFFFFFFF);
        }

        bool changed = false;
        for(OutZone* oz : entry.second)
        {
            for(std::size_t i = 0; i < oz->colors.size(); i++)
            {
                std::size_t idx = oz->start + i;
                if(idx < led_count && prev[idx] != oz->colors[i])
                {
                    changed = true;
                    break;
                }
            }
            if(changed) break;
        }
        if(!changed)
        {
            continue;
        }

        /*-------------------------------------------------*\
        | Write straight into the colour buffer: SetColor() |
        | takes an exclusive lock per LED, and every one of |
        | those waits for any hardware write in progress.   |
        | 32-bit stores can't tear, and the device thread   |
        | only ever reads this buffer.                      |
        \*-------------------------------------------------*/
        RGBColor* colors = c->GetColorsPointer();

        for(OutZone* oz : entry.second)
        {
            for(std::size_t i = 0; i < oz->colors.size(); i++)
            {
                std::size_t idx = oz->start + i;
                if(idx >= led_count)
                {
                    break;
                }
                if(colors)
                {
                    colors[idx] = oz->colors[i];
                }
                else
                {
                    c->SetColor((unsigned int)idx, oz->colors[i]);
                }
                prev[idx] = oz->colors[i];
            }
        }

        /*-------------------------------------------------*\
        | Asynchronous in OpenRGB 1.0 - flags the device's  |
        | own update thread                                 |
        \*-------------------------------------------------*/
        c->UpdateLEDs();
    }

    last_output_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

/*---------------------------------------------------------*\
| Persistence                                               |
\*---------------------------------------------------------*/
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

nlohmann::json RenderEngine::ProfileJson() const
{
    nlohmann::json j = ToJson();
    j.erase("layout");
    return j;
}

void RenderEngine::ApplyProfileJson(const nlohmann::json& j)
{
    if(!j.is_object())
    {
        return;
    }
    nlohmann::json look = j;
    look.erase("layout");       /* older (1.0.1) profiles carried the layout too */
    FromJson(look);
}

void RenderEngine::FromJson(const nlohmann::json& j)
{
    if(!j.is_object())
    {
        return;
    }

    if(j.contains("layout"))
    {
        std::lock_guard<std::mutex> lock(bind_mutex);
        layout.FromJson(j["layout"]);
        devices_valid = false;
        generation++;
    }

    if(j.contains("effect"))   params.FromJson(j["effect"]);
    if(j.contains("gradient")) gradient.FromJson(j["gradient"]);

    SetFPS(j.value("fps", fps));
    SetOutputEnabled(j.value("output", output_enabled));
    SetPlaying(j.value("playing", playing));
}

/*---------------------------------------------------------*\
| Test pattern / suspend                                    |
\*---------------------------------------------------------*/
void RenderEngine::SetTestPattern(bool active, const QColor& color, const std::set<int>& only_zones)
{
    /*-----------------------------------------------------*\
    | Leaving the test pattern while paused: send one frame |
    | of the paused effect so devices don't stay stuck on   |
    | the test colour                                       |
    \*-----------------------------------------------------*/
    force_send_once = test_active && !active && !playing;
    test_active     = active;
    test_color      = color;
    test_only_zones = only_zones;
    reset_modes     = true;
    RenderFrame();
}

void RenderEngine::Suspend()
{
    suspended = true;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        pending_frame.reset();
    }
    /* wait out any frame currently being written */
    std::lock_guard<std::mutex> wait(output_mutex);
}

void RenderEngine::Resume()
{
    suspended   = false;
    reset_modes = true;     /* profile may have changed device modes */
}
