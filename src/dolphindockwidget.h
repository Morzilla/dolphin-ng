/*
 * SPDX-FileCopyrightText: 2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHIN_DOCK_WIDGET_H
#define DOLPHIN_DOCK_WIDGET_H

#include <QDockWidget>
#include <QList>
#include <QUrl>

class QTimer;

/**
 * @brief Extends QDockWidget to be able to get locked.
 */
class DolphinDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit DolphinDockWidget(const QString &title = QString(), QWidget *parent = nullptr, Qt::WindowFlags flags = {});
    ~DolphinDockWidget() override;

    /**
     * @param lock If \a lock is true, the title bar of the dock-widget will get hidden so
     *             that it is not possible for the user anymore to move or undock the dock-widget.
     */
    void setLocked(bool lock);
    bool isLocked() const;
    /** dolphin-ng: no title bar at all, locked or not (the places: shown and hidden by the toolbar's sidebar button). */
    void ngSetBare();

    /**
     * dolphin-ng: paint the glass card of this panel with \a painter, in this widget's coordinates.
     * Returns false if nothing opaque was painted (stock look, or the "live blur" hole), in which
     * case it cannot be used to wipe something drawn on top of the card.
     */
    bool paintNgGlass(QPainter &painter);
    /** dolphin-ng: true when the sidebar glass is off (SIDEBAR=plain) / when it shows the static image (not GLASS_MODE=live). */
    /** dolphin-ng: paints over @p rect of @p from (in its coordinates) what the main window paints under the panels: the
     *  window colour, or the style's see-through sheet with its lights under Breeze NG Aurora. Replaces what is there. */
    static void ngPaintWindowGround(QPainter &painter, QWidget *from, const QRect &rect);
    static bool ngGlassPlain();
    static bool ngGlassImageMode();
    /**
     * dolphin-ng: the disks at the foot of the Places card: the file system of the home folder first, then (with the
     * NG look of the Devices, the default) every disk and partition the places know and do not hide, each with its
     * usage; an unmounted one is mounted by a click.
     */
    struct NgDisk {
        QString udi; // empty for the home file system
        QString name;
        QString fs; // "btrfs", "NTFS"
        QString root; // the mount point; empty when not mounted
        qint64 total = 0;
        qint64 free = 0;
        bool home = false;
        bool removable = false; // a USB stick, a card: it can be removed safely
        QUrl url; // a device that is not a disk (a phone through KDE Connect, a media player): where it opens
        QString kind; // what it is, said on the right of its box ("KDE Connect", "Device")
    };
    static QList<NgDisk> ngSidebarDisks();
    /** dolphin-ng: the height the disk boxes take at the foot of the card, with the gap above the card's edge. */
    static int ngDiskRoom();

Q_SIGNALS:
    /** dolphin-ng: the disk usage box was clicked; \a url is the root of that file system. */
    void ngDiskRootRequested(const QUrl &url);

protected:
    void mousePressEvent(QMouseEvent *event) override; // dolphin-ng: disk usage box
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * Make sure we do not emit QDockWidget::visibilityChanged() signals whenever Dolphin's window is minimized or restored.
     */
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override; // dolphin-ng: glass panel
    bool eventFilter(QObject *watched, QEvent *event) override; // dolphin-ng: repaint when the window moves
    void timerEvent(QTimerEvent *event) override; // dolphin-ng: refresh the disk usage box

private:
    /** dolphin-ng: average luminance (0-1) of what lies behind the glass card, or -1 if unknown. */
    qreal backdropLuminance() const;
    /** dolphin-ng: adapt the glass transparency to the backdrop. */
    void updateGlass();
    /** dolphin-ng: (re)start the timers the current settings need and repaint. */
    void ngApplySettings();

    bool m_locked;
    bool m_ngBare = false;
    QWidget *m_dockTitleBar;
    QTimer *m_glassTimer = nullptr;
    QTimer *m_lavaTimer = nullptr; // dolphin-ng: drives the animated ("lava lamp") backdrop
    bool ngDiskBoxEvent(QEvent *event);
    struct NgDiskHit {
        QRectF rect;
        QString root;
        QString udi;
        bool removable = false;
        QRectF eject; // the eject button of a mounted removable device
        QUrl url; // a device that is not a disk: opened at this address
    };
    void ngDiskMenu(const NgDiskHit &target, const QPoint &globalPos); // right click: open, mount, unmount, remove
    void ngDiskMount(const QString &udi); // mount it, then open its root
    void ngDiskUnmount(const QString &udi); // unmount it (a removable device: safely, ready to be pulled out)
    QList<NgDiskHit> m_ngDiskBoxes; // where the disk boxes were drawn, in this widget's coordinates
    int m_ngDiskPressed = -1; // the box the press began on
    bool m_ngDiskHover = false;
    qreal m_glass = 75; // share (0-100) of the window tint removed under the card
};

#endif
