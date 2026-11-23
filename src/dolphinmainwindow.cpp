/*
 * SPDX-FileCopyrightText: 2006 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2006 Stefan Monov <logixoul@gmail.com>
 * SPDX-FileCopyrightText: 2006 Cvetoslav Ludmiloff <ludmiloff@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinmainwindow.h"
#include "kitemviews/kitemlistcontainer.h"

#include "kitemviews/kitemlistwidget.h"

#include "admin/workerintegration.h"
#include "dolphin_generalsettings.h"
#include "dolphinbookmarkhandler.h"
#include "dolphincontextmenu.h"
#include "dolphindockwidget.h"
#include "dolphinmainwindowadaptor.h"
#include "dolphinnavigatorswidgetaction.h"
#include "dolphinnewfilemenu.h"
#include "dolphinplacesmodelsingleton.h"
#include "dolphinrecenttabsmenu.h"
#include "dolphintabpage.h"
#include "dolphinurlnavigatorscontroller.h"
#include "dolphinviewcontainer.h"
#include "global.h"
#include "middleclickactioneventfilter.h"
#include "panels/folders/folderspanel.h"
#include "panels/places/placespanel.h"
#include "panels/terminal/terminalpanel.h"
#include "search/dolphinquery.h"
#include "selectionmode/actiontexthelper.h"
#if KIO_VERSION >= QT_VERSION_CHECK(6, 24, 0)
#include "servicemenushortcutmanager.h"
#endif
#include "settings/dolphinsettingsdialog.h"
#include "statusbar/diskspaceusagemenu.h"
#include "views/dolphinnewfilemenuobserver.h"
#include "views/dolphinremoteencoding.h"
#include "views/dolphinviewactionhandler.h"
#include "views/draganddrophelper.h"
#include "views/viewproperties.h"

#include <functional>
#include <KActionCollection>
#include <KActionMenu>
#include <KAuthorized>
#include <KColorSchemeManager>
#include <KColorSchemeMenu>
#include <KConfig>
#include <KConfigGui>
#include <KDesktopFile>
#include <KDialogJobUiDelegate>
#include <KDualAction>
#include <KFileItemListProperties>
#include <KIO/CommandLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KMessageBox>
#include <KProtocolInfo>
#include <KProtocolManager>
#include <KRecentFilesAction>
#include <KRuntimePlatform>
#include <KShell>
#include <KShortcutsDialog>
#include <KStandardAction>
#include <KSycoca>
#include <KTerminalLauncherJob>
#include <KToggleAction>
#include <KToolBar>
#include <KToolBarPopupAction>
#include <KUrlComboBox>
#include <KUrlNavigator>
#include <KWindowEffects>
#include <KWindowSystem>
#include <KXMLGUIFactory>

#include <kwidgetsaddons_version.h>

#include <QApplication>
#include <QAbstractButton>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTabBar>
#include <QToolBar>
#include <QWindow>
#include <KColorUtils>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QProxyStyle>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDomDocument>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSharedPointer>
#include <QShowEvent>
#include <QStandardPaths>
#include <QFile>
#include <QMenu>
#include <QRegularExpression>
#include <QPlatformSurfaceEvent>
#include <QStyleOption>
#include <QTimer>
#include <QPainterPath>
#include <QToolButton>
#include <QtConcurrentRun>
#include <dolphindebug.h>

#include <algorithm>

#if HAVE_X11
#include <KStartupInfo>
#endif

namespace
{
// Used for GeneralSettings::version() to determine whether
// an updated version of Dolphin is running, so as to migrate
// removed/renamed ...etc config entries; increment it in such
// cases
const int CurrentDolphinVersion = 202;
// The maximum number of entries in the back/forward popup menu
const int MaxNumberOfNavigationentries = 12;
// The maximum number of "Go to Tab" shortcuts
const int MaxActivateTabShortcuts = 9;
}

// dolphin-ng: the separators between the docks and the view: 4 px, invisible, still draggable
// dolphin-ng: the tabs in the title bar (setting TABS=titlebar). The window has no frame of the window manager's: the
// row of tabs is its title bar, with the window's buttons at its end, and these two classes give it what the frame
// gave: buttons, moving by the row, resizing from the edges.
class NgWindowButton : public QAbstractButton
{
public:
    enum Kind { Minimize, Maximize, Close };
    NgWindowButton(Kind kind, QWidget *parent)
        : QAbstractButton(parent)
        , m_kind(kind)
    {
        setFixedSize(22, 26); // (an 18 px disc every 24 px, as the buttons of Breeze NG's window decoration)
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::ArrowCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = palette().color(QPalette::Window).lightnessF() < 0.5;
        const QColor ink = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
        const bool hovered = underMouse();
        const QRectF circle = QRectF(rect()).adjusted(2, 4, -2, -4);
        QColor fill = ink;
        fill.setAlphaF(isDown() ? 0.30 : hovered ? 0.22 : 0.12);
        if (m_kind == Close) {
            fill = QColor(232, 84, 72, isDown() ? 255 : hovered ? 240 : 205);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawEllipse(circle);
        QColor glyph = m_kind == Close ? QColor(255, 255, 255, 235) : ink;
        if (m_kind != Close) {
            glyph.setAlphaF(0.85);
        }
        painter.setPen(QPen(glyph, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF c = circle.center();
        const qreal r = 3.2;
        const bool maximized = window()->isMaximized();
        switch (m_kind) {
        case Minimize: // a chevron down, as Breeze draws it
            painter.drawPolyline(QPolygonF{QPointF(c.x() - r, c.y() - r / 2), QPointF(c.x(), c.y() + r / 2), QPointF(c.x() + r, c.y() - r / 2)});
            break;
        case Maximize:
            if (maximized) { // a diamond: back to its size
                painter.drawPolygon(QPolygonF{QPointF(c.x() - r, c.y()), QPointF(c.x(), c.y() - r), QPointF(c.x() + r, c.y()), QPointF(c.x(), c.y() + r)});
            } else { // a chevron up
                painter.drawPolyline(QPolygonF{QPointF(c.x() - r, c.y() + r / 2), QPointF(c.x(), c.y() - r / 2), QPointF(c.x() + r, c.y() + r / 2)});
            }
            break;
        case Close:
            painter.drawLine(QPointF(c.x() - r + 0.6, c.y() - r + 0.6), QPointF(c.x() + r - 0.6, c.y() + r - 0.6));
            painter.drawLine(QPointF(c.x() + r - 0.6, c.y() - r + 0.6), QPointF(c.x() - r + 0.6, c.y() + r - 0.6));
            break;
        }
    }

private:
    Kind m_kind;
};

// dolphin-ng: a corner of the window without a frame, cut round over whatever is there. The widget style cuts the
// corners where it lays the window's sheet, but a child that fills itself (the file view in the Soft look, which is
// not glass) painted its square corner over the cut: this sits on top of everything in that corner and clears what
// is outside the rounded shape.
class NgCornerCut : public QWidget
{
public:
    NgCornerCut(QWidget *window, Qt::Corner corner)
        : QWidget(window)
        , m_corner(corner)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }
    void place(int radius)
    {
        m_radius = radius;
        if (radius <= 0) {
            hide();
            return;
        }
        const QRect frame = parentWidget()->rect();
        const bool right = m_corner == Qt::TopRightCorner || m_corner == Qt::BottomRightCorner;
        const bool bottom = m_corner == Qt::BottomLeftCorner || m_corner == Qt::BottomRightCorner;
        setGeometry(right ? frame.right() + 1 - radius : 0, bottom ? frame.bottom() + 1 - radius : 0, radius, radius);
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const QRectF frame(mapFrom(parentWidget(), QPoint(0, 0)), QSizeF(parentWidget()->size()));
        QPainterPath outside;
        outside.addRect(QRectF(rect()));
        QPainterPath shape;
        shape.addRoundedRect(frame, m_radius, m_radius);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillPath(outside.subtracted(shape), Qt::black);
    }

private:
    Qt::Corner m_corner;
    int m_radius = 0;
};

class NgFrameless : public QObject
{
public:
    NgFrameless(QWidget *window, const QList<QWidget *> &titleAreas)
        : QObject(window)
        , m_window(window)
        , m_titleAreas(titleAreas)
    {
        qApp->installEventFilter(this);
    }
    ~NgFrameless() override
    {
        restoreCursor();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!watched->isWidgetType()) {
            return false;
        }
        auto *widget = static_cast<QWidget *>(watched);
        if (widget->window() != m_window || !m_window->windowHandle()) {
            return false;
        }
        const bool fixed = m_window->isMaximized() || m_window->isFullScreen();
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons() == Qt::NoButton) {
                showEdges(fixed ? Qt::Edges() : edgesAt(mouse->globalPosition()));
            }
            break;
        }
        case QEvent::Leave:
            if (watched == m_window) {
                restoreCursor();
            }
            break;
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton) {
                break;
            }
            const Qt::Edges edges = fixed ? Qt::Edges() : edgesAt(mouse->globalPosition());
            if (edges) {
                restoreCursor();
                m_window->windowHandle()->startSystemResize(edges);
                return true;
            }
            if (m_titleAreas.contains(widget)) {
                m_window->windowHandle()->startSystemMove(); // the row of tabs is the title bar: drag the window by it
                return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick:
            if (m_titleAreas.contains(widget) && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
                m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    Qt::Edges edgesAt(const QPointF &global) const
    {
        const QPoint p = m_window->mapFromGlobal(global.toPoint());
        const int margin = 6;
        Qt::Edges edges;
        if (!m_window->rect().adjusted(-2, -2, 2, 2).contains(p)) {
            return edges;
        }
        if (p.x() < margin) {
            edges |= Qt::LeftEdge;
        } else if (p.x() >= m_window->width() - margin) {
            edges |= Qt::RightEdge;
        }
        if (p.y() < 4) { // (thinner at the top: the tabs are right there)
            edges |= Qt::TopEdge;
        } else if (p.y() >= m_window->height() - margin) {
            edges |= Qt::BottomEdge;
        }
        return edges;
    }
    void showEdges(Qt::Edges edges)
    {
        if (edges == m_shown) {
            return;
        }
        restoreCursor();
        m_shown = edges;
        if (!edges) {
            return;
        }
        Qt::CursorShape shape = Qt::SizeVerCursor;
        if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
            shape = Qt::SizeFDiagCursor;
        } else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
            shape = Qt::SizeBDiagCursor;
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            shape = Qt::SizeHorCursor;
        }
        QGuiApplication::setOverrideCursor(shape);
        m_overridden = true;
    }
    void restoreCursor()
    {
        if (m_overridden) {
            QGuiApplication::restoreOverrideCursor();
            m_overridden = false;
        }
        m_shown = Qt::Edges();
    }

    QWidget *m_window;
    QList<QWidget *> m_titleAreas;
    Qt::Edges m_shown;
    bool m_overridden = false;
};

class NgSeparatorStyle : public QProxyStyle
{
public:
    explicit NgSeparatorStyle(const QString &base)
        : QProxyStyle(base)
    {
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        if (metric == PM_DockWidgetSeparatorExtent) {
            return 4;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override
    {
        if (element == PE_IndicatorDockWidgetResizeHandle) {
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

// dolphin-ng: with another widget style than Breeze NG, the one look of every element of the location row (back/forward/
// up, Root, the location bar, the filter field): the field colour nearly solid, a hairline of ink, slightly rounded corners (ngFieldRadius).
// @p state: 0 at rest, 1 hovered, 2 pressed or focused
static void ngFieldColours(QColor *fill, QColor *edge, int state)
{
    const QPalette palette = QApplication::palette();
    const bool dark = ngIsDark(palette);
    *fill = palette.color(QPalette::Active, QPalette::Base);
    fill->setAlphaF(dark ? (state == 0 ? 0.55 : state == 1 ? 0.70 : 0.80) : (state == 0 ? 0.85 : state == 1 ? 0.95 : 1.0));
    *edge = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
    edge->setAlphaF(state == 2 ? 0.28 : state == 1 ? 0.18 : 0.11);
}

// (the corners of those elements: a little rounded, rectangles more than pills)
static constexpr int ngFieldRadius = 7;

static QString ngRgba(const QColor &colour)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(colour.red()).arg(colour.green()).arg(colour.blue()).arg(QString::number(colour.alphaF(), 'f', 3));
}

// dolphin-ng: the location bar and the folder's filter field when the widget style is not Breeze NG: the soft rounded
// field of the sort bar (a faint veil, a hairline of ink) in place of the style's own frame. Stock Breeze drew a hard
// dark rectangle there, out of place among the bar's pills. Set on those widgets alone (a style does not pass to
// their children): KUrlNavigator and QLineEdit draw their frame themselves through it
class NgFieldStyle : public QProxyStyle
{
public:
    explicit NgFieldStyle(const QString &base)
        : QProxyStyle(base)
    {
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override
    {
        if (element != PE_FrameLineEdit && element != PE_PanelLineEdit) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        if (element == PE_FrameLineEdit && qobject_cast<const QLineEdit *>(widget)) {
            return; // (a line edit's panel draws the frame too)
        }
        const bool focus = option->state & State_HasFocus;
        const bool hover = option->state & State_MouseOver;
        // the field's own colour (white on a light scheme), nearly solid, and a hairline: it reads as a field on the
        // window's grey, where a veil of ink alone was lost
        // (the same fill, rim and round ends as the pill of back/forward/up and Root beside them: ngFieldColours)
        QColor fill, edge;
        ngFieldColours(&fill, &edge, focus ? 2 : hover ? 1 : 0);
        const QRectF frame = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = qMin<qreal>(ngFieldRadius, frame.height() / 2);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(edge, 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(frame, radius, radius);
        painter->restore();
    }
};

static NgFieldStyle *ngFieldStyle()
{
    static QPointer<NgFieldStyle> style;
    if (!style) {
        style = new NgFieldStyle(QApplication::style()->name());
        style->setParent(qApp);
    }
    return style;
}

// dolphin-ng: the track of the title row's tabs when the widget style is not Breeze NG: one pill behind them all, as
// the sort pill of the bar holds its buttons (the tabs themselves are drawn by a style sheet, see ngStyleTitleRow)
class NgTabTrack : public QObject
{
public:
    explicit NgTabTrack(QTabBar *bar)
        : QObject(bar)
        , m_bar(bar)
    {
        bar->installEventFilter(this);
    }
    void setColours(const QColor &fill, const QColor &edge)
    {
        m_fill = fill;
        m_edge = edge;
        m_bar->update();
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_bar && event->type() == QEvent::Paint && m_fill.isValid() && m_bar->count() > 0) {
            QRect all;
            for (int i = 0; i < m_bar->count(); ++i) {
                all = all.united(m_bar->tabRect(i));
            }
            all = all.intersected(m_bar->rect());
            if (!all.isEmpty()) {
                QPainter painter(m_bar);
                painter.setRenderHint(QPainter::Antialiasing);
                const QRectF track = QRectF(all).adjusted(0.5, 0.5, -0.5, -0.5);
                painter.setPen(QPen(m_edge, 1));
                painter.setBrush(m_fill);
                painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTabBar *m_bar;
    QColor m_fill, m_edge;
};

// dolphin-ng: the location bar's buttons, while the pointer is on them:
// - their text shadow is off (the effect shades the whole button, and its hover veil turned into a dark block);
// - they lose their highlight when the pointer has left them without a Leave event: a popup opened from them (the
//   menu of subfolders) takes the pointer, on Wayland, and the button stayed highlighted after the menu closed. While
//   one is "entered", where the pointer is is looked at; when it is gone, the button is sent the Leave it missed. (KIO
//   is the system's: Dialog NG mends the same thing inside KIO)
class NgCrumbWatch : public QObject
{
public:
    explicit NgCrumbWatch(QObject *parent)
        : QObject(parent)
    {
        m_timer.setInterval(250);
        connect(&m_timer, &QTimer::timeout, this, &NgCrumbWatch::check);
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
            const bool entered = event->type() == QEvent::Enter;
            if (widget->graphicsEffect()) {
                widget->graphicsEffect()->setEnabled(!entered);
            }
            if (entered) {
                m_entered = widget;
                m_timer.start();
            } else if (m_entered == widget) {
                m_entered.clear();
                m_timer.stop();
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void check()
    {
        if (qEnvironmentVariableIsSet("DOLPHIN_NG_DEMO_CRUMB")) {
            return; // (the demo's button is "entered" without a pointer: left as it is)
        }
        QWidget *widget = m_entered;
        if (!widget) {
            m_timer.stop();
            return;
        }
        if (QApplication::activePopupWidget() || widget->rect().contains(widget->mapFromGlobal(QCursor::pos()))) {
            return; // its menu is open, or the pointer is still on it
        }
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(widget, &leave); // (comes back through eventFilter: the timer stops there)
        widget->update();
    }

    QTimer m_timer;
    QPointer<QWidget> m_entered;
};

// dolphin-ng: runs a function, once per turn of the event loop, when a widget gains or loses children or lays itself
// out again (the location bar makes its buttons anew at every folder)
class NgChildrenWatch : public QObject
{
public:
    NgChildrenWatch(QWidget *target, std::function<void()> function)
        : QObject(target)
        , m_function(std::move(function))
    {
        target->installEventFilter(this);
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((event->type() == QEvent::ChildAdded || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show) && !m_queued) {
            m_queued = true;
            QTimer::singleShot(0, this, [this] {
                m_queued = false;
                m_function();
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_function;
    bool m_queued = false;
};

// dolphin-ng: a wider hold on the edge between a side panel and the view. The window's own separator there is 4 px and
// draws nothing (NgSeparatorStyle), so it was hard to find ("the sidebar can't be resized"). This strip, invisible
// too, covers it and the empty margins on either side of it (where the panel's card and the view's content do not
// reach) and resizes the panel as the separator does.
class NgDockGrip : public QWidget
{
public:
    NgDockGrip(QMainWindow *window, QDockWidget *dock)
        : QWidget(window)
        , m_window(window)
        , m_dock(dock)
    {
        setCursor(Qt::SplitHCursor);
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(false);
        hide();
    }
    QDockWidget *dock() const
    {
        return m_dock;
    }
    // over the gap between the panel and the view, @p reach px into each of them; hidden when the panel is not beside the view
    void place(int reach)
    {
        QWidget *central = m_window->centralWidget();
        const bool shown = m_dock && central && m_dock->isVisible() && !m_dock->isFloating();
        const Qt::DockWidgetArea area = m_dock ? m_window->dockWidgetArea(m_dock) : Qt::NoDockWidgetArea;
        if (!shown || (area != Qt::LeftDockWidgetArea && area != Qt::RightDockWidgetArea)) {
            hide();
            return;
        }
        const QRect dock = m_dock->geometry();
        const QRect view = central->geometry();
        m_left = area == Qt::LeftDockWidgetArea;
        const int from = m_left ? dock.right() + 1 - reach : view.right() + 1 - reach;
        const int to = m_left ? view.left() + reach : dock.left() + reach;
        if (to <= from || (m_left ? view.left() < dock.right() : dock.left() < view.right())) {
            hide();
            return;
        }
        setGeometry(from, dock.top(), to - from, dock.height());
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dock) {
            m_startX = event->globalPosition().x();
            m_startWidth = m_dock->width();
            m_dragging = true;
        }
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && m_dock) {
            const int delta = qRound(event->globalPosition().x() - m_startX);
            m_window->resizeDocks({m_dock.data()}, {qMax(0, m_left ? m_startWidth + delta : m_startWidth - delta)}, Qt::Horizontal);
        }
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_dragging = false;
        event->accept();
    }

private:
    QMainWindow *m_window;
    QPointer<QDockWidget> m_dock;
    bool m_left = true;
    bool m_dragging = false;
    qreal m_startX = 0;
    int m_startWidth = 0;
};

// dolphin-ng: the side panels keep the width the user gave them. Narrowing the window takes room from them too, and
// when it widens again QMainWindow gives all the new room to the view: the sidebar stayed narrow, its places cut to
// "…" (and a window closed while narrow came back so). The width a panel is dragged to is remembered (dolphinrc
// [DolphinNG] DockWidth_<panel>; the sidebar's default is a comfortable 36 characters), and the panel grows back
// towards it when the window is shown and whenever it widens, never past a third of the window.
class NgDockMemory : public QObject
{
public:
    NgDockMemory(QMainWindow *window, const QList<QDockWidget *> &docks)
        : QObject(window)
        , m_window(window)
    {
        window->installEventFilter(this);
        const KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
        for (QDockWidget *dock : docks) {
            dock->installEventFilter(this);
            m_docks.append(dock);
            const int fallback = dock->objectName() == QLatin1String("placesDock") ? window->fontMetrics().averageCharWidth() * 36 : 0;
            m_wanted[dock] = group.readEntry(QStringLiteral("DockWidth_%1").arg(dock->objectName()).toUtf8().constData(), fallback);
        }
        m_windowWidth = window->width();
        // (the panels are resized after the window, inside its resize and again a turn of the event loop later when
        // QMainWindow lays them out: a short while is let pass after the window's last resize before growing them)
        m_settle.setSingleShot(true);
        m_settle.setInterval(300);
        connect(&m_settle, &QTimer::timeout, this, [this] {
            if (m_grew) {
                m_grew = false;
                grow();
            }
        });
        m_save.setSingleShot(true);
        m_save.setInterval(600);
        connect(&m_save, &QTimer::timeout, this, [this] {
            KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
            for (auto it = m_wanted.constBegin(); it != m_wanted.constEnd(); ++it) {
                if (it.key() && it.value() > 0) {
                    group.writeEntry(QStringLiteral("DockWidth_%1").arg(it.key()->objectName()).toUtf8().constData(), it.value());
                }
            }
            group.sync();
        });
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            // shown (with the state of last time, maybe narrow) or wider: the panels grow back once it settles
            m_grew = m_grew || event->type() == QEvent::Show || m_window->width() > m_windowWidth;
            m_windowWidth = m_window->width();
            m_settle.start();
        } else if (event->type() == QEvent::Resize && !m_growing && !m_settle.isActive() && (QGuiApplication::mouseButtons() & Qt::LeftButton)) {
            // resized with the button held, the window keeping its size: the panel's edge dragged
            auto *dock = qobject_cast<QDockWidget *>(watched);
            if (dock && dock->isVisible() && !dock->isFloating() && dock->width() > 0) {
                m_wanted[dock] = dock->width();
                m_save.start();
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void grow()
    {
        QList<QDockWidget *> docks;
        QList<int> sizes;
        for (const QPointer<QDockWidget> &dock : std::as_const(m_docks)) {
            if (!dock || !dock->isVisible() || dock->isFloating()) {
                continue;
            }
            const Qt::DockWidgetArea area = m_window->dockWidgetArea(dock);
            if (area != Qt::LeftDockWidgetArea && area != Qt::RightDockWidgetArea) {
                continue;
            }
            const int wanted = qMin(m_wanted.value(dock, 0), m_window->width() / 3);
            if (wanted > dock->width()) {
                docks.append(dock);
                sizes.append(wanted);
            }
        }
        if (!docks.isEmpty()) {
            m_growing = true;
            m_window->resizeDocks(docks, sizes, Qt::Horizontal);
            m_growing = false;
        }
    }

    QMainWindow *m_window;
    QList<QPointer<QDockWidget>> m_docks;
    QHash<QDockWidget *, int> m_wanted;
    int m_windowWidth = 0;
    QTimer m_settle; // running while the window's resize is still being laid out
    QTimer m_save;
    bool m_grew = false;
    bool m_growing = false;
};

// dolphin-ng: runs a function, once per turn of the event loop, whenever one of some widgets moves, is resized or shown
class NgGeometryWatch : public QObject
{
public:
    using QObject::QObject;
    void watch(const QList<QWidget *> &widgets, std::function<void()> function)
    {
        for (const QPointer<QWidget> &old : std::as_const(m_widgets)) {
            if (old) {
                old->removeEventFilter(this);
            }
        }
        m_widgets.clear();
        for (QWidget *widget : widgets) {
            if (widget) {
                widget->installEventFilter(this);
                m_widgets.append(widget);
            }
        }
        m_function = std::move(function);
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((event->type() == QEvent::Move || event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::DynamicPropertyChange)
            && !m_queued && m_function) {
            m_queued = true;
            QTimer::singleShot(0, this, [this] {
                m_queued = false;
                if (m_function) {
                    m_function();
                }
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QList<QPointer<QWidget>> m_widgets;
    std::function<void()> m_function;
    bool m_queued = false;
};

static QIcon ngSidebarIcon(bool shown, const QColor &ink, bool right); // (defined further down)

// dolphin-ng: no window state saved yet when this process started (a first start): the sidebar gets a comfortable width
static bool ngNoSavedState = false;

DolphinMainWindow::DolphinMainWindow()
    : KXmlGuiWindow(nullptr)
    , m_newFileMenu(nullptr)
    , m_tabWidget(nullptr)
    , m_activeViewContainer(nullptr)
    , m_actionHandler(nullptr)
    , m_remoteEncoding(nullptr)
    , m_settingsDialog()
    , m_bookmarkHandler(nullptr)
    , m_disabledActionNotifier(nullptr)
    , m_lastHandleUrlOpenJob(nullptr)
    , m_terminalPanel(nullptr)
    , m_placesPanel(nullptr)
    , m_tearDownFromPlacesRequested(false)
    , m_backAction(nullptr)
    , m_forwardAction(nullptr)
    , m_splitViewAction(nullptr)
    , m_splitViewMenuAction(nullptr)
    , m_diskSpaceUsageMenu(nullptr)
    , m_sessionSaveTimer(nullptr)
    , m_sessionSaveWatcher(nullptr)
    , m_sessionSaveScheduled(false)
{
    Q_INIT_RESOURCE(dolphin);

    new MainWindowAdaptor(this);

#ifndef Q_OS_WIN
    setWindowFlags(Qt::WindowContextHelpButtonHint);
#endif
    setComponentName(QStringLiteral("dolphin"), QGuiApplication::applicationDisplayName());
    setObjectName(QStringLiteral("Dolphin#"));

    // dolphin-ng: the "live blur" sidebar is a hole in the window showing the compositor's blur, so the window
    // needs an alpha channel. Kvantum used to provide it; with an opaque widget style (Breeze) we do it here,
    // and paintEvent() fills the rest of the window with the window colour. Always, in every look of the sidebar:
    // switching it on an open window left Wayland surfaces without an alpha channel that the compositor took as
    // opaque (it stopped repainting behind them: trails, flicker, a grey or stale sidebar). Changing the look now
    // changes only what is painted and where the blur is asked for.
    setAttribute(Qt::WA_TranslucentBackground);
    if (ngSetting("TABS").isEmpty() || ngSetting("TABS").toLower() == QLatin1String("titlebar")) {
        // dolphin-ng: the tabs in the title bar: no frame from the window manager (see the row of tabs below); set
        // before the widget style first looks at the window, which then gives it a shadow
        setWindowFlag(Qt::FramelessWindowHint, true);
        setProperty("_KDE_NET_WM_FORCE_SHADOW", true);
    }

    // dolphin-ng: a state group of its own. Stock Dolphin keeps its window state (the size of the window and of the
    // sidebar among it) under "State" in the same dolphinstaterc: whoever closed last won, and with both in use the
    // sidebar came back at stock Dolphin's width every time. Started from what is there the first time
    {
        KConfigGroup own(KSharedConfig::openStateConfig(), QStringLiteral("NG State"));
        const KConfigGroup shared(KSharedConfig::openStateConfig(), QStringLiteral("State"));
        if (!own.exists() && shared.exists()) {
            shared.copyTo(&own);
            own.sync();
        }
        // read now: the window saves its state on its own while it is being built, before it first shows
        ngNoSavedState = !own.hasKey("State");
    }
    setStateConfigGroup(QStringLiteral("NG State"));
    // dolphin-ng: does the compositor blur behind windows? Known only once the application runs (asked now, KWin says
    // no). Without blur the live glass of the sidebar would show the desktop sharp: the application is told (Breeze NG
    // does the same with its own windows), and every window paints again
    QTimer::singleShot(1500, this, [] {
        if (qApp->property("_breeze_ng_bare_glass").toBool() || KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind)) {
            return;
        }
        qApp->setProperty("_breeze_ng_bare_glass", true);
        const auto windows = QApplication::topLevelWidgets();
        for (QWidget *window : windows) {
            if (auto *main = qobject_cast<DolphinMainWindow *>(window)) {
                main->ngUpdateBlurRegion();
                main->ngApplyTextHalo();
            }
            window->update();
            const auto children = window->findChildren<QWidget *>();
            for (QWidget *child : children) {
                child->update();
            }
        }
    });

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    new KColorSchemeManager(this); // Sets a sensible color scheme which fixes unreadable icons and text on Windows.
#endif

    connect(&DolphinNewFileMenuObserver::instance(), &DolphinNewFileMenuObserver::errorMessage, this, &DolphinMainWindow::showErrorMessage);

    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    undoManager->setUiInterface(new UndoUiInterface());

    connect(undoManager, &KIO::FileUndoManager::undoAvailable, this, &DolphinMainWindow::slotUndoAvailable);
    connect(undoManager, &KIO::FileUndoManager::undoTextChanged, this, &DolphinMainWindow::slotUndoTextChanged);
    connect(undoManager, &KIO::FileUndoManager::redoAvailable, this, &DolphinMainWindow::slotRedoAvailable);
    connect(undoManager, &KIO::FileUndoManager::redoTextChanged, this, &DolphinMainWindow::slotRedoTextChanged);

    const bool firstRun = (GeneralSettings::version() < 200);
    if (firstRun) {
        GeneralSettings::setViewPropsTimestamp(QDateTime::currentDateTime());
    }

    setAcceptDrops(true);

    auto *navigatorsWidgetAction = new DolphinNavigatorsWidgetAction(this);
    actionCollection()->addAction(QStringLiteral("url_navigators"), navigatorsWidgetAction);
    m_tabWidget = new DolphinTabWidget(navigatorsWidgetAction, this);
    m_tabWidget->setObjectName("tabWidget");
    connect(m_tabWidget, &DolphinTabWidget::activeViewChanged, this, &DolphinMainWindow::activeViewChanged);
    connect(m_tabWidget, &DolphinTabWidget::tabCountChanged, this, &DolphinMainWindow::tabCountChanged);
    // dolphin-ng: a new tab or a split view is a new file view: give it the Aurora background too
    connect(m_tabWidget, &DolphinTabWidget::tabCountChanged, this, &DolphinMainWindow::ngApplyAuroraViews, Qt::QueuedConnection);
    connect(m_tabWidget, &DolphinTabWidget::activeViewChanged, this, &DolphinMainWindow::ngApplyAuroraViews, Qt::QueuedConnection);
    connect(m_tabWidget, &DolphinTabWidget::currentUrlChanged, this, &DolphinMainWindow::updateWindowTitle);
    {
        // dolphin-ng: the sort/filter bar sits above the tabs and follows the active view
        auto *central = new QWidget(this);
        auto *centralLayout = new QVBoxLayout(central);
        m_ngCentralLayout = centralLayout;
        centralLayout->setContentsMargins(0, 0, 0, 0);
        centralLayout->setSpacing(0);
        m_ngSortBar = ngCreateSortBar(central);
        // (its buttons stand on sheets of their own: no glass shadow from Breeze NG under their text and icons, which it
        // only blurred; Breeze NG reads this property)
        m_ngSortBar->setProperty("_breeze_ng_no_text_shadow", true);
        m_ngSortBar->setVisible(ngTwoLineList());
        connect(NgSettings::instance(), &NgSettings::changed, m_ngSortBar, [this] {
            m_ngSortBar->setVisible(ngTwoLineList());
            ngMoveToolbarButtons();
        });
        centralLayout->addWidget(m_ngSortBar);
        centralLayout->addWidget(m_tabWidget, 1);
        setCentralWidget(central);
    }
    // dolphin-ng: the handle between the panels and the view stays usable but draws nothing. Done with a proxy style
    // on this window alone (a style is not passed on to the children): a style sheet on the window wrapped the style
    // of every widget in it, and KCapacityBar (the free space in the status bar), which asks the style for its own
    // element, no longer found it and fell back to a dark smudge of its own
    auto applyNgWindowSettings = [this] {
        if (ngTwoLineList() != (dynamic_cast<NgSeparatorStyle *>(style()) != nullptr)) {
            if (ngTwoLineList()) {
                setStyle(new NgSeparatorStyle(QApplication::style()->name()));
            } else {
                setStyle(nullptr);
            }
        }
        ngUpdateAurora();
        setContentsMargins(0, 0, 0, m_ngAurora && !ngPanelsFlush() ? 6 : 0); // (PANELS_EDGE may have changed)
        for (DolphinDockWidget *dock : findChildren<DolphinDockWidget *>()) {
            dock->update();
        }
        m_ngBlurSet = false;
        ngUpdateBlurRegion(); // the blur follows the look of the sidebar (only the live one asks for it)
    };
    applyNgWindowSettings();
    connect(NgSettings::instance(), &NgSettings::changed, this, applyNgWindowSettings);
    QDBusConnection::sessionBus().connect(QString(), QStringLiteral("/BreezeStyle"), QStringLiteral("org.kde.Breeze.Style"), QStringLiteral("reparseConfiguration"), this, SLOT(ngStyleReparsed()));

    m_actionTextHelper = new SelectionMode::ActionTextHelper(this);
    setupActions();

    m_actionHandler = new DolphinViewActionHandler(actionCollection(), m_actionTextHelper, this);
    connect(m_actionHandler, &DolphinViewActionHandler::createDirectoryTriggered, this, &DolphinMainWindow::createDirectory);
    connect(m_actionHandler, &DolphinViewActionHandler::createFileTriggered, this, &DolphinMainWindow::createFile);
    connect(m_actionHandler, &DolphinViewActionHandler::selectionModeChangeTriggered, this, &DolphinMainWindow::slotSetSelectionMode);

    QAction *newDirAction = actionCollection()->action(QStringLiteral("create_dir"));
    Q_ASSERT(newDirAction);
    m_newFileMenu->setNewFolderShortcutAction(newDirAction);

    QAction *newFileAction = actionCollection()->action(QStringLiteral("create_file"));
    Q_ASSERT(newFileAction);
    m_newFileMenu->setNewFileShortcutAction(newFileAction);

    m_remoteEncoding = new DolphinRemoteEncoding(this, m_actionHandler);
    connect(this, &DolphinMainWindow::urlChanged, m_remoteEncoding, &DolphinRemoteEncoding::slotAboutToOpenUrl);

    m_disabledActionNotifier = new DisabledActionNotifier(this);
    connect(m_disabledActionNotifier, &DisabledActionNotifier::disabledActionTriggered, this, [this](const QAction *, const QString &reason) {
        m_activeViewContainer->showMessage(reason, KMessageWidget::Warning);
    });

    setupDockWidgets();
    // dolphin-ng: a wider, invisible hold on the edges between the side panels and the view (see NgDockGrip)
    {
        QList<NgDockGrip *> grips;
        QList<QWidget *> watched{centralWidget()};
        for (QDockWidget *dock : findChildren<QDockWidget *>(Qt::FindDirectChildrenOnly)) {
            grips.append(new NgDockGrip(this, dock));
            watched.append(dock);
        }
        new NgDockMemory(this, findChildren<QDockWidget *>(Qt::FindDirectChildrenOnly));
        auto *watch = new NgGeometryWatch(this);
        watch->watch(watched, [this, grips] {
            for (NgDockGrip *grip : grips) {
                if (ngTwoLineList()) {
                    grip->place(6);
                } else {
                    grip->hide();
                }
            }
        });
    }

#if KIO_VERSION >= QT_VERSION_CHECK(6, 24, 0)
    m_serviceMenuShortcutManager = new ServiceMenuShortcutManager(actionCollection(), this);
#endif
    setupFileItemActions();

    const bool usePhoneUi{KRuntimePlatform::runtimePlatform().contains(QLatin1String("phone"))};
    setupGUI(Save | Create | ToolBar, usePhoneUi ? QStringLiteral("dolphinuiforphones.rc") : QString() /* load the default dolphinui.rc file */);
    QTimer::singleShot(0, this, &DolphinMainWindow::ngMoveToolbarButtons); // dolphin-ng
    // (an empty value is the default, the title bar: the settings page wrote "TABS=" for it, which showed the tabs
    // above the location bar under the window manager's title bar)
    const QString tabsPlace = ngSetting("TABS").isEmpty() ? QStringLiteral("titlebar") : ngSetting("TABS").toLower();
    m_ngTitleTabs = tabsPlace == QLatin1String("titlebar");
    if (tabsPlace != QLatin1String("classic")) {
        // dolphin-ng: tabs first, then the address, then the rest, as in a browser: a row of its own above the tool
        // bar that holds the location bar. The bar in it mirrors the tab widget's own (DolphinTabWidget::ngCreateTopBar).
        m_ngTabsToolBar = new QToolBar(this);
        m_ngTabsToolBar->setObjectName(QStringLiteral("ngTabsToolBar"));
        m_ngTabsToolBar->setWindowTitle(ngTr("Tabs", "Schede"));
        m_ngTabsToolBar->setMovable(false);
        m_ngTabsToolBar->setFloatable(false);
        m_ngTabsToolBar->toggleViewAction()->setVisible(false);
        m_ngTabsToolBar->setContextMenuPolicy(Qt::PreventContextMenu);
        m_ngTabsToolBar->setContentsMargins(6, 2, 6, 0);
        QTabBar *topBar = m_tabWidget->ngCreateTopBar(m_ngTabsToolBar);
        if (m_ngTitleTabs) {
            // the row is the window's title bar: no frame from the window manager, the window's buttons on the side
            // KWin has them (the close button's side), the rest of the row to drag the window by
            const KConfigGroup decoration = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2"));
            // which window buttons, on which side and in which order: KWin's own layout (System Settings > Window
            // decorations > Titlebar Buttons). Of its letters Dolphin NG draws I (minimize), A (maximize) and X (close);
            // the others (menu, on all desktops, help...) belong to the decoration (issue #12: all three were drawn
            // whatever the layout said)
            QString leftLayout = decoration.readEntry("ButtonsOnLeft", QStringLiteral("MS"));
            QString rightLayout = decoration.readEntry("ButtonsOnRight", QStringLiteral("HIAX"));
            // TITLE_BUTTONS=left: as on macOS, close, minimize and maximize at the left end of the row, before the sidebar's
            // button; the Information panel's button stays at the right end
            const bool macButtons = ngSetting("TITLE_BUTTONS").toLower() == QLatin1String("left");
            if (macButtons) {
                leftLayout = QStringLiteral("XIA");
                rightLayout.clear();
            }
            // for tests: DOLPHIN_NG_TEST_BUTTONS="left|right" (KWin's letters) in place of KWin's own layout
            if (const QString test = qEnvironmentVariable("DOLPHIN_NG_TEST_BUTTONS"); test.contains(QLatin1Char('|'))) {
                leftLayout = test.section(QLatin1Char('|'), 0, 0);
                rightLayout = test.section(QLatin1Char('|'), 1, 1);
            }
            const bool buttonsLeft = leftLayout.contains(QLatin1Char('X'));
            auto makeGroup = [this, macButtons](const QString &layout, bool left) {
                auto *group = new QWidget(m_ngTabsToolBar);
                auto *groupLayout = new QHBoxLayout(group);
                // (on the row's middle line with the tabs: the row's own layout centres them; they had 5 px under them, to
                // line up with KWin's buttons of other windows when the tool bar placed the row)
                groupLayout->setContentsMargins(left ? (macButtons ? 8 : 0) : 4, 0, left ? 4 : 0, 0);
                groupLayout->setSpacing(2);
                for (const QChar letter : layout) {
                    NgWindowButton *button = nullptr;
                    if (letter == QLatin1Char('I')) {
                        button = new NgWindowButton(NgWindowButton::Minimize, group);
                        button->setToolTip(ngTr("Minimize", "Riduci a icona"));
                        connect(button, &QAbstractButton::clicked, this, &QWidget::showMinimized);
                    } else if (letter == QLatin1Char('A')) {
                        button = new NgWindowButton(NgWindowButton::Maximize, group);
                        button->setToolTip(ngTr("Maximize", "Massimizza"));
                        connect(button, &QAbstractButton::clicked, this, [this] {
                            isMaximized() ? showNormal() : showMaximized();
                        });
                    } else if (letter == QLatin1Char('X')) {
                        button = new NgWindowButton(NgWindowButton::Close, group);
                        button->setToolTip(ngTr("Close", "Chiudi"));
                        connect(button, &QAbstractButton::clicked, this, &QWidget::close);
                    }
                    if (button) {
                        groupLayout->addWidget(button);
                    }
                }
                return group;
            };
            QWidget *leftButtons = makeGroup(leftLayout, true);
            QWidget *rightButtons = makeGroup(rightLayout, false);
            QWidget *buttons = buttonsLeft ? leftButtons : rightButtons; // (the close button's side)
            // beside them, on the inner side: the Information panel shown and hidden (as F11), the sidebar button's icon
            // mirrored, its band on the right
            auto *panel = new QToolButton(buttons);
            panel->setAutoRaise(true);
            panel->setIconSize(QSize(18, 18));
            panel->setFixedSize(28, 26);
            panel->setToolTip(ngTr("Show or hide the Information panel (F11)", "Mostra o nascondi il pannello Informazioni (F11)"));
            QTimer::singleShot(0, this, [this, panel] {
                QAction *info = actionCollection()->action(QStringLiteral("show_information_panel"));
                auto *dock = findChild<QDockWidget *>(QStringLiteral("infoDock"));
                if (!info || !dock) {
                    panel->hide(); // (built without the panel)
                    return;
                }
                connect(panel, &QToolButton::clicked, info, &QAction::trigger);
                auto update = [this, panel, dock] {
                    panel->setIcon(ngSidebarIcon(dock->isVisible(), palette().color(QPalette::WindowText), true));
                };
                connect(dock, &QDockWidget::visibilityChanged, panel, update);
                update();
            });
            if (macButtons) {
                // (macOS style: the window's buttons at the left, this one alone at the right end)
                panel->setParent(rightButtons);
                static_cast<QHBoxLayout *>(rightButtons->layout())->addWidget(panel);
            } else if (auto *buttonsLayout = static_cast<QHBoxLayout *>(buttons->layout()); buttonsLeft) {
                buttonsLayout->addWidget(panel);
            } else {
                buttonsLayout->insertWidget(0, panel);
            }
            auto *drag = new QWidget(m_ngTabsToolBar);
            drag->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            drag->setMinimumWidth(60);
            // (a side with none of them takes no room)
            m_ngMacButtons = macButtons;
            // the whole row is one widget with a layout of its own, every part on its middle line. In the tool bar's own
            // layout the window's buttons, the sidebar's button, the tabs and the panel's button each found a height of
            // their own (out of line, against the window's top edge with the buttons at the left, the tab's pill cut in a
            // lower row)
            auto *macRow = new QWidget(m_ngTabsToolBar);
            macRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            macRow->setMinimumHeight(38); // (as tall as the row: its place in it is this layout's to say, not the tool bar's)
            auto *box = new QHBoxLayout(macRow);
            box->setContentsMargins(0, 0, 0, 6); // (as much room above the tabs, from the window's top edge, as under them to the panels: 11 px)
            box->setSpacing(0);
            box->addWidget(leftButtons, 0, Qt::AlignVCenter);
            box->addWidget(topBar, 0, Qt::AlignVCenter); // (the sidebar's button goes before it, see ngPlaceSidebarToggle)
            box->addWidget(drag, 1);
            box->addWidget(rightButtons, 0, Qt::AlignVCenter);
            leftButtons->setVisible(leftButtons->layout()->count() > 0); // (a side with none of them takes no room)
            rightButtons->setVisible(rightButtons->layout()->count() > 0);
            m_ngMacRowLayout = box;
            m_ngTabsToolBar->addWidget(macRow);
            // (the buttons where the decoration of every other window has them: measured against KWin's)
            // (no margin under the row, and the row no taller than a title bar: there was a wide empty band between
            // the tabs and the sidebar and location bar below them)
            // (no margins above and below: the tool bar placed the row 6 px down or not at all, depending on the side of the
            // window's buttons; the row's own layout places its parts)
            m_ngTabsToolBar->setContentsMargins(buttonsLeft ? 0 : 8, 0, buttonsLeft ? 4 : 0, 0);
            m_ngTabsToolBar->setFixedHeight(38);
            if (QLayout *rowLayout = m_ngTabsToolBar->layout()) {
                QMargins margins = rowLayout->contentsMargins(); // (the style's own margin of a tool bar pushed the buttons inwards)
                buttonsLeft ? margins.setLeft(0) : margins.setRight(0);
                margins.setTop(0);
                margins.setBottom(0);
                rowLayout->setContentsMargins(margins);
            }
            new NgFrameless(this, {m_ngTabsToolBar, drag, leftButtons, rightButtons, macRow});
            QTimer::singleShot(0, this, &DolphinMainWindow::ngStyleTitleRow);
        } else {
            m_ngTabsToolBar->addWidget(topBar);
        }
        if (m_ngTitleTabs || !m_ngCentralLayout) {
            insertToolBar(toolBar(), m_ngTabsToolBar);
        }
        if (m_ngCentralLayout) {
            // the tool bar with the location bar goes above the files only: the sidebar reaches up to its level,
            // right under the row of tabs
            m_ngCentralLayout->insertWidget(0, toolBar());
            if (!m_ngTitleTabs) {
                // with the window manager's title bar the row of tabs goes above the files too: the sidebar takes
                // the whole left side of the window, and back, forward and up sit between it and the location bar
                // (they were at the top left corner, above the sidebar)
                m_ngCentralLayout->insertWidget(0, m_ngTabsToolBar);
            }
            if (m_ngSortBar && m_ngSortBar->layout()) {
                // the sort bar's first button starts where the pill of back and forward does, right above it
                QMargins margins = m_ngSortBar->layout()->contentsMargins();
                margins.setLeft(4);
                m_ngSortBar->layout()->setContentsMargins(margins);
            }
        } else {
            insertToolBarBreak(toolBar());
        }
        connect(m_tabWidget, &DolphinTabWidget::tabCountChanged, this, &DolphinMainWindow::ngUpdateTabsRow);
        ngUpdateTabsRow();
    }
    stateChanged(QStringLiteral("new_file"));

    QClipboard *clipboard = QApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged, this, &DolphinMainWindow::updatePasteAction);

    QAction *toggleFilterBarAction = actionCollection()->action(QStringLiteral("toggle_filter"));
    toggleFilterBarAction->setChecked(GeneralSettings::filterBar());

    if (firstRun) {
        menuBar()->setVisible(false);

        if (usePhoneUi) {
            Q_ASSERT(qobject_cast<QDockWidget *>(m_placesPanel->parent()));
            m_placesPanel->parentWidget()->hide();
            auto settings = GeneralSettings::self();
            settings->setShowZoomSlider(false); // Zooming can be done with pinch gestures instead and we are short on horizontal space.
            settings->setRenameInline(false); // This works around inline renaming currently not working well with virtual keyboards.
            settings->save(); // Otherwise the RenameInline setting is not picked up for the first time Dolphin is used.
        } else if (auto *placesDock = qobject_cast<QDockWidget *>(m_placesPanel->parentWidget())) {
            // dolphin-ng: the glass sidebar took its narrow size hint on a first start (entries cut short, disk boxes
            // without their names): it starts about 18 em wide, never more than a third of the window
            QTimer::singleShot(0, this, [this, placesDock] {
                const int wanted = qMin(fontMetrics().averageCharWidth() * 36, width() / 3);
                if (placesDock->isVisible() && placesDock->width() < wanted) {
                    resizeDocks({placesDock}, {wanted}, Qt::Horizontal);
                }
            });
        }
    }

    QAction *showMenuBarAction = actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
    showMenuBarAction->setChecked(!menuBar()->isHidden()); //workaround for bug #171080
    QTimer::singleShot(0, this, [this, showMenuBarAction]() {
        menuBar()->setVisible(showMenuBarAction->isChecked());
    });

    auto hamburgerMenu = static_cast<KHamburgerMenu *>(actionCollection()->action(KStandardAction::name(KStandardAction::HamburgerMenu)));
    hamburgerMenu->setMenuBar(menuBar());
    hamburgerMenu->setShowMenuBarAction(showMenuBarAction);
    connect(hamburgerMenu, &KHamburgerMenu::aboutToShowMenu, this, &DolphinMainWindow::updateHamburgerMenu);
    hamburgerMenu->hideActionsOf(toolBar());
    if (GeneralSettings::version() < 201 && !toolBar()->actions().contains(hamburgerMenu)) {
        addHamburgerMenuToToolbar();
    }

    updateAllowedToolbarAreas();
    updateNavigatorsBackground();

    // enable middle-click on back/forward/up to open in a new tab
    auto *middleClickEventFilter = new MiddleClickActionEventFilter(this);
    connect(middleClickEventFilter, &MiddleClickActionEventFilter::actionMiddleClicked, this, &DolphinMainWindow::slotToolBarActionMiddleClicked);
    toolBar()->installEventFilter(middleClickEventFilter);

    setupWhatsThis();

    connect(KSycoca::self(), &KSycoca::databaseChanged, this, &DolphinMainWindow::updateOpenPreferredSearchToolAction);

    QTimer::singleShot(0, this, &DolphinMainWindow::updateOpenPreferredSearchToolAction);

    m_serviceMenuConfigWatcher = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kservicemenurc")));
    connect(m_serviceMenuConfigWatcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup & /*group*/, const QByteArrayList & /*names*/) {
        setupFileItemActions();
    });
    connect(GeneralSettings::self(), &GeneralSettings::splitViewChanged, this, &DolphinMainWindow::slotSplitViewChanged);
    connect(GeneralSettings::self(), &GeneralSettings::tabBarChanged, this, &DolphinMainWindow::slotTabBarChanged);
}

