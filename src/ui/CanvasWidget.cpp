/*---------------------------------------------------------*\
| CanvasWidget.cpp                                          |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "CanvasWidget.h"
#include "GradientEditor.h"
#include "LayoutCanvas.h"
#include "RenderEngine.h"
#include "ParamSlider.h"
#include "CalibrationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>



/*=========================================================*\
| CanvasWidget                                              |
\*=========================================================*/
CanvasWidget::CanvasWidget(RenderEngine* engine_ptr, CanvasHooks hooks_in, QWidget* parent)
    : QWidget(parent), engine(engine_ptr), hooks(std::move(hooks_in))
{
    save_timer = new QTimer(this);
    save_timer->setSingleShot(true);
    save_timer->setInterval(800);
    connect(save_timer, &QTimer::timeout, this, [this]() { if(hooks.save) hooks.save(); });

    canvas = new LayoutCanvas(engine, this);

    QWidget* centre = new QWidget(this);
    QVBoxLayout* cl = new QVBoxLayout(centre);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(BuildToolbar());
    cl->addWidget(canvas, 1);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(BuildDevicePanel());
    splitter->addWidget(centre);
    splitter->addWidget(BuildEffectPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({260, 700, 300});

    QHBoxLayout* main = new QHBoxLayout(this);
    main->setContentsMargins(4, 4, 4, 4);
    main->addWidget(splitter);

    connect(canvas, &LayoutCanvas::ZoneSelected, this, &CanvasWidget::OnCanvasZoneSelected);
    connect(canvas, &LayoutCanvas::ZoneEdited,   this, &CanvasWidget::OnCanvasZoneEdited);
    connect(canvas, &LayoutCanvas::CenterMoved,  this, [this]() { ScheduleSave(); });
    connect(engine, &RenderEngine::FrameRendered, this, &CanvasWidget::OnFrame);

    ReloadAll();
}

/*---------------------------------------------------------*\
| Panels                                                    |
\*---------------------------------------------------------*/
QWidget* CanvasWidget::BuildDevicePanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* l = new QVBoxLayout(panel);
    l->setContentsMargins(0, 0, 0, 0);

    QLabel* title = new QLabel(tr("<b>Devices</b> — tick zones to place them on the map"), panel);
    title->setWordWrap(true);
    l->addWidget(title);

    device_tree = new QTreeWidget(panel);
    device_tree->setHeaderHidden(true);
    device_tree->setColumnCount(1);
    device_tree->setMinimumWidth(220);
    l->addWidget(device_tree, 1);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* arrange = new QPushButton(tr("Auto-arrange"), panel);
    QPushButton* all     = new QPushButton(tr("Add all"), panel);
    buttons->addWidget(all);
    buttons->addWidget(arrange);
    l->addLayout(buttons);

    connect(arrange, &QPushButton::clicked, this, [this]()
    {
        engine->layout.AutoArrange();
        canvas->Rebuild();
        RefreshZoneProperties();
        ScheduleSave();
    });
    connect(all, &QPushButton::clicked, this, [this]()
    {
        for(ZonePlacement& z : engine->layout.zones)
        {
            if(z.controller) z.enabled = true;
        }
        engine->layout.AutoArrange();
        RefreshDeviceTree();
        canvas->Rebuild();
        ScheduleSave();
    });

    /*-----------------------------------------------------*\
    | Selected zone properties                              |
    \*-----------------------------------------------------*/
    zone_group = new QGroupBox(tr("Selected zone"), panel);
    QFormLayout* f = new QFormLayout(zone_group);

    auto make_spin = [this](double min, double max, int dec) {
        QDoubleSpinBox* s = new QDoubleSpinBox(zone_group);
        s->setRange(min, max);
        s->setDecimals(dec);
        s->setKeyboardTracking(false);
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { ApplyZoneProperties(); });
        return s;
    };

    zone_info    = new QLabel(zone_group);
    zone_info->setWordWrap(true);
    zone_x       = make_spin(-5000, 5000, 1);
    zone_y       = make_spin(-5000, 5000, 1);
    zone_w       = make_spin(4, 5000, 1);
    zone_h       = make_spin(4, 5000, 1);
    zone_rot     = make_spin(-360, 360, 1);
    zone_rot->setWrapping(true);
    zone_rot->setSuffix(QStringLiteral("°"));
    zone_reverse = new QCheckBox(tr("Reverse LED order"), zone_group);
    connect(zone_reverse, &QCheckBox::toggled, this, [this](bool) { ApplyZoneProperties(); });

    QPushButton* reset_leds = new QPushButton(tr("Reset LED positions"), zone_group);
    QPushButton* reset_size = new QPushButton(tr("Reset size"), zone_group);

    f->addRow(zone_info);
    f->addRow(tr("X"), zone_x);
    f->addRow(tr("Y"), zone_y);
    f->addRow(tr("Width"), zone_w);
    f->addRow(tr("Height"), zone_h);
    f->addRow(tr("Rotation"), zone_rot);
    f->addRow(zone_reverse);
    QPushButton* make_ring = new QPushButton(tr("Arrange LEDs as ring"), zone_group);
    make_ring->setToolTip(tr("Lay this zone's LEDs out in a circle - ideal for fans"));

    QHBoxLayout* rb = new QHBoxLayout();
    rb->addWidget(reset_leds);
    rb->addWidget(reset_size);
    f->addRow(rb);
    f->addRow(make_ring);

    connect(make_ring, &QPushButton::clicked, this, [this]()
    {
        if(current_zone < 0) return;
        ZonePlacement& z = engine->layout.zones[current_zone];
        if(z.led_count == 0) return;

        /*-------------------------------------------------*\
        | Square the zone around its current centre, then   |
        | place LEDs clockwise from 12 o'clock              |
        \*-------------------------------------------------*/
        double size = std::max({z.w, z.h, 80.0});
        double cx   = z.x + z.w * 0.5;
        double cy   = z.y + z.h * 0.5;
        z.w = z.h = size;
        z.x = cx - size * 0.5;
        z.y = cy - size * 0.5;

        z.led_overrides.clear();
        for(unsigned int i = 0; i < z.led_count; i++)
        {
            double a = -1.5707963267948966 + 6.283185307179586 * i / z.led_count;
            z.led_overrides[i] = QPointF(0.5 + 0.42 * std::cos(a), 0.5 + 0.42 * std::sin(a));
        }

        canvas->Rebuild();
        canvas->SelectZone(current_zone);
        RefreshZoneProperties();
        Rerender();
        ScheduleSave();
    });

    connect(reset_leds, &QPushButton::clicked, this, [this]()
    {
        if(current_zone < 0) return;
        engine->layout.zones[current_zone].led_overrides.clear();
        canvas->Rebuild();
        Rerender();
        ScheduleSave();
    });
    connect(reset_size, &QPushButton::clicked, this, [this]()
    {
        if(current_zone < 0) return;
        engine->layout.zones[current_zone].ResetDefaultSize();
        canvas->SyncZone(current_zone);
        canvas->Rebuild();
        RefreshZoneProperties();
        Rerender();
        ScheduleSave();
    });

    l->addWidget(zone_group);

    connect(device_tree, &QTreeWidget::itemChanged,          this, &CanvasWidget::OnTreeItemChanged);
    connect(device_tree, &QTreeWidget::itemSelectionChanged, this, &CanvasWidget::OnTreeSelectionChanged);

    return panel;
}

