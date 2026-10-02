/*---------------------------------------------------------*\
| LayoutCanvas.cpp                                          |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "LayoutCanvas.h"
#include "RenderEngine.h"
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

static const double PI = 3.141592653589793;

/*=========================================================*\
| CanvasBackground                                          |
\*=========================================================*/
CanvasBackground::CanvasBackground(LayoutCanvas* canvas_ptr) : canvas(canvas_ptr)
{
    setZValue(-10);
    setAcceptedMouseButtons(Qt::NoButton);
}

QRectF CanvasBackground::boundingRect() const
{
    const LayoutModel& l = canvas->Engine()->layout;
    return QRectF(0, 0, l.canvas_w, l.canvas_h).adjusted(-2, -2, 2, 2);
}

void CanvasBackground::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    RenderEngine*      engine = canvas->Engine();
    const LayoutModel& l      = engine->layout;
    QRectF             rect(0, 0, l.canvas_w, l.canvas_h);

    painter->fillRect(rect, QColor(14, 14, 18));

    /*-----------------------------------------------------*\
    | Live effect preview, dimmed so zones stand out        |
    \*-----------------------------------------------------*/
    if(!preview.isNull())
    {
        /*-------------------------------------------------*\
        | Scale once per preview update to the on-screen    |
        | size, then blit 1:1                               |
        \*-------------------------------------------------*/
        QRectF dev   = painter->worldTransform().mapRect(rect);
        QSize  dsize = dev.size().toSize().boundedTo(QSize(4096, 4096)).expandedTo(QSize(1, 1));
        if(scaled_version != preview_version || scaled_size != dsize)
        {
            scaled_cache   = QPixmap::fromImage(preview.scaled(dsize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            scaled_version = preview_version;
            scaled_size    = dsize;
        }
        painter->save();
        painter->setOpacity(0.45);
        painter->drawPixmap(rect, scaled_cache, QRectF(scaled_cache.rect()));
        painter->restore();
    }

    /*-----------------------------------------------------*\
    | Grid                                                  |
    \*-----------------------------------------------------*/
    double major = std::max(10.0, canvas->GridSize() * 5.0);
    painter->setPen(QPen(QColor(255, 255, 255, 22), 0));
    for(double x = major; x < l.canvas_w; x += major)
    {
        painter->drawLine(QPointF(x, 0), QPointF(x, l.canvas_h));
    }
    for(double y = major; y < l.canvas_h; y += major)
    {
        painter->drawLine(QPointF(0, y), QPointF(l.canvas_w, y));
    }

    painter->setPen(QPen(QColor(255, 255, 255, 90), 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect);

    /*-----------------------------------------------------*\
    | Direction arrow for directional effects               |
    \*-----------------------------------------------------*/
    const EffectInfo& info = GetEffectInfo(engine->params.type);
    if(info.uses_angle)
    {
        double a   = engine->params.angle * PI / 180.0;
        double len = std::min(l.canvas_w, l.canvas_h) * 0.18;
        if(engine->params.reverse)
        {
            a += PI;
        }
        QPointF c(l.canvas_w * 0.5, l.canvas_h * 0.5);
        QPointF tip = c + QPointF(std::cos(a), std::sin(a)) * len;
        QPointF l1  = tip - QPointF(std::cos(a - 0.4), std::sin(a - 0.4)) * 14;
        QPointF l2  = tip - QPointF(std::cos(a + 0.4), std::sin(a + 0.4)) * 14;

        painter->setPen(QPen(QColor(255, 255, 255, 110), 3, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(c - (tip - c), tip);
        painter->drawLine(tip, l1);
        painter->drawLine(tip, l2);
    }
}

/*=========================================================*\
| CenterHandle                                              |
\*=========================================================*/
CenterHandle::CenterHandle(LayoutCanvas* canvas_ptr) : canvas(canvas_ptr)
{
    setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIgnoresTransformations);
    setZValue(100);
    setCursor(Qt::SizeAllCursor);
    setToolTip(QObject::tr("Effect centre - drag, or right-click anywhere on the canvas"));
}

QRectF CenterHandle::boundingRect() const
{
    return QRectF(-14, -14, 28, 28);
}

void CenterHandle::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::black, 4));
    painter->drawEllipse(QPointF(0, 0), 9, 9);
    painter->setPen(QPen(Qt::white, 2));
    painter->drawEllipse(QPointF(0, 0), 9, 9);
    painter->drawLine(QPointF(-13, 0), QPointF(-4, 0));
    painter->drawLine(QPointF(4, 0),   QPointF(13, 0));
    painter->drawLine(QPointF(0, -13), QPointF(0, -4));
    painter->drawLine(QPointF(0, 4),   QPointF(0, 13));
}