DolphinMainWindow::~DolphinMainWindow()
{
    // This fixes a crash on Wayland when closing the mainwindow while another dialog is open.
    disconnect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &DolphinMainWindow::updatePasteAction);

    // This fixes a crash in dolphinmainwindowtest where the connection below fires even though the KMainWindow destructor of this object is already running.
    Q_ASSERT(qobject_cast<DolphinDockWidget *>(m_placesPanel->parent()));
    disconnect(static_cast<DolphinDockWidget *>(m_placesPanel->parent()),
               &DolphinDockWidget::visibilityChanged,
               this,
               &DolphinMainWindow::slotPlacesPanelVisibilityChanged);
}

QVector<DolphinViewContainer *> DolphinMainWindow::viewContainers() const
{
    QVector<DolphinViewContainer *> viewContainers;

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        DolphinTabPage *tabPage = m_tabWidget->tabPageAt(i);

        viewContainers << tabPage->primaryViewContainer();
        if (tabPage->splitViewEnabled()) {
            viewContainers << tabPage->secondaryViewContainer();
        }
    }
    return viewContainers;
}

void DolphinMainWindow::openDirectories(const QList<QUrl> &dirs, bool splitView)
{
    m_tabWidget->openDirectories(dirs, splitView);
}

void DolphinMainWindow::openDirectories(const QStringList &dirs, bool splitView)
{
    openDirectories(QUrl::fromStringList(dirs), splitView);
}

void DolphinMainWindow::openFiles(const QList<QUrl> &files, bool splitView)
{
    m_tabWidget->openFiles(files, splitView);
}

bool DolphinMainWindow::isFoldersPanelEnabled() const
{
    return actionCollection()->action(QStringLiteral("show_folders_panel"))->isChecked();
}

bool DolphinMainWindow::isInformationPanelEnabled() const
{
#if HAVE_BALOO
    return actionCollection()->action(QStringLiteral("show_information_panel"))->isChecked();
#else
    return false;
#endif
}

bool DolphinMainWindow::isSplitViewEnabledInCurrentTab() const
{
    return m_tabWidget->currentTabPage()->splitViewEnabled();
}

void DolphinMainWindow::openFiles(const QStringList &files, bool splitView)
{
    openFiles(QUrl::fromStringList(files), splitView);
}

void DolphinMainWindow::activateWindow(const QString &activationToken)
{
    window()->setAttribute(Qt::WA_NativeWindow, true);

    if (KWindowSystem::isPlatformWayland()) {
        KWindowSystem::setCurrentXdgActivationToken(activationToken);
    } else if (KWindowSystem::isPlatformX11()) {
#if HAVE_X11
        KStartupInfo::setNewStartupId(window()->windowHandle(), activationToken.toUtf8());
#endif
    }

    KWindowSystem::activateWindow(window()->windowHandle());
}

bool DolphinMainWindow::isActiveWindow()
{
    return window()->isActiveWindow();
}

void DolphinMainWindow::pasteIntoFolder()
{
    m_activeViewContainer->view()->pasteIntoFolder();
}

void DolphinMainWindow::changeUrl(const QUrl &url)
{
    if (!KProtocolManager::supportsListing(url)) {
        // The URL navigator only checks for validity, not
        // if the URL can be listed. An error message is
        // shown due to DolphinViewContainer::restoreView().
        return;
    }

    m_activeViewContainer->setUrl(url);
    updateFileAndEditActions();
    updatePasteAction();
    updateViewActions();
    updateGoActions();
    m_diskSpaceUsageMenu->setUrl(url);

    // will signal used urls to activities manager, too
    m_recentFiles->addUrl(url, QString(), "inode/directory");

    Q_EMIT urlChanged(url);
}

void DolphinMainWindow::slotTerminalDirectoryChanged(const QUrl &url)
{
    if (m_tearDownFromPlacesRequested && url == QUrl::fromLocalFile(QDir::homePath())) {
        m_placesPanel->proceedWithTearDown();
        m_tearDownFromPlacesRequested = false;
    }

    m_activeViewContainer->setGrabFocusOnUrlChange(false);
    changeUrl(url);
    m_activeViewContainer->setGrabFocusOnUrlChange(true);
}

void DolphinMainWindow::slotEditableStateChanged(bool editable)
{
    KToggleAction *editableLocationAction = static_cast<KToggleAction *>(actionCollection()->action(QStringLiteral("editable_location")));
    editableLocationAction->setChecked(editable);
}

void DolphinMainWindow::slotSelectionChanged(const KFileItemList &selection)
{
    updateFileAndEditActions();

    if (m_fileItemActions) {
        if (selection.count() > 0) {
            m_fileItemActions->setItemListProperties(KFileItemListProperties(selection));
        } else {
            m_fileItemActions->setItemListProperties(KFileItemListProperties(KFileItemList() << m_activeViewContainer->rootItem()));
        }
    }

    const int selectedUrlsCount = m_tabWidget->currentTabPage()->selectedItemsCount();

    QAction *compareFilesAction = actionCollection()->action(QStringLiteral("compare_files"));
    if (selectedUrlsCount == 2) {
        compareFilesAction->setEnabled(isKompareInstalled());
    } else {
        compareFilesAction->setEnabled(false);
    }

    Q_EMIT selectionChanged(selection);
}

void DolphinMainWindow::updateHistory()
{
    const KUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigatorInternalWithHistory();
    const int index = urlNavigator->historyIndex();

    QAction *backAction = actionCollection()->action(KStandardAction::name(KStandardAction::Back));
    if (backAction) {
        backAction->setToolTip(i18nc("@info", "Go back"));
        backAction->setWhatsThis(i18nc("@info:whatsthis go back", "Return to the previously viewed folder."));
        backAction->setEnabled(index < urlNavigator->historySize() - 1);
    }

    QAction *forwardAction = actionCollection()->action(KStandardAction::name(KStandardAction::Forward));
    if (forwardAction) {
        forwardAction->setToolTip(i18nc("@info", "Go forward"));
        forwardAction->setWhatsThis(xi18nc("@info:whatsthis go forward", "This undoes a <interface>Go|Back</interface> action."));
        forwardAction->setEnabled(index > 0);
    }
}

void DolphinMainWindow::updateFilterBarAction(bool show)
{
    QAction *toggleFilterBarAction = actionCollection()->action(QStringLiteral("toggle_filter"));
    toggleFilterBarAction->setChecked(show);
}

void DolphinMainWindow::openNewMainWindow()
{
    Dolphin::openNewWindow({m_activeViewContainer->url()}, this);
}

void DolphinMainWindow::openNewActivatedTab()
{
    // keep browsers compatibility, new tab is always after last one
    auto openNewTabAfterLastTabConfigured = GeneralSettings::openNewTabAfterLastTab();
    GeneralSettings::setOpenNewTabAfterLastTab(true);
    m_tabWidget->openNewActivatedTab();
    GeneralSettings::setOpenNewTabAfterLastTab(openNewTabAfterLastTabConfigured);
}

void DolphinMainWindow::addToPlaces()
{
    QUrl url;
    QString name;

    // If nothing is selected, act on the current dir
    if (m_activeViewContainer->view()->selectedItems().isEmpty()) {
        url = m_activeViewContainer->url();
        name = m_activeViewContainer->placesText();
    } else {
        const auto dirToAdd = m_activeViewContainer->view()->selectedItems().first();
        url = dirToAdd.url();
        name = dirToAdd.name();
    }
    if (url.isValid()) {
        QString icon;
        if (isSearchUrl(url)) {
            icon = QStringLiteral("folder-saved-search-symbolic");
        } else {
            icon = KIO::iconNameForUrl(url);
        }
        DolphinPlacesModelSingleton::instance().placesModel()->addPlace(name, url, icon);
    }
}

DolphinTabPage *DolphinMainWindow::openNewTab(const QUrl &url)
{
    return m_tabWidget->openNewTab(url, QUrl());
}

void DolphinMainWindow::openNewTabAndActivate(const QUrl &url)
{
    m_tabWidget->openNewActivatedTab(url, QUrl());
}

void DolphinMainWindow::openNewWindow(const QUrl &url)
{
    Dolphin::openNewWindow({url}, this);
}

void DolphinMainWindow::slotSplitViewChanged()
{
    m_tabWidget->currentTabPage()->setSplitViewEnabled(GeneralSettings::splitView(), WithAnimation);
    updateSplitActions();
}

void DolphinMainWindow::ngUpdateTabsRow()
{
    if (m_ngTabsToolBar) {
        // as Dolphin does with its own bar: out of the way with one tab, unless the user wants it always
        // (as the title bar the row is always there)
        m_ngTabsToolBar->setVisible(m_ngTitleTabs || m_tabWidget->count() > 1 || GeneralSettings::alwaysShowTabBar());
    }
}

void DolphinMainWindow::slotTabBarChanged()
{
    if (m_tabWidget->ngHasTopBar()) {
        ngUpdateTabsRow(); // (the tab widget's own bar stays hidden)
        return;
    }
    m_tabWidget->setTabBarAutoHide(!GeneralSettings::alwaysShowTabBar());
    m_tabWidget->tabBar()->setTabsClosable(GeneralSettings::showCloseButtonOnTabs());
}

void DolphinMainWindow::openInNewTab()
{
    const KFileItemList &list = m_activeViewContainer->view()->selectedItems();
    bool tabCreated = false;

    for (const KFileItem &item : list) {
        const QUrl &url = DolphinView::openItemAsFolderUrl(item);
        if (!url.isEmpty()) {
            openNewTab(url);
            tabCreated = true;
        }
    }

    // if no new tab has been created from the selection
    // open the current directory in a new tab
    if (!tabCreated) {
        openNewTab(m_activeViewContainer->url());
    }
}

void DolphinMainWindow::openInNewWindow()
{
    QUrl newWindowUrl;

    const KFileItemList list = m_activeViewContainer->view()->selectedItems();
    if (list.isEmpty()) {
        newWindowUrl = m_activeViewContainer->url();
    } else if (list.count() == 1) {
        const KFileItem &item = list.first();
        newWindowUrl = DolphinView::openItemAsFolderUrl(item);
    }

    if (!newWindowUrl.isEmpty()) {
        Dolphin::openNewWindow({newWindowUrl}, this);
    }
}

void DolphinMainWindow::openInSplitView(const QUrl &url)
{
    QUrl newSplitViewUrl = url;

    if (newSplitViewUrl.isEmpty()) {
        const KFileItemList list = m_activeViewContainer->view()->selectedItems();
        if (list.count() == 1) {
            const KFileItem &item = list.first();
            newSplitViewUrl = DolphinView::openItemAsFolderUrl(item);
        }
    }

    if (newSplitViewUrl.isEmpty()) {
        return;
    }

    DolphinTabPage *tabPage = m_tabWidget->currentTabPage();
    if (tabPage->splitViewEnabled()) {
        tabPage->switchActiveView();
        tabPage->activeViewContainer()->setUrl(newSplitViewUrl);
    } else {
        tabPage->setSplitViewEnabled(true, WithAnimation, newSplitViewUrl);
        updateViewActions();
    }
}

void DolphinMainWindow::showTarget()
{
    const KFileItem link = m_activeViewContainer->view()->selectedItems().at(0);
    const QUrl destinationUrl = link.url().resolved(QUrl(link.linkDest()));

    auto job = KIO::stat(destinationUrl, KIO::StatJob::SourceSide, KIO::StatNoDetails);

    connect(job, &KJob::finished, this, [this, destinationUrl](KJob *job) {
        KIO::StatJob *statJob = static_cast<KIO::StatJob *>(job);

        if (statJob->error()) {
            m_activeViewContainer->showMessage(job->errorString(), KMessageWidget::Error);
        } else {
            KIO::highlightInFileManager({destinationUrl});
        }
    });
}

