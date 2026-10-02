/*---------------------------------------------------------*\
| CalibrationDialog.cpp                                     |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "CalibrationDialog.h"
#include "ParamSlider.h"
#include "RenderEngine.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>

CalibrationDialog::CalibrationDialog(RenderEngine* engine_ptr, std::function<void()> changed_cb, QWidget* parent)
    : QDialog(parent), engine(engine_ptr), on_changed(std::move(changed_cb))
{
    setWindowTitle(tr("Colour calibration"));
    resize(820, 560);

    /*-----------------------------------------------------*\
    | Device list                                           |
    \*-----------------------------------------------------*/
    tree = new QTreeWidget(this);
    tree->setHeaderHidden(true);
    tree->setMinimumWidth(240);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);

    /*-----------------------------------------------------*\
    | Test pattern                                          |
    \*-----------------------------------------------------*/
    QGroupBox*   tg = new QGroupBox(tr("1. Test colour"), this);
    QVBoxLayout* tl = new QVBoxLayout(tg);

    test_check = new QCheckBox(tr("Show test colour on devices"), tg);
    test_check->setChecked(true);
    solo_check = new QCheckBox(tr("Only light the selected device"), tg);

    QGridLayout* swatches = new QGridLayout();
    struct Swatch { const char* name; QColor color; };
    const Swatch list[] =
    {
        { QT_TR_NOOP("White"),   QColor(255, 255, 255) },
        { QT_TR_NOOP("Grey"),    QColor(128, 128, 128) },
        { QT_TR_NOOP("Dim"),     QColor( 40,  40,  40) },
        { QT_TR_NOOP("Red"),     QColor(255,   0,   0) },
        { QT_TR_NOOP("Green"),   QColor(  0, 255,   0) },
        { QT_TR_NOOP("Blue"),    QColor(  0,   0, 255) },
        { QT_TR_NOOP("Yellow"),  QColor(255, 255,   0) },
        { QT_TR_NOOP("Cyan"),    QColor(  0, 255, 255) },
        { QT_TR_NOOP("Magenta"), QColor(255,   0, 255) },
        { QT_TR_NOOP("Orange"),  QColor(255, 100,   0) },
    };
    int col = 0, row = 0;
    for(const Swatch& sw : list)
    {
        QPushButton* b = new QPushButton(tr(sw.name), tg);
        QColor c = sw.color;
        b->setStyleSheet(QString("QPushButton { border-left: 10px solid %1; padding: 4px 6px; }").arg(c.name()));
        connect(b, &QPushButton::clicked, this, [this, c]() { test_color = c; test_check->setChecked(true); UpdateTestPattern(); });
        swatches->addWidget(b, row, col);
        if(++col == 5) { col = 0; row++; }
    }
    QPushButton* pick = new QPushButton(tr("Pick…"), tg);
    connect(pick, &QPushButton::clicked, this, [this]()
    {
        QColor c = QColorDialog::getColor(test_color, this, tr("Test colour"));
        if(c.isValid()) { test_color = c; test_check->setChecked(true); UpdateTestPattern(); }
    });
    swatches->addWidget(pick, row + 1, 0);

    tl->addWidget(test_check);
    tl->addWidget(solo_check);
    tl->addLayout(swatches);

    connect(test_check, &QCheckBox::toggled, this, [this](bool) { UpdateTestPattern(); });
    connect(solo_check, &QCheckBox::toggled, this, [this](bool) { UpdateTestPattern(); });

    /*-----------------------------------------------------*\
    | Correction controls                                   |
    \*-----------------------------------------------------*/
    QGroupBox*   cg = new QGroupBox(tr("2. Correct the selected device"), this);
    QFormLayout* cf = new QFormLayout(cg);

    editing_label = new QLabel(cg);
    editing_label->setWordWrap(true);

    brightness = new ParamSlider(0, 100, 0, QStringLiteral("%"), cg);
    red        = new ParamSlider(0, 100, 0, QStringLiteral("%"), cg);
    green      = new ParamSlider(0, 100, 0, QStringLiteral("%"), cg);
    blue       = new ParamSlider(0, 100, 0, QStringLiteral("%"), cg);
    gamma      = new ParamSlider(0.3, 3.0, 2, QString(), cg);
    saturation = new ParamSlider(0, 200, 0, QStringLiteral("%"), cg);
    order      = new QComboBox(cg);
    order->addItems(ColorCalibration::OrderNames());

    red->setToolTip(tr("Lower the channel that is too strong - e.g. if white looks blue-ish, lower Blue"));
    gamma->setToolTip(tr("Above 1 darkens dim colours, below 1 brightens them.\nUse it when two devices match at full white but not at Grey/Dim."));
    order->setToolTip(tr("If the Red test shows green or blue, try the other orders until Red shows red"));

    cf->addRow(editing_label);
    cf->addRow(tr("Brightness"), brightness);
    cf->addRow(tr("Red"),        red);
    cf->addRow(tr("Green"),      green);
    cf->addRow(tr("Blue"),       blue);
    cf->addRow(tr("Gamma"),      gamma);
    cf->addRow(tr("Saturation"), saturation);
    cf->addRow(tr("Channel order"), order);

    QPushButton* reset    = new QPushButton(tr("Reset"), cg);
    QPushButton* copy_all = new QPushButton(tr("Copy to all devices"), cg);
    QHBoxLayout* bl = new QHBoxLayout();
    bl->addWidget(reset);
    bl->addWidget(copy_all);
    cf->addRow(bl);

    for(ParamSlider* s : { brightness, red, green, blue, gamma, saturation })
    {
        s->on_change = [this](double) { ApplyControls(); };
    }
    connect(order, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { ApplyControls(); });

    connect(reset, &QPushButton::clicked, this, [this]()
    {
        for(int idx : SelectedZones()) engine->layout.zones[idx].calibration = ColorCalibration();
        RefreshControls();
        Changed();
    });
    connect(copy_all, &QPushButton::clicked, this, [this]()
    {
        std::vector<int> sel = SelectedZones();
        if(sel.empty()) return;
        if(QMessageBox::question(this, tr("Copy calibration"),
              tr("Apply this device's correction to every device?")) != QMessageBox::Yes) return;
        ColorCalibration c = engine->layout.zones[sel.front()].calibration;
        for(ZonePlacement& z : engine->layout.zones) z.calibration = c;
        Changed();
    });

    /*-----------------------------------------------------*\
    | How-to                                                |
    \*-----------------------------------------------------*/
    QLabel* help = new QLabel(tr(
        "<b>How to match devices</b><ol style='margin-left:-20px'>"
        "<li>Show <b>White</b>. Pick the device that looks most neutral as your reference.</li>"
        "<li>For every other device, lower the channel that's too strong until its white matches "
        "(bluish white → lower Blue, greenish → lower Green, pinkish → lower Red).</li>"
        "<li>Use <b>Brightness</b> to match how bright they are.</li>"
        "<li>Check <b>Grey</b> and <b>Dim</b>. If they differ while White matches, adjust <b>Gamma</b>.</li>"
        "<li>Check <b>Red / Green / Blue</b>. If colours are swapped, change <b>Channel order</b>. "
        "If a colour looks pale or too intense, adjust <b>Saturation</b>.</li></ol>"
        "Corrections apply only to what's sent to the hardware; the on-screen preview always shows the intended colour. "
        "They're saved with your layout and used by every profile."), this);
    help->setWordWrap(true);
    help->setTextFormat(Qt::RichText);

    QWidget*     right = new QWidget(this);
    QVBoxLayout* rl    = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(tg);
    rl->addWidget(cg);
    rl->addWidget(help);
    rl->addStretch(1);

    QSplitter* split = new QSplitter(this);
    split->addWidget(tree);
    split->addWidget(right);
    split->setStretchFactor(1, 1);

    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::close);

    QVBoxLayout* main = new QVBoxLayout(this);
    main->addWidget(split, 1);
    main->addWidget(box);

    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this]()
    {
        RefreshControls();
        UpdateTestPattern();
    });

    Reload();
}