QVariant CenterHandle::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if(change == ItemPositionChange && !syncing)
    {
        const LayoutModel& l = canvas->Engine()->layout;
        QPointF p = value.toPointF();
        p.setX(std::clamp(p.x(), 0.0, l.canvas_w));
        p.setY(std::clamp(p.y(), 0.0, l.canvas_h));
        return p;
    }
    if(change == ItemPositionHasChanged && !syncing)
    {
        canvas->NotifyCenterMoved(pos());
    }
    return QGraphicsItem::itemChange(change, value);
}

/*=========================================================*\
| LedItem                                                   |
\*=========================================================*/
LedItem::LedItem(LayoutCanvas* canvas_ptr, int zone_idx, unsigned int led_idx, double r, QGraphicsItem* parent)
    : QGraphicsItem(parent), led(led_idx), canvas(canvas_ptr), zone_index(zone_idx), radius(r)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(1);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QObject::tr("LED %1 - drag to move, double-click to reset").arg(led_idx));
}

QRectF LedItem::boundingRect() const
{
    return QRectF(-radius - 3, -radius - 3, radius * 2 + 6, radius * 2 + 6);
}

void LedItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const ZonePlacement& z = canvas->Engine()->layout.zones[zone_index];

    QColor c = (led < z.preview.size()) ? z.preview[led] : QColor(80, 80, 80);
    bool   overridden = z.led_overrides.count(led) > 0;

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(c);
    if(isSelected())
    {
        painter->setPen(QPen(Qt::white, 2.5));
    }
    else if(overridden)
    {
        painter->setPen(QPen(QColor(255, 180, 0), 1.5));
    }
    else
    {
        painter->setPen(QPen(QColor(0, 0, 0, 160), 1));
    }
    painter->drawEllipse(QPointF(0, 0), radius, radius);
}

QVariant LedItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if(syncing)
    {
        return QGraphicsItem::itemChange(change, value);
    }

    ZonePlacement& z = canvas->Engine()->layout.zones[zone_index];

    if(change == ItemPositionChange)
    {
        QPointF p = value.toPointF();

        /*-------------------------------------------------*\
        | Snap in zone-local space. Matrix zones snap to    |
        | their own cell grid, others to the canvas grid.   |
        \*-------------------------------------------------*/
        if(canvas->SnapEnabled())
        {
            if(z.IsMatrix())
            {
                double cw = z.w / z.matrix_w;
                double ch = z.h / z.matrix_h;
                p.setX((std::floor(p.x() / cw) + 0.5) * cw);
                p.setY((std::floor(p.y() / ch) + 0.5) * ch);
            }
            else
            {
                p = canvas->Snap(p);
            }
        }

        p.setX(std::clamp(p.x(), 0.0, z.w));
        p.setY(std::clamp(p.y(), 0.0, z.h));
        return p;
    }

    if(change == ItemPositionHasChanged)
    {
        QPointF p = pos();
        z.led_overrides[led] = QPointF(z.w > 0 ? p.x() / z.w : 0.5, z.h > 0 ? p.y() / z.h : 0.5);
        canvas->NotifyZoneEdited(zone_index);
    }

    return QGraphicsItem::itemChange(change, value);
}

void LedItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent*)
{
    ZonePlacement& z = canvas->Engine()->layout.zones[zone_index];
    z.led_overrides.erase(led);

    QPointF local = z.LedLocal(led);
    syncing = true;
    setPos(local.x() * z.w, local.y() * z.h);
    syncing = false;

    canvas->NotifyZoneEdited(zone_index);
}

/*=========================================================*\
| ZoneItem                                                  |
\*=========================================================*/
ZoneItem::ZoneItem(LayoutCanvas* canvas_ptr, int zone_idx) : canvas(canvas_ptr), zone_index(zone_idx)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    SyncFromModel();
}

ZonePlacement& ZoneItem::Z() const
{
    return canvas->Engine()->layout.zones[zone_index];
}