bool DolphinMainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        QTimer::singleShot(700, this, &DolphinMainWindow::ngUpdateAurora); // a global theme writes the decoration last
        QTimer::singleShot(750, this, &DolphinMainWindow::ngStyleTitleRow); // (after the style has said glass or not)
    }
    if (event->type() == QEvent::ApplicationPaletteChange) {
        // (once more when the change has settled: a colour scheme is written in more than one step)
        QTimer::singleShot(800, this, [this] {
            ngStyleReparsed();
        });
        // dolphin-ng: a new colour scheme. Widgets with a style sheet of their own (the location bar, the fields and
        // buttons NG styles) keep the palette they were polished with and showed the old scheme's text colours, white
        // on a light field: their sheets are applied again, against the new palette, once the change has gone round
        QTimer::singleShot(0, this, [this] {
            const auto all = findChildren<QWidget *>();
            for (QWidget *child : all) {
                if (!child->styleSheet().isEmpty()) {
                    const QString sheet = child->styleSheet();
                    child->setStyleSheet(QString());
                    child->setStyleSheet(sheet);
                }
            }
            ngStyleReparsed();
        });
    }
    if (m_ngTitleTabs && (event->type() == QEvent::Resize || event->type() == QEvent::WindowStateChange)) {
        // dolphin-ng: without a frame the window rounds its own corners (not when it fills the screen)
        // (the widget style cuts them, wherever it lays the window's sheet: property "_breeze_ng_radius"; with the
        // decoration's own setting for rounded corners off, they stay square)
        const bool rounded = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Common")).readEntry("RoundedCorners", true);
        const int radius = (isMaximized() || isFullScreen() || !rounded) ? 0 : 14;
        if (property("_breeze_ng_radius").toInt() != radius) {
            setProperty("_breeze_ng_radius", radius > 0 ? QVariant(radius) : QVariant());
            update();
        }
        if (m_ngCorners.isEmpty()) {
            for (Qt::Corner corner : {Qt::TopLeftCorner, Qt::TopRightCorner, Qt::BottomLeftCorner, Qt::BottomRightCorner}) {
                m_ngCorners.append(new NgCornerCut(this, corner));
            }
        }
        for (QWidget *corner : std::as_const(m_ngCorners)) {
            static_cast<NgCornerCut *>(corner)->place(radius);
        }
        m_ngBlurSet = false;
        QMetaObject::invokeMethod(this, "ngUpdateBlurRegion", Qt::QueuedConnection);
        if (m_ngTabsToolBar) {
            m_ngTabsToolBar->update(); // the maximize button changes its sign
        }
    }
    // dolphin-ng: a new native window (Qt makes one when the translucency changes) has no blur yet: ask again,
    // even for the same region
    if (event->type() == QEvent::PlatformSurface
        && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
        m_ngBlurSet = false;
        QMetaObject::invokeMethod(this, "ngUpdateBlurRegion", Qt::QueuedConnection);
    }
    if (event->type() == QEvent::ShortcutOverride) {
        const QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Space && m_activeViewContainer->view()->handleSpaceAsNormalKey()) {
            event->accept();
            return true;
        }
    }

    return KXmlGuiWindow::event(event);
}

void DolphinMainWindow::ngUpdateBlurRegion()
{
    if (!windowHandle()) {
        return;
    }
    // Only the cards of the panels are see-through. Asking for the blur behind the whole window made the
    // compositor blur again under the file view at every scrolled frame, where nothing of it shows.
    QRegion region;
    static const bool wholeWindow = qEnvironmentVariable("DOLPHIN_NG_BLUR_REGION") == QLatin1String("window"); // for tests
    const bool live = !DolphinDockWidget::ngGlassPlain() && !DolphinDockWidget::ngGlassImageMode();
    if (m_ngAurora) {
        region = rect(); // the whole window is a sheet of glass
    } else if (live && wholeWindow) {
        region = rect();
    } else if (live) {
        const auto docks = findChildren<DolphinDockWidget *>();
        for (const DolphinDockWidget *dock : docks) {
            if (dock->isVisible() && !dock->isFloating()) {
                // (the card, as the panel paints it: flush with the window's edges with PANELS_EDGE=flush)
                const QRect card = dock->ngCardRect().toAlignedRect() & dock->rect();
                region += card.translated(dock->mapTo(this, QPoint(0, 0)));
            }
        }
    }
    if (const int radius = property("_breeze_ng_radius").toInt(); m_ngTitleTabs && radius > 0 && !region.isEmpty()) {
        QPainterPath shape; // (no blur outside the rounded corners of a window without a frame)
        shape.addRoundedRect(QRectF(rect()), radius, radius);
        region &= QRegion(shape.toFillPolygon().toPolygon());
    }
    if (m_ngBlurSet && region == m_ngBlurRegion) {
        return;
    }
    m_ngBlurSet = true;
    m_ngBlurRegion = region;
    KWindowEffects::enableBlurBehind(windowHandle(), !region.isEmpty(), region);
}

void DolphinMainWindow::showEvent(QShowEvent *event)
{
    KXmlGuiWindow::showEvent(event);

    // dolphin-ng: with "Remember open tabs" (the default) the window is restored from the last session, and that path
    // leaves out the window's saved state: the sidebar came back at its first width whatever the user had dragged it
    // to. The state saved with the window's settings ("NG State") is laid again once, when the window first shows
    static bool ngStateApplied = false;
    if (!ngStateApplied) {
        ngStateApplied = true;
        const QByteArray state = QByteArray::fromBase64(KConfigGroup(KSharedConfig::openStateConfig(), QStringLiteral("NG State")).readEntry("State", QByteArray()));
        if (!ngNoSavedState && !state.isEmpty()) {
            QTimer::singleShot(0, this, [this, state] {
                restoreState(state);
            });
        } else {
            // nothing saved yet (a first start): the sidebar a comfortable width, about 17 em, never more than a third
            // of the window (it came up at its narrow size hint, the places cut short)
            QTimer::singleShot(0, this, [this] {
                if (auto *places = findChild<QDockWidget *>(QStringLiteral("placesDock")); places && places->isVisible()) {
                    const int wanted = qMin(fontMetrics().averageCharWidth() * 36, width() / 3);
                    if (places->width() < wanted) {
                        resizeDocks({places}, {wanted}, Qt::Horizontal);
                    }
                }
            });
        }
    }

    m_ngBlurSet = false;
    ngUpdateBlurRegion(); // dolphin-ng: ask the compositor to blur behind the glass

    // for tests: DOLPHIN_NG_DEMO_CRUMB=name draws that button of the location bar as under the pointer (no input sent)
    if (const QString crumb = qEnvironmentVariable("DOLPHIN_NG_DEMO_CRUMB"); !crumb.isEmpty()) {
        QTimer::singleShot(3000, this, [this, crumb] {
            const auto navigators = findChildren<KUrlNavigator *>();
            for (KUrlNavigator *navigator : navigators) {
                const auto buttons = navigator->findChildren<QAbstractButton *>();
                for (QAbstractButton *button : buttons) {
                    if (button->isVisible() && button->text() == crumb) {
                        button->setAttribute(Qt::WA_UnderMouse, true);
                        QEnterEvent enter(QPointF(4, 4), QPointF(4, 4), button->mapToGlobal(QPointF(4, 4)));
                        QCoreApplication::sendEvent(button, &enter);
                        button->update();
                    }
                }
            }
        });
    }
    // for tests: DOLPHIN_NG_TEST_TOOLBAR_SAVE applies the tool bar configuration again, twice, once the window is up, as
    // saving it from "Configure Toolbars…" does (issue #10: it crashed)
    static bool ngToolbarSaveDone = false;
    if (qEnvironmentVariableIsSet("DOLPHIN_NG_TEST_TOOLBAR_SAVE") && !ngToolbarSaveDone) {
        ngToolbarSaveDone = true;
        for (int delay : {2000, 3500}) {
            QTimer::singleShot(delay, this, [this] {
                saveNewToolbarConfig();
            });
        }
    }
    // for tests: DOLPHIN_NG_TEST_INFO shows the Information panel once the window is up (the panels' actions are not
    // reachable over D-Bus)
    if (qEnvironmentVariableIsSet("DOLPHIN_NG_TEST_INFO")) {
        QTimer::singleShot(1200, this, [this] {
            if (auto *dock = findChild<QDockWidget *>(QStringLiteral("infoDock"))) {
                dock->show();
                if (const int width = qEnvironmentVariableIntValue("DOLPHIN_NG_TEST_INFO"); width > 1) {
                    resizeDocks({dock}, {width}, Qt::Horizontal);
                }
            }
        });
    }
    // for tests: DOLPHIN_NG_TEST_HIDE_PLACES presses the sidebar button once the window is up (no input sent)
    if (qEnvironmentVariableIsSet("DOLPHIN_NG_TEST_HIDE_PLACES")) {
        QTimer::singleShot(1500, this, [this] {
            if (m_ngSidebarToggle) {
                m_ngSidebarToggle->trigger();
            }
        });
    }
    // for demos: DOLPHIN_NG_DEMO_MENU="Button text|Item text|/path/menu.png[|trigger]" opens the menu of the bar button whose text
    // starts with "Button text", lights "Item text" as if the pointer were on it, saves a picture of the menu, and
    // writes next to it (".txt") where the button and the menu are in the window. No input is sent
    static bool ngDemoMenuDone = false;
    if (const QString demo = qEnvironmentVariable("DOLPHIN_NG_DEMO_MENU"); !demo.isEmpty() && !ngDemoMenuDone) {
        ngDemoMenuDone = true;
        const QStringList parts = demo.split(QLatin1Char('|'));
        const int delay = qEnvironmentVariableIsSet("DOLPHIN_NG_DEMO_MENU_DELAY") ? qEnvironmentVariableIntValue("DOLPHIN_NG_DEMO_MENU_DELAY") : 5000;
        QTimer::singleShot(delay, this, [this, parts] {
            if (parts.size() < 3) {
                return;
            }
            QToolButton *button = nullptr;
            const auto buttons = findChildren<QToolButton *>();
            for (QToolButton *b : buttons) {
                if (b->isVisible() && b->menu() && KLocalizedString::removeAcceleratorMarker(b->text()).startsWith(parts[0])) {
                    button = b;
                    break;
                }
            }
            // a button whose menu someone else opens on its press (the hamburger menu): pressed from inside, as a click
            // would (click() sends pressed too)
            bool clicked = false;
            if (!button) {
                for (QToolButton *b : buttons) {
                    if (b->isVisible() && !parts[0].isEmpty() && KLocalizedString::removeAcceleratorMarker(b->text()).contains(parts[0])) {
                        button = b;
                        clicked = true;
                        break;
                    }
                }
            }
            if (!button) {
                QStringList seen;
                for (QToolButton *b : buttons) {
                    seen << QStringLiteral("%1 visible=%2 menu=%3").arg(b->text()).arg(b->isVisible()).arg(b->menu() != nullptr);
                }
                QFile log(parts[2] + QStringLiteral(".log"));
                if (log.open(QIODevice::WriteOnly)) {
                    log.write(seen.join(QLatin1Char('\n')).toUtf8());
                }
                return;
            }
            const QString item = parts[1], out = parts[2];
            const bool trigger = parts.size() > 3 && parts[3] == QLatin1String("trigger");
            if (clicked) {
                button->click();
            }
            QTimer::singleShot(700, this, [this, button, item, out, trigger] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu) {
                    menu = button->menu(); // the popup may not be "active" when the window is not focused
                }
                if (!menu || !menu->isVisible()) {
                    QFile log(out + QStringLiteral(".log"));
                    if (log.open(QIODevice::WriteOnly)) {
                        log.write("menu not visible");
                    }
                    return;
                }
                const auto actions = menu->actions();
                for (QAction *a : actions) {
                    if (!item.isEmpty() && KLocalizedString::removeAcceleratorMarker(a->text()).contains(item)) {
                        menu->setActiveAction(a);
                    }
                }
                QTimer::singleShot(300, this, [this, button, menu, out, trigger] {
                    menu->grab().save(out);
                    const QRect b(button->mapTo(this, QPoint(0, 0)), button->size());
                    // the button too, as it looks with its menu open (it joins the menu), with a little of the bar around
                    const QRect around = b.adjusted(-8, -8, 8, 8);
                    grab(around).save(out + QStringLiteral(".button.png"));
                    const QRect m(mapFromGlobal(menu->geometry().topLeft()), menu->size());
                    QFile f(out + QStringLiteral(".txt"));
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(QStringLiteral("button %1 %2 %3 %4\nmenu %5 %6 %7 %8\nbuttonshot %9 %10 %11 %12\n")
                                    .arg(b.x()).arg(b.y()).arg(b.width()).arg(b.height())
                                    .arg(m.x()).arg(m.y()).arg(m.width()).arg(m.height())
                                    .arg(around.x()).arg(around.y()).arg(around.width()).arg(around.height()).toUtf8());
                    }
                    QAction *chosen = trigger ? menu->activeAction() : nullptr;
                    // DOLPHIN_NG_DEMO_MENU_HOLD=ms: the menu stays open that long first (a screen capture of the real thing)
                    if (const int hold = qEnvironmentVariableIntValue("DOLPHIN_NG_DEMO_MENU_HOLD"); hold > 0 && !chosen) {
                        QTimer::singleShot(hold, menu, &QMenu::close);
                        return;
                    }
                    menu->close();
                    if (chosen) { // "|trigger": then choose the lit item, as a click would
                        QTimer::singleShot(0, chosen, &QAction::trigger);
                    }
                });
            });
            button->showMenu();
        });
    }
    // for demos: DOLPHIN_NG_DEMO_SELECT="name|name|..." selects those items of the folder once it is shown (no input sent)
    static bool ngDemoSelectDone = false;
    if (const QString demo = qEnvironmentVariable("DOLPHIN_NG_DEMO_SELECT"); !demo.isEmpty() && !ngDemoSelectDone) {
        ngDemoSelectDone = true;
        QTimer::singleShot(3000, this, [this, demo] {
            if (!m_activeViewContainer) {
                return;
            }
            QStringList names = demo.split(QLatin1Char('|'), Qt::SkipEmptyParts);
            for (QString &name : names) {
                name = QRegularExpression::escape(name);
            }
            m_activeViewContainer->view()->selectItems(QRegularExpression(QStringLiteral("^(%1)$").arg(names.join(QLatin1Char('|')))), true);
        });
    }
    // for tests: DOLPHIN_NG_TEST_TOGGLE="recent,filters" folds and unfolds those sidebar sections, one every 2 s, as a click on
    // their title does (no input sent)
    static bool ngTestToggleDone = false;
    if (const QString keys = qEnvironmentVariable("DOLPHIN_NG_TEST_TOGGLE"); !keys.isEmpty() && !ngTestToggleDone) {
        ngTestToggleDone = true;
        int delay = 3000;
        for (const QString &key : keys.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            for (int twice = 0; twice < 2; ++twice) {
                QTimer::singleShot(delay, this, [key] {
                    ngToggleSection(key);
                });
                delay += 2000;
            }
        }
    }
    // for tests: DOLPHIN_NG_TEST_DOCK_PROBE prints, once the window is up, the places panel's and the view's geometry and
    // the widget under each point of the gap between them (where the separator that resizes the panel should be)
    if (qEnvironmentVariableIsSet("DOLPHIN_NG_TEST_DOCK_PROBE")) {
        QTimer::singleShot(2500, this, [this] {
            auto *dock = findChild<QDockWidget *>(QStringLiteral("placesDock"));
            if (!dock || !centralWidget()) {
                return;
            }
            const QRect central = centralWidget()->geometry();
            qWarning() << "NGPROBE dock" << dock->geometry() << "central" << central << "window" << geometry();
            const int y = central.center().y();
            for (int x = dock->geometry().right() - 3; x <= central.left() + 3; ++x) {
                QWidget *child = childAt(x, y);
                qWarning() << "NGPROBE x" << x << (child ? child->metaObject()->className() : "none") << (child ? child->objectName() : QString());
            }
        });
    }
    // for tests: DOLPHIN_NG_TEST_DOCK_WIDTH narrows the places panel step by step down to that width, or widens it to
    // that width at once (no input sent; the window remembers the width as it does a dragged one)
    if (const int target = qEnvironmentVariableIntValue("DOLPHIN_NG_TEST_DOCK_WIDTH"); target > 0) {
        if (auto *dock = findChild<QDockWidget *>(QStringLiteral("placesDock")); dock && dock->width() < target) {
            QTimer::singleShot(500, this, [this, dock, target] {
                resizeDocks({dock}, {target}, Qt::Horizontal);
            });
        } else if (dock) {
            auto *timer = new QTimer(this);
            connect(timer, &QTimer::timeout, this, [this, dock, timer, target] {
                const int width = dock->width() - 10;
                resizeDocks({dock}, {qMax(target, width)}, Qt::Horizontal);
                if (width <= target) {
                    timer->stop();
                }
            });
            timer->start(150);
        }
    }

    if (!event->spontaneous() && m_activeViewContainer) {
        m_activeViewContainer->view()->setFocus();
    }
}

void DolphinMainWindow::paintEvent(QPaintEvent *event)
{
    if (testAttribute(Qt::WA_TranslucentBackground)) {
        QPainter painter(this);
        if (m_ngAurora) {
            // Breeze NG Aurora: the whole window is a see-through sheet with the style's lights behind it. The style
            // paints it (and lays it again under its controls) for a window that carries this property.
            painter.setClipRegion(event->region());
            QStyleOption option;
            option.initFrom(this);
            style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        } else if (!ngWidgetStyleIsNg()) {
            // another widget style (stock Breeze...): one grey gradient for the whole window, the panels on it
            painter.setClipRegion(event->region());
            ngPaintWindowGradient(&painter, QRectF(rect()), palette());
            if (const int radius = property("_breeze_ng_radius").toInt(); radius > 0) {
                QPainterPath outside; // without a frame the window rounds its own corners
                outside.addRect(QRectF(rect()));
                QPainterPath shape;
                shape.addRoundedRect(QRectF(rect()), radius, radius);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                painter.fillPath(outside.subtracted(shape), Qt::black);
            }
        } else {
            painter.fillRect(event->rect(), palette().color(QPalette::Window)); // the docks cut their glass out of this
            if (const int radius = property("_breeze_ng_radius").toInt(); radius > 0) {
                QPainterPath outside; // without a frame the window rounds its own corners
                outside.addRect(QRectF(rect()));
                QPainterPath shape;
                shape.addRoundedRect(QRectF(rect()), radius, radius);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                painter.fillPath(outside.subtracted(shape), Qt::black);
            }
        }
    }
    KXmlGuiWindow::paintEvent(event);
}

void DolphinMainWindow::ngStyleTitleRow()
{
    if (!m_ngTitleTabs || !m_ngTabsToolBar || !m_tabWidget->ngHasTopBar()) {
        return;
    }
    QTabBar *top = m_tabWidget->ngCreateTopBar(m_ngTabsToolBar); // (the bar made already)
    // The tabs take the sort pill's form in every look and with any widget style: one track holding them, the current
    // one in the accent with the accent's own text colour (a style's own tabs did not match the bar below, which
    // Dolphin NG draws itself: stock Breeze drew a line over the current tab). The row is the window's own ground.
    const QPalette app = QApplication::palette();
    const bool dark = ngIsDark(app);
    NgTabTrack *track = nullptr;
    for (QObject *child : top->children()) {
        if (auto *found = dynamic_cast<NgTabTrack *>(child)) {
            track = found;
        }
    }
    if (!track) {
        track = new NgTabTrack(top);
    }
    const QString inkRgb = dark ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
    QColor fill, edge, on, onText;
    QString hover;
    if (ngAurora()) {
        // Aurora: the veil of every surface at rest, no edge; what is on in Aurora's selection colour
        fill = ngAuroraVeil(dark, true);
        edge = Qt::transparent;
        on = ngAuroraSelection(app);
        onText = ngAuroraSelectionText(app);
        hover = QStringLiteral("rgba(255, 255, 255, %1)").arg(QString::number(qMin(1.0, fill.alphaF() + (dark ? 0.06 : 0.20)), 'f', 3));
    } else {
        fill = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
        edge = fill;
        fill.setAlphaF(0.05);
        edge.setAlphaF(0.09);
        on = app.color(QPalette::Active, QPalette::Highlight);
        onText = ngSelectionText(app);
        hover = QStringLiteral("rgba(%1, 0.08)").arg(inkRgb);
    }
    track->setColours(fill, edge);
    const QString onRgba = QStringLiteral("rgba(%1, %2, %3, %4)").arg(on.red()).arg(on.green()).arg(on.blue()).arg(QString::number(on.alphaF(), 'f', 3));
    const QString sheet = QStringLiteral("QTabBar { background: transparent; }"
                                         // (no margin above and below the tab: the track's veil showed round the current tab as a light
                                         // rim, white on Aurora's light glass)
                                         "QTabBar::tab { background: transparent; border: none; border-radius: 11px; margin: 0px 1px;"
                                         " padding: 2px 2px 2px 10px; min-height: 18px; color: %1; }"
                                         "QTabBar::tab:first { margin-left: 2px; } QTabBar::tab:last { margin-right: 2px; }"
                                         "QTabBar::tab:hover:!selected { background: %2; }"
                                         "QTabBar::tab:selected { background: %3; color: %4; }")
                              .arg(app.color(QPalette::WindowText).name(), hover, onRgba, onText.name());
    m_ngTabsToolBar->setAutoFillBackground(false);
    m_ngTabsToolBar->setPalette(app);
    if (top->styleSheet() != sheet) {
        top->setStyleSheet(sheet);
    }
    // the current tab's icon and close button in the same colour as its text (see DolphinTabWidget::ngSyncTopBar)
    top->setProperty("_ng_on_text", onText);
    top->setProperty("_ng_text", app.color(QPalette::WindowText));
    m_tabWidget->ngRefreshTopBar();
    top->update();
    m_ngTabsToolBar->update();
}

void DolphinMainWindow::ngStyleReparsed()
{
    ngLoadAuroraSettings();
    QTimer::singleShot(0, this, &DolphinMainWindow::ngStyleTitleRow);
    QTimer::singleShot(0, this, &DolphinMainWindow::ngMoveToolbarButtons); // (the pill of back, forward and up follows the veil)
    const auto all = findChildren<QWidget *>();
    for (QWidget *child : all) {
        if (child->objectName() == QLatin1String("ngSortBar")) {
            QEvent change(QEvent::PaletteChange); // the bar builds its sheet again, with the veils of now
            QApplication::sendEvent(child, &change);
        }
        child->update();
    }
    update();
}

void DolphinMainWindow::ngUpdateAurora()
{
    ngLoadAuroraSettings();
    // the look is the window decoration's theme (what a Breeze NG global theme sets), as the widget style reads it
    const QString theme = KSharedConfig::openConfig(QStringLiteral("kwinrc"), KConfig::NoGlobals)->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString());
    const bool aurora = theme.startsWith(QLatin1String("Breeze NG Aurora"), Qt::CaseInsensitive) && QApplication::style()->name().compare(QLatin1String("breezeng"), Qt::CaseInsensitive) == 0;
    if (aurora != m_ngAurora) {
        m_ngAurora = aurora;
        ngSetAurora(aurora);
        setContentsMargins(0, 0, 0, aurora && !ngPanelsFlush() ? 6 : 0); // a little air under the sidebar and the view, inside the glass
        const auto all = findChildren<QWidget *>();
        for (QWidget *child : all) {
            child->update(); // rims come and go everywhere
        }
        setProperty("_breeze_ng_glass", aurora ? QVariant(true) : QVariant());
        m_ngBlurSet = false;
        ngUpdateBlurRegion();
        update();
    }
    ngApplyAuroraViews();
}

void DolphinMainWindow::ngApplyTextHalo()
{
    // On glass the text of the sidebar stood on whatever lay behind the window: light text over a white page behind
    // could not be read (and dark text over a dark one), and an application cannot see what is behind its window. As
    // macOS does, the text gets a soft shadow of the opposite tone, a halo with a little drop: it reads over light and
    // dark alike while the glass stays clear. On the sidebar's contents, above its glass card (the dock paints the card
    // itself); not where there is no glass (the plain sidebar, a window made solid for want of blur), nor with Breeze
    // NG's "Shadow around text on glass" off
    // (the sidebar's glass: Aurora's sidebar opacity, else the card's own; "where the contrast may be low" reckons the
    // worst case on it, see ngLowContrastOnGlass)
    const KConfigGroup breeze = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Common"));
    const int sidebarOpacity = m_ngAurora ? (breeze.readEntry("AuroraSidebarOpacity", -1) >= 0 ? breeze.readEntry("AuroraSidebarOpacity", -1)
                                                                                                 : breeze.readEntry("AuroraBodyOpacity", 60))
                                          : 100 - qBound(0, ngSettingInt("GLASS", 55), 100); // (GLASS: how much shows through)
    const bool glass = (m_ngAurora || !DolphinDockWidget::ngGlassPlain()) && !DolphinDockWidget::ngBareGlass()
        && (ngTextShadowMode() == 2 || (ngTextShadowMode() == 1 && ngLowContrastOnGlass(sidebarOpacity / 100.0)));
    const bool dark = ngIsDark(QApplication::palette());
    // (painted with the text itself, by the sidebar's parts: an effect over the whole sidebar redrew and blurred all of
    // it at every step of scrolling, and at a fractional scale shifted what it redrew in part by a pixel)
    // (this runs whenever the location bar lays itself out: the sidebar is painted again only when the choice changes)
    if (qApp->property("_ng_sidebar_halo").toBool() != glass) {
        qApp->setProperty("_ng_sidebar_halo", glass);
        const auto docks = findChildren<DolphinDockWidget *>();
        for (DolphinDockWidget *dock : docks) {
            if (QWidget *content = dock->widget()) {
                content->update();
                const auto children = content->findChildren<QWidget *>();
                for (QWidget *child : children) {
                    child->update();
                    if (child->inherits("NgInfoView")) {
                        QEvent restyle(QEvent::StyleChange); // (the Information panel shades its labels again)
                        QApplication::sendEvent(child, &restyle);
                    }
                }
            }
            dock->update();
        }
    }

    // the location bar's buttons and the search field write their text themselves, not through the widget style:
    // the same shadow on each of them, where the whole window is glass (Aurora). The location bar makes its buttons
    // anew at every folder: watched, and given the shadow again
    const bool bar = ngTextHalo(); // (Aurora, with the window's opacity)
    auto shade = [bar, dark](QWidget *widget, int blur) {
        auto *effect = qobject_cast<QGraphicsDropShadowEffect *>(widget->graphicsEffect());
        if (!bar) {
            if (effect) {
                widget->setGraphicsEffect(nullptr);
            }
            return;
        }
        if (!effect) {
            effect = new QGraphicsDropShadowEffect(widget);
            widget->setGraphicsEffect(effect);
        }
        effect->setBlurRadius(blur);
        effect->setOffset(0, 1);
        effect->setColor(dark ? QColor(0, 0, 0, 255) : QColor(255, 255, 255, 255));
    };
    const auto navigators = findChildren<KUrlNavigator *>();
    for (KUrlNavigator *navigator : navigators) {
        if (!navigator->property("_ng_halo_watch").toBool()) {
            navigator->setProperty("_ng_halo_watch", true);
            new NgChildrenWatch(navigator, [this] {
                ngApplyTextHalo();
            });
        }
        const auto buttons = navigator->findChildren<QAbstractButton *>();
        for (QAbstractButton *button : buttons) {
            // (KIO's private classes: "KDEPrivate::KUrlNavigatorButton" and its kin)
            if (QString::fromLatin1(button->metaObject()->className()).contains(QLatin1String("KUrlNavigator"))) {
                shade(button, 5);
                if (!button->property("_ng_hover_filter").toBool()) {
                    button->setProperty("_ng_hover_filter", true);
                    static NgCrumbWatch *crumbWatch = new NgCrumbWatch(qApp);
                    button->installEventFilter(crumbWatch);
                }
                if (button->graphicsEffect() && button->underMouse()) {
                    button->graphicsEffect()->setEnabled(false);
                }
            }
        }
    }
    if (m_ngFilterAction) {
        if (QWidget *field = toolBar(QStringLiteral("mainToolBar"))->widgetForAction(m_ngFilterAction)) {
            shade(field, 4);
            // its placeholder ("Search in the folder") solid on glass: see-through, it vanished over a white window
            const auto edits = field->findChildren<QLineEdit *>() << qobject_cast<QLineEdit *>(field);
            for (QLineEdit *edit : edits) {
                if (edit) {
                    QPalette own = edit->palette();
                    const QColor stock = QApplication::palette().color(QPalette::Active, QPalette::PlaceholderText);
                    own.setColor(QPalette::PlaceholderText, ngHaloInk(stock, QApplication::palette()));
                    edit->setPalette(own);
                }
            }
        }
    }
    // with a widget style other than Breeze NG: the location bar and the filter field as the sort bar's soft fields
    if (!ngWidgetStyleIsNg()) {
        for (KUrlNavigator *navigator : findChildren<KUrlNavigator *>()) {
            if (navigator->style() != ngFieldStyle()) {
                navigator->setStyle(ngFieldStyle());
            }
        }
        KToolBar *mainBar = toolBar(QStringLiteral("mainToolBar"));
        if (QWidget *field = mainBar && m_ngFilterAction ? mainBar->widgetForAction(m_ngFilterAction) : nullptr; field && field->style() != ngFieldStyle()) {
            field->setStyle(ngFieldStyle());
        }
    }
}

void DolphinMainWindow::ngApplyAuroraViews()
{
    ngApplyTextHalo();
    // Aurora: the file views stand on the window's glass (no background of their own), as the Folders panel's list
    // does; in the other looks they keep their own sheet
    if (!m_tabWidget) {
        return;
    }
    const auto containers = m_tabWidget->findChildren<KItemListContainer *>();
    for (KItemListContainer *container : containers) {
        if (container->property("_ng_aurora_view").toBool() != m_ngAurora) {
            container->setProperty("_ng_aurora_view", m_ngAurora);
            container->setEnabledFrame(!m_ngAurora);
            container->update();
        }
    }
}

