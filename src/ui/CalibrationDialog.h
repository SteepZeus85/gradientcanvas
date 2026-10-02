/*---------------------------------------------------------*\
| CalibrationDialog.h                                       |
|                                                           |
|   Match colours across devices: light everything with a  |
|   test colour, then adjust each device's white balance,  |
|   brightness, gamma, saturation and channel order until   |
|   they look the same.                                     |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QDialog>
#include <QColor>
#include <functional>
#include <set>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QTreeWidget;
class ParamSlider;
class RenderEngine;
struct ColorCalibration;

class CalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    CalibrationDialog(RenderEngine* engine, std::function<void()> on_changed, QWidget* parent = nullptr);

    void    Reload();               /* device list changed */

protected:
    void    showEvent(QShowEvent* event) override;
    void    hideEvent(QHideEvent* event) override;

private:
    std::vector<int>    SelectedZones() const;
    void    RefreshTree();
    void    RefreshControls();
    void    ApplyControls();
    void    UpdateTestPattern();
    void    Changed();

    RenderEngine*           engine;
    std::function<void()>   on_changed;
    bool                    updating = false;

    QTreeWidget*            tree;
    QLabel*                 editing_label;
    QCheckBox*              test_check;
    QCheckBox*              solo_check;
    QColor                  test_color = Qt::white;

    ParamSlider*            brightness;
    ParamSlider*            red;
    ParamSlider*            green;
    ParamSlider*            blue;
    ParamSlider*            gamma;
    ParamSlider*            saturation;
    QComboBox*              order;
};