QRectF ZoneItem::boundingRect() const
{
    const ZonePlacement& z = Z();
    return QRectF(-12, -40, z.w + 26, z.h + 54);
}

QRectF ZoneItem::ResizeHandle() const
{
    return QRectF(Z().w - 7, Z().h - 7, 14, 14);
}

QPointF ZoneItem::RotateHandle() const
{
    return QPointF(Z().w * 0.5, -30);
}

double ZoneItem::LedRadius() const
{
    const ZonePlacement& z = Z();
    double cell;

    if(z.IsMatrix())
    {
        cell = std::min(z.w / z.matrix_w, z.h / z.matrix_h);
    }
    else if(z.led_count > 1)
    {
        cell = std::min(z.w / z.led_count, z.h);
    }
    else
    {
        cell = std::min(z.w, z.h);
    }
    return std::clamp(cell * 0.36, 2.0, 12.0);
}

void ZoneItem::SyncFromModel()
{
    const ZonePlacement& z = Z();

    syncing = true;
    prepareGeometryChange();
    setTransformOriginPoint(z.w * 0.5, z.h * 0.5);
    setPos(z.x, z.y);
    setRotation(z.rotation);

    for(LedItem* item : led_items)
    {
        QPointF local = z.LedLocal(item->led);
        item->syncing = true;
        item->setPos(local.x() * z.w, local.y() * z.h);
        item->syncing = false;
    }
    syncing = false;
    update();
}

void ZoneItem::SetLedEditing(bool editing)
{
    if(editing == IsLedEditing())
    {
        return;
    }

    if(!editing)
    {
        for(LedItem* item : led_items)
        {
            delete item;
        }
        led_items.clear();
        update();
        return;
    }

    const ZonePlacement& z = Z();
    double r = LedRadius();

    for(unsigned int i = 0; i < z.led_count; i++)
    {
        LedItem* item = new LedItem(canvas, zone_index, i, r, this);
        QPointF  local = z.LedLocal(i);
        item->syncing = true;
        item->setPos(local.x() * z.w, local.y() * z.h);
        item->syncing = false;
        led_items.push_back(item);
    }
    update();
}

void ZoneItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const ZonePlacement& z = Z();
    QRectF rect(0, 0, z.w, z.h);
    bool   online = (z.controller != nullptr);

    painter->setRenderHint(QPainter::Antialiasing);

    /*-----------------------------------------------------*\
    | Body                                                  |
    \*-----------------------------------------------------*/
    QPen border(isSelected() ? QColor(80, 170, 255) : QColor(200, 200, 200, 150), isSelected() ? 2.0 : 1.0);
    if(!online)
    {
        border.setStyle(Qt::DashLine);
    }
    painter->setPen(border);
    painter->setBrush(QColor(10, 10, 14, IsLedEditing() ? 120 : 170));
    painter->drawRoundedRect(rect, 4, 4);

    /*-----------------------------------------------------*\
    | Label                                                 |
    \*-----------------------------------------------------*/
    QFont f = painter->font();
    f.setPointSizeF(7.5);
    painter->setFont(f);
    painter->setPen(QColor(230, 230, 230, online ? 220 : 120));
    QString label = QString::fromStdString(z.device_name) + " · " + QString::fromStdString(z.zone_name);
    if(!online)
    {
        label += QObject::tr("  (offline)");
    }
    painter->drawText(QRectF(0, -16, std::max(z.w, 240.0), 14), Qt::AlignLeft | Qt::AlignVCenter, label);

    /*-----------------------------------------------------*\
    | LEDs (drawn by child items in LED-edit mode)          |
    \*-----------------------------------------------------*/
    if(!IsLedEditing())
    {
        double r = LedRadius();
        painter->setPen(Qt::NoPen);
        for(unsigned int i = 0; i < z.led_count; i++)
        {
            QPointF local = z.LedLocal(i);
            QColor  c     = (i < z.preview.size()) ? z.preview[i] : QColor(80, 80, 80);
            painter->setBrush(c);
            painter->drawEllipse(QPointF(local.x() * z.w, local.y() * z.h), r, r);
        }
    }

    /*-----------------------------------------------------*\
    | Handles                                               |
    \*-----------------------------------------------------*/
    if(isSelected())
    {
        painter->setPen(QPen(QColor(80, 170, 255), 1.5));
        painter->setBrush(QColor(20, 20, 26));
        painter->drawRect(ResizeHandle());

        QPointF rh = RotateHandle();
        painter->drawLine(QPointF(z.w * 0.5, 0), rh + QPointF(0, 6));
        painter->drawEllipse(rh, 6, 6);
    }
}