void DolphinMainWindow::closeEvent(QCloseEvent *event)
{
    // Find out if Dolphin is closed directly by the user or
    // by the session manager because the session is closed
    bool closedByUser = true;
    if (qApp->isSavingSession()) {
        closedByUser = false;
    }

    if (m_tabWidget->count() > 1 && GeneralSettings::confirmClosingMultipleTabs() && !GeneralSettings::rememberOpenedTabs() && closedByUser) {
        // Ask the user if he really wants to quit and close all tabs.
        // Open a confirmation dialog with 3 buttons:
        // QDialogButtonBox::Yes    -> Quit
        // QDialogButtonBox::No     -> Close only the current tab
        // QDialogButtonBox::Cancel -> do nothing
        QDialog *dialog = new QDialog(this, Qt::Dialog);
        dialog->setWindowTitle(i18nc("@title:window", "Confirmation"));
        dialog->setModal(true);
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No | QDialogButtonBox::Cancel);
        KGuiItem::assign(buttons->button(QDialogButtonBox::Yes),
                         KGuiItem(i18nc("@action:button 'Quit Dolphin' button", "&Quit %1", QGuiApplication::applicationDisplayName()),
                                  QIcon::fromTheme(QStringLiteral("application-exit"))));
        KGuiItem::assign(buttons->button(QDialogButtonBox::No), KGuiItem(i18n("C&lose Current Tab"), QIcon::fromTheme(QStringLiteral("tab-close"))));
        KGuiItem::assign(buttons->button(QDialogButtonBox::Cancel), KStandardGuiItem::cancel());
        buttons->button(QDialogButtonBox::Yes)->setDefault(true);

        bool doNotAskAgainCheckboxResult = false;

        const auto result = KMessageBox::createKMessageBox(dialog,
                                                           buttons,
                                                           QMessageBox::Warning,
                                                           i18n("You have multiple tabs open in this window, are you sure you want to quit?"),
                                                           QStringList(),
                                                           i18n("Do not ask again"),
                                                           &doNotAskAgainCheckboxResult,
                                                           KMessageBox::Notify);

        if (doNotAskAgainCheckboxResult) {
            GeneralSettings::setConfirmClosingMultipleTabs(false);
        }

        switch (result) {
        case QDialogButtonBox::Yes:
            // Quit
            break;
        case QDialogButtonBox::No:
            // Close only the current tab
            m_tabWidget->closeTab();
            Q_FALLTHROUGH();
        default:
            event->ignore();
            return;
        }
    }

    if (m_terminalPanel && m_terminalPanel->hasProgramRunning() && GeneralSettings::confirmClosingTerminalRunningProgram() && closedByUser) {
        // Ask if the user really wants to quit Dolphin with a program that is still running in the Terminal panel
        // Open a confirmation dialog with 3 buttons:
        // QDialogButtonBox::Yes    -> Quit
        // QDialogButtonBox::No     -> Show Terminal Panel
        // QDialogButtonBox::Cancel -> do nothing
        QDialog *dialog = new QDialog(this, Qt::Dialog);
        dialog->setWindowTitle(i18nc("@title:window", "Confirmation"));
        dialog->setModal(true);
        auto standardButtons = QDialogButtonBox::Yes | QDialogButtonBox::Cancel;
        if (!m_terminalPanel->isVisible()) {
            standardButtons |= QDialogButtonBox::No;
        }
        QDialogButtonBox *buttons = new QDialogButtonBox(standardButtons);
        KGuiItem::assign(buttons->button(QDialogButtonBox::Yes), KStandardGuiItem::quit());
        if (!m_terminalPanel->isVisible()) {
            KGuiItem::assign(buttons->button(QDialogButtonBox::No), KGuiItem(i18n("Show &Terminal Panel"), QIcon::fromTheme(QStringLiteral("dialog-scripts"))));
        }
        KGuiItem::assign(buttons->button(QDialogButtonBox::Cancel), KStandardGuiItem::cancel());

        bool doNotAskAgainCheckboxResult = false;

        const auto result = KMessageBox::createKMessageBox(
            dialog,
            buttons,
            QMessageBox::Warning,
            i18n("The program '%1' is still running in the Terminal panel. Are you sure you want to quit?", m_terminalPanel->runningProgramName()),
            QStringList(),
            i18n("Do not ask again"),
            &doNotAskAgainCheckboxResult,
            KMessageBox::Notify | KMessageBox::Dangerous);

        if (doNotAskAgainCheckboxResult) {
            GeneralSettings::setConfirmClosingTerminalRunningProgram(false);
        }

        switch (result) {
        case QDialogButtonBox::Yes:
            // Quit
            break;
        case QDialogButtonBox::No:
            actionCollection()->action("show_terminal_panel")->trigger();
            // Do not quit, ignore quit event
            Q_FALLTHROUGH();
        default:
            event->ignore();
            return;
        }
    }

    if (m_sessionSaveTimer && (m_sessionSaveTimer->isActive() || m_sessionSaveWatcher->isRunning())) {
        const bool sessionSaveTimerActive = m_sessionSaveTimer->isActive();

        m_sessionSaveTimer->stop();
        m_sessionSaveWatcher->disconnect();
        m_sessionSaveWatcher->waitForFinished();

        if (sessionSaveTimerActive || m_sessionSaveScheduled) {
            slotSaveSession();
        }
    }

    GeneralSettings::setVersion(CurrentDolphinVersion);
    GeneralSettings::self()->save();

    KXmlGuiWindow::closeEvent(event);
}

void DolphinMainWindow::slotSaveSession()
{
    m_sessionSaveScheduled = false;

    if (m_sessionSaveWatcher->isRunning()) {
        // The previous session is still being saved - schedule another save.
        m_sessionSaveWatcher->disconnect();
        connect(m_sessionSaveWatcher, &QFutureWatcher<void>::finished, this, &DolphinMainWindow::slotSaveSession, Qt::SingleShotConnection);
        m_sessionSaveScheduled = true;
    } else if (!m_sessionSaveTimer->isActive()) {
        // No point in saving the session if the timer is running (since it will save the session again when it times out).
        KConfigGui::setSessionConfig(QStringLiteral("dolphin"), QStringLiteral("dolphin"));
        KConfig *config = KConfigGui::sessionConfig();
        saveGlobalProperties(config);
        savePropertiesInternal(config, 1);
        KConfigGroup group = config->group(QStringLiteral("Number"));
        group.writeEntry("NumberOfWindows", 1); // Makes session restore aware that there is a window to restore.

        // Copy the config in the main thread so sync() can safely run in the worker.
        QSharedPointer<KConfig> configCopy(config->copyTo(config->name()));
        auto future = QtConcurrent::run([configCopy]() {
            configCopy->sync();
        });
        m_sessionSaveWatcher->setFuture(future);
    }
}

void DolphinMainWindow::slotClickViewBackground()
{
    auto navigators = static_cast<DolphinNavigatorsWidgetAction *>(actionCollection()->action(QStringLiteral("url_navigators")));
    KUrlNavigator *navigator =
        m_tabWidget->currentTabPage()->primaryViewActive() ? navigators->primaryUrlNavigator() : navigators->secondaryUrlNavigator();

    if (navigator->isUrlEditable() && !GeneralSettings::editableUrl()) {
        navigator->setUrlEditable(false);
    }
}

void DolphinMainWindow::setSessionAutoSaveEnabled(bool enable)
{
    if (enable) {
        if (!m_sessionSaveTimer) {
            m_sessionSaveTimer = new QTimer(this);
            m_sessionSaveWatcher = new QFutureWatcher<void>(this);
            m_sessionSaveTimer->setSingleShot(true);
            m_sessionSaveTimer->setInterval(22000);

            connect(m_sessionSaveTimer, &QTimer::timeout, this, &DolphinMainWindow::slotSaveSession);
        }

        connect(m_tabWidget, &DolphinTabWidget::urlChanged, m_sessionSaveTimer, qOverload<>(&QTimer::start), Qt::UniqueConnection);
        connect(m_tabWidget, &DolphinTabWidget::tabCountChanged, m_sessionSaveTimer, qOverload<>(&QTimer::start), Qt::UniqueConnection);
        connect(m_tabWidget, &DolphinTabWidget::activeViewChanged, m_sessionSaveTimer, qOverload<>(&QTimer::start), Qt::UniqueConnection);
    } else if (m_sessionSaveTimer) {
        m_sessionSaveTimer->stop();
        m_sessionSaveWatcher->disconnect();
        m_sessionSaveScheduled = false;

        m_sessionSaveWatcher->waitForFinished();

        m_sessionSaveTimer->deleteLater();
        m_sessionSaveWatcher->deleteLater();
        m_sessionSaveTimer = nullptr;
        m_sessionSaveWatcher = nullptr;
    }
}

void DolphinMainWindow::saveProperties(KConfigGroup &group)
{
    m_tabWidget->saveProperties(group);
}

void DolphinMainWindow::readProperties(const KConfigGroup &group)
{
    m_tabWidget->readProperties(group);
}

void DolphinMainWindow::updateNewMenu()
{
    m_newFileMenu->checkUpToDate();
    m_newFileMenu->setWorkingDirectory(activeViewContainer()->url());
}

void DolphinMainWindow::createDirectory(const QUrl &parent)
{
    // When creating directory, namejob is being run. In network folders,
    // this job can take long time, so instead of starting multiple namejobs,
    // just check if we are already running one. This prevents opening multiple
    // dialogs. BUG:481401
    if (!m_newFileMenu->isCreateDirectoryRunning()) {
        m_newFileMenu->setWorkingDirectory(parent);
        m_newFileMenu->createDirectory();
    }
}

void DolphinMainWindow::createFile()
{
    // Use the same logic as in createDirectory()
    if (!m_newFileMenu->isCreateFileRunning()) {
        m_newFileMenu->setWorkingDirectory(activeViewContainer()->url());
        m_newFileMenu->createFile();
    }
}

void DolphinMainWindow::quit()
{
    close();
}

void DolphinMainWindow::showErrorMessage(const QString &message)
{
    m_activeViewContainer->showMessage(message, KMessageWidget::Error);
}

void DolphinMainWindow::slotUndoAvailable(bool available)
{
    QAction *undoAction = actionCollection()->action(KStandardAction::name(KStandardAction::Undo));
    if (undoAction) {
        undoAction->setEnabled(available);
    }
}

void DolphinMainWindow::slotUndoTextChanged(const QString &text)
{
    QAction *undoAction = actionCollection()->action(KStandardAction::name(KStandardAction::Undo));
    if (undoAction) {
        undoAction->setText(text);
    }
}

void DolphinMainWindow::undo()
{
    KIO::FileUndoManager::self()->uiInterface()->setParentWidget(this);
    KIO::FileUndoManager::self()->undo();
}

void DolphinMainWindow::slotRedoAvailable(bool available)
{
    QAction *redoAction = actionCollection()->action(KStandardAction::name(KStandardAction::Redo));
    if (redoAction) {
        redoAction->setEnabled(available);
    }
}

void DolphinMainWindow::slotRedoTextChanged(const QString &text)
{
    QAction *redoAction = actionCollection()->action(KStandardAction::name(KStandardAction::Redo));
    if (redoAction) {
        redoAction->setText(text);
    }
}

void DolphinMainWindow::redo()
{
    KIO::FileUndoManager::self()->uiInterface()->setParentWidget(this);
    KIO::FileUndoManager::self()->redo();
}

void DolphinMainWindow::cut()
{
    if (m_activeViewContainer->view()->selectedItems().isEmpty()) {
        m_activeViewContainer->setSelectionModeEnabled(true, actionCollection(), SelectionMode::BottomBar::Contents::CutContents);
    } else {
        m_activeViewContainer->view()->cutSelectedItemsToClipboard();
        m_activeViewContainer->setSelectionModeEnabled(false);
    }
}

void DolphinMainWindow::copy()
{
    if (m_activeViewContainer->view()->selectedItems().isEmpty()) {
        m_activeViewContainer->setSelectionModeEnabled(true, actionCollection(), SelectionMode::BottomBar::Contents::CopyContents);
    } else {
        m_activeViewContainer->view()->copySelectedItemsToClipboard();
        m_activeViewContainer->setSelectionModeEnabled(false);
    }
}

void DolphinMainWindow::paste()
{
    m_activeViewContainer->view()->paste();
}

void DolphinMainWindow::find()
{
    m_activeViewContainer->setSearchBarVisible(true);
    m_activeViewContainer->setFocusToSearchBar();
}

void DolphinMainWindow::updateSearchAction()
{
    QAction *toggleSearchAction = actionCollection()->action(QStringLiteral("toggle_search"));
    toggleSearchAction->setChecked(m_activeViewContainer->isSearchBarVisible());
}

void DolphinMainWindow::updatePasteAction()
{
    QAction *pasteAction = actionCollection()->action(KStandardAction::name(KStandardAction::Paste));
    QPair<bool, QString> pasteInfo = m_activeViewContainer->view()->pasteInfo();
    pasteAction->setEnabled(pasteInfo.first);
    m_disabledActionNotifier->setDisabledReason(pasteAction,
                                                m_activeViewContainer->rootItem().isWritable()
                                                    ? i18nc("@info", "Cannot paste: The clipboard is empty.")
                                                    : i18nc("@info", "Cannot paste: You do not have permission to write into this folder."));
    pasteAction->setText(pasteInfo.second);
}

void DolphinMainWindow::slotDirectoryLoadingCompleted()
{
    updatePasteAction();
}

void DolphinMainWindow::slotToolBarActionMiddleClicked(QAction *action)
{
    if (action == actionCollection()->action(KStandardAction::name(KStandardAction::Back))) {
        goBackInNewTab();
    } else if (action == actionCollection()->action(KStandardAction::name(KStandardAction::Forward))) {
        goForwardInNewTab();
    } else if (action == actionCollection()->action(QStringLiteral("go_up"))) {
        goUpInNewTab();
    } else if (action == actionCollection()->action(QStringLiteral("go_home"))) {
        goHomeInNewTab();
    }
}

QAction *DolphinMainWindow::urlNavigatorHistoryAction(const KUrlNavigator *urlNavigator, int historyIndex, QObject *parent)
{
    const QUrl url = urlNavigator->locationUrl(historyIndex);

    QString text;

    if (isSearchUrl(url)) {
        text = Search::DolphinQuery(url, QUrl{}).title();
    } else if (urlNavigator->showFullPath()) {
        text = url.toDisplayString(QUrl::PreferLocalFile);
    } else {
        const KFilePlacesModel *placesModel = DolphinPlacesModelSingleton::instance().placesModel();

        const QModelIndex closestIdx = placesModel->closestItem(url);
        if (closestIdx.isValid()) {
            const QUrl placeUrl = placesModel->url(closestIdx);

            text = placesModel->text(closestIdx);

            QString pathInsidePlace = url.path().mid(placeUrl.path().length());

            if (!pathInsidePlace.isEmpty() && !pathInsidePlace.startsWith(QLatin1Char('/'))) {
                pathInsidePlace.prepend(QLatin1Char('/'));
            }

            if (pathInsidePlace != QLatin1Char('/')) {
                text.append(pathInsidePlace);
            }
        }
    }

    QAction *action = new QAction(QIcon::fromTheme(KIO::iconNameForUrl(url)), text, parent);
    action->setData(historyIndex);
    return action;
}

void DolphinMainWindow::slotAboutToShowBackPopupMenu()
{
    const KUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigatorInternalWithHistory();
    int entries = 0;
    QMenu *menu = m_backAction->popupMenu();
    menu->clear();
    for (int i = urlNavigator->historyIndex() + 1; i < urlNavigator->historySize() && entries < MaxNumberOfNavigationentries; ++i, ++entries) {
        QAction *action = urlNavigatorHistoryAction(urlNavigator, i, menu);
        menu->addAction(action);
    }
}

void DolphinMainWindow::slotGoBack(QAction *action)
{
    int gotoIndex = action->data().value<int>();
    const KUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigatorInternalWithHistory();
    for (int i = gotoIndex - urlNavigator->historyIndex(); i > 0; --i) {
        goBack();
    }
}

void DolphinMainWindow::slotBackForwardActionMiddleClicked(QAction *action)
{
    if (action) {
        const KUrlNavigator *urlNavigator = activeViewContainer()->urlNavigatorInternalWithHistory();
        openNewTab(urlNavigator->locationUrl(action->data().value<int>()));
    }
}

void DolphinMainWindow::slotAboutToShowForwardPopupMenu()
{
    const KUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigatorInternalWithHistory();
    int entries = 0;
    QMenu *menu = m_forwardAction->popupMenu();
    menu->clear();
    for (int i = urlNavigator->historyIndex() - 1; i >= 0 && entries < MaxNumberOfNavigationentries; --i, ++entries) {
        QAction *action = urlNavigatorHistoryAction(urlNavigator, i, menu);
        menu->addAction(action);
    }
}

void DolphinMainWindow::slotGoForward(QAction *action)
{
    int gotoIndex = action->data().value<int>();
    const KUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigatorInternalWithHistory();
    for (int i = urlNavigator->historyIndex() - gotoIndex; i > 0; --i) {
        goForward();
    }
}

void DolphinMainWindow::slotSetSelectionMode(bool enabled, SelectionMode::BottomBar::Contents bottomBarContents)
{
    m_activeViewContainer->setSelectionModeEnabled(enabled, actionCollection(), bottomBarContents);
}

void DolphinMainWindow::selectAll()
{
    // if the URL navigator is editable and focused, select the whole
    // URL instead of all items of the view

    KUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigator();
    QLineEdit *lineEdit = urlNavigator->editor()->lineEdit();
    const bool selectUrl = urlNavigator->isUrlEditable() && lineEdit->hasFocus();
    if (selectUrl) {
        lineEdit->selectAll();
    } else {
        m_activeViewContainer->view()->selectAll();
    }
}

void DolphinMainWindow::invertSelection()
{
    m_activeViewContainer->view()->invertSelection();
}

void DolphinMainWindow::toggleSplitView()
{
    QUrl newSplitViewUrl;
    const KFileItemList list = m_activeViewContainer->view()->selectedItems();
    if (list.count() == 1) {
        const KFileItem &item = list.first();
        newSplitViewUrl = DolphinView::openItemAsFolderUrl(item);
    }

    DolphinTabPage *tabPage = m_tabWidget->currentTabPage();
    tabPage->setSplitViewEnabled(!tabPage->splitViewEnabled(), WithAnimation, newSplitViewUrl);
    m_tabWidget->updateTabName(m_tabWidget->indexOf(tabPage));
    updateViewActions();
}

void DolphinMainWindow::popoutSplitView()
{
    DolphinTabPage *tabPage = m_tabWidget->currentTabPage();
    if (!tabPage->splitViewEnabled())
        return;
    using Choice = GeneralSettings::EnumCloseSplitViewChoice;
    switch (GeneralSettings::closeSplitViewChoice()) {
    case Choice::ActiveView:
        openNewWindow(tabPage->activeViewContainer()->url());
        break;
    case Choice::InactiveView:
        openNewWindow(tabPage->inactiveViewContainer()->url());
        break;
    case Choice::RightView:
        openNewWindow(tabPage->secondaryViewContainer()->url());
        break;
    default:
        Q_UNREACHABLE();
    }

    tabPage->setSplitViewEnabled(false, WithAnimation);
    updateSplitActions();
}

void DolphinMainWindow::toggleSplitStash()
{
    DolphinTabPage *tabPage = m_tabWidget->currentTabPage();
    tabPage->setSplitViewEnabled(false, WithAnimation);
    tabPage->setSplitViewEnabled(true, WithAnimation, QUrl("stash:/"));
}

void DolphinMainWindow::copyToInactiveSplitView()
{
    if (m_activeViewContainer->view()->selectedItems().isEmpty()) {
        m_activeViewContainer->setSelectionModeEnabled(true, actionCollection(), SelectionMode::BottomBar::Contents::CopyToOtherViewContents);
    } else {
        m_tabWidget->copyToInactiveSplitView();
        m_activeViewContainer->setSelectionModeEnabled(false);
    }
}

void DolphinMainWindow::moveToInactiveSplitView()
{
    if (m_activeViewContainer->view()->selectedItems().isEmpty()) {
        m_activeViewContainer->setSelectionModeEnabled(true, actionCollection(), SelectionMode::BottomBar::Contents::MoveToOtherViewContents);
    } else {
        m_tabWidget->moveToInactiveSplitView();
        m_activeViewContainer->setSelectionModeEnabled(false);
    }
}

void DolphinMainWindow::reloadView()
{
    m_activeViewContainer->reload();
    Q_EMIT urlRefreshed(m_activeViewContainer->url());
}

void DolphinMainWindow::stopLoading()
{
    m_activeViewContainer->view()->stopLoading();
}

void DolphinMainWindow::enableStopAction()
{
    actionCollection()->action(QStringLiteral("stop"))->setEnabled(true);
}

void DolphinMainWindow::disableStopAction()
{
    actionCollection()->action(QStringLiteral("stop"))->setEnabled(false);
}

void DolphinMainWindow::toggleSelectionMode()
{
    const bool checked = !m_activeViewContainer->isSelectionModeEnabled();

    m_activeViewContainer->setSelectionModeEnabled(checked, actionCollection(), SelectionMode::BottomBar::Contents::GeneralContents);
    actionCollection()->action(QStringLiteral("toggle_selection_mode"))->setChecked(checked);
}

void DolphinMainWindow::showFilterBar()
{
    m_activeViewContainer->setFilterBarVisible(true);
}

void DolphinMainWindow::toggleFilterBar()
{
    const bool checked = !m_activeViewContainer->isFilterBarVisible();
    m_activeViewContainer->setFilterBarVisible(checked);

    QAction *toggleFilterBarAction = actionCollection()->action(QStringLiteral("toggle_filter"));
    toggleFilterBarAction->setChecked(checked);
}

void DolphinMainWindow::toggleEditLocation()
{
    QAction *action = actionCollection()->action(QStringLiteral("editable_location"));
    KUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigator();
    urlNavigator->setUrlEditable(action->isChecked());
}

void DolphinMainWindow::replaceLocation()
{
    KUrlNavigator *navigator = m_activeViewContainer->urlNavigator();
    QLineEdit *lineEdit = navigator->editor()->lineEdit();

    // If the text field currently has focus and everything is selected,
    // pressing the keyboard shortcut returns the whole thing to breadcrumb mode
    // and goes back to the view, just like how it was before this action was triggered the first time.
    if (navigator->isUrlEditable() && lineEdit->hasFocus() && lineEdit->selectedText() == lineEdit->text()) {
        navigator->setUrlEditable(false);
        m_activeViewContainer->view()->setFocus();
    } else {
        navigator->setUrlEditable(true);
        navigator->setFocus();
        lineEdit->selectAll();
    }
}

void DolphinMainWindow::togglePanelLockState()
{
    const bool newLockState = !GeneralSettings::lockPanels();
    const auto childrenObjects = children();
    for (QObject *child : childrenObjects) {
        DolphinDockWidget *dock = qobject_cast<DolphinDockWidget *>(child);
        if (dock) {
            dock->setLocked(newLockState);
        }
    }

    DolphinPlacesModelSingleton::instance().placesModel()->setPanelsLocked(newLockState);

    GeneralSettings::setLockPanels(newLockState);
}

void DolphinMainWindow::slotTerminalPanelVisibilityChanged(bool visible)
{
    if (!visible && m_activeViewContainer) {
        m_activeViewContainer->view()->setFocus();
    }
    // Putting focus to the Terminal is not handled here but in TerminalPanel::showEvent().
}

void DolphinMainWindow::slotPlacesPanelVisibilityChanged(bool visible)
{
    if (!visible && m_activeViewContainer) {
        m_activeViewContainer->view()->setFocus();
        return;
    }
    m_placesPanel->setFocus();
}

void DolphinMainWindow::goBack()
{
    DolphinUrlNavigator *urlNavigator = m_activeViewContainer->urlNavigatorInternalWithHistory();
    urlNavigator->goBack();

    if (urlNavigator->locationState().isEmpty()) {
        // An empty location state indicates a redirection URL,
        // which must be skipped too
        urlNavigator->goBack();
    }
}

void DolphinMainWindow::goForward()
{
    m_activeViewContainer->urlNavigatorInternalWithHistory()->goForward();
}

void DolphinMainWindow::goUp()
{
    m_activeViewContainer->urlNavigatorInternalWithHistory()->goUp();
}

void DolphinMainWindow::goHome()
{
    m_activeViewContainer->urlNavigatorInternalWithHistory()->goHome();
}

void DolphinMainWindow::goBackInNewTab()
{
    const KUrlNavigator *urlNavigator = activeViewContainer()->urlNavigatorInternalWithHistory();
    const int index = urlNavigator->historyIndex() + 1;
    openNewTab(urlNavigator->locationUrl(index));
}

void DolphinMainWindow::goForwardInNewTab()
{
    const KUrlNavigator *urlNavigator = activeViewContainer()->urlNavigatorInternalWithHistory();
    const int index = urlNavigator->historyIndex() - 1;
    openNewTab(urlNavigator->locationUrl(index));
}

void DolphinMainWindow::goUpInNewTab()
{
    const QUrl currentUrl = activeViewContainer()->urlNavigator()->locationUrl();
    openNewTab(KIO::upUrl(currentUrl));
}

void DolphinMainWindow::goHomeInNewTab()
{
    openNewTab(Dolphin::homeUrl());
}

void DolphinMainWindow::compareFiles()
{
    const KFileItemList items = m_tabWidget->currentTabPage()->selectedItems();
    if (items.count() != 2) {
        // The action is disabled in this case, but it could have been triggered
        // via D-Bus, see https://bugs.kde.org/show_bug.cgi?id=325517
        return;
    }

    QUrl urlA = items.at(0).url();
    QUrl urlB = items.at(1).url();

    const QString program = QStringLiteral("kompare");
    const QStringList args({QStringLiteral("-c"), urlA.toDisplayString(QUrl::PreferLocalFile), urlB.toDisplayString(QUrl::PreferLocalFile)});

    KIO::CommandLauncherJob *job = new KIO::CommandLauncherJob(program, args, this);
    job->setDesktopName(QStringLiteral("org.kde.kompare"));
    job->start();
}

void DolphinMainWindow::toggleShowMenuBar()
{
    QAction *showMenuBarAction = actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
    menuBar()->setVisible(showMenuBarAction->isChecked());
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("MainWindow"));
    group.writeEntry("MenuBar", showMenuBarAction->isChecked() ? "Enabled" : "Disabled");
    group.sync();
}

QPointer<QAction> DolphinMainWindow::preferredSearchTool()
{
    m_searchTools.clear();

    KService::Ptr kfind = KService::serviceByDesktopName(QStringLiteral("org.kde.kfind"));

    if (!kfind) {
        return nullptr;
    }

    auto *action = new QAction(QIcon::fromTheme(kfind->icon()), kfind->name(), this);

    connect(action, &QAction::triggered, this, [this, kfind] {
        auto *job = new KIO::ApplicationLauncherJob(kfind);
        job->setUrls({m_activeViewContainer->url()});
        job->start();
    });

    return action;
}

void DolphinMainWindow::updateOpenPreferredSearchToolAction()
{
    QAction *openPreferredSearchTool = actionCollection()->action(QStringLiteral("open_preferred_search_tool"));
    if (!openPreferredSearchTool) {
        return;
    }
    QPointer<QAction> tool = preferredSearchTool();
    if (tool) {
        openPreferredSearchTool->setVisible(true);
        openPreferredSearchTool->setText(i18nc("@action:inmenu Tools", "Open %1", tool->text()));
        // Only override with the app icon if it is the default, i.e. the user hasn't configured one manually
        // https://bugs.kde.org/show_bug.cgi?id=442815
        if (openPreferredSearchTool->icon().name() == QLatin1String("search")) {
            openPreferredSearchTool->setIcon(tool->icon());
        }
    } else {
        openPreferredSearchTool->setVisible(false);
        // still visible in Shortcuts configuration window
        openPreferredSearchTool->setText(i18nc("@action:inmenu Tools", "Open Preferred Search Tool"));
        openPreferredSearchTool->setIcon(QIcon::fromTheme(QStringLiteral("search")));
    }
}

void DolphinMainWindow::openPreferredSearchTool()
{
    QPointer<QAction> tool = preferredSearchTool();
    if (tool) {
        tool->trigger();
    }
}

void DolphinMainWindow::openTerminal()
{
    openTerminalJob(m_activeViewContainer->url());
}

void DolphinMainWindow::openTerminalHere()
{
    QList<QUrl> urls = {};

    const auto selectedItems = m_activeViewContainer->view()->selectedItems();
    for (const KFileItem &item : selectedItems) {
        QUrl url = item.targetUrl();
        if (item.isFile()) {
            url.setPath(QFileInfo(url.path()).absolutePath());
        }
        if (!urls.contains(url)) {
            urls << url;
        }
    }

    // No items are selected. Open a terminal window for the current location.
    if (urls.count() == 0) {
        openTerminal();
        return;
    }

    if (urls.count() > 5) {
        QString question = i18np("Are you sure you want to open 1 terminal window?", "Are you sure you want to open %1 terminal windows?", urls.count());
        const int answer = KMessageBox::warningContinueCancel(
            this,
            question,
            {},
            KGuiItem(i18ncp("@action:button", "Open %1 Terminal", "Open %1 Terminals", urls.count()), QStringLiteral("utilities-terminal")),
            KStandardGuiItem::cancel(),
            QStringLiteral("ConfirmOpenManyTerminals"));
        if (answer != KMessageBox::PrimaryAction && answer != KMessageBox::Continue) {
            return;
        }
    }

    for (const QUrl &url : std::as_const(urls)) {
        openTerminalJob(url);
    }
}

void DolphinMainWindow::openTerminalJob(const QUrl &url)
{
    if (url.isLocalFile()) {
        auto job = new KTerminalLauncherJob(QString());
        job->setWorkingDirectory(url.toLocalFile());
        job->start();
        return;
    }

    // Not a local file, with protocol Class ":local", try stat'ing
    if (KProtocolInfo::protocolClass(url.scheme()) == QLatin1String(":local")) {
        KIO::StatJob *job = KIO::mostLocalUrl(url);
        KJobWidgets::setWindow(job, this);
        connect(job, &KJob::result, this, [job]() {
            QUrl statUrl;
            if (!job->error()) {
                statUrl = job->mostLocalUrl();
            }

            auto job = new KTerminalLauncherJob(QString());
            job->setWorkingDirectory(statUrl.isLocalFile() ? statUrl.toLocalFile() : QDir::homePath());
            job->start();
        });

        return;
    }

    // Nothing worked, just use $HOME
    auto job = new KTerminalLauncherJob(QString());
    job->setWorkingDirectory(QDir::homePath());
    job->start();
}

void DolphinMainWindow::editSettings()
{
    if (!m_settingsDialog) {
        DolphinViewContainer *container = activeViewContainer();
        container->view()->writeSettings();

        const QUrl url = container->url();
        DolphinSettingsDialog *settingsDialog = new DolphinSettingsDialog(url, this, actionCollection());
        connect(settingsDialog, &DolphinSettingsDialog::settingsChanged, this, &DolphinMainWindow::refreshViews);
        connect(settingsDialog, &DolphinSettingsDialog::settingsChanged, &DolphinUrlNavigatorsController::slotReadSettings);
        settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
        settingsDialog->show();
        m_settingsDialog = settingsDialog;
    } else {
        m_settingsDialog.data()->raise();
    }
}

void DolphinMainWindow::handleUrl(const QUrl &url)
{
    delete m_lastHandleUrlOpenJob;
    m_lastHandleUrlOpenJob = nullptr;

    if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
        activeViewContainer()->setUrl(url);
    } else {
        m_lastHandleUrlOpenJob = new KIO::OpenUrlJob(url);
        m_lastHandleUrlOpenJob->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
        m_lastHandleUrlOpenJob->setShowOpenOrExecuteDialog(true);

        connect(m_lastHandleUrlOpenJob, &KIO::OpenUrlJob::mimeTypeFound, this, [this, url](const QString &mimetype) {
            if (mimetype == QLatin1String("inode/directory")) {
                // If it's a dir, we'll take it from here
                m_lastHandleUrlOpenJob->kill();
                m_lastHandleUrlOpenJob = nullptr;
                activeViewContainer()->setUrl(url);
            }
        });

        connect(m_lastHandleUrlOpenJob, &KIO::OpenUrlJob::result, this, [this]() {
            m_lastHandleUrlOpenJob = nullptr;
        });

        m_lastHandleUrlOpenJob->start();
    }
}

void DolphinMainWindow::slotWriteStateChanged(bool isFolderWritable)
{
    // trash:/ is writable but we don't want to create new items in it.
    // TODO: remove the trash check once https://phabricator.kde.org/T8234 is implemented
    newFileMenu()->setEnabled(isFolderWritable && m_activeViewContainer->url().scheme() != QLatin1String("trash"));
    // When the menu is disabled, actions in it are disabled later in the event loop, and we need to set the disabled reason after that.
    QTimer::singleShot(0, this, [this]() {
        m_disabledActionNotifier->setDisabledReason(actionCollection()->action(QStringLiteral("create_file")),
                                                    i18nc("@info", "Cannot create new file: You do not have permission to create items in this folder."));
        m_disabledActionNotifier->setDisabledReason(actionCollection()->action(QStringLiteral("create_dir")),
                                                    i18nc("@info", "Cannot create new folder: You do not have permission to create items in this folder."));
        m_disabledActionNotifier->setDisabledReason(actionCollection()->action(QStringLiteral("create_subdir")),
                                                    i18nc("@info", "Cannot create new subfolder: Select a single folder you can write to."));
    });
}

