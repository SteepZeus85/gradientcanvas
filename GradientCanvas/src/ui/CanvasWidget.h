/*---------------------------------------------------------*\
| CanvasWidget.h                                            |
|                                                           |
|   The plugin tab: device list, layout map, effect and     |
|   gradient controls.                                      |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QWidget>
#include <functional>

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
class GradientEditor;
class LayoutCanvas;
class ParamSlider;
class RenderEngine;

class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    CanvasWidget(RenderEngine* engine, std::function<void()> save_callback, QWidget* parent = nullptr);

    /*-----------------------------------------------------*\
    | Call after the model was replaced / devices rebound   |
    \*-----------------------------------------------------*/
    void    ReloadAll();

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
    std::function<void()>   save_callback;
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

    /* canvas */
    LayoutCanvas*           canvas;

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

    int                     frame_counter = 0;
    qint64                  fps_window_start = 0;
};
