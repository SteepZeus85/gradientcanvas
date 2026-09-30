/*---------------------------------------------------------*\
| GradientEditor.cpp                                        |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "GradientEditor.h"
#include <QColorDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <algorithm>
#include <cmath>

static const int BAR_H      = 26;
static const int HANDLE_H   = 14;
static const int HANDLE_W   = 12;
static const int PAD        = 8;

GradientEditor::GradientEditor(Gradient* gradient_ptr, QWidget* parent)
    : QWidget(parent), gradient(gradient_ptr)
{
    setMouseTracking(true);
    setToolTip(tr("Click the bar to add a stop • drag a handle to move it\n"
                  "Double-click a handle to change its colour • right-click to remove"));
}

QSize GradientEditor::sizeHint() const
{
    return QSize(260, BAR_H + HANDLE_H + PAD * 2);
}

QSize GradientEditor::minimumSizeHint() const
{
    return QSize(120, BAR_H + HANDLE_H + PAD * 2);
}

QRect GradientEditor::BarRect() const
{
    return QRect(PAD, PAD, width() - PAD * 2, BAR_H);
}

double GradientEditor::PosFromX(int x) const
{
    QRect bar = BarRect();
    return std::clamp((double)(x - bar.left()) / std::max(1, bar.width()), 0.0, 1.0);
}

int GradientEditor::XFromPos(double pos) const
{
    QRect bar = BarRect();
    return bar.left() + (int)std::lround(pos * bar.width());
}

int GradientEditor::StopAt(const QPoint& p) const
{
    QRect bar = BarRect();
    const auto& stops = gradient->Stops();

    for(int i = (int)stops.size() - 1; i >= 0; i--)
    {
        int x = XFromPos(stops[i].pos);
        QRect handle(x - HANDLE_W / 2 - 2, bar.bottom() - 4, HANDLE_W + 4, HANDLE_H + 8);
        if(handle.contains(p))
        {
            return i;
        }
    }
    return -1;
}

void GradientEditor::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect bar = BarRect();

    /*-----------------------------------------------------*\
    | Gradient bar - drawn from the real sampler so the     |
    | preview matches the OKLab blend exactly               |
    \*-----------------------------------------------------*/
    QImage img(std::max(1, bar.width()), 1, QImage::Format_RGB32);
    for(int x = 0; x < img.width(); x++)
    {
        float r, g, b;
        gradient->Sample((double)x / std::max(1, img.width() - 1), false, r, g, b);
        img.setPixel(x, 0, qRgb((int)(r * 255), (int)(g * 255), (int)(b * 255)));
    }

    QPainterPath clip;
    clip.addRoundedRect(bar, 5, 5);
    painter.save();
    painter.setClipPath(clip);
    painter.drawImage(bar, img);
    painter.restore();
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRoundedRect(bar, 5, 5);

    /*-----------------------------------------------------*\
    | Stop handles                                          |
    \*-----------------------------------------------------*/
    const auto& stops = gradient->Stops();
    for(int i = 0; i < (int)stops.size(); i++)
    {
        int x   = XFromPos(stops[i].pos);
        int top = bar.bottom() + 2;

        QPainterPath handle;
        handle.moveTo(x, top);
        handle.lineTo(x + HANDLE_W / 2, top + 5);
        handle.lineTo(x + HANDLE_W / 2, top + HANDLE_H);
        handle.lineTo(x - HANDLE_W / 2, top + HANDLE_H);
        handle.lineTo(x - HANDLE_W / 2, top + 5);
        handle.closeSubpath();

        painter.setBrush(stops[i].color);
        painter.setPen(QPen(i == selected ? palette().color(QPalette::Highlight)
                                          : palette().color(QPalette::WindowText),
                            i == selected ? 2.5 : 1.0));
        painter.drawPath(handle);
    }
}

void GradientEditor::mousePressEvent(QMouseEvent* event)
{
    QPoint p   = event->pos();
    int    hit = StopAt(p);

    if(event->button() == Qt::RightButton)
    {
        if(hit >= 0 && gradient->Stops().size() > 1)
        {
            gradient->RemoveStop(hit);
            selected = -1;
            update();
            emit GradientChanged();
        }
        return;
    }

    if(event->button() != Qt::LeftButton)
    {
        return;
    }

    if(hit >= 0)
    {
        selected = hit;
        dragging = true;
        update();
        return;
    }

    if(BarRect().adjusted(0, 0, 0, HANDLE_H).contains(p))
    {
        double pos = PosFromX(p.x());
        float r, g, b;
        gradient->Sample(pos, false, r, g, b);
        gradient->AddStop(pos, QColor::fromRgbF(r, g, b));

        /* find the new stop so it can be dragged immediately */
        const auto& stops = gradient->Stops();
        for(int i = 0; i < (int)stops.size(); i++)
        {
            if(std::fabs(stops[i].pos - pos) < 1e-9)
            {
                selected = i;
            }
        }
        dragging = true;
        update();
        emit GradientChanged();
    }
}

void GradientEditor::mouseMoveEvent(QMouseEvent* event)
{
    if(dragging && selected >= 0)
    {
        gradient->MoveStop(selected, PosFromX(event->pos().x()));
        update();
        emit GradientChanged();
    }
    else
    {
        setCursor(StopAt(event->pos()) >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
}

void GradientEditor::mouseReleaseEvent(QMouseEvent*)
{
    if(dragging && selected >= 0)
    {
        /*-------------------------------------------------*\
        | Re-sort and keep the same stop selected           |
        \*-------------------------------------------------*/
        GradientStop moved = gradient->Stops()[selected];
        gradient->SetStops(gradient->Stops());

        const auto& stops = gradient->Stops();
        for(int i = 0; i < (int)stops.size(); i++)
        {
            if(stops[i].pos == moved.pos && stops[i].color == moved.color)
            {
                selected = i;
                break;
            }
        }
        emit GradientChanged();
    }
    dragging = false;
    update();
}

void GradientEditor::mouseDoubleClickEvent(QMouseEvent* event)
{
    int hit = StopAt(event->pos());
    if(hit < 0)
    {
        return;
    }

    dragging = false;
    QColor c = QColorDialog::getColor(gradient->Stops()[hit].color, this, tr("Stop colour"));
    if(c.isValid())
    {
        gradient->SetStopColor(hit, c);
        update();
        emit GradientChanged();
    }
}