void DolphinMainWindow::openContextMenu(const QPoint &pos, const KFileItem &item, const KFileItemList &selectedItems, const QUrl &url)
{
    QPointer<DolphinContextMenu> contextMenu = new DolphinContextMenu(this, item, selectedItems, url, m_fileItemActions);
    contextMenu->exec(pos);
    delete contextMenu;
}

QMenu *DolphinMainWindow::createPopupMenu()
{
    QMenu *menu = KXmlGuiWindow::createPopupMenu();

    menu->addSeparator();
    menu->addAction(actionCollection()->action(QStringLiteral("lock_panels")));

    return menu;
}

void DolphinMainWindow::updateHamburgerMenu()
{
    KActionCollection *ac = actionCollection();
    auto hamburgerMenu = static_cast<KHamburgerMenu *>(ac->action(KStandardAction::name(KStandardAction::HamburgerMenu)));
    auto menu = hamburgerMenu->menu();
    if (!menu) {
        menu = new QMenu(this);
        hamburgerMenu->setMenu(menu);
        hamburgerMenu->hideActionsOf(ac->action(QStringLiteral("basic_actions"))->menu());
        hamburgerMenu->hideActionsOf(qobject_cast<KToolBarPopupAction *>(ac->action(QStringLiteral("zoom")))->popupMenu());
    } else {
        menu->clear();
    }
    const QList<QAction *> toolbarActions = toolBar()->actions();

    if (!toolBar()->isVisible()) {
        // If neither the menu bar nor the toolbar are visible, these actions should be available.
        menu->addAction(ac->action(KStandardAction::name(KStandardAction::ShowMenubar)));
        menu->addAction(toolBarMenuAction());
        menu->addSeparator();
    }

    // This group of actions (until the next separator) contains all the most basic actions
    // necessary to use Dolphin effectively.
    menu->addAction(ac->action(QStringLiteral("go_back")));
    menu->addAction(ac->action(QStringLiteral("go_forward")));

    menu->addMenu(m_newFileMenu->menu());
    if (!toolBar()->isVisible() || !toolbarActions.contains(ac->action(QStringLiteral("toggle_selection_mode_tool_bar")))) {
        menu->addAction(ac->action(QStringLiteral("toggle_selection_mode")));
    }
    menu->addAction(ac->action(QStringLiteral("basic_actions")));
    menu->addAction(ac->action(KStandardAction::name(KStandardAction::Undo)));
    menu->addAction(ac->action(KStandardAction::name(KStandardAction::Redo)));
    if (!toolBar()->isVisible()
        || (!toolbarActions.contains(ac->action(QStringLiteral("toggle_search")))
            && !toolbarActions.contains(ac->action(QStringLiteral("open_preferred_search_tool"))))) {
        menu->addAction(ac->action(KStandardAction::name(KStandardAction::Find)));
        // This way a search action will only be added if none of the three available
        // search actions is present on the toolbar.
    }
    if (!toolBar()->isVisible() || !toolbarActions.contains(ac->action(QStringLiteral("toggle_filter")))) {
        menu->addAction(ac->action(QStringLiteral("show_filter_bar")));
        // This way a filter action will only be added if none of the two available
        // filter actions is present on the toolbar.
    }
    menu->addSeparator();

    // The second group of actions (up until the next separator) contains actions for opening
    // additional views to interact with the file system.
    menu->addAction(ac->action(QStringLiteral("file_new")));
    menu->addAction(ac->action(QStringLiteral("new_tab")));
    if (ac->action(QStringLiteral("undo_close_tab"))->isEnabled()) {
        menu->addAction(ac->action(QStringLiteral("closed_tabs")));
    }
    menu->addAction(ac->action(QStringLiteral("open_terminal")));
    menu->addSeparator();

    // The third group contains actions to change what one sees in the view
    // and to change the more general UI.
    if (!toolBar()->isVisible()
        || ((!toolbarActions.contains(ac->action(QStringLiteral("icons"))) && !toolbarActions.contains(ac->action(QStringLiteral("compact")))
             && !toolbarActions.contains(ac->action(QStringLiteral("details"))) && !toolbarActions.contains(ac->action(QStringLiteral("view_mode"))))
            && !toolbarActions.contains(ac->action(QStringLiteral("view_settings"))))) {
        menu->addAction(ac->action(QStringLiteral("view_mode")));
    }
    if (!toolBar()->isVisible() || !toolbarActions.contains(ac->action(QStringLiteral("view_settings")))) {
        menu->addAction(ac->action(QStringLiteral("show_hidden_files")));
        menu->addAction(ac->action(QStringLiteral("sort")));
        menu->addAction(ac->action(QStringLiteral("additional_info")));
        if (!GeneralSettings::showStatusBar() || !GeneralSettings::showZoomSlider()) {
            menu->addAction(ac->action(QStringLiteral("zoom")));
        }
    }
    menu->addAction(ac->action(QStringLiteral("panels")));

    // The "Configure" menu is not added to the actionCollection() because there is hardly
    // a good reason for users to put it on their toolbar.
    auto configureMenu = menu->addMenu(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu menu for configure actions", "Configure"));
    configureMenu->addAction(actionCollection()->action(QStringLiteral("window_color_sheme")));
    configureMenu->addSeparator();
    configureMenu->addAction(ac->action(KStandardAction::name(KStandardAction::SwitchApplicationLanguage)));
    configureMenu->addAction(ac->action(KStandardAction::name(KStandardAction::KeyBindings)));
    configureMenu->addAction(ac->action(KStandardAction::name(KStandardAction::ConfigureToolbars)));
    configureMenu->addAction(ac->action(KStandardAction::name(KStandardAction::Preferences)));
    hamburgerMenu->hideActionsOf(configureMenu);
}

void DolphinMainWindow::slotPlaceActivated(const QUrl &url)
{
    DolphinViewContainer *view = activeViewContainer();

    if (view->url() == url) {
        view->clearFilterBar(); // Fixes bug 259382.

        // We can end up here if the user clicked a device in the Places Panel
        // which had been unmounted earlier, see https://bugs.kde.org/show_bug.cgi?id=161385.
        reloadView();

        m_activeViewContainer->view()->setFocus(); // We always want the focus on the view after activating a place.
    } else {
        view->disableUrlNavigatorSelectionRequests();
        changeUrl(url);
        view->enableUrlNavigatorSelectionRequests();
    }
}

void DolphinMainWindow::closedTabsCountChanged(unsigned int count)
{
    actionCollection()->action(QStringLiteral("undo_close_tab"))->setEnabled(count > 0);
}

void DolphinMainWindow::activeViewChanged(DolphinViewContainer *viewContainer)
{
    DolphinViewContainer *oldViewContainer = m_activeViewContainer;
    Q_ASSERT(viewContainer);

    m_activeViewContainer = viewContainer;
    ngSortBarFollow(m_ngSortBar, viewContainer->view()); // dolphin-ng

    if (oldViewContainer) {
        // Disconnect all signals between the old view container (container,
        // view and url navigator) and main window.
        oldViewContainer->disconnect(this);
        oldViewContainer->view()->disconnect(this);
        oldViewContainer->urlNavigatorInternalWithHistory()->disconnect(this);
        auto navigators = static_cast<DolphinNavigatorsWidgetAction *>(actionCollection()->action(QStringLiteral("url_navigators")));
        navigators->primaryUrlNavigator()->disconnect(this);
        if (auto secondaryUrlNavigator = navigators->secondaryUrlNavigator()) {
            secondaryUrlNavigator->disconnect(this);
        }
        oldViewContainer->disconnect(m_diskSpaceUsageMenu);

        // except the requestItemInfo so that on hover the information panel can still be updated
        connect(oldViewContainer->view(), &DolphinView::requestItemInfo, this, &DolphinMainWindow::requestItemInfo);

        // Disconnect other slots.
        disconnect(oldViewContainer,
                   &DolphinViewContainer::selectionModeChanged,
                   actionCollection()->action(QStringLiteral("toggle_selection_mode")),
                   &QAction::setChecked);

        disconnect(oldViewContainer->view(), &DolphinView::viewSettingsChanged, this, nullptr);
    }

    connectViewSignals(viewContainer);

    m_actionHandler->setCurrentView(viewContainer->view());

    updateHistory();
    updateFileAndEditActions();
    updatePasteAction();
    updateViewActions();
    updateGoActions();
    updateSearchAction();
    connect(m_diskSpaceUsageMenu,
            &DiskSpaceUsageMenu::showMessage,
            viewContainer,
            [viewContainer](const QString &message, KMessageWidget::MessageType messageType) {
                viewContainer->showMessage(message, messageType);
            });
    connect(m_diskSpaceUsageMenu, &DiskSpaceUsageMenu::showInstallationProgress, viewContainer, &DolphinViewContainer::showProgress);

    const QUrl url = viewContainer->url();
    Q_EMIT urlChanged(url);
    Q_EMIT selectionChanged(m_activeViewContainer->view()->selectedItems());
}

void DolphinMainWindow::tabCountChanged(int count)
{
    const bool enableTabActions = (count > 1);
    for (int i = 0; i < MaxActivateTabShortcuts; ++i) {
        actionCollection()->action(QStringLiteral("activate_tab_%1").arg(i))->setEnabled(enableTabActions);
    }
    actionCollection()->action(QStringLiteral("activate_last_tab"))->setEnabled(enableTabActions);
    actionCollection()->action(QStringLiteral("activate_next_tab"))->setEnabled(enableTabActions);
    actionCollection()->action(QStringLiteral("activate_prev_tab"))->setEnabled(enableTabActions);
}

void DolphinMainWindow::updateWindowTitle()
{
    const QString newTitle = m_activeViewContainer->captionWindowTitle();
    if (windowTitle() != newTitle) {
        setWindowTitle(newTitle);
    }
}

void DolphinMainWindow::slotStorageTearDownFromPlacesRequested(const QString &mountPath)
{
    connect(m_placesPanel, &PlacesPanel::storageTearDownSuccessful, this, [this, mountPath]() {
        setViewsToHomeIfMountPathOpen(mountPath);
    });

    if (m_terminalPanel && m_terminalPanel->currentWorkingDirectoryIsChildOf(mountPath)) {
        m_tearDownFromPlacesRequested = true;
        m_terminalPanel->goHome();
        // m_placesPanel->proceedWithTearDown() will be called in slotTerminalDirectoryChanged
    } else {
        m_placesPanel->proceedWithTearDown();
    }
}

void DolphinMainWindow::slotStorageTearDownExternallyRequested(const QString &mountPath)
{
    connect(m_placesPanel, &PlacesPanel::storageTearDownSuccessful, this, [this, mountPath]() {
        setViewsToHomeIfMountPathOpen(mountPath);
    });

    if (m_terminalPanel && m_terminalPanel->currentWorkingDirectoryIsChildOf(mountPath)) {
        m_tearDownFromPlacesRequested = false;
        m_terminalPanel->goHome();
    }
}

void DolphinMainWindow::slotKeyBindings()
{
#if KIO_VERSION >= QT_VERSION_CHECK(6, 24, 0)
    m_serviceMenuShortcutManager->cleanupStaleShortcuts(this);
#endif

    KShortcutsDialog dialog(KShortcutsEditor::AllActions, KShortcutsEditor::LetterShortcutsAllowed, this);
    dialog.addCollection(actionCollection());
    if (m_terminalPanel) {
        KActionCollection *konsolePartActionCollection = m_terminalPanel->actionCollection();
        if (konsolePartActionCollection) {
            dialog.addCollection(konsolePartActionCollection, QStringLiteral("KonsolePart"));
        }
    }
    dialog.configure();
}

void DolphinMainWindow::setViewsToHomeIfMountPathOpen(const QString &mountPath)
{
    const QVector<DolphinViewContainer *> theViewContainers = viewContainers();
    for (DolphinViewContainer *viewContainer : theViewContainers) {
        if (!viewContainer) {
            continue;
        }
        const auto viewPath = viewContainer->url().toLocalFile();
        if (viewPath.startsWith(mountPath + QLatin1String("/")) || viewPath == mountPath) {
            viewContainer->setUrl(QUrl::fromLocalFile(QDir::homePath()));
        }
    }
    disconnect(m_placesPanel, &PlacesPanel::storageTearDownSuccessful, nullptr, nullptr);
}

void DolphinMainWindow::setupActions()
{
    auto hamburgerMenuAction = KStandardAction::hamburgerMenu(nullptr, nullptr, actionCollection());

    // setup 'File' menu
    m_newFileMenu = new DolphinNewFileMenu(nullptr, nullptr, this);
    actionCollection()->addAction(QStringLiteral("new_menu"), m_newFileMenu);
    QMenu *menu = m_newFileMenu->menu();
    menu->setTitle(i18nc("@title:menu Create new folder, file, link, etc.", "Create New"));
    menu->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_newFileMenu->setPopupMode(QToolButton::InstantPopup);
    connect(menu, &QMenu::aboutToShow, this, &DolphinMainWindow::updateNewMenu);
    connect(m_newFileMenu, &KNewFileMenu::directoryCreated, this, [this](const QUrl &createdDirectory) {
        activeViewContainer()->view()->expandToUrl(createdDirectory);
    });

    QAction *newWindow = KStandardAction::openNew(this, &DolphinMainWindow::openNewMainWindow, actionCollection());
    newWindow->setText(i18nc("@action:inmenu File", "New &Window"));
    newWindow->setToolTip(i18nc("@info", "Open a new Dolphin window"));
    newWindow->setWhatsThis(xi18nc("@info:whatsthis",
                                   "This opens a new "
                                   "window just like this one with the current location."
                                   "<nl/>You can drag and drop items between windows."));
    newWindow->setIcon(QIcon::fromTheme(QStringLiteral("window-new")));

    QAction *newTab = actionCollection()->addAction(QStringLiteral("new_tab"));
    newTab->setIcon(QIcon::fromTheme(QStringLiteral("tab-new")));
    newTab->setText(i18nc("@action:inmenu File", "New Tab"));
    newTab->setWhatsThis(xi18nc("@info:whatsthis",
                                "This opens a new "
                                "<emphasis>Tab</emphasis> with the current location."
                                "<nl/>Tabs allow you to quickly switch between multiple locations and views within this window. "
                                "You can drag and drop items between tabs."));
    actionCollection()->setDefaultShortcut(newTab, Qt::CTRL | Qt::Key_T);
    connect(newTab, &QAction::triggered, this, &DolphinMainWindow::openNewActivatedTab);

    QAction *addToPlaces = actionCollection()->addAction(QStringLiteral("add_to_places"));
    addToPlaces->setIcon(QIcon::fromTheme(QStringLiteral("bookmark-new")));
    addToPlaces->setText(i18nc("@action:inmenu Add current folder to places", "Add to Places"));
    addToPlaces->setWhatsThis(xi18nc("@info:whatsthis",
                                     "This adds the selected folder "
                                     "to the Places panel."));
    connect(addToPlaces, &QAction::triggered, this, &DolphinMainWindow::addToPlaces);

    QAction *closeTab = KStandardAction::close(m_tabWidget, QOverload<>::of(&DolphinTabWidget::closeTab), actionCollection());
    closeTab->setText(i18nc("@action:inmenu File", "Close Tab"));
    closeTab->setToolTip(i18nc("@info", "Close Tab"));
    closeTab->setWhatsThis(i18nc("@info:whatsthis",
                                 "This closes the "
                                 "currently viewed tab. If no more tabs are left, this closes "
                                 "the whole window instead."));

    QAction *quitAction = KStandardAction::quit(this, &DolphinMainWindow::quit, actionCollection());
    quitAction->setWhatsThis(i18nc("@info:whatsthis quit", "This closes this window."));

    // setup 'Edit' menu
    KStandardAction::undo(this, &DolphinMainWindow::undo, actionCollection());
    KStandardAction::redo(this, &DolphinMainWindow::redo, actionCollection());

    // i18n: This will be the last paragraph for the whatsthis for all three:
    // Cut, Copy and Paste
    const QString cutCopyPastePara = xi18nc("@info:whatsthis",
                                            "<para><emphasis>Cut, "
                                            "Copy</emphasis> and <emphasis>Paste</emphasis> work between many "
                                            "applications and are among the most used commands. That's why their "
                                            "<emphasis>keyboard shortcuts</emphasis> are prominently placed right "
                                            "next to each other on the keyboard: <shortcut>Ctrl+X</shortcut>, "
                                            "<shortcut>Ctrl+C</shortcut> and <shortcut>Ctrl+V</shortcut>.</para>");
    QAction *cutAction = KStandardAction::cut(this, &DolphinMainWindow::cut, actionCollection());
    m_actionTextHelper->registerTextWhenNothingIsSelected(cutAction, i18nc("@action", "Cut…"));
    cutAction->setWhatsThis(xi18nc("@info:whatsthis cut",
                                   "This copies the items "
                                   "in your current selection to the <emphasis>clipboard</emphasis>.<nl/>"
                                   "Use the <emphasis>Paste</emphasis> action afterwards to copy them from "
                                   "the clipboard to a new location. The items will be removed from their "
                                   "initial location.")
                            + cutCopyPastePara);
    QAction *copyAction = KStandardAction::copy(this, &DolphinMainWindow::copy, actionCollection());
    m_actionTextHelper->registerTextWhenNothingIsSelected(copyAction, i18nc("@action", "Copy…"));
    copyAction->setWhatsThis(xi18nc("@info:whatsthis copy",
                                    "This copies the "
                                    "items in your current selection to the <emphasis>clipboard</emphasis>."
                                    "<nl/>Use the <emphasis>Paste</emphasis> action afterwards to copy them "
                                    "from the clipboard to a new location.")
                             + cutCopyPastePara);
    QAction *paste = KStandardAction::paste(this, &DolphinMainWindow::paste, actionCollection());
    // The text of the paste-action is modified dynamically by Dolphin
    // (e. g. to "Paste One Folder"). To prevent that the size of the toolbar changes
    // due to the long text, the text "Paste" is used:
    paste->setIconText(i18nc("@action:inmenu Edit", "Paste"));
    paste->setWhatsThis(xi18nc("@info:whatsthis paste",
                               "This copies the items from "
                               "your <emphasis>clipboard</emphasis> to the currently viewed folder.<nl/>"
                               "If the items were added to the clipboard by the <emphasis>Cut</emphasis> "
                               "action they are removed from their old location.")
                        + cutCopyPastePara);

    QAction *copyToOtherViewAction = actionCollection()->addAction(QStringLiteral("copy_to_inactive_split_view"));
    copyToOtherViewAction->setText(i18nc("@action:inmenu", "Copy to Other View"));
    m_actionTextHelper->registerTextWhenNothingIsSelected(copyToOtherViewAction, i18nc("@action:inmenu", "Copy to Other View…"));
    copyToOtherViewAction->setWhatsThis(xi18nc("@info:whatsthis Copy",
                                               "This copies the selected items from "
                                               "the view in focus to the other view. "
                                               "(Only available while in Split View mode.)"));
    copyToOtherViewAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copyToOtherViewAction->setIconText(i18nc("@action:inmenu Edit", "Copy to Other View"));
    actionCollection()->setDefaultShortcut(copyToOtherViewAction, Qt::SHIFT | Qt::Key_F5);
    connect(copyToOtherViewAction, &QAction::triggered, this, &DolphinMainWindow::copyToInactiveSplitView);

    QAction *moveToOtherViewAction = actionCollection()->addAction(QStringLiteral("move_to_inactive_split_view"));
    moveToOtherViewAction->setText(i18nc("@action:inmenu", "Move to Other View"));
    m_actionTextHelper->registerTextWhenNothingIsSelected(moveToOtherViewAction, i18nc("@action:inmenu", "Move to Other View…"));
    moveToOtherViewAction->setWhatsThis(xi18nc("@info:whatsthis Move",
                                               "This moves the selected items from "
                                               "the view in focus to the other view. "
                                               "(Only available while in Split View mode.)"));
    moveToOtherViewAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-cut")));
    moveToOtherViewAction->setIconText(i18nc("@action:inmenu Edit", "Move to Other View"));
    actionCollection()->setDefaultShortcut(moveToOtherViewAction, Qt::SHIFT | Qt::Key_F6);
    connect(moveToOtherViewAction, &QAction::triggered, this, &DolphinMainWindow::moveToInactiveSplitView);

    QAction *focusOtherViewAction = actionCollection()->addAction(QStringLiteral("focus_inactive_split_view"));
    focusOtherViewAction->setText(i18nc("@action:inmenu", "Focus Other View"));
    focusOtherViewAction->setToolTip(i18nc("@info:tooltip", "Move keyboard focus to the inactive view."));
    focusOtherViewAction->setIcon(QIcon::fromTheme(QStringLiteral("swap-panels")));
    actionCollection()->setDefaultShortcut(focusOtherViewAction, Qt::CTRL | Qt::Key_F3);
    connect(focusOtherViewAction, &QAction::triggered, m_tabWidget, &DolphinTabWidget::focusInactiveSplitView);

    QAction *showFilterBar = actionCollection()->addAction(QStringLiteral("show_filter_bar"));
    showFilterBar->setText(i18nc("@action:inmenu Tools", "Filter…"));
    showFilterBar->setToolTip(i18nc("@info:tooltip", "Show Filter Bar"));
    showFilterBar->setWhatsThis(xi18nc("@info:whatsthis",
                                       "This opens the "
                                       "<emphasis>Filter Bar</emphasis> at the bottom of the window.<nl/> "
                                       "There you can enter text to filter the files and folders currently displayed. "
                                       "Only those that contain the text in their name will be kept in view."));
    showFilterBar->setIcon(QIcon::fromTheme(QStringLiteral("view-filter")));
    actionCollection()->setDefaultShortcuts(showFilterBar, {Qt::CTRL | Qt::Key_I, Qt::Key_Slash});
    connect(showFilterBar, &QAction::triggered, this, &DolphinMainWindow::showFilterBar);

    // toggle_filter acts as a copy of the main showFilterBar to be used mainly
    // in the toolbar, with no default shortcut attached, to avoid messing with
    // existing workflows (filter bar always open and Ctrl-I to focus)
    QAction *toggleFilter = actionCollection()->addAction(QStringLiteral("toggle_filter"));
    toggleFilter->setText(i18nc("@action:inmenu", "Toggle Filter Bar"));
    toggleFilter->setIconText(i18nc("@action:intoolbar", "Filter"));
    toggleFilter->setIcon(showFilterBar->icon());
    toggleFilter->setToolTip(showFilterBar->toolTip());
    toggleFilter->setWhatsThis(showFilterBar->whatsThis());
    toggleFilter->setCheckable(true);
    connect(toggleFilter, &QAction::triggered, this, &DolphinMainWindow::toggleFilterBar);

    QAction *searchAction = KStandardAction::find(this, &DolphinMainWindow::find, actionCollection());
    searchAction->setText(i18n("Search…"));
    searchAction->setToolTip(i18nc("@info:tooltip", "Search for files and folders"));
    searchAction->setWhatsThis(xi18nc("@info:whatsthis find",
                                      "<para>This helps you "
                                      "find files and folders by opening a <emphasis>search bar</emphasis>. "
                                      "There you can enter search terms and specify settings to find the "
                                      "items you are looking for.</para>"));

    // toggle_search acts as a copy of the main searchAction to be used mainly
    // in the toolbar, with no default shortcut attached, to avoid messing with
    // existing workflows (search bar always open and Ctrl-F to focus)
    QAction *toggleSearchAction = actionCollection()->addAction(QStringLiteral("toggle_search"));
    toggleSearchAction->setText(i18nc("@action:inmenu", "Toggle Search Bar"));
    toggleSearchAction->setIconText(i18nc("@action:intoolbar", "Search"));
    toggleSearchAction->setIcon(searchAction->icon());
    toggleSearchAction->setToolTip(searchAction->toolTip());
    toggleSearchAction->setWhatsThis(searchAction->whatsThis());
    toggleSearchAction->setCheckable(true);
    connect(toggleSearchAction, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            find();
        } else {
            m_activeViewContainer->setSearchBarVisible(false);
        }
    });

    QAction *toggleSelectionModeAction = actionCollection()->addAction(QStringLiteral("toggle_selection_mode"));
    // i18n: This action toggles a selection mode.
    toggleSelectionModeAction->setText(i18nc("@action:inmenu", "Select Files and Folders"));
    // i18n: Opens a selection mode for selecting files/folders.
    // The text is kept so unspecific because it will be shown on the toolbar where space is at a premium.
    toggleSelectionModeAction->setIconText(i18nc("@action:intoolbar", "Select"));
    toggleSelectionModeAction->setWhatsThis(xi18nc(
        "@info:whatsthis",
        "<para>This application only knows which files or folders should be acted on if they are"
        " <emphasis>selected</emphasis> first. Press this to toggle a <emphasis>Selection Mode</emphasis> which makes selecting and deselecting as easy as "
        "pressing an item once.</para><para>While in this mode, a quick access bar at the bottom shows available actions for the currently selected items."
        "</para>"));
    toggleSelectionModeAction->setIcon(QIcon::fromTheme(QStringLiteral("quickwizard")));
    toggleSelectionModeAction->setCheckable(true);
    actionCollection()->setDefaultShortcut(toggleSelectionModeAction, Qt::Key_Space);
    connect(toggleSelectionModeAction, &QAction::triggered, this, &DolphinMainWindow::toggleSelectionMode);

    // A special version of the toggleSelectionModeAction for the toolbar that also contains a menu
    // with the selectAllAction and invertSelectionAction.
    auto *toggleSelectionModeToolBarAction =
        new KToolBarPopupAction(toggleSelectionModeAction->icon(), toggleSelectionModeAction->iconText(), actionCollection());
    toggleSelectionModeToolBarAction->setToolTip(toggleSelectionModeAction->text());
    toggleSelectionModeToolBarAction->setWhatsThis(toggleSelectionModeAction->whatsThis());
    actionCollection()->addAction(QStringLiteral("toggle_selection_mode_tool_bar"), toggleSelectionModeToolBarAction);
    toggleSelectionModeToolBarAction->setCheckable(true);
    toggleSelectionModeToolBarAction->setPopupMode(KToolBarPopupAction::DelayedPopup);
    connect(toggleSelectionModeToolBarAction, &QAction::triggered, toggleSelectionModeAction, &QAction::trigger);
    connect(toggleSelectionModeAction, &QAction::toggled, toggleSelectionModeToolBarAction, &QAction::setChecked);

    QAction *selectAllAction = KStandardAction::selectAll(this, &DolphinMainWindow::selectAll, actionCollection());
    selectAllAction->setWhatsThis(xi18nc("@info:whatsthis",
                                         "This selects all "
                                         "files and folders in the current location."));

    QAction *invertSelection = actionCollection()->addAction(QStringLiteral("invert_selection"));
    invertSelection->setText(i18nc("@action:inmenu Edit", "Invert Selection"));
    invertSelection->setWhatsThis(xi18nc("@info:whatsthis invert",
                                         "This selects all "
                                         "items that you have currently <emphasis>not</emphasis> selected instead."));
    invertSelection->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-invert")));
    actionCollection()->setDefaultShortcut(invertSelection, Qt::CTRL | Qt::SHIFT | Qt::Key_A);
    connect(invertSelection, &QAction::triggered, this, &DolphinMainWindow::invertSelection);

    // dolphin-ng: pin the selection to the top of the folder, or unpin it if it is all pinned already
    QAction *ngPin = actionCollection()->addAction(QStringLiteral("ng_pin"));
    ngPin->setText(ngTr("Pin to the Top / Unpin", "Fissa in alto / Togli dall'alto"));
    ngPin->setIcon(QIcon::fromTheme(QStringLiteral("window-pin")));
    actionCollection()->setDefaultShortcut(ngPin, Qt::CTRL | Qt::ALT | Qt::Key_P);
    connect(ngPin, &QAction::triggered, this, [this] {
        if (DolphinViewContainer *container = activeViewContainer()) {
            DolphinView *view = container->view();
            const KFileItemList items = view->selectedItems();
            view->ngSetPinned(items, !view->ngAllPinned(items));
        }
    });

    QMenu *toggleSelectionModeActionMenu = new QMenu(this);
    toggleSelectionModeActionMenu->addAction(selectAllAction);
    toggleSelectionModeActionMenu->addAction(invertSelection);
    toggleSelectionModeToolBarAction->setMenu(toggleSelectionModeActionMenu);

    // setup 'View' menu
    // (note that most of it is set up in DolphinViewActionHandler)

    Admin::WorkerIntegration::createActAsAdminAction(actionCollection(), this);

    m_splitViewAction = actionCollection()->add<KActionMenu>(QStringLiteral("split_view"));
    m_splitViewMenuAction = actionCollection()->addAction(QStringLiteral("split_view_menu"));

    m_splitViewAction->setWhatsThis(xi18nc("@info:whatsthis split",
                                           "<para>This presents "
                                           "a second view side-by-side with the current view, so you can see "
                                           "the contents of two folders at once and easily move items between "
                                           "them.</para><para>The view that is not \"in focus\" will be dimmed. "
                                           "</para>Click this button again to close one of the views."));
    m_splitViewMenuAction->setWhatsThis(m_splitViewAction->whatsThis());

    // only set it for the menu version
    actionCollection()->setDefaultShortcut(m_splitViewMenuAction, Qt::Key_F3);

    connect(m_splitViewAction, &QAction::triggered, this, &DolphinMainWindow::toggleSplitView);
    connect(m_splitViewMenuAction, &QAction::triggered, this, &DolphinMainWindow::toggleSplitView);

    QAction *popoutSplit = actionCollection()->addAction(QStringLiteral("popout_split_view"));
    popoutSplit->setWhatsThis(xi18nc("@info:whatsthis",
                                     "If the view has been split, this will pop the view in focus "
                                     "out into a new window."));
    popoutSplit->setIcon(QIcon::fromTheme(QStringLiteral("window-new")));
    actionCollection()->setDefaultShortcut(popoutSplit, Qt::SHIFT | Qt::Key_F3);
    connect(popoutSplit, &QAction::triggered, this, &DolphinMainWindow::popoutSplitView);

    QAction *stashSplit = actionCollection()->addAction(QStringLiteral("split_stash"));
    actionCollection()->setDefaultShortcut(stashSplit, Qt::CTRL | Qt::Key_S);
    stashSplit->setText(i18nc("@action:intoolbar Stash", "Stash"));
    stashSplit->setToolTip(i18nc("@info", "Opens the stash virtual directory in a split window"));
    stashSplit->setIcon(QIcon::fromTheme(QStringLiteral("folder-stash")));
    stashSplit->setCheckable(false);
    QDBusConnectionInterface *sessionInterface = QDBusConnection::sessionBus().interface();
    stashSplit->setVisible(sessionInterface && sessionInterface->isServiceRegistered(QStringLiteral("org.kde.kio.StashNotifier")));
    connect(stashSplit, &QAction::triggered, this, &DolphinMainWindow::toggleSplitStash);

    QAction *redisplay = KStandardAction::redisplay(this, &DolphinMainWindow::reloadView, actionCollection());
    redisplay->setAutoRepeat(false);
    redisplay->setToolTip(i18nc("@info:tooltip", "Refresh view"));
    redisplay->setWhatsThis(xi18nc("@info:whatsthis refresh",
                                   "<para>This refreshes "
                                   "the folder view.</para>"
                                   "<para>If the contents of this folder have changed, refreshing will re-scan this folder "
                                   "and show you a newly-updated view of the files and folders contained here.</para>"
                                   "<para>If the view is split, this refreshes the one that is currently in focus.</para>"));

    QAction *stop = actionCollection()->addAction(QStringLiteral("stop"));
    stop->setText(i18nc("@action:inmenu View", "Stop"));
    stop->setToolTip(i18nc("@info", "Stop loading"));
    stop->setWhatsThis(i18nc("@info", "This stops the loading of the contents of the current folder."));
    stop->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    connect(stop, &QAction::triggered, this, &DolphinMainWindow::stopLoading);

    KToggleAction *editableLocation = actionCollection()->add<KToggleAction>(QStringLiteral("editable_location"));
    editableLocation->setText(i18nc("@action:inmenu Navigation Bar", "Editable Location"));
    editableLocation->setWhatsThis(xi18nc("@info:whatsthis",
                                          "This toggles the <emphasis>Location Bar</emphasis> to be "
                                          "editable so you can directly enter a location you want to go to.<nl/>"
                                          "You can also switch to editing by clicking to the right of the "
                                          "location and switch back by confirming the edited location."));
    actionCollection()->setDefaultShortcut(editableLocation, Qt::Key_F6);
    connect(editableLocation, &KToggleAction::triggered, this, &DolphinMainWindow::toggleEditLocation);

    QAction *replaceLocation = actionCollection()->addAction(QStringLiteral("replace_location"));
    replaceLocation->setText(i18nc("@action:inmenu Navigation Bar", "Replace Location"));
    // i18n: "enter" is used both in the meaning of "writing" and "going to" a new location here.
    // Both meanings are useful but not necessary to understand the use of "Replace Location".
    // So you might want to be more verbose in your language to convey the meaning but it's up to you.
    replaceLocation->setWhatsThis(xi18nc("@info:whatsthis",
                                         "This switches to editing the location and selects it "
                                         "so you can quickly enter a different location."));
    actionCollection()->setDefaultShortcuts(replaceLocation, {Qt::CTRL | Qt::Key_L, Qt::ALT | Qt::Key_D});
    connect(replaceLocation, &QAction::triggered, this, &DolphinMainWindow::replaceLocation);

    // setup 'Go' menu
    {
        QScopedPointer<QAction> backAction(KStandardAction::back(nullptr, nullptr, nullptr));
        m_backAction = new KToolBarPopupAction(backAction->icon(), backAction->text(), actionCollection());
        m_backAction->setObjectName(backAction->objectName());
        m_backAction->setShortcuts(backAction->shortcuts());
    }
    m_backAction->setPopupMode(KToolBarPopupAction::DelayedPopup);
    connect(m_backAction, &QAction::triggered, this, &DolphinMainWindow::goBack);
    connect(m_backAction->popupMenu(), &QMenu::aboutToShow, this, &DolphinMainWindow::slotAboutToShowBackPopupMenu);
    connect(m_backAction->popupMenu(), &QMenu::triggered, this, &DolphinMainWindow::slotGoBack);
    actionCollection()->addAction(m_backAction->objectName(), m_backAction);

    auto backShortcuts = m_backAction->shortcuts();
    // Prepend this shortcut, to avoid being hidden by the two-slot UI (#371130)
    backShortcuts.prepend(QKeySequence(Qt::Key_Backspace));
    actionCollection()->setDefaultShortcuts(m_backAction, backShortcuts);

    DolphinRecentTabsMenu *recentTabsMenu = new DolphinRecentTabsMenu(this);
    actionCollection()->addAction(QStringLiteral("closed_tabs"), recentTabsMenu);
    connect(m_tabWidget, &DolphinTabWidget::rememberClosedTab, recentTabsMenu, &DolphinRecentTabsMenu::rememberClosedTab);
    connect(recentTabsMenu, &DolphinRecentTabsMenu::restoreClosedTab, m_tabWidget, &DolphinTabWidget::restoreClosedTab);
    connect(recentTabsMenu, &DolphinRecentTabsMenu::closedTabsCountChanged, this, &DolphinMainWindow::closedTabsCountChanged);

    QAction *undoCloseTab = actionCollection()->addAction(QStringLiteral("undo_close_tab"));
    undoCloseTab->setText(i18nc("@action:inmenu File", "Undo close tab"));
    undoCloseTab->setWhatsThis(i18nc("@info:whatsthis undo close tab", "This returns you to the previously closed tab."));
    actionCollection()->setDefaultShortcut(undoCloseTab, Qt::CTRL | Qt::SHIFT | Qt::Key_T);
    undoCloseTab->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    undoCloseTab->setEnabled(false);
    connect(undoCloseTab, &QAction::triggered, recentTabsMenu, &DolphinRecentTabsMenu::undoCloseTab);

    auto undoAction = actionCollection()->action(KStandardAction::name(KStandardAction::Undo));
    undoAction->setWhatsThis(xi18nc("@info:whatsthis",
                                    "This undoes "
                                    "the last change you made to files or folders.<nl/>"
                                    "Such changes include <interface>creating</interface>, <interface>renaming</interface> "
                                    "and <interface>moving</interface> them to a different location "
                                    "or to the <filename>Trash</filename>. <nl/>Any changes that cannot be undone "
                                    "will ask for your confirmation beforehand."));
    undoAction->setEnabled(false); // undo should be disabled by default

    auto redoAction = actionCollection()->action(KStandardAction::name(KStandardAction::Redo));
    redoAction->setWhatsThis(xi18nc("@info:whatsthis",
                                    "This redoes "
                                    "the last change you undid.<nl/>"
                                    "Such changes include <interface>creating</interface>, <interface>renaming</interface> "
                                    "and <interface>moving</interface> files or folders to a different location "
                                    "or to the <filename>Trash</filename>.<nl/>Any changes that cannot be undone "
                                    "will ask for your confirmation beforehand."));
    redoAction->setEnabled(false); // redo should be disabled by default

    {
        QScopedPointer<QAction> forwardAction(KStandardAction::forward(nullptr, nullptr, nullptr));
        m_forwardAction = new KToolBarPopupAction(forwardAction->icon(), forwardAction->text(), actionCollection());
        m_forwardAction->setObjectName(forwardAction->objectName());
        m_forwardAction->setShortcuts(forwardAction->shortcuts());
    }
    m_forwardAction->setPopupMode(KToolBarPopupAction::DelayedPopup);
    connect(m_forwardAction, &QAction::triggered, this, &DolphinMainWindow::goForward);
    connect(m_forwardAction->popupMenu(), &QMenu::aboutToShow, this, &DolphinMainWindow::slotAboutToShowForwardPopupMenu);
    connect(m_forwardAction->popupMenu(), &QMenu::triggered, this, &DolphinMainWindow::slotGoForward);
    actionCollection()->addAction(m_forwardAction->objectName(), m_forwardAction);
    actionCollection()->setDefaultShortcuts(m_forwardAction, m_forwardAction->shortcuts());

    // enable middle-click to open in a new tab
    auto *middleClickEventFilter = new MiddleClickActionEventFilter(this);
    connect(middleClickEventFilter, &MiddleClickActionEventFilter::actionMiddleClicked, this, &DolphinMainWindow::slotBackForwardActionMiddleClicked);
    m_backAction->popupMenu()->installEventFilter(middleClickEventFilter);
    m_forwardAction->popupMenu()->installEventFilter(middleClickEventFilter);
    KStandardAction::up(this, &DolphinMainWindow::goUp, actionCollection());
    // dolphin-ng: a toolbar action that goes to the root of the file system
    QAction *rootAction = actionCollection()->addAction(QStringLiteral("go_root"));
    rootAction->setText(QStringLiteral("Root"));
    rootAction->setIcon(QIcon::fromTheme(QStringLiteral("folder-root")));
    connect(rootAction, &QAction::triggered, this, [this] {
        changeUrl(QUrl::fromLocalFile(QStringLiteral("/")));
    });
    QAction *homeAction = KStandardAction::home(this, &DolphinMainWindow::goHome, actionCollection());
    homeAction->setWhatsThis(xi18nc("@info:whatsthis",
                                    "Go to your "
                                    "<filename>Home</filename> folder.<nl/>Every user account "
                                    "has their own <filename>Home</filename> that contains their personal files, "
                                    "as well as hidden folders for their applications' data and configuration files."));

    // setup 'Tools' menu
    QAction *compareFiles = actionCollection()->addAction(QStringLiteral("compare_files"));
    compareFiles->setText(i18nc("@action:inmenu Tools", "Compare Files"));
    compareFiles->setIcon(QIcon::fromTheme(QStringLiteral("kompare")));
    compareFiles->setEnabled(false);
    connect(compareFiles, &QAction::triggered, this, &DolphinMainWindow::compareFiles);

    QAction *manageDiskSpaceUsage = actionCollection()->addAction(QStringLiteral("manage_disk_space"));
    manageDiskSpaceUsage->setText(i18nc("@action:inmenu Tools", "Manage Disk Space Usage"));
    manageDiskSpaceUsage->setIcon(QIcon::fromTheme(QStringLiteral("filelight")));
    m_diskSpaceUsageMenu = new DiskSpaceUsageMenu{this};
    manageDiskSpaceUsage->setMenu(m_diskSpaceUsageMenu);

    QAction *openPreferredSearchTool = actionCollection()->addAction(QStringLiteral("open_preferred_search_tool"));
    openPreferredSearchTool->setText(i18nc("@action:inmenu Tools", "Open Preferred Search Tool"));
    openPreferredSearchTool->setWhatsThis(xi18nc("@info:whatsthis",
                                                 "<para>This opens a preferred search tool for the viewed location.</para>"
                                                 "<para>Use <emphasis>More Search Tools</emphasis> menu to configure it.</para>"));
    openPreferredSearchTool->setIcon(QIcon::fromTheme(QStringLiteral("search")));
    actionCollection()->setDefaultShortcut(openPreferredSearchTool, Qt::CTRL | Qt::SHIFT | Qt::Key_F);
    connect(openPreferredSearchTool, &QAction::triggered, this, &DolphinMainWindow::openPreferredSearchTool);

    if (KAuthorized::authorize(QStringLiteral("shell_access"))) {
        // Get icon of user default terminal emulator application
        const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::SimpleConfig), QStringLiteral("General"));
        const QString terminalDesktopFilename = group.readEntry("TerminalService");
        // Use utilities-terminal icon from theme if readEntry() has failed
        const QString terminalIcon = terminalDesktopFilename.isEmpty() ? "utilities-terminal" : KDesktopFile(terminalDesktopFilename).readIcon();

        QAction *openTerminal = actionCollection()->addAction(QStringLiteral("open_terminal"));
        openTerminal->setText(i18nc("@action:inmenu Tools", "Open Terminal"));
        openTerminal->setWhatsThis(xi18nc("@info:whatsthis",
                                          "<para>This opens a <emphasis>terminal</emphasis> application for the viewed location.</para>"
                                          "<para>To learn more about terminals use the help features in the terminal application.</para>"));
        openTerminal->setIcon(QIcon::fromTheme(terminalIcon));
        actionCollection()->setDefaultShortcut(openTerminal, Qt::SHIFT | Qt::Key_F4);
        connect(openTerminal, &QAction::triggered, this, &DolphinMainWindow::openTerminal);

        QAction *openTerminalHere = actionCollection()->addAction(QStringLiteral("open_terminal_here"));
        // i18n: "Here" refers to the location(s) of the currently selected item(s) or the currently viewed location if nothing is selected.
        openTerminalHere->setText(i18nc("@action:inmenu Tools", "Open Terminal Here"));
        openTerminalHere->setWhatsThis(xi18nc("@info:whatsthis",
                                              "<para>This opens <emphasis>terminal</emphasis> applications for the selected items' locations.</para>"
                                              "<para>To learn more about terminals use the help features in the terminal application.</para>"));
        openTerminalHere->setIcon(QIcon::fromTheme(terminalIcon));
        actionCollection()->setDefaultShortcut(openTerminalHere, Qt::SHIFT | Qt::ALT | Qt::Key_F4);
        connect(openTerminalHere, &QAction::triggered, this, &DolphinMainWindow::openTerminalHere);
    }

    // setup 'Bookmarks' menu
    KActionMenu *bookmarkMenu = new KActionMenu(i18nc("@title:menu", "&Bookmarks"), this);
    bookmarkMenu->setIcon(QIcon::fromTheme(QStringLiteral("bookmarks")));
    // Make the toolbar button version work properly on click
    bookmarkMenu->setPopupMode(QToolButton::InstantPopup);
    m_bookmarkHandler = new DolphinBookmarkHandler(this, actionCollection(), bookmarkMenu->menu(), this);
    actionCollection()->addAction(QStringLiteral("bookmarks"), bookmarkMenu);

    // setup 'Settings' menu
    KToggleAction *showMenuBar = KStandardAction::showMenubar(nullptr, nullptr, actionCollection());
    showMenuBar->setWhatsThis(xi18nc("@info:whatsthis",
                                     "<para>This switches between having a <emphasis>Menubar</emphasis> "
                                     "and having an <interface>%1</interface> button. Both "
                                     "contain mostly the same actions and configuration options.</para>"
                                     "<para>The Menubar takes up more space but allows for fast and organized access to all "
                                     "actions an application has to offer.</para><para>The %1 button "
                                     "is simpler and small which makes triggering advanced actions more time consuming.</para>",
                                     hamburgerMenuAction->text().replace('&', "")));
    connect(showMenuBar,
            &KToggleAction::triggered, // Fixes #286822
            this,
            &DolphinMainWindow::toggleShowMenuBar,
            Qt::QueuedConnection);

    KStandardAction::keyBindings(this, &DolphinMainWindow::slotKeyBindings, actionCollection());
    KStandardAction::preferences(this, &DolphinMainWindow::editSettings, actionCollection());

    // not in menu actions
    QList<QKeySequence> nextTabKeys = KStandardShortcut::tabNext();
    nextTabKeys.append(QKeySequence(Qt::CTRL | Qt::Key_Tab));

    QList<QKeySequence> prevTabKeys = KStandardShortcut::tabPrev();
    prevTabKeys.append(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));

    for (int i = 0; i < MaxActivateTabShortcuts; ++i) {
        QAction *activateTab = actionCollection()->addAction(QStringLiteral("activate_tab_%1").arg(i));
        activateTab->setText(i18nc("@action:inmenu", "Go to Tab %1", i + 1));
        activateTab->setEnabled(false);
        connect(activateTab, &QAction::triggered, this, [this, i]() {
            m_tabWidget->activateTab(i);
        });

        // only add default shortcuts for the first 9 tabs regardless of MaxActivateTabShortcuts
        if (i < 9) {
            actionCollection()->setDefaultShortcut(activateTab, QStringLiteral("Alt+%1").arg(i + 1));
        }
    }

    QAction *activateLastTab = actionCollection()->addAction(QStringLiteral("activate_last_tab"));
    activateLastTab->setIconText(i18nc("@action:inmenu", "Last Tab"));
    activateLastTab->setText(i18nc("@action:inmenu", "Go to Last Tab"));
    activateLastTab->setEnabled(false);
    connect(activateLastTab, &QAction::triggered, m_tabWidget, &DolphinTabWidget::activateLastTab);
    actionCollection()->setDefaultShortcut(activateLastTab, Qt::ALT | Qt::Key_0);

    QAction *activateNextTab = actionCollection()->addAction(QStringLiteral("activate_next_tab"));
    activateNextTab->setIconText(i18nc("@action:inmenu", "Next Tab"));
    activateNextTab->setText(i18nc("@action:inmenu", "Go to Next Tab"));
    activateNextTab->setEnabled(false);
    connect(activateNextTab, &QAction::triggered, m_tabWidget, &DolphinTabWidget::activateNextTab);
    actionCollection()->setDefaultShortcuts(activateNextTab, nextTabKeys);

    QAction *activatePrevTab = actionCollection()->addAction(QStringLiteral("activate_prev_tab"));
    activatePrevTab->setIconText(i18nc("@action:inmenu", "Previous Tab"));
    activatePrevTab->setText(i18nc("@action:inmenu", "Go to Previous Tab"));
    activatePrevTab->setEnabled(false);
    connect(activatePrevTab, &QAction::triggered, m_tabWidget, &DolphinTabWidget::activatePrevTab);
    actionCollection()->setDefaultShortcuts(activatePrevTab, prevTabKeys);

    // for context menu
    QAction *showTarget = actionCollection()->addAction(QStringLiteral("show_target"));
    showTarget->setText(i18nc("@action:inmenu", "Show Target"));
    showTarget->setIcon(QIcon::fromTheme(QStringLiteral("document-open-folder")));
    showTarget->setEnabled(false);
    connect(showTarget, &QAction::triggered, this, &DolphinMainWindow::showTarget);

    QAction *openInNewTab = actionCollection()->addAction(QStringLiteral("open_in_new_tab"));
    openInNewTab->setText(i18nc("@action:inmenu", "Open in New Tab"));
    openInNewTab->setIcon(QIcon::fromTheme(QStringLiteral("tab-new")));
    connect(openInNewTab, &QAction::triggered, this, &DolphinMainWindow::openInNewTab);

    QAction *openInNewTabs = actionCollection()->addAction(QStringLiteral("open_in_new_tabs"));
    openInNewTabs->setText(i18nc("@action:inmenu", "Open in New Tabs"));
    openInNewTabs->setIcon(QIcon::fromTheme(QStringLiteral("tab-new")));
    connect(openInNewTabs, &QAction::triggered, this, &DolphinMainWindow::openInNewTab);

    QAction *openInNewWindow = actionCollection()->addAction(QStringLiteral("open_in_new_window"));
    openInNewWindow->setText(i18nc("@action:inmenu", "Open in New Window"));
    openInNewWindow->setIcon(QIcon::fromTheme(QStringLiteral("window-new")));
    connect(openInNewWindow, &QAction::triggered, this, &DolphinMainWindow::openInNewWindow);

    QAction *openInSplitViewAction = actionCollection()->addAction(QStringLiteral("open_in_split_view"));
    openInSplitViewAction->setText(i18nc("@action:inmenu", "Open in Split View"));
    openInSplitViewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    connect(openInSplitViewAction, &QAction::triggered, this, [this]() {
        openInSplitView(QUrl());
    });

    // Window color scheme menu
    auto *manager = KColorSchemeManager::instance();
    KActionMenu *selectionMenu = KColorSchemeMenu::createMenu(manager, this);
    auto windowColorSchemeMenu = new QAction(this);
    windowColorSchemeMenu->setMenu(selectionMenu->menu());
    windowColorSchemeMenu->menu()->setIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-color")));
    windowColorSchemeMenu->menu()->setTitle(i18n("&Window Color Scheme"));
    actionCollection()->addAction(QStringLiteral("window_color_sheme"), windowColorSchemeMenu);

    m_recentFiles = new KRecentFilesAction(this);
}