void CalibrationDialog::Reload()
{
    RefreshTree();
    RefreshControls();
    if(isVisible())
    {
        UpdateTestPattern();
    }
}

void CalibrationDialog::RefreshTree()
{
    /*-----------------------------------------------------*\
    | Keep the current selection across rebuilds            |
    \*-----------------------------------------------------*/
    QString sel_key;
    int     sel_zone = -1;
    if(!tree->selectedItems().isEmpty())
    {
        sel_key  = tree->selectedItems().first()->data(0, Qt::UserRole + 1).toString();
        sel_zone = tree->selectedItems().first()->data(0, Qt::UserRole).toInt();
    }

    tree->blockSignals(true);
    tree->clear();

    std::map<std::string, QTreeWidgetItem*> devices;
    const LayoutModel& l = engine->layout;
    QTreeWidgetItem* to_select = nullptr;

    for(int i = 0; i < (int)l.zones.size(); i++)
    {
        const ZonePlacement& z = l.zones[i];
        if(!z.enabled)
        {
            continue;   /* only zones on the map are driven */
        }

        QTreeWidgetItem*& dev = devices[z.device_key];
        if(!dev)
        {
            dev = new QTreeWidgetItem(tree);
            dev->setText(0, QString::fromStdString(z.device_name));
            dev->setData(0, Qt::UserRole, -1);
            dev->setData(0, Qt::UserRole + 1, QString::fromStdString(z.device_key));
            dev->setExpanded(true);
            if(!z.controller)
            {
                dev->setForeground(0, QBrush(Qt::gray));
            }
            if(sel_zone < 0 && sel_key == QString::fromStdString(z.device_key))
            {
                to_select = dev;
            }
        }

        QTreeWidgetItem* zi = new QTreeWidgetItem(dev);
        zi->setText(0, QString::fromStdString(z.zone_name) + (z.calibration.IsIdentity() ? "" : "  •"));
        zi->setData(0, Qt::UserRole, i);
        zi->setData(0, Qt::UserRole + 1, QString::fromStdString(z.device_key));
        if(i == sel_zone)
        {
            to_select = zi;
            dev->setExpanded(true);
        }
    }

    if(!to_select && tree->topLevelItemCount() > 0)
    {
        to_select = tree->topLevelItem(0);
    }
    if(to_select)
    {
        to_select->setSelected(true);
    }

    if(tree->topLevelItemCount() == 0)
    {
        QTreeWidgetItem* empty = new QTreeWidgetItem(tree);
        empty->setText(0, tr("Place devices on the map first"));
        empty->setData(0, Qt::UserRole, -2);
        empty->setFlags(Qt::NoItemFlags);
    }

    tree->blockSignals(false);
}