QWidget* CanvasWidget::BuildToolbar()
{
    QWidget* bar = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(bar);
    l->setContentsMargins(0, 0, 0, 0);

    play_button = new QPushButton(bar);
    play_button->setCheckable(true);
    play_button->setMinimumWidth(80);

    output_check = new QCheckBox(tr("Send to devices"), bar);
    output_check->setToolTip(tr("Untick to design with the on-screen preview only"));

    fps_spin = new QSpinBox(bar);
    fps_spin->setRange(1, 120);
    fps_spin->setSuffix(tr(" fps"));

    fps_label = new QLabel(bar);
    fps_label->setMinimumWidth(50);
    fps_label->setStyleSheet("color: gray;");

    led_edit_button = new QPushButton(tr("Edit LEDs"), bar);
    led_edit_button->setCheckable(true);
    led_edit_button->setToolTip(tr("Select a zone, then drag its individual LEDs.\n"
                                   "Rubber-band select several LEDs to move them together.\n"
                                   "Double-click an LED to reset it."));

    snap_check = new QCheckBox(tr("Snap"), bar);
    grid_spin  = new QSpinBox(bar);
    grid_spin->setRange(2, 200);
    grid_spin->setSuffix(tr(" px"));

    canvas_w_spin = new QSpinBox(bar);
    canvas_h_spin = new QSpinBox(bar);
    canvas_w_spin->setRange(100, 10000);
    canvas_h_spin->setRange(100, 10000);
    canvas_w_spin->setKeyboardTracking(false);
    canvas_h_spin->setKeyboardTracking(false);
    canvas_w_spin->setToolTip(tr("Canvas width"));
    canvas_h_spin->setToolTip(tr("Canvas height"));

    QPushButton* calibrate = new QPushButton(tr("Calibrate colours…"), bar);
    calibrate->setToolTip(tr("Match colours between devices that show the same value differently"));
    connect(calibrate, &QPushButton::clicked, this, [this]()
    {
        if(!calibration_dialog)
        {
            calibration_dialog = new CalibrationDialog(engine, [this]()
            {
                canvas->RefreshFrame();
                ScheduleSave();
            }, this);
        }
        calibration_dialog->show();
        calibration_dialog->raise();
        calibration_dialog->activateWindow();
    });

    QPushButton* fit = new QPushButton(tr("Fit"), bar);
    fit->setToolTip(tr("Fit canvas to view (Ctrl + wheel zooms)"));

    l->addWidget(play_button);
    l->addWidget(output_check);
    l->addWidget(fps_spin);
    l->addWidget(fps_label);
    l->addSpacing(12);
    l->addWidget(calibrate);
    l->addSpacing(12);
    l->addWidget(led_edit_button);
    l->addWidget(snap_check);
    l->addWidget(grid_spin);
    l->addStretch(1);
    l->addWidget(new QLabel(tr("Canvas"), bar));
    l->addWidget(canvas_w_spin);
    l->addWidget(new QLabel(QStringLiteral("×"), bar));
    l->addWidget(canvas_h_spin);
    l->addWidget(fit);

    connect(play_button, &QPushButton::toggled, this, [this](bool on)
    {
        play_button->setText(on ? tr("Pause") : tr("Play"));
        play_button->setIcon(style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        if(updating_ui) return;
        engine->SetPlaying(on);
        ScheduleSave();
    });
    connect(output_check, &QCheckBox::toggled, this, [this](bool on)
    {
        if(updating_ui) return;
        engine->SetOutputEnabled(on);
        ScheduleSave();
    });
    connect(fps_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v)
    {
        if(updating_ui) return;
        engine->SetFPS(v);
        ScheduleSave();
    });
    connect(led_edit_button, &QPushButton::toggled, this, [this](bool on)
    {
        canvas->SetLedEditMode(on);
    });
    auto snap_changed = [this]()
    {
        canvas->SetSnap(snap_check->isChecked(), grid_spin->value());
    };
    connect(snap_check, &QCheckBox::toggled, this, snap_changed);
    connect(grid_spin,  QOverload<int>::of(&QSpinBox::valueChanged), this, snap_changed);

    auto canvas_size_changed = [this]()
    {
        if(updating_ui) return;
        engine->layout.canvas_w = canvas_w_spin->value();
        engine->layout.canvas_h = canvas_h_spin->value();
        canvas->ResetZoom();
        canvas->Rebuild();
        ScheduleSave();
    };
    connect(canvas_w_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, canvas_size_changed);
    connect(canvas_h_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, canvas_size_changed);
    connect(fit, &QPushButton::clicked, canvas, &LayoutCanvas::ResetZoom);

    return bar;
}

