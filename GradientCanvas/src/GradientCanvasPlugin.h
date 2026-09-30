/*---------------------------------------------------------*\
| GradientCanvasPlugin.h                                    |
|                                                           |
|   OpenRGB plugin entry point (plugin API v5 / OpenRGB 1.0)|
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QPointer>
#include "OpenRGBPluginInterface.h"

class CanvasWidget;
class RenderEngine;

class GradientCanvasPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    GradientCanvasPlugin();
    ~GradientCanvasPlugin() override;

    /*-----------------------------------------------------*\
    | Plugin Information                                    |
    \*-----------------------------------------------------*/
    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;

    /*-----------------------------------------------------*\
    | Plugin Functionality                                  |
    \*-----------------------------------------------------*/
    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;
    void                OnProfileAboutToLoad() override;
    void                OnProfileLoad(nlohmann::json profile_data) override;
    nlohmann::json      OnProfileSave() override;
    unsigned char*      OnSDKCommand(unsigned int pkt_id, unsigned char* pkt_data, unsigned int* pkt_size) override;

    /*-----------------------------------------------------*\
    | Update Signals                                        |
    \*-----------------------------------------------------*/
    void                ProfileManagerUpdated(unsigned int update_reason) override;
    void                ResourceManagerUpdated(unsigned int update_reason) override;
    void                SettingsManagerUpdated(unsigned int update_reason) override;

private:
    void                SaveSettings();
    void                RebindDevices();

    OpenRGBPluginAPIInterface*  api     = nullptr;
    RenderEngine*               engine  = nullptr;
    QPointer<CanvasWidget>      widget;
};
