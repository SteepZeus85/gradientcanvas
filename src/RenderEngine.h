/*---------------------------------------------------------*\
| RenderEngine.h                                            |
|                                                           |
|   Frame timer: evaluates the active effect at every LED's |
|   position on the layout map and hands the colours to a   |
|   background output thread that pushes them to devices.   |
|                                                           |
|   Threading model                                         |
|     GUI thread     - timer, effect maths, preview, layout |
|     output thread  - the ONLY code that writes to device  |
|                      controllers. Slow hardware (SMBus    |
|                      RAM, some USB) can block for tens of |
|                      ms per write; doing that on the GUI  |
|                      thread froze OpenRGB's window.       |
|     OpenRGB thread - ReleaseDevices() on rescans          |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QColor>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include "Effects.h"
#include "Gradient.h"
#include "LayoutModel.h"

class RenderEngine : public QObject
{
    Q_OBJECT

public:
    explicit RenderEngine(QObject* parent = nullptr);
    ~RenderEngine() override;

    LayoutModel     layout;
    EffectParams    params;
    Gradient        gradient;

    void            SetPlaying(bool playing);
    bool            IsPlaying() const { return playing; }

    void            SetOutputEnabled(bool enabled);
    bool            IsOutputEnabled() const { return output_enabled; }

    void            SetFPS(int fps);
    int             GetFPS() const { return fps; }

    double          Phase() const { return phase; }
    double          Time()  const { return time_s; }

    /*-----------------------------------------------------*\
    | Device lifetime handling.                             |
    |   ReleaseDevices() is safe to call from any thread    |
    |   (OpenRGB calls it from its detection thread before  |
    |   controllers are freed). When it returns, the output |
    |   thread is guaranteed not to touch old controllers.  |
    |   DeviceListReady() is called (any thread) once the   |
    |   controller list is consistent again.                |
    |   BindDevices() must be called on the GUI thread; it  |
    |   returns false if a rescan is in progress.           |
    \*-----------------------------------------------------*/
    void            ReleaseDevices();
    void            DeviceListReady();
    bool            BindDevices(const std::function<std::vector<RGBControllerInterface*>()>& get_controllers);

    /*-----------------------------------------------------*\
    | Remove our update callbacks from controllers that are |
    | still alive (call before a rescan frees them, and on  |
    | unload). Pass the current controller list.            |
    \*-----------------------------------------------------*/
    void            UnhookDevices(const std::vector<RGBControllerInterface*>& alive);

    /*-----------------------------------------------------*\
    | Render a single frame immediately (used to refresh    |
    | the preview after edits while paused)                 |
    \*-----------------------------------------------------*/
    void            RenderFrame();

    /*-----------------------------------------------------*\
    | Persistence                                           |
    |   ToJson / FromJson     - everything (plugin settings)|
    |   ProfileJson / Apply.. - look only (effect, gradient,|
    |                           play state) for OpenRGB     |
    |                           profiles. The layout map is |
    |                           physical, so it stays global|
    \*-----------------------------------------------------*/
    nlohmann::json  ToJson() const;
    void            FromJson(const nlohmann::json& j);
    nlohmann::json  ProfileJson() const;
    void            ApplyProfileJson(const nlohmann::json& j);

    /*-----------------------------------------------------*\
    | Calibration test pattern: while active, every placed  |
    | zone gets this solid colour (through its calibration) |
    | even when paused, so devices can be compared by eye.  |
    \*-----------------------------------------------------*/
    void            SetTestPattern(bool active, const QColor& color = Qt::white, const std::set<int>& only_zones = {});
    bool            TestPatternActive() const { return test_active; }

    /*-----------------------------------------------------*\
    | Suspend() stops device output immediately and is safe |
    | from any thread - used while OpenRGB applies a        |
    | profile so we never paint over it.                    |
    \*-----------------------------------------------------*/
    void            Suspend();
    void            Resume();
    bool            IsSuspended() const { return suspended; }

    /*-----------------------------------------------------*\
    | Standby: another OpenRGB window owns the lights, so   |
    | this one only previews. Safe from any thread.         |
    \*-----------------------------------------------------*/
    void            SetStandby(bool standby);
    bool            IsStandby() const { return standby; }

    /*-----------------------------------------------------*\
    | Diagnostics: how long the last device write took, and |
    | how many frames were skipped because hardware was     |
    | slower than the frame rate                            |
    \*-----------------------------------------------------*/
    double          LastOutputMs() const { return last_output_ms; }
    unsigned int    DroppedFrames() const { return dropped_frames; }