QVariant ZoneItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if(!syncing && change == ItemPositionChange && canvas->SnapEnabled())
    {
        return canvas->Snap(value.toPointF());
    }

    if(!syncing && change == ItemPositionHasChanged)
    {
        Z().x = pos().x();
        Z().y = pos().y();
        canvas->NotifyZoneEdited(zone_index);
    }

    return QGraphicsItem::itemChange(change, value);
}

void ZoneItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    if(isSelected() && ResizeHandle().contains(event->pos()))
    {
        setCursor(Qt::SizeFDiagCursor);
    }
    else if(isSelected() && QLineF(event->pos(), RotateHandle()).length() < 9)
    {
        setCursor(Qt::CrossCursor);
    }
    else
    {
        setCursor(Qt::SizeAllCursor);
    }
}

void ZoneItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if(event->button() == Qt::LeftButton && isSelected())
    {
        if(ResizeHandle().contains(event->pos()))
        {
            drag_mode = DRAG_RESIZE;
            event->accept();
            return;
        }
        if(QLineF(event->pos(), RotateHandle()).length() < 9)
        {
            drag_mode = DRAG_ROTATE;
            event->accept();
            return;
        }
    }

    drag_mode = DRAG_NONE;
    QGraphicsItem::mousePressEvent(event);
}

void ZoneItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    ZonePlacement& z = Z();

    if(drag_mode == DRAG_RESIZE)
    {
        /*-------------------------------------------------*\
        | Keep the top-left corner fixed in scene space     |
        | even when rotated                                 |
        \*-------------------------------------------------*/
        QPointF before = mapToScene(0, 0);

        double nw = std::max(8.0, event->pos().x());
        double nh = std::max(8.0, event->pos().y());
        if(canvas->SnapEnabled())
        {
            double g = canvas->GridSize();
            nw = std::max(g, std::round(nw / g) * g);
            nh = std::max(g, std::round(nh / g) * g);
        }

        syncing = true;
        prepareGeometryChange();
        z.w = nw;
        z.h = nh;
        setTransformOriginPoint(nw * 0.5, nh * 0.5);
        QPointF after = mapToScene(0, 0);
        setPos(pos() + (before - after));
        z.x = pos().x();
        z.y = pos().y();
        syncing = false;

        SyncFromModel();
        canvas->NotifyZoneEdited(zone_index);
        return;
    }

    if(drag_mode == DRAG_ROTATE)
    {
        QPointF c   = mapToScene(z.w * 0.5, z.h * 0.5);
        QPointF d   = event->scenePos() - c;
        double  ang = std::atan2(d.y(), d.x()) * 180.0 / PI + 90.0;

        if(canvas->SnapEnabled() || (event->modifiers() & Qt::ShiftModifier))
        {
            ang = std::round(ang / 15.0) * 15.0;
        }
        ang = std::fmod(ang + 360.0, 360.0);

        z.rotation = ang;
        syncing = true;
        setRotation(ang);
        syncing = false;
        canvas->NotifyZoneEdited(zone_index);
        return;
    }

    QGraphicsItem::mouseMoveEvent(event);
}

void ZoneItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    drag_mode = DRAG_NONE;
    QGraphicsItem::mouseReleaseEvent(event);
}

/*=========================================================*\
| LayoutCanvas                                              |
\*=========================================================*/
LayoutCanvas::LayoutCanvas(RenderEngine* engine_ptr, QWidget* parent)
    : QGraphicsView(parent), engine(engine_ptr)
{
    scene_ptr = new QGraphicsScene(this);
    setScene(scene_ptr);

    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::RubberBandDrag);
    setBackgroundBrush(QColor(28, 28, 32));
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setOptimizationFlags(QGraphicsView::DontSavePainterState | QGraphicsView::DontAdjustForAntialiasing);
    refresh_clock.start();
    trailing_refresh = new QTimer(this);
    trailing_refresh->setSingleShot(true);
    connect(trailing_refresh, &QTimer::timeout, this, &LayoutCanvas::DoRefresh);
    setMinimumSize(400, 260);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    background    = new CanvasBackground(this);
    center_handle = new CenterHandle(this);
    scene_ptr->addItem(background);
    scene_ptr->addItem(center_handle);

    connect(scene_ptr, &QGraphicsScene::selectionChanged, this, &LayoutCanvas::OnSelectionChanged);

    Rebuild();
}