void DolphinMainWindow::setupDockWidgets()
{
    const bool lock = GeneralSettings::lockPanels();

    DolphinPlacesModelSingleton::instance().placesModel()->setPanelsLocked(lock);

    KDualAction *lockLayoutAction = actionCollection()->add<KDualAction>(QStringLiteral("lock_panels"));
    lockLayoutAction->setActiveText(i18nc("@action:inmenu Panels", "Unlock Panels"));
    lockLayoutAction->setActiveIcon(QIcon::fromTheme(QStringLiteral("object-unlocked")));
    lockLayoutAction->setInactiveText(i18nc("@action:inmenu Panels", "Lock Panels"));
    lockLayoutAction->setInactiveIcon(QIcon::fromTheme(QStringLiteral("object-locked")));
    lockLayoutAction->setWhatsThis(xi18nc("@info:whatsthis",
                                          "This "
                                          "switches between having panels <emphasis>locked</emphasis> or "
                                          "<emphasis>unlocked</emphasis>.<nl/>Unlocked panels can be "
                                          "dragged to the other side of the window and have a close "
                                          "button.<nl/>Locked panels are embedded more cleanly."));
    lockLayoutAction->setActive(lock);
    connect(lockLayoutAction, &KDualAction::triggered, this, &DolphinMainWindow::togglePanelLockState);

    // Setup "Information"
    DolphinDockWidget *infoDock = new DolphinDockWidget(i18nc("@title:window", "Information"), this);
    infoDock->setLocked(lock);
    infoDock->setObjectName(QStringLiteral("infoDock"));
    infoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

#if HAVE_BALOO
    InformationPanel *infoPanel = new InformationPanel(infoDock);
    infoPanel->setCustomContextMenuActions({lockLayoutAction});
    connect(infoPanel, &InformationPanel::urlActivated, this, &DolphinMainWindow::handleUrl);
    // dolphin-ng: what the NG Information panel asks of the view in front
    connect(infoPanel, &InformationPanel::ngQuickActionRequested, this, [this](const KFileItem &item, int action) {
        if (m_activeViewContainer) {
            m_activeViewContainer->view()->ngQuickAction(item, action);
        }
    });
    connect(infoPanel, &InformationPanel::ngPinRequested, this, [this](const KFileItemList &items, bool pinned) {
        if (m_activeViewContainer) {
            m_activeViewContainer->view()->ngSetPinned(items, pinned);
        }
    });
    connect(infoPanel, &InformationPanel::ngSelectRequested, this, [this](const QUrl &url) {
        if (!m_activeViewContainer) {
            return;
        }
        // the item in the folder shown: selected there; in another (a subfolder selected in the view): that one is
        // opened, the item selected once it has loaded
        const QUrl folder = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
        DolphinView *view = m_activeViewContainer->view();
        const bool here = view->url().adjusted(QUrl::StripTrailingSlash).matches(folder, QUrl::StripTrailingSlash);
        if (here) {
            view->ngSelectUrl(url);
        } else {
            // (as "Open Path" does: the folder first, then the marks, which the view applies once it has loaded; marked
            // before, the change of folder cleared them)
            changeUrl(folder);
            m_activeViewContainer->view()->markUrlsAsSelected({url});
            m_activeViewContainer->view()->markUrlAsCurrent(url);
        }
    });
    connect(infoPanel, &InformationPanel::ngLightboxRequested, this, [this](const KFileItem &item) {
        if (m_activeViewContainer) {
            m_activeViewContainer->ngShowLightbox(item);
        }
    });
    infoPanel->ngSetPinnedQuery([this](const KFileItemList &items) {
        return m_activeViewContainer && m_activeViewContainer->view()->ngAllPinned(items);
    });
    infoDock->setWidget(infoPanel);

    createPanelAction(QIcon::fromTheme(QStringLiteral("documentinfo")), Qt::Key_F11, infoDock, QStringLiteral("show_information_panel"));

    addDockWidget(Qt::RightDockWidgetArea, infoDock);
    connect(this, &DolphinMainWindow::urlChanged, infoPanel, &InformationPanel::setUrl);
    connect(this, &DolphinMainWindow::selectionChanged, infoPanel, &InformationPanel::setSelection);
    connect(this, &DolphinMainWindow::requestItemInfo, infoPanel, &InformationPanel::requestDelayedItemInfo);
    connect(this, &DolphinMainWindow::fileItemsChanged, infoPanel, &InformationPanel::slotFilesItemChanged);
    connect(this, &DolphinMainWindow::settingsChanged, infoPanel, &InformationPanel::readSettings);
#endif

    // i18n: This is the last paragraph for the "What's This"-texts of all four panels.
    const QString panelWhatsThis = xi18nc("@info:whatsthis",
                                          "<para>To show or "
                                          "hide panels like this go to <interface>Menu|Panels</interface> "
                                          "or <interface>View|Panels</interface>.</para>");
#if HAVE_BALOO
    actionCollection()
        ->action(QStringLiteral("show_information_panel"))
        ->setWhatsThis(xi18nc("@info:whatsthis",
                              "<para> This toggles the "
                              "<emphasis>information</emphasis> panel at the right side of the "
                              "window.</para><para>The panel provides in-depth information "
                              "about the items your mouse is hovering over or about the selected "
                              "items. Otherwise it informs you about the currently viewed folder.<nl/>"
                              "For single items a preview of their contents is provided.</para>"));
#endif
    infoDock->setWhatsThis(xi18nc("@info:whatsthis",
                                  "<para>This panel "
                                  "provides in-depth information about the items your mouse is "
                                  "hovering over or about the selected items. Otherwise it informs "
                                  "you about the currently viewed folder.<nl/>For single items a "
                                  "preview of their contents is provided.</para><para>You can configure "
                                  "which and how details are given here by right-clicking.</para>")
                           + panelWhatsThis);

    // Setup "Folders"
    DolphinDockWidget *foldersDock = new DolphinDockWidget(i18nc("@title:window", "Folders"));
    foldersDock->setLocked(lock);
    foldersDock->setObjectName(QStringLiteral("foldersDock"));
    foldersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    FoldersPanel *foldersPanel = new FoldersPanel(foldersDock);
    foldersPanel->setCustomContextMenuActions({lockLayoutAction});
    foldersDock->setWidget(foldersPanel);

    createPanelAction(QIcon::fromTheme(QStringLiteral("folder")), Qt::Key_F7, foldersDock, QStringLiteral("show_folders_panel"));

    addDockWidget(Qt::LeftDockWidgetArea, foldersDock);
    connect(this, &DolphinMainWindow::urlChanged, foldersPanel, &FoldersPanel::setUrl);
    connect(foldersPanel, &FoldersPanel::folderActivated, this, &DolphinMainWindow::changeUrl);
    connect(foldersPanel, &FoldersPanel::folderInNewTab, this, &DolphinMainWindow::openNewTab);
    connect(foldersPanel, &FoldersPanel::folderInNewActiveTab, this, &DolphinMainWindow::openNewTabAndActivate);
    connect(foldersPanel, &FoldersPanel::errorMessage, this, &DolphinMainWindow::showErrorMessage);

    actionCollection()
        ->action(QStringLiteral("show_folders_panel"))
        ->setWhatsThis(xi18nc("@info:whatsthis",
                              "This toggles the "
                              "<emphasis>folders</emphasis> panel at the left side of the window."
                              "<nl/><nl/>It shows the folders of the <emphasis>file system"
                              "</emphasis> in a <emphasis>tree view</emphasis>."));
    foldersDock->setWhatsThis(xi18nc("@info:whatsthis",
                                     "<para>This panel "
                                     "shows the folders of the <emphasis>file system</emphasis> in a "
                                     "<emphasis>tree view</emphasis>.</para><para>Click a folder to go "
                                     "there. Click the arrow to the left of a folder to see its subfolders. "
                                     "This allows quick switching between any folders.</para>")
                              + panelWhatsThis);

    // Setup "Terminal"
#if HAVE_TERMINAL
    if (KAuthorized::authorize(QStringLiteral("shell_access"))) {
        DolphinDockWidget *terminalDock = new DolphinDockWidget(i18nc("@title:window Shell terminal", "Terminal"));
        terminalDock->setLocked(lock);
        terminalDock->setObjectName(QStringLiteral("terminalDock"));
        terminalDock->setContentsMargins(0, 0, 0, 0);
        m_terminalPanel = new TerminalPanel(terminalDock);
        m_terminalPanel->setCustomContextMenuActions({lockLayoutAction});
        terminalDock->setWidget(m_terminalPanel);

        connect(m_terminalPanel, &TerminalPanel::hideTerminalPanel, terminalDock, &DolphinDockWidget::hide);
        connect(m_terminalPanel, &TerminalPanel::changeUrl, this, &DolphinMainWindow::slotTerminalDirectoryChanged);
        connect(terminalDock, &DolphinDockWidget::visibilityChanged, m_terminalPanel, &TerminalPanel::dockVisibilityChanged);
        connect(terminalDock, &DolphinDockWidget::visibilityChanged, this, &DolphinMainWindow::slotTerminalPanelVisibilityChanged);

        createPanelAction(QIcon::fromTheme(QStringLiteral("dialog-scripts")), Qt::Key_F4, terminalDock, QStringLiteral("show_terminal_panel"));

        addDockWidget(Qt::BottomDockWidgetArea, terminalDock);
        connect(this, &DolphinMainWindow::urlChanged, m_terminalPanel, &TerminalPanel::setUrl);
        connect(this, &DolphinMainWindow::urlRefreshed, m_terminalPanel, &TerminalPanel::refreshUrl);

        if (GeneralSettings::version() < 200) {
            terminalDock->hide();
        }

        actionCollection()
            ->action(QStringLiteral("show_terminal_panel"))
            ->setWhatsThis(xi18nc("@info:whatsthis",
                                  "<para>This toggles the "
                                  "<emphasis>terminal</emphasis> panel at the bottom of the window."
                                  "<nl/>The location in the terminal will always match the folder "
                                  "view so you can navigate using either.</para><para>The terminal "
                                  "panel is not needed for basic computer usage but can be useful "
                                  "for advanced tasks. To learn more about terminals use the help features "
                                  "in a standalone terminal application like Konsole.</para>"));
        terminalDock->setWhatsThis(xi18nc("@info:whatsthis",
                                          "<para>This is "
                                          "the <emphasis>terminal</emphasis> panel. It behaves like a "
                                          "normal terminal but will match the location of the folder view "
                                          "so you can navigate using either.</para><para>The terminal panel "
                                          "is not needed for basic computer usage but can be useful for "
                                          "advanced tasks. To learn more about terminals use the help features in a "
                                          "standalone terminal application like Konsole.</para>")
                                   + panelWhatsThis);

        QAction *focusTerminalPanel = actionCollection()->addAction(QStringLiteral("focus_terminal_panel"));
        focusTerminalPanel->setText(i18nc("@action:inmenu Tools", "Focus Terminal Panel"));
        focusTerminalPanel->setToolTip(i18nc("@info:tooltip", "Move keyboard focus to and from the Terminal panel."));
        focusTerminalPanel->setIcon(QIcon::fromTheme(QStringLiteral("swap-panels")));
        actionCollection()->setDefaultShortcut(focusTerminalPanel, Qt::CTRL | Qt::SHIFT | Qt::Key_F4);
        connect(focusTerminalPanel, &QAction::triggered, this, &DolphinMainWindow::toggleTerminalPanelFocus);

        QAction *switchTerminalUrlSync = actionCollection()->addAction(QStringLiteral("switch_terminal_url_sync"));
        switchTerminalUrlSync->setText(i18nc("@action:inmenu", "Follow Directory Switch"));
        switchTerminalUrlSync->setToolTip(
            i18nc("@info:tooltip", "Determines if current working directory must be kept in sync with terminal whenever directory is changed."));
        switchTerminalUrlSync->setCheckable(true);
        connect(switchTerminalUrlSync, &QAction::toggled, m_terminalPanel, &TerminalPanel::switchSync);
        switchTerminalUrlSync->setChecked(true);

        m_terminalPanel->setSwitchTerminalUrlSyncAction(switchTerminalUrlSync);

    } // endif "shell_access" allowed
#endif // HAVE_TERMINAL

    if (GeneralSettings::version() < 200) {
        infoDock->hide();
        foldersDock->hide();
    }

    // Setup "Places"
    DolphinDockWidget *placesDock = new DolphinDockWidget(i18nc("@title:window", "Places"));
    placesDock->setLocked(lock);
    placesDock->setObjectName(QStringLiteral("placesDock"));
    placesDock->ngSetBare(); // dolphin-ng: no float/close buttons over the card
    placesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_placesPanel = new PlacesPanel(placesDock);
    m_placesPanel->setCustomContextMenuActions({lockLayoutAction});
    placesDock->setWidget(m_placesPanel);

    createPanelAction(QIcon::fromTheme(QStringLiteral("compass")), Qt::Key_F9, placesDock, QStringLiteral("show_places_panel"));

    addDockWidget(Qt::LeftDockWidgetArea, placesDock);
    connect(m_placesPanel, &PlacesPanel::placeActivated, this, &DolphinMainWindow::slotPlaceActivated);
    connect(placesDock, &DolphinDockWidget::ngDiskRootRequested, this, &DolphinMainWindow::slotPlaceActivated); // dolphin-ng: disk usage box
    // dolphin-ng: the pinned items and the filters as sidebar sections
    {
        auto open = [this](const QUrl &url) {
            slotPlaceActivated(url);
        };
        m_placesPanel->ngSetSections(ngCreateFilterPanel(m_ngSortBar, m_placesPanel, open, 1), ngCreateFilterPanel(m_ngSortBar, m_placesPanel, open, 2));
    }
    connect(m_placesPanel, &PlacesPanel::tabRequested, this, &DolphinMainWindow::openNewTab);
    connect(m_placesPanel, &PlacesPanel::activeTabRequested, this, &DolphinMainWindow::openNewTabAndActivate);
    connect(m_placesPanel, &PlacesPanel::newWindowRequested, this, [this](const QUrl &url) {
        Dolphin::openNewWindow({url}, this);
    });
    connect(m_placesPanel, &PlacesPanel::openInSplitViewRequested, this, &DolphinMainWindow::openInSplitView);
    connect(m_placesPanel, &PlacesPanel::errorMessage, this, &DolphinMainWindow::showErrorMessage);
    connect(this, &DolphinMainWindow::urlChanged, m_placesPanel, &PlacesPanel::setUrl);
    connect(placesDock, &DolphinDockWidget::visibilityChanged, &DolphinUrlNavigatorsController::slotPlacesPanelVisibilityChanged);
    connect(placesDock, &DolphinDockWidget::visibilityChanged, this, &DolphinMainWindow::slotPlacesPanelVisibilityChanged);
    connect(this, &DolphinMainWindow::settingsChanged, m_placesPanel, &PlacesPanel::readSettings);
    connect(m_placesPanel, &PlacesPanel::storageTearDownRequested, this, &DolphinMainWindow::slotStorageTearDownFromPlacesRequested);
    connect(m_placesPanel, &PlacesPanel::storageTearDownExternallyRequested, this, &DolphinMainWindow::slotStorageTearDownExternallyRequested);
    DolphinUrlNavigatorsController::slotPlacesPanelVisibilityChanged(m_placesPanel->isVisible());

    auto actionShowAllPlaces = new QAction(QIcon::fromTheme(QStringLiteral("view-hidden")), i18nc("@item:inmenu", "Show Hidden Places"), this);
    actionShowAllPlaces->setCheckable(true);
    actionShowAllPlaces->setDisabled(true);
    actionShowAllPlaces->setWhatsThis(i18nc("@info:whatsthis",
                                            "This displays "
                                            "all places in the places panel that have been hidden. They will "
                                            "appear semi-transparent and allow you to uncheck their \"Hide\" property."));

    connect(actionShowAllPlaces, &QAction::triggered, this, [this](bool checked) {
        m_placesPanel->setShowAll(checked);
    });
    connect(m_placesPanel, &PlacesPanel::allPlacesShownChanged, actionShowAllPlaces, &QAction::setChecked);

    actionCollection()
        ->action(QStringLiteral("show_places_panel"))
        ->setWhatsThis(xi18nc("@info:whatsthis",
                              "<para>This toggles the "
                              "<emphasis>places</emphasis> panel at the left side of the window."
                              "</para><para>It allows you to go to locations you have "
                              "bookmarked and to access disk or media attached to the computer "
                              "or to the network. It also contains sections to find recently "
                              "saved files or files of a certain type.</para>"));
    placesDock->setWhatsThis(xi18nc("@info:whatsthis",
                                    "<para>This is the "
                                    "<emphasis>Places</emphasis> panel. It allows you to go to locations "
                                    "you have bookmarked and to access disk or media attached to the "
                                    "computer or to the network. It also contains sections to find "
                                    "recently saved files or files of a certain type.</para><para>"
                                    "Click on an entry to go there. Click with the right mouse button "
                                    "instead to open any entry in a new tab or new window.</para>"
                                    "<para>New entries can be added by dragging folders onto this panel. "
                                    "Right-click any section or entry to hide it. Right-click an empty "
                                    "space on this panel and select <interface>Show Hidden Places"
                                    "</interface> to display it again.</para>")
                             + panelWhatsThis);

    QAction *focusPlacesPanel = actionCollection()->addAction(QStringLiteral("focus_places_panel"));
    focusPlacesPanel->setText(i18nc("@action:inmenu View", "Focus Places Panel"));
    focusPlacesPanel->setToolTip(i18nc("@info:tooltip", "Move keyboard focus to and from the Places panel."));
    focusPlacesPanel->setIcon(QIcon::fromTheme(QStringLiteral("swap-panels")));
    actionCollection()->setDefaultShortcut(focusPlacesPanel, Qt::CTRL | Qt::Key_P);
    connect(focusPlacesPanel, &QAction::triggered, this, &DolphinMainWindow::togglePlacesPanelFocus);

    // Add actions into the "Panels" menu
    KActionMenu *panelsMenu = new KActionMenu(i18nc("@action:inmenu View", "Show Panels"), this);
    actionCollection()->addAction(QStringLiteral("panels"), panelsMenu);
    panelsMenu->setIcon(QIcon::fromTheme(QStringLiteral("view-sidetree")));
    panelsMenu->setPopupMode(QToolButton::InstantPopup);
    const KActionCollection *ac = actionCollection();
    panelsMenu->addAction(ac->action(QStringLiteral("show_places_panel")));
#if HAVE_BALOO
    panelsMenu->addAction(ac->action(QStringLiteral("show_information_panel")));
#endif
    panelsMenu->addAction(ac->action(QStringLiteral("show_folders_panel")));
    panelsMenu->addAction(ac->action(QStringLiteral("show_terminal_panel")));
    panelsMenu->addSeparator();
    panelsMenu->addAction(lockLayoutAction);
    panelsMenu->addSeparator();
    panelsMenu->addAction(actionShowAllPlaces);
    panelsMenu->addAction(focusPlacesPanel);
    panelsMenu->addAction(ac->action(QStringLiteral("focus_terminal_panel")));

    connect(panelsMenu->menu(), &QMenu::aboutToShow, this, [actionShowAllPlaces] {
        actionShowAllPlaces->setEnabled(DolphinPlacesModelSingleton::instance().placesModel()->hiddenCount());
    });
}