signals:
    void            FrameRendered();

private slots:
    void            OnTick();

private:
    /*-----------------------------------------------------*\
    | A frame handed to the output thread                   |
    \*-----------------------------------------------------*/
    /*-----------------------------------------------------*\
    | Flow control per device.                              |
    |                                                       |
    |   With OpenRGB 1.0's background service, UpdateLEDs() |
    |   only queues a network packet and returns at once.   |
    |   The service keeps an UNLIMITED queue per device and |
    |   drains it at hardware speed, so sending faster than |
    |   a slow device (SMBus RAM, mainboard) can take makes |
    |   the queue grow forever: the lights fall further and |
    |   further behind and the animation crawls.            |
    |                                                       |
    |   The service echoes every applied frame back to its  |
    |   clients, so we count frames in flight and only send |
    |   the next one when the device has caught up.         |
    \*-----------------------------------------------------*/
    struct FlowState
    {
        std::atomic<int>                pending     {0};    /* sent, not yet echoed */
        std::atomic<bool>               seen_echo   {false};
        std::atomic<bool>               in_call     {false};/* inside our UpdateLEDs() */
        std::atomic<std::thread::id>    caller;
        unsigned int                    sends       = 0;    /* output thread only */
        std::chrono::steady_clock::time_point last_send{};  /* output thread only */
    };
    static constexpr int MAX_IN_FLIGHT = 2;
    static void     OnControllerUpdate(void* arg, unsigned int reason, void* controller);
    FlowState*      FlowFor(RGBControllerInterface* c);     /* bind_mutex held */

    struct OutZone
    {
        RGBControllerInterface* controller;
        unsigned int            start;
        std::vector<RGBColor>   colors;
        FlowState*              flow;
    };
    struct OutFrame
    {
        uint64_t                generation;
        std::vector<OutZone>    zones;
    };

    void            OutputThreadFunction();
    void            WriteFrame(OutFrame& frame);

    QTimer                              timer;
    QElapsedTimer                       clock;
    qint64                              last_ns         = 0;
    double                              phase           = 0.0;
    double                              time_s          = 0.0;
    int                                 fps             = 30;
    bool                                playing         = false;
    bool                                output_enabled  = true;

    /*-----------------------------------------------------*\
    | Binding: protects the controller pointers in layout.  |
    | Only ever held briefly.                               |
    \*-----------------------------------------------------*/
    std::mutex                          bind_mutex;
    bool                                devices_valid   = false;
    bool                                devices_blocked = false;
    std::atomic<uint64_t>               generation      {1};

    /*-----------------------------------------------------*\
    | Output thread                                         |
    \*-----------------------------------------------------*/
    std::thread                         output_thread;
    std::mutex                          queue_mutex;
    std::condition_variable             queue_cv;
    std::unique_ptr<OutFrame>           pending_frame;
    bool                                output_running  = true;
    std::mutex                          output_mutex;       /* held while touching devices */
    std::set<RGBControllerInterface*>   custom_mode_set;    /* output thread only */
    std::map<RGBControllerInterface*, std::vector<RGBColor>> last_sent; /* output thread only */
    uint64_t                            output_generation = 0;
    std::atomic<bool>                   reset_modes     {true};
    std::atomic<double>                 last_output_ms  {0.0};
    std::atomic<unsigned int>           dropped_frames  {0};

    /*-----------------------------------------------------*\
    | FlowStates are never freed: OpenRGB may still be      |
    | running a copied callback list after we unregister.  |
    \*-----------------------------------------------------*/
    std::map<RGBControllerInterface*, FlowState*> flow;     /* bind_mutex */
    std::set<RGBControllerInterface*>   hooked;             /* bind_mutex */

    std::atomic<bool>                   suspended       {false};
    std::atomic<bool>                   standby         {false};
    bool                                test_active     = false;
    QColor                              test_color      = Qt::white;
    std::set<int>                       test_only_zones;
    bool                                force_send_once = false;
};
