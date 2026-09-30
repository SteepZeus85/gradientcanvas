/*---------------------------------------------------------*\
| GradientCanvasPlugin.cpp                                  |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "GradientCanvasPlugin.h"
#include "RenderEngine.h"
#include "ResourceManagerCallback.h"
#include "ui/CanvasWidget.h"

#include <QConicalGradient>
#include <QImage>
#include <QMetaObject>
#include <QPainter>

#ifndef GRADIENT_CANVAS_VERSION
#define GRADIENT_CANVAS_VERSION "1.0.0"
#endif
#ifndef GRADIENT_CANVAS_COMMIT
#define GRADIENT_CANVAS_COMMIT  "local"
#endif

static const char* PLUGIN_NAME  = "Gradient Canvas";
static const char* SETTINGS_KEY = "GradientCanvasPlugin";

/*---------------------------------------------------------*\
| Log helper - level 3 is LL_INFO in OpenRGB's LogManager   |
\*---------------------------------------------------------*/
#define GC_LOG(api, ...) do { if(api) api->LogEntry(__FILE__, __LINE__, 3, __VA_ARGS__); } while(0)

static QImage MakeIcon(int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    QConicalGradient g(size / 2.0, size / 2.0, 90);
    g.setColorAt(0.00, QColor(255,   0, 128));
    g.setColorAt(0.33, QColor(255, 140,   0));
    g.setColorAt(0.66, QColor(  0, 200, 255));
    g.setColorAt(1.00, QColor(255,   0, 128));

    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(size * 0.06, size * 0.06, size * 0.88, size * 0.88), size * 0.2, size * 0.2);

    p.setBrush(QColor(255, 255, 255, 220));
    double d = size * 0.16;
    for(int r = 0; r < 3; r++)
    {
        for(int c = 0; c < 3; c++)
        {
            p.drawEllipse(QPointF(size * (0.28 + 0.22 * c), size * (0.28 + 0.22 * r)), d / 2, d / 2);
        }
    }
    return img;
}

GradientCanvasPlugin::GradientCanvasPlugin()
{
}

GradientCanvasPlugin::~GradientCanvasPlugin()
{
}

OpenRGBPluginInfo GradientCanvasPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name           = PLUGIN_NAME;
    info.Description    = "Animated gradients, spirals, waves and plasma mapped across a 2D layout of all your devices";
    info.Version        = GRADIENT_CANVAS_VERSION;
    info.Commit         = GRADIENT_CANVAS_COMMIT;
    info.URL            = "";
    info.Icon           = MakeIcon(64);

    info.Location       = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label          = "Gradient Canvas";
    info.TabIconString  = "";
    info.TabIcon        = MakeIcon(16);

    info.ProtocolVersion = OPENRGB_PLUGIN_API_VERSION;

    return info;
}

unsigned int GradientCanvasPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void GradientCanvasPlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    api = plugin_api_ptr;
    GC_LOG(api, "[%s] Loaded", PLUGIN_NAME);
}

QWidget* GradientCanvasPlugin::GetWidget()
{
    /*-----------------------------------------------------*\
    | Created here (not in Load) so the engine's QTimer     |
    | lives on the GUI thread                               |
    \*-----------------------------------------------------*/
    if(!engine)
    {
        engine = new RenderEngine();

        if(api)
        {
            nlohmann::json settings = api->GetSettings(SETTINGS_KEY);
            if(settings.is_object() && !settings.empty())
            {
                engine->FromJson(settings);
            }
            engine->BindDevices([this]() { return api->GetRGBControllers(); });
        }
    }

    if(!widget)
    {
        widget = new CanvasWidget(engine, [this]() { SaveSettings(); });
    }

    return widget;
}

QMenu* GradientCanvasPlugin::GetTrayMenu()
{
    return nullptr;
}

void GradientCanvasPlugin::Unload()
{
    if(engine)
    {
        engine->SetPlaying(false);
        SaveSettings();
    }

    delete widget.data();
    widget = nullptr;

    delete engine;
    engine = nullptr;

    GC_LOG(api, "[%s] Unloaded", PLUGIN_NAME);
}

void GradientCanvasPlugin::OnProfileAboutToLoad()
{
}

void GradientCanvasPlugin::OnProfileLoad(nlohmann::json profile_data)
{
    if(!engine)
    {
        return;
    }

    QMetaObject::invokeMethod(engine, [this, profile_data]()
    {
        engine->FromJson(profile_data);
        RebindDevices();
    }, Qt::QueuedConnection);
}

nlohmann::json GradientCanvasPlugin::OnProfileSave()
{
    return engine ? engine->ToJson() : nlohmann::json();
}

unsigned char* GradientCanvasPlugin::OnSDKCommand(unsigned int, unsigned char*, unsigned int* pkt_size)
{
    if(pkt_size)
    {
        *pkt_size = 0;
    }
    return nullptr;
}

void GradientCanvasPlugin::ProfileManagerUpdated(unsigned int)
{
}

void GradientCanvasPlugin::ResourceManagerUpdated(unsigned int update_reason)
{
    if(!engine)
    {
        return;
    }

    switch(update_reason)
    {
        /*-------------------------------------------------*\
        | Controllers may be freed after this - drop every  |
        | pointer synchronously (we're on OpenRGB's thread) |
        \*-------------------------------------------------*/
        case RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED:
            engine->ReleaseDevices();
            break;

        /*-------------------------------------------------*\
        | New list is ready - rebind on the GUI thread      |
        \*-------------------------------------------------*/
        case RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE:
        case RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED:
            engine->DeviceListReady();
            QMetaObject::invokeMethod(engine, [this]() { RebindDevices(); }, Qt::QueuedConnection);
            break;

        default:
            break;
    }
}

void GradientCanvasPlugin::SettingsManagerUpdated(unsigned int)
{
}

void GradientCanvasPlugin::RebindDevices()
{
    if(!engine || !api)
    {
        return;
    }

    /*-----------------------------------------------------*\
    | If a rescan started meanwhile this binds nothing and  |
    | a later DETECTION_COMPLETE rebinds. The widget is     |
    | reloaded either way since the model may have changed |
    \*-----------------------------------------------------*/
    engine->BindDevices([this]() { return api->GetRGBControllers(); });

    if(widget)
    {
        widget->ReloadAll();
    }
}

void GradientCanvasPlugin::SaveSettings()
{
    if(!engine || !api)
    {
        return;
    }

    api->SetSettings(SETTINGS_KEY, engine->ToJson());
    api->SaveSettings();
}