QWidget* CanvasWidget::BuildEffectPanel()
{
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(290);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* panel = new QWidget(scroll);
    QVBoxLayout* l = new QVBoxLayout(panel);
    l->setContentsMargins(0, 0, 0, 0);

    /*-----------------------------------------------------*\
    | Effect                                                |
    \*-----------------------------------------------------*/
    QGroupBox* eg = new QGroupBox(tr("Effect"), panel);
    QFormLayout* f = new QFormLayout(eg);

    effect_combo = new QComboBox(eg);
    for(int i = 0; i < EFFECT_COUNT; i++)
    {
        effect_combo->addItem(GetEffectInfo(i).name);
    }

    speed_slider      = new ParamSlider(0.0, 3.0, 2, tr(" /s"), eg);
    scale_slider      = new ParamSlider(0.05, 4.0, 2, QString(), eg);
    angle_slider      = new ParamSlider(0.0, 360.0, 0, QStringLiteral("°"), eg);
    param1_slider     = new ParamSlider(0.0, 1.0, 2, QString(), eg);
    param2_slider     = new ParamSlider(0.0, 1.0, 2, QString(), eg);
    brightness_slider = new ParamSlider(0.0, 100.0, 0, QStringLiteral("%"), eg);

    scale_label  = new QLabel(tr("Size"), eg);
    angle_label  = new QLabel(tr("Angle"), eg);
    param1_label = new QLabel(eg);
    param2_label = new QLabel(eg);
    center_hint  = new QLabel(tr("<i>Drag the ⊕ marker or right-click the canvas to move the centre.</i>"), eg);
    center_hint->setWordWrap(true);

    mirror_check  = new QCheckBox(tr("Mirror (ping-pong)"), eg);
    reverse_check = new QCheckBox(tr("Reverse direction"), eg);

    f->addRow(tr("Type"), effect_combo);
    f->addRow(tr("Speed"), speed_slider);
    f->addRow(scale_label, scale_slider);
    f->addRow(angle_label, angle_slider);
    f->addRow(param1_label, param1_slider);
    f->addRow(param2_label, param2_slider);
    f->addRow(tr("Brightness"), brightness_slider);
    f->addRow(mirror_check);
    f->addRow(reverse_check);
    f->addRow(center_hint);

    connect(effect_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CanvasWidget::OnEffectChanged);

    auto bind = [this](ParamSlider* s, std::function<void(double)> setter)
    {
        s->on_change = [this, setter](double v)
        {
            if(updating_ui) return;
            setter(v);
            canvas->UpdateEffectOverlays();
            Rerender();
            ScheduleSave();
        };
    };
    bind(speed_slider,      [this](double v) { engine->params.speed      = v; });
    bind(scale_slider,      [this](double v) { engine->params.scale      = v; });
    bind(angle_slider,      [this](double v) { engine->params.angle      = v; });
    bind(param1_slider,     [this](double v) { engine->params.param1     = v; });
    bind(param2_slider,     [this](double v) { engine->params.param2     = v; });
    bind(brightness_slider, [this](double v) { engine->params.brightness = v / 100.0; });

    connect(mirror_check, &QCheckBox::toggled, this, [this](bool on)
    {
        if(updating_ui) return;
        engine->params.mirror = on;
        Rerender();
        ScheduleSave();
    });
    connect(reverse_check, &QCheckBox::toggled, this, [this](bool on)
    {
        if(updating_ui) return;
        engine->params.reverse = on;
        canvas->UpdateEffectOverlays();
        Rerender();
        ScheduleSave();
    });

    l->addWidget(eg);

    /*-----------------------------------------------------*\
    | Gradient                                              |
    \*-----------------------------------------------------*/
    QGroupBox* gg = new QGroupBox(tr("Gradient"), panel);
    QVBoxLayout* gl = new QVBoxLayout(gg);

    preset_combo = new QComboBox(gg);
    preset_combo->addItem(tr("Load preset…"));
    preset_combo->addItems(Gradient::PresetNames());

    gradient_editor = new GradientEditor(&engine->gradient, gg);

    QPushButton* randomise = new QPushButton(tr("Randomise"), gg);
    QLabel* help = new QLabel(tr("<small>Click the bar to add a stop · drag to move · "
                                 "double-click to recolour · right-click to delete</small>"), gg);
    help->setWordWrap(true);

    gl->addWidget(preset_combo);
    gl->addWidget(gradient_editor);
    gl->addWidget(help);
    gl->addWidget(randomise);

    connect(preset_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx)
    {
        if(idx <= 0) return;
        engine->gradient = Gradient::Preset(preset_combo->itemText(idx));
        preset_combo->blockSignals(true);
        preset_combo->setCurrentIndex(0);
        preset_combo->blockSignals(false);
        gradient_editor->update();
        Rerender();
        ScheduleSave();
    });
    connect(gradient_editor, &GradientEditor::GradientChanged, this, [this]()
    {
        Rerender();
        ScheduleSave();
    });
    connect(randomise, &QPushButton::clicked, this, [this]()
    {
        QRandomGenerator* rng = QRandomGenerator::global();
        int    count = 2 + rng->bounded(3);
        double hue   = rng->generateDouble();
        std::vector<GradientStop> stops;
        for(int i = 0; i < count; i++)
        {
            /* spread hues around the wheel with jitter for vivid combos */
            double h = std::fmod(hue + (double)i / count + (rng->generateDouble() - 0.5) * 0.15 + 1.0, 1.0);
            stops.push_back({(double)i / count, QColor::fromHsvF(h, 0.85 + rng->generateDouble() * 0.15, 1.0)});
        }
        engine->gradient.SetStops(stops);
        gradient_editor->update();
        Rerender();
        ScheduleSave();
    });

    l->addWidget(gg);

    /*-----------------------------------------------------*\
    | Profiles                                              |
    \*-----------------------------------------------------*/
    QGroupBox*   pg = new QGroupBox(tr("Profiles"), panel);
    QVBoxLayout* pl = new QVBoxLayout(pg);

    profile_combo = new QComboBox(pg);
    profile_combo->setToolTip(tr("OpenRGB profiles"));

    QPushButton* save_profile = new QPushButton(tr("Save to profile"), pg);
    QPushButton* new_profile  = new QPushButton(tr("New…"), pg);
    QPushButton* load_profile = new QPushButton(tr("Load"), pg);
    save_profile->setToolTip(tr("Store the current effect, gradient and play state in this profile.\n"
                                "The profile's device colours and other plugins are kept.\n"
                                "The layout map is shared by all profiles."));

    QHBoxLayout* pb = new QHBoxLayout();
    pb->addWidget(load_profile);
    pb->addWidget(save_profile);
    pb->addWidget(new_profile);

    status_label = new QLabel(pg);
    status_label->setWordWrap(true);
    status_label->setStyleSheet("color: gray;");

    QLabel* phelp = new QLabel(tr("<small>Your setup is saved automatically. Profiles remember the effect, gradient and "
                                  "whether it's playing. Loading a profile without Gradient Canvas settings pauses the "
                                  "animation so the profile's own colours show.</small>"), pg);
    phelp->setWordWrap(true);

    pl->addWidget(profile_combo);
    pl->addLayout(pb);
    pl->addWidget(status_label);
    pl->addWidget(phelp);

    status_timer = new QTimer(this);
    status_timer->setSingleShot(true);
    status_timer->setInterval(8000);
    connect(status_timer, &QTimer::timeout, status_label, &QLabel::clear);

    auto do_save = [this](const std::string& name)
    {
        if(!hooks.save_to_profile) return;
        if(hooks.save) hooks.save();
        QString err = hooks.save_to_profile(name);
        SetStatus(err.isEmpty() ? tr("Saved to “%1”.").arg(QString::fromStdString(name)) : err);
        RefreshProfiles();
    };

    connect(save_profile, &QPushButton::clicked, this, [this, do_save]()
    {
        if(profile_combo->currentIndex() < 0 || profile_combo->currentText().isEmpty())
        {
            SetStatus(tr("No profiles yet - click New… to create one."));
            return;
        }
        do_save(profile_combo->currentText().toStdString());
    });
    connect(new_profile, &QPushButton::clicked, this, [this, do_save]()
    {
        bool ok = false;
        QString name = QInputDialog::getText(this, tr("New profile"), tr("Profile name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if(ok && !name.isEmpty())
        {
            do_save(name.toStdString());
            profile_combo->setCurrentText(name);
        }
    });
    connect(load_profile, &QPushButton::clicked, this, [this]()
    {
        if(hooks.load_profile && !profile_combo->currentText().isEmpty())
        {
            hooks.load_profile(profile_combo->currentText().toStdString());
        }
    });

    l->addWidget(pg);
    l->addStretch(1);

    scroll->setWidget(panel);
    return scroll;
}

/*---------------------------------------------------------*\
| Refresh helpers                                           |
\*---------------------------------------------------------*/
void CanvasWidget::ReloadAll()
{
    updating_ui = true;

    play_button->setChecked(engine->IsPlaying());
    play_button->setText(engine->IsPlaying() ? tr("Pause") : tr("Play"));
    play_button->setIcon(style()->standardIcon(engine->IsPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    output_check->setChecked(engine->IsOutputEnabled());
    fps_spin->setValue(engine->GetFPS());
    bool   snap = canvas->SnapEnabled();
    double grid = canvas->GridSize();
    grid_spin->blockSignals(true);
    snap_check->blockSignals(true);
    grid_spin->setValue((int)grid);
    snap_check->setChecked(snap);
    grid_spin->blockSignals(false);
    snap_check->blockSignals(false);
    canvas_w_spin->setValue((int)engine->layout.canvas_w);
    canvas_h_spin->setValue((int)engine->layout.canvas_h);

    updating_ui = false;

    RefreshDeviceTree();
    RefreshProfiles();
    canvas->Rebuild();
    RefreshEffectControls();
    gradient_editor->update();

    if(current_zone >= (int)engine->layout.zones.size())
    {
        current_zone = -1;
    }
    RefreshZoneProperties();

    if(calibration_dialog)
    {
        calibration_dialog->Reload();
    }
}

void CanvasWidget::RefreshProfiles()
{
    if(!hooks.list_profiles)
    {
        return;
    }

    QString current = profile_combo->currentText();
    profile_combo->blockSignals(true);
    profile_combo->clear();
    for(const std::string& name : hooks.list_profiles())
    {
        profile_combo->addItem(QString::fromStdString(name));
    }
    int idx = profile_combo->findText(current);
    if(idx >= 0)
    {
        profile_combo->setCurrentIndex(idx);
    }
    profile_combo->blockSignals(false);
}

void CanvasWidget::SetStatus(const QString& text)
{
    status_label->setText(text);
    status_timer->start();
}

void CanvasWidget::RefreshDeviceTree()
{
    bool was_updating = updating_ui;
    updating_ui = true;
    device_tree->blockSignals(true);
    device_tree->clear();

    std::map<std::string, QTreeWidgetItem*> devices;
    const LayoutModel& l = engine->layout;

    for(int i = 0; i < (int)l.zones.size(); i++)
    {
        const ZonePlacement& z = l.zones[i];
        QTreeWidgetItem*& dev  = devices[z.device_key];

        if(!dev)
        {
            dev = new QTreeWidgetItem(device_tree);
            dev->setText(0, QString::fromStdString(z.device_name));
            dev->setToolTip(0, QString::fromStdString(z.device_key));
            dev->setData(0, Qt::UserRole, -1);
            dev->setFlags(dev->flags() | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);
            dev->setExpanded(true);
        }

        QTreeWidgetItem* zi = new QTreeWidgetItem(dev);
        zi->setText(0, QString("%1  (%2 LED%3)")
                        .arg(QString::fromStdString(z.zone_name))
                        .arg(z.led_count)
                        .arg(z.led_count == 1 ? "" : "s"));
        zi->setData(0, Qt::UserRole, i);
        zi->setFlags(zi->flags() | Qt::ItemIsUserCheckable);
        zi->setCheckState(0, z.enabled ? Qt::Checked : Qt::Unchecked);

        if(!z.controller)
        {
            dev->setForeground(0, QBrush(Qt::gray));
            zi->setForeground(0, QBrush(Qt::gray));
            dev->setText(0, QString::fromStdString(z.device_name) + tr(" (offline)"));
        }
    }

    device_tree->blockSignals(false);
    updating_ui = was_updating;
}

void CanvasWidget::RefreshZoneProperties()
{
    bool was_updating = updating_ui;
    updating_ui = true;

    bool valid = current_zone >= 0 && current_zone < (int)engine->layout.zones.size()
              && engine->layout.zones[current_zone].enabled;
    zone_group->setEnabled(valid);

    if(valid)
    {
        const ZonePlacement& z = engine->layout.zones[current_zone];
        static const char* type_names[] = { "Single", "Linear", "Matrix", "Linear loop", "Matrix loop X", "Matrix loop Y", "Segmented" };
        QString type = (z.type < 7) ? type_names[z.type] : "Unknown";
        if(z.IsMatrix())
        {
            type += QString(" %1×%2").arg(z.matrix_w).arg(z.matrix_h);
        }

        zone_info->setText(QString("<b>%1</b><br>%2 · %3 LEDs%4")
                            .arg(QString::fromStdString(z.zone_name).toHtmlEscaped())
                            .arg(type)
                            .arg(z.led_count)
                            .arg(z.led_overrides.empty() ? QString()
                                 : tr(" · %1 moved").arg(z.led_overrides.size())));
        zone_x->setValue(z.x);
        zone_y->setValue(z.y);
        zone_w->setValue(z.w);
        zone_h->setValue(z.h);
        zone_rot->setValue(z.rotation);
        zone_reverse->setChecked(z.reverse);
        zone_reverse->setEnabled(!z.IsMatrix());
    }
    else
    {
        zone_info->setText(tr("Click a zone on the map to edit it."));
    }

    updating_ui = was_updating;
}

void CanvasWidget::RefreshEffectControls()
{
    bool was_updating = updating_ui;
    updating_ui = true;

    const EffectParams& p    = engine->params;
    const EffectInfo&   info = GetEffectInfo(p.type);

    effect_combo->setCurrentIndex(p.type);
    speed_slider->SetValue(p.speed);
    scale_slider->SetValue(p.scale);
    angle_slider->SetValue(p.angle);
    brightness_slider->SetValue(p.brightness * 100.0);
    mirror_check->setChecked(p.mirror);
    reverse_check->setChecked(p.reverse);

    scale_slider->setVisible(info.uses_scale);
    scale_label->setVisible(info.uses_scale);
    angle_slider->setVisible(info.uses_angle);
    angle_label->setVisible(info.uses_angle);
    center_hint->setVisible(info.uses_center);

    bool has_p1 = !info.param1_label.isEmpty();
    bool has_p2 = !info.param2_label.isEmpty();
    param1_label->setVisible(has_p1);
    param1_slider->setVisible(has_p1);
    param2_label->setVisible(has_p2);
    param2_slider->setVisible(has_p2);

    if(has_p1)
    {
        param1_label->setText(info.param1_label);
        param1_slider->SetRange(info.param1_min, info.param1_max, (info.param1_max - info.param1_min) >= 5 ? 1 : 2);
        param1_slider->SetValue(p.param1);
    }
    if(has_p2)
    {
        param2_label->setText(info.param2_label);
        param2_slider->SetRange(info.param2_min, info.param2_max, (info.param2_max - info.param2_min) >= 5 ? 1 : 2);
        param2_slider->SetValue(p.param2);
    }

    updating_ui = was_updating;
    canvas->UpdateEffectOverlays();
}

/*---------------------------------------------------------*\
| Slots                                                     |
\*---------------------------------------------------------*/
void CanvasWidget::OnTreeItemChanged(QTreeWidgetItem* item, int)
{
    if(updating_ui)
    {
        return;
    }

    int idx = item->data(0, Qt::UserRole).toInt();
    if(idx < 0 || idx >= (int)engine->layout.zones.size())
    {
        return;     /* device row - its children fire their own events */
    }

    ZonePlacement& z  = engine->layout.zones[idx];
    bool now_enabled  = (item->checkState(0) == Qt::Checked);

    if(now_enabled && !z.enabled)
    {
        /*-------------------------------------------------*\
        | Drop the newly added zone below the others so it  |
        | doesn't land on top of anything                   |
        \*-------------------------------------------------*/
        double bottom = 0.0;
        bool   any    = false;
        for(const ZonePlacement& o : engine->layout.zones)
        {
            if(o.enabled)
            {
                bottom = std::max(bottom, o.y + o.h);
                any    = true;
            }
        }
        double ny = any ? bottom + 30.0 : 30.0;
        if(ny + z.h > engine->layout.canvas_h)
        {
            ny = (engine->layout.canvas_h - z.h) * 0.5;
        }
        z.x = 30.0;
        z.y = ny;
    }

    z.enabled = now_enabled;

    canvas->Rebuild();
    if(now_enabled)
    {
        current_zone = idx;
        canvas->SelectZone(idx);
    }
    RefreshZoneProperties();
    ScheduleSave();
}

void CanvasWidget::OnTreeSelectionChanged()
{
    QList<QTreeWidgetItem*> sel = device_tree->selectedItems();
    if(sel.isEmpty())
    {
        return;
    }
    int idx = sel.first()->data(0, Qt::UserRole).toInt();
    if(idx >= 0)
    {
        canvas->SelectZone(idx);
    }
}

void CanvasWidget::OnCanvasZoneSelected(int zone_index)
{
    current_zone = zone_index;
    RefreshZoneProperties();

    /* mirror selection in the tree without feedback */
    device_tree->blockSignals(true);
    device_tree->clearSelection();
    for(int d = 0; d < device_tree->topLevelItemCount(); d++)
    {
        QTreeWidgetItem* dev = device_tree->topLevelItem(d);
        for(int c = 0; c < dev->childCount(); c++)
        {
            if(dev->child(c)->data(0, Qt::UserRole).toInt() == zone_index)
            {
                dev->child(c)->setSelected(true);
                device_tree->scrollToItem(dev->child(c));
            }
        }
    }
    device_tree->blockSignals(false);
}

void CanvasWidget::OnCanvasZoneEdited(int zone_index)
{
    if(zone_index == current_zone)
    {
        RefreshZoneProperties();
    }
    ScheduleSave();
}

void CanvasWidget::ApplyZoneProperties()
{
    if(updating_ui || current_zone < 0 || current_zone >= (int)engine->layout.zones.size())
    {
        return;
    }

    ZonePlacement& z = engine->layout.zones[current_zone];
    bool size_changed = (z.w != zone_w->value()) || (z.h != zone_h->value());

    z.x        = zone_x->value();
    z.y        = zone_y->value();
    z.w        = zone_w->value();
    z.h        = zone_h->value();
    z.rotation = zone_rot->value();
    z.reverse  = zone_reverse->isChecked();

    if(size_changed)
    {
        canvas->Rebuild();  /* LED radii depend on size */
    }
    else
    {
        canvas->SyncZone(current_zone);
    }
    Rerender();
    ScheduleSave();
}

void CanvasWidget::OnEffectChanged(int index)
{
    if(updating_ui)
    {
        return;
    }

    const EffectInfo& info = GetEffectInfo(index);
    engine->params.type    = index;
    engine->params.param1  = info.param1_default;
    engine->params.param2  = info.param2_default;

    RefreshEffectControls();
    Rerender();
    ScheduleSave();
}

void CanvasWidget::OnFrame()
{
    canvas->RefreshFrame();

    /*-----------------------------------------------------*\
    | Measured frame rate                                   |
    \*-----------------------------------------------------*/
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    frame_counter++;
    if(now - fps_window_start >= 1000)
    {
        fps_label->setText(engine->IsPlaying()
                           ? QString("%1 fps").arg(frame_counter * 1000.0 / std::max<qint64>(1, now - fps_window_start), 0, 'f', 0)
                           : QString());
        frame_counter    = 0;
        fps_window_start = now;
    }
}

void CanvasWidget::Rerender()
{
    if(!engine->IsPlaying())
    {
        engine->RenderFrame();
    }
}

void CanvasWidget::ScheduleSave()
{
    save_timer->start();
}