void DolphinMainWindow::setupFileItemActions()
{
    if (m_fileItemActions) {
        delete m_fileItemActions;
    }

    m_fileItemActions = new KFileItemActions(this);
    m_fileItemActions->setParentWidget(this);
    connect(m_fileItemActions, &KFileItemActions::error, this, [this](const QString &errorMessage) {
        showErrorMessage(errorMessage);
    });

#if KIO_VERSION >= QT_VERSION_CHECK(6, 24, 0)
    m_serviceMenuShortcutManager->refresh(m_fileItemActions);
#endif
}

void DolphinMainWindow::updateFileAndEditActions()
{
    const KFileItemList list = m_activeViewContainer->view()->selectedItems();
    const KActionCollection *col = actionCollection();
    KFileItemListProperties capabilitiesSource(list);

    QAction *renameAction = col->action(KStandardAction::name(KStandardAction::RenameFile));
    QAction *moveToTrashAction = col->action(KStandardAction::name(KStandardAction::MoveToTrash));
    QAction *deleteAction = col->action(KStandardAction::name(KStandardAction::DeleteFile));
    QAction *cutAction = col->action(KStandardAction::name(KStandardAction::Cut));
    QAction *duplicateAction = col->action(QStringLiteral("duplicate")); // see DolphinViewActionHandler
    QAction *addToPlacesAction = col->action(QStringLiteral("add_to_places"));
    QAction *copyToOtherViewAction = col->action(QStringLiteral("copy_to_inactive_split_view"));
    QAction *moveToOtherViewAction = col->action(QStringLiteral("move_to_inactive_split_view"));
    QAction *copyLocation = col->action(QStringLiteral("copy_location"));

    if (list.isEmpty()) {
        stateChanged(QStringLiteral("has_no_selection"));

        // All actions that need a selection to function can be enabled because they should trigger selection mode.
        renameAction->setEnabled(true);
        moveToTrashAction->setEnabled(true);
        deleteAction->setEnabled(true);
        cutAction->setEnabled(true);
        duplicateAction->setEnabled(true);
        addToPlacesAction->setEnabled(true);
        copyLocation->setEnabled(true);
        // Them triggering selection mode and not directly acting on selected items is signified by adding "…" to their text.
        m_actionTextHelper->textsWhenNothingIsSelectedEnabled(true);

    } else {
        m_actionTextHelper->textsWhenNothingIsSelectedEnabled(false);
        stateChanged(QStringLiteral("has_selection"));

        QAction *deleteWithTrashShortcut = col->action(QStringLiteral("delete_shortcut")); // see DolphinViewActionHandler
        QAction *showTarget = col->action(QStringLiteral("show_target"));

        if (list.length() == 1 && list.first().isDir()) {
            addToPlacesAction->setEnabled(true);
        } else {
            addToPlacesAction->setEnabled(false);
        }

        const bool enableMoveToTrash = capabilitiesSource.isLocal() && capabilitiesSource.supportsMoving();

        renameAction->setEnabled(capabilitiesSource.supportsMoving());
        m_disabledActionNotifier->setDisabledReason(renameAction, i18nc("@info", "Cannot rename: You do not have permission to rename items in this folder."));
        deleteAction->setEnabled(capabilitiesSource.supportsDeleting());
        m_disabledActionNotifier->setDisabledReason(deleteAction,
                                                    i18nc("@info", "Cannot delete: You do not have permission to remove items from this folder."));
        cutAction->setEnabled(capabilitiesSource.supportsMoving());
        m_disabledActionNotifier->setDisabledReason(cutAction, i18nc("@info", "Cannot cut: You do not have permission to move items from this folder."));
        copyLocation->setEnabled(list.length() == 1);
        showTarget->setEnabled(list.length() == 1 && list.at(0).isLink());
        duplicateAction->setEnabled(capabilitiesSource.supportsWriting());
        m_disabledActionNotifier->setDisabledReason(duplicateAction,
                                                    i18nc("@info", "Cannot duplicate here: You do not have permission to create items in this folder."));

        if (enableMoveToTrash) {
            moveToTrashAction->setEnabled(true);
            deleteWithTrashShortcut->setEnabled(false);
            m_disabledActionNotifier->clearDisabledReason(deleteWithTrashShortcut);
        } else {
            moveToTrashAction->setEnabled(false);
            deleteWithTrashShortcut->setEnabled(capabilitiesSource.supportsDeleting());
            m_disabledActionNotifier->setDisabledReason(deleteWithTrashShortcut,
                                                        i18nc("@info", "Cannot delete: You do not have permission to remove items from this folder."));
        }
    }

    if (!m_tabWidget->currentTabPage()->splitViewEnabled()) {
        // No need to set the disabled reason here, as it's obvious to the user that the reason is the split view being disabled.
        copyToOtherViewAction->setEnabled(false);
        m_disabledActionNotifier->clearDisabledReason(copyToOtherViewAction);
        moveToOtherViewAction->setEnabled(false);
        m_disabledActionNotifier->clearDisabledReason(moveToOtherViewAction);
    } else if (list.isEmpty()) {
        copyToOtherViewAction->setEnabled(false);
        m_disabledActionNotifier->setDisabledReason(copyToOtherViewAction, i18nc("@info", "Cannot copy to other view: No files selected."));
        moveToOtherViewAction->setEnabled(false);
        m_disabledActionNotifier->setDisabledReason(moveToOtherViewAction, i18nc("@info", "Cannot move to other view: No files selected."));
    } else {
        DolphinTabPage *tabPage = m_tabWidget->currentTabPage();
        KFileItem capabilitiesDestination;

        if (tabPage->primaryViewActive()) {
            capabilitiesDestination = tabPage->secondaryViewContainer()->rootItem();
        } else {
            capabilitiesDestination = tabPage->primaryViewContainer()->rootItem();
        }

        const auto destUrl = capabilitiesDestination.url();
        const bool allNotTargetOrigin = std::all_of(list.cbegin(), list.cend(), [destUrl](const KFileItem &item) {
            return item.url().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash) != destUrl;
        });

        if (!allNotTargetOrigin) {
            copyToOtherViewAction->setEnabled(false);
            m_disabledActionNotifier->setDisabledReason(copyToOtherViewAction,
                                                        i18nc("@info", "Cannot copy to other view: The other view already contains these items."));
            moveToOtherViewAction->setEnabled(false);
            m_disabledActionNotifier->setDisabledReason(moveToOtherViewAction,
                                                        i18nc("@info", "Cannot move to other view: The other view already contains these items."));
        } else if (!capabilitiesDestination.isWritable()) {
            copyToOtherViewAction->setEnabled(false);
            m_disabledActionNotifier->setDisabledReason(
                copyToOtherViewAction,
                i18nc("@info", "Cannot copy to other view: You do not have permission to write into the destination folder."));
            moveToOtherViewAction->setEnabled(false);
            m_disabledActionNotifier->setDisabledReason(
                moveToOtherViewAction,
                i18nc("@info", "Cannot move to other view: You do not have permission to write into the destination folder."));
        } else {
            copyToOtherViewAction->setEnabled(true);
            moveToOtherViewAction->setEnabled(capabilitiesSource.supportsMoving());
            m_disabledActionNotifier->setDisabledReason(
                moveToOtherViewAction,
                i18nc("@info", "Cannot move to other view: You do not have permission to move items from this folder."));
        }
    }
}

void DolphinMainWindow::updateViewActions()
{
    m_actionHandler->updateViewActions();

    QAction *toggleFilterBarAction = actionCollection()->action(QStringLiteral("toggle_filter"));
    toggleFilterBarAction->setChecked(m_activeViewContainer->isFilterBarVisible());

    QAction *restoreViewSettingsToDefault = actionCollection()->action(QStringLiteral("restore_view_settings_default"));
    restoreViewSettingsToDefault->setEnabled(!m_activeViewContainer->view()->isViewSettingsDefaults());

    updateSplitActions();
}

void DolphinMainWindow::updateGoActions()
{
    QAction *goUpAction = actionCollection()->action(KStandardAction::name(KStandardAction::Up));
    const QUrl currentUrl = m_activeViewContainer->url();
    // I think this is one of the best places to firstly be confronted
    // with a file system and its hierarchy. Talking about the root
    // directory might seem too much here but it is the question that
    // naturally arises in this context.
    goUpAction->setWhatsThis(xi18nc("@info:whatsthis",
                                    "<para>Go to "
                                    "the folder that contains the currently viewed one.</para>"
                                    "<para>All files and folders are organized in a hierarchical "
                                    "<emphasis>file system</emphasis>. At the top of this hierarchy is "
                                    "a directory that contains all data connected to this computer"
                                    "—the <emphasis>root directory</emphasis>.</para>"));
    goUpAction->setEnabled(KIO::upUrl(currentUrl) != currentUrl);
}

void DolphinMainWindow::refreshViews()
{
    setupFileItemActions();
    m_tabWidget->refreshViews();

    if (GeneralSettings::modifiedStartupSettings()) {
        updateWindowTitle();
    }

    updateSplitActions();

    Q_EMIT settingsChanged();
}

void DolphinMainWindow::connectViewSignals(DolphinViewContainer *container)
{
    connect(container, &DolphinViewContainer::showFilterBarChanged, this, &DolphinMainWindow::updateFilterBarAction);
    connect(container, &DolphinViewContainer::writeStateChanged, this, &DolphinMainWindow::slotWriteStateChanged);
    slotWriteStateChanged(container->view()->isFolderWritable());
    connect(container, &DolphinViewContainer::searchBarVisibilityChanged, this, &DolphinMainWindow::updateSearchAction);
    connect(container, &DolphinViewContainer::captionChanged, this, &DolphinMainWindow::updateWindowTitle);
    connect(container, &DolphinViewContainer::tabRequested, this, &DolphinMainWindow::openNewTab);
    connect(container, &DolphinViewContainer::activeTabRequested, this, &DolphinMainWindow::openNewTabAndActivate);

    // Make the toggled state of the selection mode actions visually follow the selection mode state of the view.
    auto toggleSelectionModeAction = actionCollection()->action(QStringLiteral("toggle_selection_mode"));
    toggleSelectionModeAction->setChecked(m_activeViewContainer->isSelectionModeEnabled());
    connect(m_activeViewContainer, &DolphinViewContainer::selectionModeChanged, toggleSelectionModeAction, &QAction::setChecked);

    const DolphinView *view = container->view();
    connect(view, &DolphinView::selectionChanged, this, &DolphinMainWindow::slotSelectionChanged);
    connect(view, &DolphinView::requestItemInfo, this, &DolphinMainWindow::requestItemInfo);
    connect(view, &DolphinView::fileItemsChanged, this, &DolphinMainWindow::fileItemsChanged);
    connect(view, &DolphinView::tabRequested, this, &DolphinMainWindow::openNewTab);
    connect(view, &DolphinView::activeTabRequested, this, &DolphinMainWindow::openNewTabAndActivate);
    connect(view, &DolphinView::windowRequested, this, &DolphinMainWindow::openNewWindow);
    connect(view, &DolphinView::requestContextMenu, this, &DolphinMainWindow::openContextMenu);
    connect(view, &DolphinView::directoryLoadingStarted, this, &DolphinMainWindow::enableStopAction);
    connect(view, &DolphinView::directoryLoadingCompleted, this, &DolphinMainWindow::disableStopAction);
    connect(view, &DolphinView::directoryLoadingCompleted, this, &DolphinMainWindow::slotDirectoryLoadingCompleted);
    connect(view, &DolphinView::goBackRequested, this, &DolphinMainWindow::goBack);
    connect(view, &DolphinView::goForwardRequested, this, &DolphinMainWindow::goForward);
    connect(view, &DolphinView::urlActivated, this, &DolphinMainWindow::handleUrl);
    connect(view, &DolphinView::goUpRequested, this, &DolphinMainWindow::goUp);
    connect(view, &DolphinView::doubleClickViewBackground, this, &DolphinMainWindow::slotDoubleClickViewBackground);
    connect(view, &DolphinView::clickViewBackground, this, &DolphinMainWindow::slotClickViewBackground);

    connect(container->urlNavigatorInternalWithHistory(), &KUrlNavigator::urlChanged, this, &DolphinMainWindow::changeUrl);
    connect(container->urlNavigatorInternalWithHistory(), &KUrlNavigator::historyChanged, this, &DolphinMainWindow::updateHistory);

    auto navigators = static_cast<DolphinNavigatorsWidgetAction *>(actionCollection()->action(QStringLiteral("url_navigators")));
    const KUrlNavigator *navigator =
        m_tabWidget->currentTabPage()->primaryViewActive() ? navigators->primaryUrlNavigator() : navigators->secondaryUrlNavigator();

    QAction *editableLocactionAction = actionCollection()->action(QStringLiteral("editable_location"));
    editableLocactionAction->setChecked(navigator->isUrlEditable());
    connect(navigator, &KUrlNavigator::editableStateChanged, this, &DolphinMainWindow::slotEditableStateChanged);
    connect(navigator, &KUrlNavigator::tabRequested, this, &DolphinMainWindow::openNewTab);
    connect(navigator, &KUrlNavigator::activeTabRequested, this, &DolphinMainWindow::openNewTabAndActivate);
    connect(navigator, &KUrlNavigator::newWindowRequested, this, &DolphinMainWindow::openNewWindow);

    connect(view, &DolphinView::viewSettingsChanged, this, [this](bool isDefault) {
        QAction *restoreViewSettingsToDefault = actionCollection()->action(QStringLiteral("restore_view_settings_default"));
        restoreViewSettingsToDefault->setEnabled(!isDefault);
    });
}

void DolphinMainWindow::updateSplitActions()
{
    QAction *popoutSplitAction = actionCollection()->action(QStringLiteral("popout_split_view"));

    auto setActionPopupMode = [this](KActionMenu *action, QToolButton::ToolButtonPopupMode popupMode) {
        action->setPopupMode(popupMode);
        if (auto *buttonForAction = qobject_cast<QToolButton *>(toolBar()->widgetForAction(action))) {
            buttonForAction->setPopupMode(popupMode);
        }
    };

    const DolphinTabPage *tabPage = m_tabWidget->currentTabPage();
    if (tabPage->splitViewEnabled()) {
        using Choice = GeneralSettings::EnumCloseSplitViewChoice;
        switch (GeneralSettings::closeSplitViewChoice()) {
        case Choice::ActiveView:
            if (tabPage->primaryViewActive()) {
                m_splitViewAction->setText(i18nc("@action:intoolbar Close left view", "Close"));
                m_splitViewAction->setToolTip(i18nc("@info View refer here to split view", "Close Left View"));
                m_splitViewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-left-close")));
                m_splitViewMenuAction->setText(i18nc("@action:inmenu View refer here to split view", "Close Left View"));

                popoutSplitAction->setText(i18nc("@action:intoolbar Move left split view to a new window", "Pop out Left View"));
                popoutSplitAction->setToolTip(i18nc("@info View refer here to split view", "Move left split view to a new window"));
            } else {
                m_splitViewAction->setText(i18nc("@action:intoolbar Close right view", "Close"));
                m_splitViewAction->setToolTip(i18nc("@info View refer here to split view", "Close Right View"));
                m_splitViewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-right-close")));
                m_splitViewMenuAction->setText(i18nc("@action:inmenu View refer here to split view", "Close Right View"));

                popoutSplitAction->setText(i18nc("@action:intoolbar Move right split view to a new window", "Pop out Right View"));
                popoutSplitAction->setToolTip(i18nc("@info View refer here to split view", "Move right split view to a new window"));
            }
            break;
        case Choice::InactiveView:
            if (!tabPage->primaryViewActive()) {
                m_splitViewAction->setText(i18nc("@action:intoolbar Close left view", "Close"));
                m_splitViewAction->setToolTip(i18nc("@info View refer here to split view", "Close Left View"));
                m_splitViewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-left-close")));
                m_splitViewMenuAction->setText(i18nc("@action:inmenu View refer here to split view", "Close Left View"));

                popoutSplitAction->setText(i18nc("@action:intoolbar Move left split view to a new window", "Pop out Left View"));
                popoutSplitAction->setToolTip(i18nc("@info View refer here to split view", "Move left split view to a new window"));
            } else {
                m_splitViewAction->setText(i18nc("@action:intoolbar Close right view", "Close"));
                m_splitViewAction->setToolTip(i18nc("@info View refer here to split view", "Close Right View"));
                m_splitViewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-right-close")));
                m_splitViewMenuAction->setText(i18nc("@action:inmenu View refer here to split view", "Close Right View"));

                popoutSplitAction->setText(i18nc("@action:intoolbar Move right split view to a new window", "Pop out Right View"));
                popoutSplitAction->setToolTip(i18nc("@info View refer here to split view", "Move right split view to a new window"));
            }
            break;
        case Choice::RightView:
            m_splitViewAction->setText(i18nc("@action:intoolbar Close right view", "Close"));
            m_splitViewAction->setToolTip(i18nc("@info View refer here to split view", "Close Right View"));
            m_splitViewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-right-close")));
            m_splitViewMenuAction->setText(i18nc("@action:inmenu View refer here to split view", "Close Right View"));

            popoutSplitAction->setText(i18nc("@action:intoolbar Move right split view to a new window", "Pop out Right View"));
            popoutSplitAction->setToolTip(i18nc("@info View refer here to split view", "Move right split view to a new window"));
            break;
        default:
            Q_UNREACHABLE();
        }
        popoutSplitAction->setEnabled(true);
        if (!m_splitViewAction->menu()) {
            setActionPopupMode(m_splitViewAction, QToolButton::MenuButtonPopup);
            m_splitViewAction->setMenu(new QMenu(this));
            m_splitViewAction->addAction(popoutSplitAction);
        }
    } else {
        m_splitViewAction->setText(i18nc("@action:intoolbar Split view", "Split"));
        m_splitViewMenuAction->setText(m_splitViewAction->text());
        m_splitViewAction->setToolTip(i18nc("@info", "Split view"));
        m_splitViewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
        popoutSplitAction->setText(i18nc("@action:intoolbar Move view in focus to a new window", "Pop out"));
        popoutSplitAction->setEnabled(false);
        if (m_splitViewAction->menu()) {
            m_splitViewAction->removeAction(popoutSplitAction);
            m_splitViewAction->menu()->deleteLater();
            m_splitViewAction->setMenu(nullptr);
            setActionPopupMode(m_splitViewAction, QToolButton::DelayedPopup);
        }
    }

    // Update state from toolbar action
    m_splitViewMenuAction->setToolTip(m_splitViewAction->toolTip());
    m_splitViewMenuAction->setIcon(m_splitViewAction->icon());
}

void DolphinMainWindow::updateAllowedToolbarAreas()
{
    auto navigators = static_cast<DolphinNavigatorsWidgetAction *>(actionCollection()->action(QStringLiteral("url_navigators")));
    if (toolBar()->actions().contains(navigators)) {
        toolBar()->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
        if (toolBarArea(toolBar()) == Qt::LeftToolBarArea || toolBarArea(toolBar()) == Qt::RightToolBarArea) {
            addToolBar(Qt::TopToolBarArea, toolBar());
        }
    } else {
        toolBar()->setAllowedAreas(Qt::AllToolBarAreas);
    }
}

void DolphinMainWindow::updateNavigatorsBackground()
{
    auto navigators = static_cast<DolphinNavigatorsWidgetAction *>(actionCollection()->action(QStringLiteral("url_navigators")));
    navigators->setBackgroundEnabled(navigators->isInToolbar());
}

bool DolphinMainWindow::isKompareInstalled() const
{
    static bool initialized = false;
    static bool installed = false;
    if (!initialized) {
        // TODO: maybe replace this approach later by using a menu
        // plugin like kdiff3plugin.cpp
        installed = !QStandardPaths::findExecutable(QStringLiteral("kompare")).isEmpty();
        initialized = true;
    }
    return installed;
}

void DolphinMainWindow::createPanelAction(const QIcon &icon, const QKeySequence &shortcut, QDockWidget *dockWidget, const QString &actionName)
{
    auto dockAction = dockWidget->toggleViewAction();
    dockAction->setIcon(icon);
    dockAction->setEnabled(true);

    QAction *panelAction = actionCollection()->addAction(actionName, dockAction);
    actionCollection()->setDefaultShortcut(panelAction, shortcut);
}
// clang-format off
void DolphinMainWindow::setupWhatsThis()
{
    // main widgets
    menuBar()->setWhatsThis(xi18nc("@info:whatsthis", "<para>This is the "
        "<emphasis>Menubar</emphasis>. It provides access to commands and "
        "configuration options. Left-click on any of the menus on this "
        "bar to see its contents.</para><para>The Menubar can be hidden "
        "by unchecking <interface>Settings|Show Menubar</interface>. Then "
        "most of its contents become available through a <interface>Menu"
        "</interface> button on the <emphasis>Toolbar</emphasis>.</para>"));
    toolBar()->setWhatsThis(xi18nc("@info:whatsthis", "<para>This is the "
        "<emphasis>Toolbar</emphasis>. It allows quick access to "
        "frequently used actions.</para><para>It is highly customizable. "
        "All items you see in the <interface>Menu</interface> or "
        "in the <interface>Menubar</interface> can be placed on the "
        "Toolbar. Just right-click on it and select <interface>Configure "
        "Toolbars…</interface> or find this action within the <interface>"
        "menu</interface>."
        "</para><para>The location of the bar and the style of its "
        "buttons can also be changed in the right-click menu. Right-click "
        "a button if you want to show or hide its text.</para>"));
    m_tabWidget->setWhatsThis(xi18nc("@info:whatsthis main view",
        "<para>Here you can see the <emphasis>folders</emphasis> and "
        "<emphasis>files</emphasis> that are at the location described in "
        "the <interface>Location Bar</interface> above. This area is the "
        "central part of this application where you navigate to the files "
        "you want to use.</para><para>For an elaborate and general "
        "introduction to this application <link "
        "url='https://userbase.kde.org/Dolphin/File_Management#Introduction_to_Dolphin'>"
        "click here</link>. This will open an introductory article from "
        "the <emphasis>KDE UserBase Wiki</emphasis>.</para><para>For brief "
        "explanations of all the features of this <emphasis>view</emphasis> "
        "<link url='help:/dolphin/dolphin-view.html'>click here</link> "
        "instead. This will open a page from the <emphasis>Handbook"
        "</emphasis> that covers the basics.</para>"));

    // Settings menu
    actionCollection()->action(KStandardAction::name(KStandardAction::KeyBindings))
        ->setWhatsThis(xi18nc("@info:whatsthis","<para>This opens a window "
        "that lists the <emphasis>keyboard shortcuts</emphasis>.<nl/>"
        "There you can set up key combinations to trigger an action when "
        "they are pressed simultaneously. All commands in this application can "
        "be triggered this way.</para>"));
    actionCollection()->action(KStandardAction::name(KStandardAction::ConfigureToolbars))
        ->setWhatsThis(xi18nc("@info:whatsthis","<para>This opens a window in which "
        "you can change which buttons appear on the <emphasis>Toolbar</emphasis>.</para>"
        "<para>All items you see in the <interface>Menu</interface> can also be placed on the Toolbar.</para>"));
    actionCollection()->action(KStandardAction::name(KStandardAction::Preferences))
        ->setWhatsThis(xi18nc("@info:whatsthis","This opens a window where you can "
        "change a multitude of settings for this application. For an explanation "
        "of the various settings go to the chapter <emphasis>Configuring Dolphin"
        "</emphasis> in <interface>Help|Dolphin Handbook</interface>."));

    // Help menu

    auto setStandardActionWhatsThis = [this](KStandardAction::StandardAction actionId,
                                             const QString &whatsThis) {
        // Check for the existence of an action since it can be restricted through the Kiosk system
        if (auto *action = actionCollection()->action(KStandardAction::name(actionId))) {
            action->setWhatsThis(whatsThis);
        }
    };

    // i18n: If the external link isn't available in your language it might make
    // sense to state the external link's language in brackets to not
    // frustrate the user. If there are multiple languages that the user might
    // know with a reasonable chance you might want to have 2 external links.
    // The same might be true for any external link you translate.
    setStandardActionWhatsThis(KStandardAction::HelpContents, xi18nc("@info:whatsthis handbook", "<para>This opens the Handbook for this application. It provides explanations for every part of <emphasis>Dolphin</emphasis>.</para><para>If you want more elaborate introductions to the different features of <emphasis>Dolphin</emphasis> <link url='https://userbase.kde.org/Dolphin/File_Management'>click here</link>. It will open the dedicated page in the KDE UserBase Wiki.</para>"));
    // (The i18n call should be completely in the line following the i18n: comment without any line breaks within the i18n call or the comment might not be correctly extracted. See: https://commits.kde.org/kxmlgui/a31135046e1b3335b5d7bbbe6aa9a883ce3284c1 )

    setStandardActionWhatsThis(KStandardAction::WhatsThis,
        xi18nc("@info:whatsthis whatsthis button",
        "<para>This is the button that invokes the help feature you are "
        "using right now! Click it, then click any component of this "
        "application to ask \"What's this?\" about it. The mouse cursor "
        "will change appearance if no help is available for a spot.</para>"
        "<para>There are two other ways to get help: "
        "The <link url='help:/dolphin/index.html'>Dolphin Handbook</link> and "
        "the <link url='https://userbase.kde.org/Dolphin/File_Management'>KDE "
        "UserBase Wiki</link>.</para><para>The \"What's this?\" help is "
        "missing in most other windows so don't get too used to this.</para>"));

    setStandardActionWhatsThis(KStandardAction::ReportBug,
        xi18nc("@info:whatsthis","<para>This opens a "
        "window that will guide you through reporting errors or flaws "
        "in this application or in other KDE software.</para>"
        "<para>High-quality bug reports are much appreciated. To learn "
        "how to make your bug report as effective as possible "
        "<link url='https://community.kde.org/Get_Involved/Bug_Reporting'>"
        "click here</link>.</para>"));

    setStandardActionWhatsThis(KStandardAction::Donate,
        xi18nc("@info:whatsthis", "<para>This opens a "
        "<emphasis>web page</emphasis> where you can donate to "
        "support the continued work on this application and many "
        "other projects by the <emphasis>KDE</emphasis> community.</para>"
        "<para>Donating is the easiest and fastest way to efficiently "
        "support KDE and its projects. KDE projects are available for "
        "free therefore your donation is needed to cover things that "
        "require money like servers, contributor meetings, etc.</para>"
        "<para><emphasis>KDE e.V.</emphasis> is the non-profit "
        "organization behind the KDE community.</para>"));

    setStandardActionWhatsThis(KStandardAction::SwitchApplicationLanguage,
        xi18nc("@info:whatsthis",
        "With this you can change the language this application uses."
        "<nl/>You can even set secondary languages which will be used "
        "if texts are not available in your preferred language."));

    setStandardActionWhatsThis(KStandardAction::AboutApp,
        xi18nc("@info:whatsthis","This opens a "
        "window that informs you about the version, license, "
        "used libraries and maintainers of this application."));

    setStandardActionWhatsThis(KStandardAction::AboutKDE,
        xi18nc("@info:whatsthis","This opens a "
        "window with information about <emphasis>KDE</emphasis>. "
        "The KDE community are the people behind this free software."
        "<nl/>If you like using this application but don't know "
        "about KDE or want to see a cute dragon have a look!"));
}
// clang-format on

