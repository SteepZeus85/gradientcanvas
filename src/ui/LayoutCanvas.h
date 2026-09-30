/*---------------------------------------------------------*\
| LayoutCanvas.h                                            |
|                                                           |
|   The layout map editor. Device zones are rectangles you  |
|   drag, resize and rotate; in LED-edit mode each LED of   |
|   the selected zone becomes a draggable dot that snaps to |
|   a grid. The live effect is drawn behind everything.     |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QImage>
#include <vector>

class RenderEngine;
class LayoutCanvas;
struct ZonePlacement;

/*---------------------------------------------------------*\
| Background: effect preview, grid, direction arrow         |
\*---------------------------------------------------------*/
class CanvasBackground : public QGraphicsItem
{
public:
    explicit CanvasBackground(LayoutCanvas* canvas);
    QRectF  boundingRect() const override;
    void    paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;
    void    CanvasResized() { prepareGeometryChange(); }

    QImage  preview;

private:
    LayoutCanvas* canvas;
};

/*---------------------------------------------------------*\
| Draggable effect centre marker                            |
\*---------------------------------------------------------*/
class CenterHandle : public QGraphicsItem
{
public:
    explicit CenterHandle(LayoutCanvas* canvas);
    QRectF  boundingRect() const override;
    void    paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

    bool    syncing = false;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    LayoutCanvas* canvas;
};

/*---------------------------------------------------------*\
| One LED in LED-edit mode                                  |
\*---------------------------------------------------------*/
class LedItem : public QGraphicsItem
{
public:
    LedItem(LayoutCanvas* canvas, int zone_index, unsigned int led, double radius, QGraphicsItem* parent);
    QRectF  boundingRect() const override;
    void    paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

    unsigned int led;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void     mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    LayoutCanvas*   canvas;
    int             zone_index;
    double          radius;
    bool            syncing = false;
    friend class ZoneItem;
};

/*---------------------------------------------------------*\
| One device zone                                           |
\*---------------------------------------------------------*/
class ZoneItem : public QGraphicsItem
{
public:
    ZoneItem(LayoutCanvas* canvas, int zone_index);

    QRectF  boundingRect() const override;
    void    paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

    int     ZoneIndex() const { return zone_index; }
    void    SyncFromModel();
    void    SetLedEditing(bool editing);
    bool    IsLedEditing() const { return !led_items.empty(); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void     mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void     mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void     mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void     hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    enum DragMode { DRAG_NONE, DRAG_RESIZE, DRAG_ROTATE };

    ZonePlacement&  Z() const;
    QRectF          ResizeHandle() const;
    QPointF         RotateHandle() const;
    double          LedRadius() const;

    LayoutCanvas*           canvas;
    int                     zone_index;
    DragMode                drag_mode   = DRAG_NONE;
    bool                    syncing     = false;
    std::vector<LedItem*>   led_items;
};

/*---------------------------------------------------------*\
| The view                                                  |
\*---------------------------------------------------------*/
class LayoutCanvas : public QGraphicsView
{
    Q_OBJECT

public:
    explicit LayoutCanvas(RenderEngine* engine, QWidget* parent = nullptr);

    RenderEngine*   Engine() const { return engine; }

    void    Rebuild();                  /* recreate items from the model   */
    void    RefreshFrame();             /* repaint colours for a new frame */
    void    SyncZone(int zone_index);   /* model changed outside canvas    */
    void    SelectZone(int zone_index);
    int     SelectedZone() const;
    void    UpdateEffectOverlays();     /* centre handle / arrow           */

    void    SetLedEditMode(bool enabled);
    bool    LedEditMode() const         { return led_edit_mode; }
    void    SetSnap(bool enabled, double grid);
    bool    SnapEnabled() const         { return snap_enabled; }
    double  GridSize() const            { return grid_size; }
    QPointF Snap(const QPointF& p) const;
    void    ResetZoom();

    /* called by items */
    void    NotifyZoneEdited(int zone_index);
    void    NotifyCenterMoved(const QPointF& scene_pos);

signals:
    void    ZoneSelected(int zone_index);
    void    ZoneEdited(int zone_index);
    void    CenterMoved();

protected:
    void    resizeEvent(QResizeEvent* event) override;
    void    mousePressEvent(QMouseEvent* event) override;
    void    wheelEvent(QWheelEvent* event) override;

private slots:
    void    OnSelectionChanged();

private:
    void    FitCanvas();
    void    UpdatePreviewImage();
    void    UpdateLedEditing();

    RenderEngine*           engine;
    QGraphicsScene*         scene_ptr;
    CanvasBackground*       background;
    CenterHandle*           center_handle;
    std::vector<ZoneItem*>  zone_items;
    bool                    led_edit_mode   = false;
    bool                    snap_enabled    = true;
    double                  grid_size       = 10.0;
    bool                    user_zoomed     = false;
    bool                    rebuilding      = false;
};