void LayoutCanvas::Rebuild()
{
    rebuilding = true;

    int previously_selected = SelectedZone();

    for(ZoneItem* item : zone_items)
    {
        scene_ptr->removeItem(item);
        delete item;
    }
    zone_items.clear();

    LayoutModel& l = engine->layout;
    for(int i = 0; i < (int)l.zones.size(); i++)
    {
        if(l.zones[i].enabled)
        {
            ZoneItem* item = new ZoneItem(this, i);
            scene_ptr->addItem(item);
            zone_items.push_back(item);
            if(i == previously_selected)
            {
                item->setSelected(true);
            }
        }
    }

    background->CanvasResized();
    scene_ptr->setSceneRect(QRectF(-60, -60, l.canvas_w + 120, l.canvas_h + 120));

    UpdateEffectOverlays();
    engine->RenderFrame();

    if(!user_zoomed)
    {
        FitCanvas();
    }

    rebuilding = false;
    UpdateLedEditing();
}

void LayoutCanvas::RefreshFrame()
{
    /*-----------------------------------------------------*\
    | LED dots repaint at up to 20 fps. If a frame arrives  |
    | too soon, a trailing refresh makes sure the final     |
    | state of an edit is always shown.                     |
    \*-----------------------------------------------------*/
    const qint64 led_interval = 50;
    qint64 now = refresh_clock.elapsed();

    if(now - last_led_paint >= led_interval)
    {
        DoRefresh();
    }
    else if(!trailing_refresh->isActive())
    {
        trailing_refresh->start(int(led_interval - (now - last_led_paint)));
    }
}

void LayoutCanvas::DoRefresh()
{
    qint64 now     = refresh_clock.elapsed();
    last_led_paint = now;

    /*-----------------------------------------------------*\
    | The animated background is the expensive part: ~8 fps |
    | is plenty for a dimmed backdrop                       |
    \*-----------------------------------------------------*/
    if(live_preview && now - last_bg_paint >= 120)
    {
        last_bg_paint = now;
        UpdatePreviewImage();
        background->update();
    }

    for(ZoneItem* item : zone_items)
    {
        item->update();
        if(item->IsLedEditing())
        {
            for(QGraphicsItem* child : item->childItems())
            {
                child->update();
            }
        }
    }
}

void LayoutCanvas::SetLivePreview(bool enabled)
{
    live_preview = enabled;
    if(!enabled)
    {
        background->preview = QImage();
        background->preview_version++;
    }
    last_bg_paint = -1000;
    DoRefresh();
    background->update();
}

void LayoutCanvas::SyncZone(int zone_index)
{
    for(ZoneItem* item : zone_items)
    {
        if(item->ZoneIndex() == zone_index)
        {
            item->SyncFromModel();
        }
    }
}

void LayoutCanvas::SelectZone(int zone_index)
{
    for(ZoneItem* item : zone_items)
    {
        if(item->ZoneIndex() == zone_index)
        {
            if(!item->isSelected())
            {
                scene_ptr->clearSelection();
                item->setSelected(true);
                ensureVisible(item);
            }
            return;
        }
    }
    scene_ptr->clearSelection();
}

int LayoutCanvas::SelectedZone() const
{
    for(QGraphicsItem* item : scene_ptr->selectedItems())
    {
        if(ZoneItem* zi = dynamic_cast<ZoneItem*>(item))
        {
            return zi->ZoneIndex();
        }
        if(ZoneItem* parent = dynamic_cast<ZoneItem*>(item->parentItem()))
        {
            return parent->ZoneIndex();
        }
    }
    return -1;
}

void LayoutCanvas::UpdateEffectOverlays()
{
    const EffectInfo&  info = GetEffectInfo(engine->params.type);
    const LayoutModel& l    = engine->layout;

    center_handle->setVisible(info.uses_center);
    center_handle->syncing = true;
    center_handle->setPos(engine->params.cx * l.canvas_w, engine->params.cy * l.canvas_h);
    center_handle->syncing = false;
    background->update();
}

