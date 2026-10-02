/*---------------------------------------------------------*\
| ParamSlider.h                                             |
|                                                           |
|   Slider + spin box bound to one double value             |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSlider>
#include <QWidget>
#include <cmath>
#include <functional>

/*=========================================================*\
| ParamSlider - slider + spin box bound to a double         |
\*=========================================================*/
class ParamSlider : public QWidget
{
public:
    ParamSlider(double min, double max, int decimals, const QString& suffix = QString(), QWidget* parent = nullptr)
        : QWidget(parent)
    {
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, 1000);
        spin   = new QDoubleSpinBox(this);
        spin->setDecimals(decimals);
        spin->setSuffix(suffix);
        spin->setFixedWidth(78);
        SetRange(min, max, decimals);

        QHBoxLayout* l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(slider, 1);
        l->addWidget(spin);

        connect(slider, &QSlider::valueChanged, this, [this](int v)
        {
            if(guard) return;
            double d = lo + (hi - lo) * v / 1000.0;
            guard = true;
            spin->setValue(d);
            guard = false;
            if(on_change) on_change(spin->value());
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double d)
        {
            if(guard) return;
            guard = true;
            slider->setValue(ToSlider(d));
            guard = false;
            if(on_change) on_change(d);
        });
    }

    void SetRange(double min, double max, int decimals)
    {
        lo = min;
        hi = max;
        guard = true;
        spin->setDecimals(decimals);
        spin->setRange(min, max);
        spin->setSingleStep(std::pow(10.0, -decimals) * (decimals > 0 ? 5 : 1));
        guard = false;
    }

    void SetValue(double d)
    {
        guard = true;
        spin->setValue(d);
        slider->setValue(ToSlider(d));
        guard = false;
    }

    double Value() const { return spin->value(); }

    std::function<void(double)> on_change;

private:
    int ToSlider(double d) const
    {
        return (hi > lo) ? (int)std::lround((d - lo) / (hi - lo) * 1000.0) : 0;
    }

    QSlider*        slider;
    QDoubleSpinBox* spin;
    double          lo = 0, hi = 1;
    bool            guard = false;
};
