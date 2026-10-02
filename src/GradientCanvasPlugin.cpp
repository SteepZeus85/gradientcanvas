/*---------------------------------------------------------*\
| GradientCanvasPlugin.cpp                                  |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "GradientCanvasPlugin.h"
#include "RenderEngine.h"
#include "InstanceGuard.h"
#include "ResourceManagerCallback.h"
#include "ui/CanvasWidget.h"

#include <QConicalGradient>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <fstream>
#include <regex>

#ifndef GRADIENT_CANVAS_VERSION
#define GRADIENT_CANVAS_VERSION "1.0.0"
#endif
#ifndef GRADIENT_CANVAS_COMMIT
#define GRADIENT_CANVAS_COMMIT  "local"
#endif

static const char* PLUGIN_NAME  = "Gradient Canvas";
static const char* SETTINGS_KEY = "GradientCanvasPlugin";

/*---------------------------------------------------------*\
| From ProfileManager.h (not included - it pulls in the     |
| full RGBController class, which plugins don't link)      |
\*---------------------------------------------------------*/
static const unsigned int PROFILE_LIST_UPDATED = 0;

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

        nlohmann::json settings = LoadSettingsFile();
        if(settings.is_object() && !settings.empty())
        {
            engine->FromJson(settings);
        }

        /*-------------------------------------------------*\
        | A profile OpenRGB auto-loaded at startup, before  |
        | our tab existed, takes priority                   |
        \*-------------------------------------------------*/
        if(!pending_profile.is_null())
        {
            engine->ApplyProfileJson(pending_profile);
            pending_profile = nlohmann::json();
        }

        if(api)
        {
            engine->BindDevices([this]() { return api->GetRGBControllers(); });
        }
    }

    if(!widget)
    {
        CanvasHooks hooks;
        hooks.save              = [this]() { SaveSettings(); };
        hooks.list_profiles     = [this]() { return api ? api->GetProfileList() : std::vector<std::string>(); };
        hooks.save_to_profile   = [this](const std::string& name) { return SaveToProfile(name); };
        hooks.load_profile      = [this](const std::string& name) { if(api) api->LoadProfile(name); };
        hooks.take_control      = [this]() { if(guard) guard->Claim(); };

        widget = new CanvasWidget(engine, hooks);
    }

    /*-----------------------------------------------------*    | Only one OpenRGB window may drive the lights. The     |
    | window opened last takes over; any other copy (e.g.   |
    | one minimised to the tray) drops to standby.          |
    \*-----------------------------------------------------*/
    if(!guard && api)
    {
        filesystem::path owner_path = SettingsFilePath();
        owner_path.replace_extension(".owner");

        guard = new InstanceGuard(QString::fromStdWString(owner_path.wstring()), engine);
        connect(guard, &InstanceGuard::OwnershipChanged, this, &GradientCanvasPlugin::OnOwnershipChanged);
        guard->Claim();
        OnOwnershipChanged(guard->IsOwner());
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
        SaveSettings();             /* save first so the play state survives a restart */
        engine->SetPlaying(false);
    }

    if(guard)
    {
        guard->Release();           /* lets a standby window take over at once */
        guard = nullptr;            /* deleted with the engine */
    }

    delete widget.data();
    widget = nullptr;

    delete engine;
    engine = nullptr;

    GC_LOG(api, "[%s] Unloaded", PLUGIN_NAME);
}