void LayoutCanvas::SetLedEditMode(bool enabled)
{
    led_edit_mode = enabled;
    setDragMode(QGraphicsView::RubberBandDrag);
    UpdateLedEditing();
}

void LayoutCanvas::UpdateLedEditing()
{
    /*-----------------------------------------------------*\
    | Deferred so items are never deleted from inside a     |
    | scene event that may still reference them            |
    \*-----------------------------------------------------*/
    QTimer::singleShot(0, this, [this]()
    {
        int selected = SelectedZone();
        for(ZoneItem* item : zone_items)
        {
            item->SetLedEditing(led_edit_mode && item->ZoneIndex() == selected);
        }
    });
}

void LayoutCanvas::SetSnap(bool enabled, double grid)
{
    snap_enabled = enabled;
    grid_size    = std::max(1.0, grid);
    background->update();
}

QPointF LayoutCanvas::Snap(const QPointF& p) const
{
    if(!snap_enabled)
    {
        return p;
    }
    return QPointF(std::round(p.x() / grid_size) * grid_size,
                   std::round(p.y() / grid_size) * grid_size);
}

void LayoutCanvas::ResetZoom()
{
    user_zoomed = false;
    FitCanvas();
}

void LayoutCanvas::NotifyZoneEdited(int zone_index)
{
    emit ZoneEdited(zone_index);
    if(!engine->IsPlaying())
    {
        engine->RenderFrame();
    }
}

void LayoutCanvas::NotifyCenterMoved(const QPointF& scene_pos)
{
    const LayoutModel& l = engine->layout;
    engine->params.cx = std::clamp(scene_pos.x() / l.canvas_w, 0.0, 1.0);
    engine->params.cy = std::clamp(scene_pos.y() / l.canvas_h, 0.0, 1.0);
    emit CenterMoved();
    if(!engine->IsPlaying())
    {
        engine->RenderFrame();
    }
}

void LayoutCanvas::OnSelectionChanged()
{
    if(rebuilding)
    {
        return;
    }
    emit ZoneSelected(SelectedZone());
    UpdateLedEditing();
}

void LayoutCanvas::FitCanvas()
{
    fitInView(scene_ptr->sceneRect(), Qt::KeepAspectRatio);
}

void LayoutCanvas::UpdatePreviewImage()
{
    const LayoutModel& l = engine->layout;
    double aspect = l.canvas_w / std::max(1.0, l.canvas_h);
    int    iw     = 160;
    int    ih     = std::max(1, (int)std::lround(iw / aspect));

    if(background->preview.width() != iw || background->preview.height() != ih)
    {
        background->preview = QImage(iw, ih, QImage::Format_RGB32);
    }
    background->preview_version++;

    for(int y = 0; y < ih; y++)
    {
        QRgb* line = reinterpret_cast<QRgb*>(background->preview.scanLine(y));
        for(int x = 0; x < iw; x++)
        {
            float r, g, b;
            EvaluateEffect(engine->params, engine->gradient,
                           (x + 0.5) / iw, (y + 0.5) / ih, aspect,
                           engine->Phase(), engine->Time(), r, g, b);
            line[x] = qRgb((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f));
        }
    }
}

void LayoutCanvas::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if(!user_zoomed)
    {
        FitCanvas();
    }
}

void LayoutCanvas::mousePressEvent(QMouseEvent* event)
{
    /*-----------------------------------------------------*\
    | Right-click on empty canvas moves the effect centre   |
    \*-----------------------------------------------------*/
    if(event->button() == Qt::RightButton && GetEffectInfo(engine->params.type).uses_center)
    {
        QGraphicsItem* hit = itemAt(event->pos());
        if(hit == nullptr || hit == background)
        {
            QPointF p = mapToScene(event->pos());
            const LayoutModel& l = engine->layout;
            p.setX(std::clamp(p.x(), 0.0, l.canvas_w));
            p.setY(std::clamp(p.y(), 0.0, l.canvas_h));
            center_handle->syncing = true;
            center_handle->setPos(p);
            center_handle->syncing = false;
            NotifyCenterMoved(p);
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void LayoutCanvas::wheelEvent(QWheelEvent* event)
{
    if(event->modifiers() & Qt::ControlModifier)
    {
        double factor = (event->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        scale(factor, factor);
        user_zoomed = true;
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}
