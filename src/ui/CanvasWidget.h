/*---------------------------------------------------------*\
| CanvasWidget.h                                            |
|                                                           |
|   The plugin tab: device list, layout map, effect and     |
|   gradient controls.                                      |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QString>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <string>
#include <vector>

/*---------------------------------------------------------*\
| Calls back into the plugin (settings file, profiles)      |
\*---------------------------------------------------------*/
struct CanvasHooks
{
    std::function<void()>                               save;
    std::function<std::vector<std::string>()>           list_profiles;
    std::function<QString(const std::string&)>          save_to_profile;    /* returns error or "" */
    std::function<void(const std::string&)>             load_profile;
    std::function<void()>                               take_control;       /* make this window drive the lights */
};

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class CalibrationDialog;
class GradientEditor;
class LayoutCanvas;
class ParamSlider;
class RenderEngine;

class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    CanvasWidget(RenderEngine* engine, CanvasHooks hooks, QWidget* parent = nullptr);

    /*-----------------------------------------------------*\
    | Call after the model was replaced / devices rebound   |
    \*-----------------------------------------------------*/
    void    ReloadAll();
    void    RefreshProfiles();
    void    SetStatus(const QString& text);

    /*-----------------------------------------------------*\
    | Another OpenRGB window is driving the lights: show a  |
    | banner and lock the editors (preview keeps running)   |
    \*-----------------------------------------------------*/
    void    SetStandby(bool standby);

private slots:
    void    OnTreeItemChanged(QTreeWidgetItem* item, int column);
    void    OnTreeSelectionChanged();
    void    OnCanvasZoneSelected(int zone_index);
    void    OnCanvasZoneEdited(int zone_index);
    void    OnEffectChanged(int index);
    void    OnFrame();

private:
    QWidget*    BuildDevicePanel();
    QWidget*    BuildToolbar();
    QWidget*    BuildEffectPanel();

    void    RefreshDeviceTree();
    void    RefreshZoneProperties();
    void    RefreshEffectControls();
    void    ApplyZoneProperties();
    void    ScheduleSave();
    void    Rerender();

    RenderEngine*           engine;
    CanvasHooks             hooks;
    QTimer*                 save_timer;
    bool                    updating_ui = false;
    int                     current_zone = -1;

    /* devices */
    QTreeWidget*            device_tree;
    QGroupBox*              zone_group;
    QDoubleSpinBox*         zone_x;
    QDoubleSpinBox*         zone_y;
    QDoubleSpinBox*         zone_w;
    QDoubleSpinBox*         zone_h;
    QDoubleSpinBox*         zone_rot;
    QCheckBox*              zone_reverse;
    QLabel*                 zone_info;

    /* toolbar */
    QPushButton*            play_button;
    QCheckBox*              output_check;
    QSpinBox*               fps_spin;
    QPushButton*            led_edit_button;
    QCheckBox*              snap_check;
    QSpinBox*               grid_spin;
    QSpinBox*               canvas_w_spin;
    QSpinBox*               canvas_h_spin;
    QLabel*                 fps_label;

    QPointer<CalibrationDialog> calibration_dialog;

    /* canvas */
    LayoutCanvas*           canvas;
    QWidget*                main_area;
    QWidget*                standby_banner;

    /* effect */
    QComboBox*              effect_combo;
    ParamSlider*            speed_slider;
    ParamSlider*            scale_slider;
    ParamSlider*            angle_slider;
    ParamSlider*            param1_slider;
    ParamSlider*            param2_slider;
    ParamSlider*            brightness_slider;
    QLabel*                 param1_label;
    QLabel*                 param2_label;
    QLabel*                 scale_label;
    QLabel*                 angle_label;
    QLabel*                 center_hint;
    QCheckBox*              mirror_check;
    QCheckBox*              reverse_check;
    QComboBox*              preset_combo;
    GradientEditor*         gradient_editor;

    /* profiles */
    QComboBox*              profile_combo;
    QLabel*                 status_label;
    QTimer*                 status_timer;

    int                     frame_counter = 0;
    qint64                  fps_window_start = 0;
};