bool DolphinMainWindow::addHamburgerMenuToToolbar()
{
    QDomDocument domDocument = KXMLGUIClient::domDocument();
    if (domDocument.isNull()) {
        return false;
    }
    QDomNode toolbar = domDocument.elementsByTagName(QStringLiteral("ToolBar")).at(0);
    if (toolbar.isNull()) {
        return false;
    }

    QDomElement hamburgerMenuElement = domDocument.createElement(QStringLiteral("Action"));
    hamburgerMenuElement.setAttribute(QStringLiteral("name"), QStringLiteral("hamburger_menu"));
    toolbar.appendChild(hamburgerMenuElement);

    KXMLGUIFactory::saveConfigFile(domDocument, xmlFile());
    reloadXML();
    createGUI();
    return true;
    // Make sure to also remove the <KXMLGUIFactory> and <QDomDocument> include
    // whenever this method is removed (maybe in the year ~2026).
}

// Set a sane initial window size
QSize DolphinMainWindow::sizeHint() const
{
    return KXmlGuiWindow::sizeHint().expandedTo(QSize(760, 550));
}

// dolphin-ng: with the sort bar shown, the buttons that the toolbar has before the location bar (back, forward, up
// and whatever the user put there) go to the start of the sort bar, next to the sorting and the filters, and the
// space above the panels stays empty. The view mode button is dropped: the sort bar has the cards button, the
// menu has the rest. Without the sort bar (classic list) they go back where they were.
// dolphin-ng: the sidebar button's icon, drawn here in the NG outline style: a window with the sidebar column on
// its left, filled while the sidebar is shown and empty while it is hidden
static QIcon ngSidebarIcon(bool shown, const QColor &ink, bool right)
{
    QIcon icon;
    for (const int size : {16, 22, 24, 32, 48}) {
        for (const qreal dpr : {1.0, 2.0}) {
            QPixmap pixmap(QSize(size, size) * dpr);
            pixmap.setDevicePixelRatio(dpr);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            const qreal k = size / 24.0;
            painter.scale(k, k);
            const QRectF frame(3, 5, 18, 14);
            QPainterPath outline;
            outline.addRoundedRect(frame, 3.2, 3.2);
            if (shown) {
                QColor fill = ink;
                fill.setAlphaF(0.55);
                painter.save();
                painter.setClipPath(outline);
                painter.fillRect(right ? QRectF(frame.right() - 6.5, frame.top(), 6.5, frame.height()) : QRectF(frame.left(), frame.top(), 6.5, frame.height()), fill);
                painter.restore();
            }
            QPen pen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(outline);
            const qreal edge = right ? frame.right() - 6.5 : frame.left() + 6.5; // (the Information panel: on the right)
            painter.drawLine(QPointF(edge, frame.top()), QPointF(edge, frame.bottom()));
            painter.end();
            icon.addPixmap(pixmap);
        }
    }
    return icon;
}

// dolphin-ng: a button first in the toolbar, above the panel, that shows and hides it (as F9 does)
void DolphinMainWindow::ngPlaceSidebarToggle()
{
    KToolBar *bar = toolBar(QStringLiteral("mainToolBar"));
    if (bar) {
        bar->setProperty("_breeze_ng_no_text_shadow", true); // (as the sort bar: its buttons have sheets of their own)
    }
    QAction *places = actionCollection()->action(QStringLiteral("show_places_panel"));
    auto *dock = findChild<QDockWidget *>(QStringLiteral("placesDock"));
    if (!bar || !places || !dock) {
        return;
    }
    if (!m_ngSidebarToggle) {
        m_ngSidebarToggle = new QAction(ngTr("Sidebar", "Barra laterale"), this);
        m_ngSidebarToggle->setToolTip(ngTr("Show or hide the sidebar (F9)", "Mostra o nascondi la barra laterale (F9)"));
        m_ngSidebarToggle->setPriority(QAction::LowPriority); // the icon alone, the name in the tooltip
        connect(m_ngSidebarToggle, &QAction::triggered, places, &QAction::trigger);
        auto update = [this, dock] {
            m_ngSidebarToggle->setIcon(ngSidebarIcon(dock->isVisible(), palette().color(QPalette::WindowText), false));
        };
        connect(dock, &QDockWidget::visibilityChanged, this, update);
        update();
    }
    if (m_ngTitleTabs && m_ngTabsToolBar) {
        // with the tabs in the title bar the button comes first in that row, before the tabs
        if (bar->actions().contains(m_ngSidebarToggle)) {
            bar->removeAction(m_ngSidebarToggle);
        }
        if (!m_ngTitleToggle) {
            auto *button = new QToolButton(m_ngTabsToolBar);
            button->setDefaultAction(m_ngSidebarToggle);
            button->setAutoRaise(true);
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setIconSize(QSize(20, 20));
            button->setFocusPolicy(Qt::NoFocus);
            // in a holder of its own: the row centres each of its widgets, and the button came out 2 px above the
            // tabs' pills. The holder is as tall as the tab bar and puts the button on the tab's centre (measured
            // again when the row changes)
            auto *holder = new QWidget(m_ngTabsToolBar);
            auto *holderLayout = new QVBoxLayout(holder);
            holderLayout->setContentsMargins(0, 0, 0, 0);
            holderLayout->setSpacing(0);
            holderLayout->addWidget(button);
            m_ngTitleToggle = button;
            // first in the row; with the window's buttons at the left (macOS style) after them, before the tabs, in that
            // row's own layout
            if (m_ngMacRowLayout) {
                m_ngMacRowLayout->insertWidget(m_ngMacButtons ? 1 : 0, holder, 0, Qt::AlignVCenter);
            } else {
                m_ngTabsToolBar->insertWidget(m_ngTabsToolBar->actions().value(0), holder);
            }
            auto align = [this, holder, holderLayout, button] {
                auto *tabs = m_ngTabsToolBar ? m_ngTabsToolBar->findChild<QTabBar *>() : nullptr;
                if (!tabs || tabs->count() == 0 || tabs->height() <= 0) {
                    return;
                }
                // the holder as tall as the tab bar (the row puts both at the same height), the button inside it
                // centred on the tab
                const QRect tab = tabs->tabRect(qMax(0, tabs->currentIndex()));
                // the pill as drawn (the style draws it in a frame of its own, a little lower than the tab's rect):
                // the current tab painted aside, its rows with colour in the middle column
                qreal centre = tab.top() + tab.height() / 2.0;
                const QImage shot = tabs->grab(tab).toImage();
                if (!shot.isNull()) {
                    const qreal dpr = shot.devicePixelRatio();
                    const int x = qMin(shot.width() - 1, qRound(12 * dpr)); // (inside the pill, before its text)
                    // the run of rows in the pill's own colour around a row well inside it (the current tab also
                    // runs on into the window below, in another colour: not part of the pill)
                    const int middle = qBound(0, int(shot.height() * 0.4), shot.height() - 1);
                    const QRgb inside = shot.pixel(x, middle);
                    auto same = [inside](QRgb p) {
                        return qAbs(qAlpha(p) - qAlpha(inside)) + qAbs(qRed(p) - qRed(inside)) + qAbs(qGreen(p) - qGreen(inside)) + qAbs(qBlue(p) - qBlue(inside)) <= 24;
                    };
                    int first = middle, last = middle;
                    while (first > 0 && same(shot.pixel(x, first - 1))) {
                        --first;
                    }
                    while (last < shot.height() - 1 && same(shot.pixel(x, last + 1))) {
                        ++last;
                    }
                    if (first >= 0 && last > first) {
                        centre = tab.top() + (first + last + 1) / 2.0 / dpr;
                    }
                }
                // the holder as tall as the row, centred as the tab bar is: the same middle. The button inside it
                // centred on the pill (the button is taller than the tab bar, which its tabs overflow)
                // (no taller than the tab bar: as tall as the row, it had no room to move and stayed below the pills)
                const int buttonHeight = qMin(button->sizeHint().height(), qMax(24, tabs->height()));
                const int rowHeight = qMax(buttonHeight, 2 * tabs->y() + tabs->height());
                // (where the row put the holder, once it has its height; before, where centring would put it)
                const qreal holderTop = holder->height() == rowHeight ? holder->y() : tabs->y() + tabs->height() / 2.0 - rowHeight / 2.0;
                const int top = qBound(0, qRound(tabs->y() + centre - holderTop - buttonHeight / 2.0), rowHeight - buttonHeight);
                const QMargins wanted(0, top, 0, rowHeight - buttonHeight - top);
                if (holder->height() != rowHeight || holderLayout->contentsMargins() != wanted) {
                    holder->setFixedHeight(rowHeight);
                    button->setFixedHeight(buttonHeight);
                    holderLayout->setContentsMargins(wanted);
                }
            };
            QTimer::singleShot(0, holder, align);
            auto *watch = new NgGeometryWatch(holder);
            QList<QWidget *> watched{m_ngTabsToolBar, holder};
            if (auto *tabs = m_ngTabsToolBar->findChild<QTabBar *>()) {
                watched.append(tabs);
            }
            watch->watch(watched, align);
        }
        return;
    }
    if (!bar->actions().contains(m_ngSidebarToggle)) {
        bar->insertAction(bar->actions().value(0), m_ngSidebarToggle);
    }
}

void DolphinMainWindow::ngMoveToolbarButtons()
{
    ngPlaceSidebarToggle();
    KToolBar *bar = toolBar(QStringLiteral("mainToolBar"));
    QAction *navigator = actionCollection()->action(QStringLiteral("url_navigators"));
    if (!m_ngSortBar || !bar || !navigator || !bar->actions().contains(navigator)) {
        return;
    }
    if (!ngTwoLineList()) {
        if (!m_ngMovedActions.isEmpty()) {
            bar->insertActions(navigator, m_ngMovedActions);
            m_ngMovedActions.clear();
            ngSortBarSetLeadingActions(m_ngSortBar, {});
        }
        if (!m_ngMovedTrailing.isEmpty()) {
            ngSortBarSetTrailingActions(m_ngSortBar, {});
            bar->addActions(m_ngMovedTrailing);
            m_ngMovedTrailing.clear();
        }
        if (m_ngFilterAction) {
            m_ngFilterAction->setVisible(false);
        }
        return;
    }
    {
        // what stood after the location bar (split, search, the menu) goes to the end of the sort bar, and the
        // field that filters the folder comes up next to the location bar: address and search side by side
        QList<QAction *> trailing;
        bool after = false;
        const QList<QAction *> all = bar->actions();
        for (QAction *action : all) {
            if (action == navigator) {
                after = true;
            } else if (after && action != m_ngFilterAction && action != bar->property("_ng_filter_gap").value<QObject *>()) {
                // (not the gap before the field: a later pass took it for one of the buttons and moved it away with
                // them, and the field ended up against the location bar)
                trailing.append(action);
            }
        }
        for (QAction *action : std::as_const(trailing)) {
            bar->removeAction(action);
        }
        if (!trailing.isEmpty()) {
            m_ngMovedTrailing += trailing;
            ngSortBarSetTrailingActions(m_ngSortBar, m_ngMovedTrailing);
        }
        if (!m_ngFilterAction) {
            if (QWidget *field = ngSortBarFilterField(m_ngSortBar)) {
                auto *gap = new QWidget(bar); // a little air between the location bar and the field
                gap->setFixedWidth(12);
                bar->setProperty("_ng_filter_gap", QVariant::fromValue<QObject *>(bar->addWidget(gap)));
                field->setFixedWidth(240);
                if (!ngWidgetStyleIsNg()) {
                    field->setStyle(ngFieldStyle()); // (the sort bar's soft field, see NgFieldStyle)
                }
                m_ngFilterAction = bar->addWidget(field);
            }
        }
        if (m_ngFilterAction) {
            m_ngFilterAction->setVisible(true);
        }
    }
    // back and forward stay before the location bar, with "up" beside them; the view settings button, which the
    // sort bar replaces, leaves the tool bar
    QAction *viewSettings = actionCollection()->action(QStringLiteral("view_settings"));
    const QList<QAction *> actions = bar->actions();
    for (QAction *action : actions) {
        if (action == navigator) {
            break;
        }
        if (action == viewSettings) {
            bar->removeAction(action);
            if (!m_ngMovedActions.contains(action)) {
                m_ngMovedActions.append(action);
            }
        }
    }
    if (QAction *up = actionCollection()->action(QStringLiteral("go_up")); up && !bar->actions().contains(up) && !m_ngUpPlaced) {
        m_ngUpPlaced = true;
        bar->insertAction(navigator, up);
    }
    ngSortBarSetLeadingActions(m_ngSortBar, {});
    // back, forward and up as one pill of glass, the veil of every other button (Breeze NG's setting in Aurora)
    {
        const bool dark = ngIsDark(palette());
        // (Aurora on a light scheme: frost, white and denser, not black ink)
        // (an opaque light look: the pill is white like the fields beside it, not a grey veil of ink)
        // (opaque light looks: Soft Light and Grey, whose fields are white too; not Glass, where it is a veil)
        const bool glassLook = property("_breeze_ng_glass").toBool()
            || KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString()).contains(QLatin1String("Glass"));
        const bool tonedLight = !dark && !ngAurora() && !glassLook;
        const QString ink = (dark || ngAurora() || tonedLight) ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
        const qreal veil = ngAurora() ? ngAuroraVeil(dark, true).alphaF() : tonedLight ? 0.78 : 0.06;
        QList<QWidget *> pill;
        for (const char *name : {"go_back", "go_forward", "go_up"}) {
            QAction *action = actionCollection()->action(QString::fromLatin1(name));
            QWidget *button = action ? bar->widgetForAction(action) : nullptr;
            if (button && action->isVisible()) { // (the button of an action added a moment ago is not shown yet)
                pill.append(button);
            }
        }
        // the pill and the filter field are as tall as the location bar and rounded like it: one row, one style
        QWidget *location = bar->widgetForAction(navigator);
        const int height = location && location->sizeHint().height() > 20 ? location->sizeHint().height() : 34;
        if (location) {
            location->setFixedHeight(height);
        }
        setProperty("_ng_row_height", height);
        // the sort bar's first item ("Sort by") starts under the back arrow, the first thing in the pill above it (at
        // the pill's own edge it looked out of line: the pill's round end and faint veil sit further in than its box).
        // Measured once both are laid out, and again whenever either moves (the sidebar, a resize)
        if (!pill.isEmpty() && m_ngSortBar && m_ngSortBar->layout()) {
            QPointer<QWidget> first = pill.first();
            auto align = [this, first] {
                if (!first || !m_ngSortBar || !m_ngSortBar->layout() || !first->isVisible()) {
                    return;
                }
                int inset = 0;
                const bool leadPill = m_ngSortBar->property("_ng_lead_pill").toBool(); // (a pill under a pill: edge to edge)
                if (auto *button = qobject_cast<QToolButton *>(first.data()); button && !button->icon().isNull() && !leadPill) {
                    inset = qMax(0, (button->width() - button->iconSize().width()) / 2);
                }
                QMargins margins = m_ngSortBar->layout()->contentsMargins();
                // (a pill: the edge of the navigation's own pill, which holds the back button)
                const QWidget *edge = leadPill && first->parentWidget() && first->parentWidget()->objectName() == QLatin1String("navPill") ? first->parentWidget() : first.data();
                const int navLeft = edge->mapTo(this, QPoint(0, 0)).x() + inset;
                const int barLeft = m_ngSortBar->mapTo(this, QPoint(0, 0)).x();
                const int left = qMax(0, navLeft - barLeft);
                if (margins.left() != left) {
                    margins.setLeft(left);
                    m_ngSortBar->layout()->setContentsMargins(margins);
                }
            };
            QTimer::singleShot(0, this, align);
            if (!m_ngAlignWatch) {
                m_ngAlignWatch = new NgGeometryWatch(this);
            }
            m_ngAlignWatch->watch({first.data(), m_ngSortBar}, align);
        }
        ngSortBarSetRowHeight(m_ngSortBar, height); // (the bar's buttons below, the same height)
        if (m_ngFilterAction) {
            if (QWidget *field = bar->widgetForAction(m_ngFilterAction)) {
                // as tall as the row of the tool bar, like the location bar beside it (a fixed height taken from the
                // location bar's size hint stood taller than it where the tool bar lays it out lower)
                // (and never taller than the location bar itself: where something else made the row taller, the
                // field grew with the row and stood above and below the location bar)
                // (one height for the whole row: the location bar, this field and the pill of back and forward, from
                // the location bar's own size hint; the field grew a pixel or two past the location bar otherwise)
                field->setFixedHeight(height);
            }
        }
        // Glass (not Aurora): the pill is a field like the location bar and the search field beside it, the window's
        // field colour at the glass' strength with the same faint rim (it was a grey veil of ink, a third kind of thing)
        const bool otherStyle = !ngWidgetStyleIsNg(); // (stock Breeze...: the pill takes the fields' look, ngFieldColours)
        const bool glassField = (glassLook && !ngAurora()) || otherStyle;
        const QColor base = palette().color(QPalette::Base);
        // (the fields' colour in Glass: the field colour on a dark scheme, a grey veil of ink on a light one)
        const QString fieldRgb = dark ? QStringLiteral("%1, %2, %3").arg(base.red()).arg(base.green()).arg(base.blue()) : QStringLiteral("0, 0, 0");
        const QString fieldA = dark ? QStringLiteral("0.62") : QStringLiteral("0.07"), fieldHover = dark ? QStringLiteral("0.80") : QStringLiteral("0.12"),
                      fieldDown = dark ? QStringLiteral("0.92") : QStringLiteral("0.17");
        const QString rim = dark ? QStringLiteral("rgba(255, 255, 255, 0.13)") : QStringLiteral("rgba(0, 0, 0, 0.13)");
        for (int i = 0; i < pill.size(); ++i) {
            // round ends, half the height: a pill like every other control of the bars (9 px corners read as a box
            // beside the round pills below)
            const QString round = QStringLiteral("%1px").arg(otherStyle ? ngFieldRadius : height / 2);
            const QString left = i == 0 ? round : QStringLiteral("0");
            const QString right = i == pill.size() - 1 ? round : QStringLiteral("0");
            if (otherStyle) {
                QColor rest, restEdge, over, overEdge, down, downEdge;
                ngFieldColours(&rest, &restEdge, 0);
                ngFieldColours(&over, &overEdge, 1);
                ngFieldColours(&down, &downEdge, 2);
                const QString sheet = QStringLiteral("QToolButton { background: %1; border: 1px solid %2; border-left-width: %3; border-right-width: %4;"
                                                     " padding: 0 8px; min-height: 0; max-height: %5px; margin: 0 %6px 0 0;"
                                                     " border-top-left-radius: %7; border-bottom-left-radius: %7; border-top-right-radius: %8; border-bottom-right-radius: %8; }"
                                                     "QToolButton:hover { background: %9; }"
                                                     "QToolButton:pressed { background: %10; }"
                                                     "QToolButton::menu-indicator { image: none; }")
                                          .arg(ngRgba(rest), ngRgba(restEdge), i == 0 ? QStringLiteral("1px") : QStringLiteral("0"), i == pill.size() - 1 ? QStringLiteral("1px") : QStringLiteral("0"))
                                          .arg(height)
                                          .arg(i == pill.size() - 1 ? 8 : 0)
                                          .arg(left, right)
                                          .arg(ngRgba(over), ngRgba(down));
                if (pill.at(i)->styleSheet() != sheet) {
                    pill.at(i)->setStyleSheet(sheet);
                }
                pill.at(i)->setFixedHeight(height);
                continue;
            }
            if (glassField) {
                // one outline around the whole pill: each button draws the sides that are the pill's
                const QString sheet = QStringLiteral("QToolButton { background: rgba(%1, %9); border: 1px solid %2; border-left-width: %3; border-right-width: %4;"
                                                     " padding: 0 8px; min-height: 0; max-height: %5px; margin: 0 %6px 0 0;"
                                                     " border-top-left-radius: %7; border-bottom-left-radius: %7; border-top-right-radius: %8; border-bottom-right-radius: %8; }"
                                                     "QToolButton:hover { background: rgba(%1, %10); }"
                                                     "QToolButton:pressed { background: rgba(%1, %11); }"
                                                     "QToolButton::menu-indicator { image: none; }")
                                          .arg(fieldRgb, rim, i == 0 ? QStringLiteral("1px") : QStringLiteral("0"), i == pill.size() - 1 ? QStringLiteral("1px") : QStringLiteral("0"))
                                          .arg(height)
                                          .arg(i == pill.size() - 1 ? 8 : 0)
                                          .arg(left, right)
                                          .arg(fieldA, fieldHover, fieldDown);
                if (pill.at(i)->styleSheet() != sheet) {
                    pill.at(i)->setStyleSheet(sheet);
                }
                pill.at(i)->setFixedHeight(height);
                continue;
            }
            const QString sheet = QStringLiteral("QToolButton { background: rgba(%1, %2); border: 0; padding: 0 8px; min-height: 0; max-height: %7px; margin: 0 %8px 0 0;"
                                                 " border-top-left-radius: %4; border-bottom-left-radius: %4; border-top-right-radius: %5; border-bottom-right-radius: %5; }"
                                                 "QToolButton:hover { background: rgba(%1, %3); }"
                                                 "QToolButton:pressed { background: rgba(%1, %6); }"
                                                 "QToolButton::menu-indicator { image: none; }")
                                      .arg(ink, QString::number(veil, 'f', 3), QString::number(veil + 0.08, 'f', 3), left, right, QString::number(veil + 0.14, 'f', 3))
                                      .arg(height)
                                      .arg(i == pill.size() - 1 ? 8 : 0); // (air between the pill and the location bar)
            if (pill.at(i)->styleSheet() != sheet) {
                pill.at(i)->setStyleSheet(sheet);
            }
            pill.at(i)->setFixedHeight(height); // (the style sheet's own height came out taller than the location bar)
        }
        // with another widget style than Breeze NG: Root (and any other button before the location bar) as a pill of its
        // own beside back/forward/up, its name in the text colour; stock Breeze drew it faint on the window's ground
        if (!ngWidgetStyleIsNg()) {
            if (QAction *root = actionCollection()->action(QStringLiteral("go_root"))) {
                if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(root))) {
                    QColor rest, restEdge, over, overEdge, down, downEdge;
                    ngFieldColours(&rest, &restEdge, 0);
                    ngFieldColours(&over, &overEdge, 1);
                    ngFieldColours(&down, &downEdge, 2);
                    const QString sheet = QStringLiteral("QToolButton { background: %1; border: 1px solid %2; border-radius: %3px; padding: 0 12px 0 9px; min-height: 0; max-height: %4px;"
                                                         " margin: 0 8px 0 0; color: %5; }"
                                                         "QToolButton:hover { background: %6; }"
                                                         "QToolButton:pressed { background: %7; }")
                                              .arg(ngRgba(rest), ngRgba(restEdge))
                                              .arg(ngFieldRadius)
                                              .arg(height)
                                              .arg(palette().color(QPalette::WindowText).name(), ngRgba(over), ngRgba(down));
                    if (button->styleSheet() != sheet) {
                        button->setStyleSheet(sheet);
                    }
                    button->setFixedHeight(height);
                    // (its icon in the text colour too: a monochrome icon theme drew it in a pale grey there)
                    const QIcon themed = QIcon::fromTheme(QStringLiteral("folder-root"));
                    const QSize side = button->iconSize().isValid() ? button->iconSize() : QSize(16, 16);
                    QPixmap glyph = themed.pixmap(side, devicePixelRatioF());
                    if (!glyph.isNull()) {
                        QPainter tint(&glyph);
                        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                        tint.fillRect(glyph.rect(), palette().color(QPalette::WindowText));
                        tint.end();
                        button->setIcon(QIcon(glyph));
                    }
                }
            }
        }
    }
}

void DolphinMainWindow::saveNewToolbarConfig()
{
    KXmlGuiWindow::saveNewToolbarConfig(); // Applies the new config. This has to be called first
                                           // because the rest of this method decides things
                                           // based on the new config.
    auto navigators = static_cast<DolphinNavigatorsWidgetAction *>(actionCollection()->action(QStringLiteral("url_navigators")));

    m_tabWidget->currentTabPage()->insertNavigatorsWidget(navigators);

    updateAllowedToolbarAreas();
    updateNavigatorsBackground();
    (static_cast<KHamburgerMenu *>(actionCollection()->action(KStandardAction::name(KStandardAction::HamburgerMenu))))->hideActionsOf(toolBar());
    // the tool bar was built anew from the new configuration: the buttons moved from it to the sort bar are back in it,
    // and the folder's filter field went with the old one (issue #10: its action was still used after being deleted)
    m_ngMovedActions.clear();
    m_ngMovedTrailing.clear();
    ngSortBarSetLeadingActions(m_ngSortBar, {});
    ngSortBarSetTrailingActions(m_ngSortBar, {});
    ngMoveToolbarButtons();
}

void DolphinMainWindow::toggleTerminalPanelFocus()
{
    if (!m_terminalPanel->isVisible()) {
        actionCollection()->action(QStringLiteral("show_terminal_panel"))->trigger(); // Also moves focus to the panel.
        actionCollection()->action(QStringLiteral("focus_terminal_panel"))->setText(i18nc("@action:inmenu Tools", "Defocus Terminal Panel"));
        return;
    }

    if (m_terminalPanel->terminalHasFocus()) {
        m_activeViewContainer->view()->setFocus(Qt::FocusReason::ShortcutFocusReason);
        actionCollection()->action(QStringLiteral("focus_terminal_panel"))->setText(i18nc("@action:inmenu Tools", "Focus Terminal Panel"));
        return;
    }

    m_terminalPanel->setFocus(Qt::FocusReason::ShortcutFocusReason);
    actionCollection()->action(QStringLiteral("focus_terminal_panel"))->setText(i18nc("@action:inmenu Tools", "Defocus Terminal Panel"));
}

void DolphinMainWindow::togglePlacesPanelFocus()
{
    if (!m_placesPanel->isVisible()) {
        actionCollection()->action(QStringLiteral("show_places_panel"))->trigger(); // Also moves focus to the panel.
        actionCollection()->action(QStringLiteral("focus_places_panel"))->setText(i18nc("@action:inmenu View", "Defocus Terminal Panel"));
        return;
    }

    if (m_placesPanel->hasFocus()) {
        m_activeViewContainer->view()->setFocus(Qt::FocusReason::ShortcutFocusReason);
        actionCollection()->action(QStringLiteral("focus_places_panel"))->setText(i18nc("@action:inmenu View", "Focus Places Panel"));
        return;
    }

    m_placesPanel->setFocus(Qt::FocusReason::ShortcutFocusReason);
    actionCollection()->action(QStringLiteral("focus_places_panel"))->setText(i18nc("@action:inmenu View", "Defocus Places Panel"));
}

DolphinMainWindow::UndoUiInterface::UndoUiInterface()
    : KIO::FileUndoManager::UiInterface()
{
}

DolphinMainWindow::UndoUiInterface::~UndoUiInterface() = default;

void DolphinMainWindow::UndoUiInterface::jobError(KIO::Job *job)
{
    DolphinMainWindow *mainWin = qobject_cast<DolphinMainWindow *>(parentWidget());
    if (mainWin) {
        DolphinViewContainer *container = mainWin->activeViewContainer();
        container->showMessage(job->errorString(), KMessageWidget::Error);
    } else {
        KIO::FileUndoManager::UiInterface::jobError(job);
    }
}

bool DolphinMainWindow::isUrlOpen(const QString &url)
{
    return m_tabWidget->isUrlOpen(QUrl::fromUserInput(url));
}

bool DolphinMainWindow::isItemVisibleInAnyView(const QString &urlOfItem)
{
    return m_tabWidget->isItemVisibleInAnyView(QUrl::fromUserInput(urlOfItem));
}

void DolphinMainWindow::slotDoubleClickViewBackground(Qt::MouseButton button)
{
    if (button != Qt::MouseButton::LeftButton) {
        // only handle left mouse button for now
        return;
    }

    GeneralSettings *settings = GeneralSettings::self();
    QString clickAction = settings->doubleClickViewAction();

    DolphinView *view = activeViewContainer()->view();
    if (view == nullptr || clickAction == "none") {
        return;
    }

    if (clickAction == customCommand) {
        // run custom command set by the user
        QString path = view->url().toLocalFile();
        QString clickCustomAction = settings->doubleClickViewCustomAction();
        clickCustomAction.replace("{path}", path.prepend('"').append('"'));

        m_job = new KIO::CommandLauncherJob(clickCustomAction);
        m_job->setUiDelegate(new KDialogJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
        m_job->start();

    } else {
        // get the action set by the user and trigger it
        const KActionCollection *actions = actionCollection();
        QAction *action = actions->action(clickAction);
        if (action == nullptr) {
            qCWarning(DolphinDebug) << QStringLiteral("Double-click view: action `%1` was not found").arg(clickAction);
            return;
        }
        action->trigger();
    }
}

#include "moc_dolphinmainwindow.cpp"
