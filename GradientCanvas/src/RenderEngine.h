/*---------------------------------------------------------*\
| RenderEngine.h                                            |
|                                                           |
|   Frame timer: evaluates the active effect at every LED's |
|   position on the layout map and pushes the colours to    |
|   the devices.                                            |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <mutex>
#include <set>
#include "Effects.h"
#include "Gradient.h"
#include "LayoutModel.h"

class RenderEngine : public QObject
{
    Q_OBJECT

public:
    explicit RenderEngine(QObject* parent = nullptr);

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
    |   controllers are freed).                             |
    |   DeviceListReady() is called (any thread) once the   |
    |   controller list is consistent again.                |
    |   BindDevices() must be called on the GUI thread; it  |
    |   fetches the list under the engine lock and returns  |
    |   false if a rescan is in progress.                   |
    \*-----------------------------------------------------*/
    void            ReleaseDevices();
    void            DeviceListReady();
    bool            BindDevices(const std::function<std::vector<RGBControllerInterface*>()>& get_controllers);

    /*-----------------------------------------------------*\
    | Render a single frame immediately (used to refresh    |
    | the preview after edits while paused)                 |
    \*-----------------------------------------------------*/
    void            RenderFrame();

    nlohmann::json  ToJson() const;
    void            FromJson(const nlohmann::json& j);

signals:
    void            FrameRendered();

private slots:
    void            OnTick();

private:
    void            EnsureCustomModes();

    QTimer                              timer;
    QElapsedTimer                       clock;
    qint64                              last_ns         = 0;
    double                              phase           = 0.0;
    double                              time_s          = 0.0;
    int                                 fps             = 30;
    bool                                playing         = false;
    bool                                output_enabled  = true;

    std::mutex                          device_mutex;
    bool                                devices_valid   = false;
    bool                                devices_blocked = false;
    std::set<RGBControllerInterface*>   custom_mode_set;
};