/*---------------------------------------------------------*\
| Profiles                                                  |
|                                                           |
|   Profile stores the *look* (effect, gradient, playing).  |
|   The layout map is physical and stays global.            |
|                                                           |
|   AboutToLoad  -> stop sending immediately so we never    |
|                   paint over the profile being applied    |
|   Load(data)   -> the profile has our settings: apply and |
|                   resume                                  |
|   no Load call -> the profile has no Gradient Canvas      |
|                   data: stay paused so its colours show   |
\*---------------------------------------------------------*/
void GradientCanvasPlugin::OnProfileAboutToLoad()
{
    if(!engine)
    {
        return;
    }

    engine->Suspend();
    profile_had_data = false;
    int seq = ++profile_seq;

    QMetaObject::invokeMethod(engine, [this, seq]()
    {
        /*-------------------------------------------------*\
        | Allow time for OnProfileLoad - it arrives over    |
        | the network when OpenRGB runs as a local client   |
        \*-------------------------------------------------*/
        QTimer::singleShot(2500, engine, [this, seq]()
        {
            if(seq != profile_seq || profile_had_data || !engine)
            {
                return;
            }

            engine->SetPlaying(false);
            engine->Resume();
            SaveSettings();

            if(widget)
            {
                widget->ReloadAll();
                widget->SetStatus(tr("A profile without Gradient Canvas settings was loaded, so playback was paused."));
            }
        });
    }, Qt::QueuedConnection);
}

void GradientCanvasPlugin::OnProfileLoad(nlohmann::json profile_data)
{
    if(!engine)
    {
        pending_profile = profile_data;
        return;
    }

    profile_had_data = true;

    QMetaObject::invokeMethod(engine, [this, profile_data]()
    {
        engine->ApplyProfileJson(profile_data);
        engine->Resume();
        SaveSettings();

        if(widget)
        {
            widget->ReloadAll();
            widget->SetStatus(tr("Profile loaded."));
        }
    }, Qt::QueuedConnection);
}

nlohmann::json GradientCanvasPlugin::OnProfileSave()
{
    return engine ? engine->ProfileJson() : nlohmann::json();
}

unsigned char* GradientCanvasPlugin::OnSDKCommand(unsigned int, unsigned char*, unsigned int* pkt_size)
{
    if(pkt_size)
    {
        *pkt_size = 0;
    }
    return nullptr;
}

void GradientCanvasPlugin::ProfileManagerUpdated(unsigned int update_reason)
{
    if(update_reason == PROFILE_LIST_UPDATED && engine)
    {
        QMetaObject::invokeMethod(engine, [this]()
        {
            if(widget) widget->RefreshProfiles();
        }, Qt::QueuedConnection);
    }
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

void GradientCanvasPlugin::OnOwnershipChanged(bool is_owner)
{
    if(!engine)
    {
        return;
    }

    if(is_owner)
    {
        /*-------------------------------------------------*\
        | Taking over from another window: it may have      |
        | changed the settings since we loaded them         |
        \*-------------------------------------------------*/
        if(was_standby)
        {
            nlohmann::json settings = LoadSettingsFile();
            if(settings.is_object() && !settings.empty())
            {
                engine->FromJson(settings);
            }
            RebindDevices();
            GC_LOG(api, "[%s] This window now controls the lights", PLUGIN_NAME);
        }
        engine->SetStandby(false);
    }
    else
    {
        engine->SetStandby(true);
        engine->SetTestPattern(false);
        GC_LOG(api, "[%s] Another OpenRGB window controls the lights - standby", PLUGIN_NAME);
    }

    was_standby = !is_owner;

    if(widget)
    {
        widget->SetStandby(!is_owner);
    }
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

/*---------------------------------------------------------*\
| Settings file                                             |
|                                                           |
|   Kept in our own file (plugins/settings/GradientCanvas.  |
|   json) rather than OpenRGB.json. When OpenRGB runs as a  |
|   client of a background OpenRGB server, OpenRGB.json     |
|   settings are routed to the server and can be lost.      |
\*---------------------------------------------------------*/
filesystem::path GradientCanvasPlugin::SettingsFilePath()
{
    if(!api)
    {
        return filesystem::path();
    }
    return api->GetConfigurationDirectory() / "plugins" / "settings" / "GradientCanvas.json";
}

nlohmann::json GradientCanvasPlugin::LoadSettingsFile()
{
    nlohmann::json j;
    filesystem::path path = SettingsFilePath();

    if(!path.empty() && filesystem::exists(path))
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        j = nlohmann::json::parse(in, nullptr, false);
        if(j.is_discarded())
        {
            GC_LOG(api, "[%s] Settings file is corrupt, ignoring", PLUGIN_NAME);
            j = nlohmann::json();
        }
    }
    else if(api)
    {
        j = api->GetSettings(SETTINGS_KEY);     /* 1.0.0 / 1.0.1 stored it here */
    }
    return j;
}

void GradientCanvasPlugin::SaveSettings()
{
    if(!engine || !api)
    {
        return;
    }

    /*-----------------------------------------------------*\
    | A standby window must not overwrite the settings of   |
    | the window that is driving the lights                 |
    \*-----------------------------------------------------*/
    if(guard && !guard->IsOwner())
    {
        return;
    }

    filesystem::path path = SettingsFilePath();
    std::error_code  ec;
    filesystem::create_directories(path.parent_path(), ec);

    /*-----------------------------------------------------*\
    | Write to a temp file then swap, so a crash mid-write  |
    | can't leave a half-written settings file              |
    \*-----------------------------------------------------*/
    filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if(!out)
        {
            GC_LOG(api, "[%s] Cannot write settings file", PLUGIN_NAME);
            return;
        }
        out << engine->ToJson().dump(4);
    }
    filesystem::rename(tmp, path, ec);
    if(ec)
    {
        filesystem::remove(path, ec);
        filesystem::rename(tmp, path, ec);
    }
}