std::vector<int> CalibrationDialog::SelectedZones() const
{
    std::vector<int> out;
    if(tree->selectedItems().isEmpty())
    {
        return out;
    }

    QTreeWidgetItem* item = tree->selectedItems().first();
    int idx = item->data(0, Qt::UserRole).toInt();

    if(idx >= 0)
    {
        out.push_back(idx);
    }
    else if(idx == -1)
    {
        /* device row: every zone of that device on the map */
        for(int c = 0; c < item->childCount(); c++)
        {
            out.push_back(item->child(c)->data(0, Qt::UserRole).toInt());
        }
    }
    return out;
}

void CalibrationDialog::RefreshControls()
{
    std::vector<int> sel = SelectedZones();
    bool valid = !sel.empty();

    for(QWidget* w : std::initializer_list<QWidget*>{ brightness, red, green, blue, gamma, saturation, order })
    {
        w->setEnabled(valid);
    }

    if(!valid)
    {
        editing_label->setText(tr("Select a device on the left."));
        return;
    }

    const ZonePlacement& first = engine->layout.zones[sel.front()];
    QTreeWidgetItem*     item  = tree->selectedItems().first();

    if(item->data(0, Qt::UserRole).toInt() == -1)
    {
        bool mixed = false;
        for(int idx : sel)
        {
            if(engine->layout.zones[idx].calibration.ToJson() != first.calibration.ToJson()) mixed = true;
        }
        editing_label->setText(tr("<b>%1</b> - all %2 zone(s)%3")
            .arg(QString::fromStdString(first.device_name).toHtmlEscaped())
            .arg(sel.size())
            .arg(mixed ? tr("<br><i>Zones currently differ; editing sets them all to these values.</i>") : QString()));
    }
    else
    {
        editing_label->setText(tr("<b>%1</b> - %2 only")
            .arg(QString::fromStdString(first.device_name).toHtmlEscaped())
            .arg(QString::fromStdString(first.zone_name).toHtmlEscaped()));
    }

    updating = true;
    const ColorCalibration& c = first.calibration;
    brightness->SetValue(c.brightness * 100.0);
    red->SetValue(c.red * 100.0);
    green->SetValue(c.green * 100.0);
    blue->SetValue(c.blue * 100.0);
    gamma->SetValue(c.gamma);
    saturation->SetValue(c.saturation * 100.0);
    order->setCurrentIndex(c.order);
    updating = false;
}

void CalibrationDialog::ApplyControls()
{
    if(updating)
    {
        return;
    }

    ColorCalibration c;
    c.brightness = brightness->Value() / 100.0;
    c.red        = red->Value() / 100.0;
    c.green      = green->Value() / 100.0;
    c.blue       = blue->Value() / 100.0;
    c.gamma      = gamma->Value();
    c.saturation = saturation->Value() / 100.0;
    c.order      = order->currentIndex();

    for(int idx : SelectedZones())
    {
        engine->layout.zones[idx].calibration = c;
    }
    Changed();
}

void CalibrationDialog::Changed()
{
    /* mark calibrated zones in the list without rebuilding it */
    for(int d = 0; d < tree->topLevelItemCount(); d++)
    {
        QTreeWidgetItem* dev = tree->topLevelItem(d);
        for(int c = 0; c < dev->childCount(); c++)
        {
            int idx = dev->child(c)->data(0, Qt::UserRole).toInt();
            if(idx >= 0 && idx < (int)engine->layout.zones.size())
            {
                const ZonePlacement& z = engine->layout.zones[idx];
                dev->child(c)->setText(0, QString::fromStdString(z.zone_name) + (z.calibration.IsIdentity() ? "" : "  •"));
            }
        }
    }

    UpdateTestPattern();
    if(on_changed)
    {
        on_changed();
    }
}

void CalibrationDialog::UpdateTestPattern()
{
    if(!isVisible())
    {
        return;
    }

    std::set<int> only;
    if(solo_check->isChecked())
    {
        for(int idx : SelectedZones()) only.insert(idx);
        if(only.empty()) only.insert(-1);   /* nothing selected: everything dark */
    }

    if(test_check->isChecked())
    {
        engine->SetTestPattern(true, test_color, only);
    }
    else
    {
        engine->SetTestPattern(false);
    }
}

void CalibrationDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    Reload();
    UpdateTestPattern();
}

void CalibrationDialog::hideEvent(QHideEvent* event)
{
    engine->SetTestPattern(false);
    QDialog::hideEvent(event);
}
