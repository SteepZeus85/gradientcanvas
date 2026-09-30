/*---------------------------------------------------------*\
| GradientEditor.h                                          |
|                                                           |
|   Interactive gradient bar:                               |
|     click bar          add a stop                         |
|     drag handle        move a stop                        |
|     double-click       pick colour                        |
|     right-click        remove stop                        |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QWidget>
#include "Gradient.h"

class GradientEditor : public QWidget
{
    Q_OBJECT

public:
    explicit GradientEditor(Gradient* gradient, QWidget* parent = nullptr);

    QSize   sizeHint() const override;
    QSize   minimumSizeHint() const override;

signals:
    void    GradientChanged();

protected:
    void    paintEvent(QPaintEvent* event) override;
    void    mousePressEvent(QMouseEvent* event) override;
    void    mouseMoveEvent(QMouseEvent* event) override;
    void    mouseReleaseEvent(QMouseEvent* event) override;
    void    mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRect   BarRect() const;
    int     StopAt(const QPoint& pos) const;
    double  PosFromX(int x) const;
    int     XFromPos(double pos) const;

    Gradient*   gradient;
    int         selected    = -1;
    bool        dragging    = false;
};