/*---------------------------------------------------------*\
| Mirrors OpenRGB's StringUtils::make_filename so we find   |
| the same file OpenRGB wrote for a profile name            |
\*---------------------------------------------------------*/
static std::string ProfileFilename(std::string name)
{
    name = std::regex_replace(name, std::regex(":"), "-");
    name = std::regex_replace(name, std::regex("[#%&\\{\\}\\\\<>\\*\\?/!`';@+|=]"), "");
    name = std::regex_replace(name, std::regex("^\\.+"), "");
    name = std::regex_replace(name, std::regex("[\\x00-\\x1F\\x7F]"), "");
    return name + ".json";
}

QString GradientCanvasPlugin::SaveToProfile(const std::string& profile_name)
{
    if(!engine || !api || profile_name.empty())
    {
        return tr("No profile selected.");
    }

    filesystem::path path = api->GetConfigurationDirectory() / "profiles" / filesystem::u8path(ProfileFilename(profile_name));

    if(filesystem::exists(path))
    {
        /*-------------------------------------------------*\
        | Existing profile: merge our section in, keeping   |
        | its device states and other plugins' data.        |
        | (OpenRGB's own Save only refreshes plugins that   |
        | are already in an existing profile.)              |
        \*-------------------------------------------------*/
        nlohmann::json profile;
        {
            std::ifstream in(path, std::ios::in | std::ios::binary);
            profile = nlohmann::json::parse(in, nullptr, false);
        }
        if(profile.is_discarded() || !profile.is_object())
        {
            return tr("Couldn't read that profile file.");
        }

        profile["plugins"][PLUGIN_NAME] = engine->ProfileJson();

        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if(!out)
        {
            return tr("Couldn't write that profile file.");
        }
        out << profile.dump(4) << std::endl;
    }
    else
    {
        /*-------------------------------------------------*\
        | The profile exists but not as a local file: the   |
        | OpenRGB background service keeps it. Saving from  |
        | a plugin would REPLACE it with just our section   |
        | and lose its device colours, so don't.            |
        \*-------------------------------------------------*/
        std::vector<std::string> existing = api->GetProfileList();
        if(std::find(existing.begin(), existing.end(), profile_name) != existing.end())
        {
            return tr("This profile is stored by the OpenRGB service, so Gradient Canvas can't update it without "
                      "wiping its device colours. Load it and use OpenRGB's own Save Profile button instead "
                      "(it includes Gradient Canvas when the profile already has it), or click New… to make a new one.");
        }

        /*-------------------------------------------------*\
        | New profile containing just the Gradient Canvas   |
        | look (device states are left as they are)         |
        \*-------------------------------------------------*/
        if(!api->SaveProfileFromPlugin(profile_name, PLUGIN_NAME, engine->ProfileJson()))
        {
            return tr("OpenRGB refused to create the profile.");
        }
    }

    GC_LOG(api, "[%s] Saved to profile %s", PLUGIN_NAME, profile_name.c_str());
    return QString();
}
