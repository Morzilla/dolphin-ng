/*
 * SPDX-FileCopyrightText: 2008-2012 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2010 Christian Muehlhaeuser <muesli@gmail.com>
 * SPDX-FileCopyrightText: 2021 Kai Uwe Broulik <kde@broulik.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PLACESPANEL_H
#define PLACESPANEL_H

#include "panels/panel.h"

#include <QPersistentModelIndex>
#include <QRectF>
#include <KFilePlacesView>
#include <QPalette>

class KFilePlacesModel;
#include <QUrl>

#include <Solid/SolidNamespace> // Solid::ErrorType

class QTimer;
namespace Solid
{
class StorageAccess;
}

/**
 * @brief Combines bookmarks and mounted devices as list.
 */
/** dolphin-ng: the sidebar's sections in their order ("places", "pinned", "filters", "disks"; see SIDEBAR_ORDER). */
QStringList ngSidebarOrder();

class QVariantAnimation;

class PlacesPanel : public KFilePlacesView
{
public:
    /** dolphin-ng: a widget kept at the bottom of the panel, above the disk usage box (the filters section). */
    void ngSetBottomWidget(QWidget *widget);
    /** dolphin-ng: the pinned items and the filters, two sections the sidebar orders with the disks (SIDEBAR_ORDER). */
    void ngSetSections(QWidget *pinned, QWidget *filters);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void ngLayoutBottom();
    void ngUpdateFade();
    void ngPlaceScrollBar();
    class QScrollBar *m_ngOverlayBar = nullptr;
    class QGraphicsOpacityEffect *m_ngBarOpacity = nullptr;
    class QPropertyAnimation *m_ngBarFade = nullptr;
    void ngFadeBar(bool in);
    bool m_ngFadeWatched = false;
    QWidget *m_ngFadeTop = nullptr; // live blur: the list fades into the glass where it continues, see ngUpdateFade()
    QWidget *m_ngFadeBottom = nullptr;
    QWidget *m_ngBottom = nullptr;
    QWidget *m_ngPinned = nullptr;
    QWidget *m_ngFilters = nullptr;
    bool m_ngLayouting = false;
    bool m_ngDevicesPending = false;
    bool m_ngBarWatched = false;

    Q_OBJECT

public:
    explicit PlacesPanel(QWidget *parent);
    ~PlacesPanel() override;

    void setUrl(const QUrl &url); // override

    // for compatibility with Panel, actions that are shown
    // on the view's context menu
    QList<QAction *> customContextMenuActions() const;
    void setCustomContextMenuActions(const QList<QAction *> &actions);

    void requestTearDown();
    void proceedWithTearDown();

public Q_SLOTS:
    void readSettings();

Q_SIGNALS:
    void errorMessage(const QString &error);
    void storageTearDownRequested(const QString &mountPath);
    void storageTearDownExternallyRequested(const QString &mountPath);
    void storageTearDownSuccessful();
    void openInSplitViewRequested(const QUrl &url);

public:
    // dolphin-ng: folded sections (a click on their title): their first row is only the title, the others are hidden
    bool ngHeaderOnly(const QModelIndex &index) const;
    // while a section folds or opens: how much of a row of it shows (1 all, 0 none); `first` says the row carries the title
    qreal ngRowFactor(const QModelIndex &index, bool *first) const;
    int ngPlainRowHeight() const; // the height of an entry without a title above it

protected:
    void paintEvent(QPaintEvent *event) override; // dolphin-ng: section titles as pills
    void updateGeometries() override; // dolphin-ng: the sections that follow the list move with its end
    void enterEvent(QEnterEvent *event) override; // dolphin-ng: the scroll bar shows while the pointer is over the panel
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override; // dolphin-ng: a click on a section title folds it
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;

private Q_SLOTS:
    void slotConfigureTrash();
    void slotUrlsDropped(const QUrl &dest, QDropEvent *event, QWidget *parent);
    void slotContextMenuAboutToShow(const QModelIndex &index, QMenu *menu);
    void slotTearDownRequested(const QModelIndex &index);
    void slotTearDownRequestedExternally(const QString &udi);
    void slotTearDownDone(const QModelIndex &index, Solid::ErrorType error, const QVariant &errorData);
    void slotRowsInserted(const QModelIndex &parent, int first, int last);
    void slotRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last);

private:
    void ngApplyPalette(); // dolphin-ng: glass highlight
    void ngPaintPills(); // dolphin-ng: glass pill under the selected / hovered entry
public:
    QColor ngBarColour() const; // dolphin-ng: the colour of the free space bars on the glass card (invalid: no change)
private:
    void ngApplyDevices(KFilePlacesModel *placesModel); // dolphin-ng: hide the Devices section when switched off
    bool ngDeviceRowHidden(int group) const; // a row of this group gives way to the disk boxes, or is switched off
    bool ngRowShown(const KFilePlacesModel *placesModel, int row) const; // not hidden by KDE, nor given way to the disk boxes
    bool ngFirstOfGroup(const KFilePlacesModel *placesModel, int row) const; // the first shown row of its section
    bool ngRowHiddenByNg(const KFilePlacesModel *placesModel, int row) const; // a disk row, or a row of a folded section
    QString ngTitleAt(const QPoint &pos) const; // the section whose title pill is there
    QList<std::pair<QRect, QString>> m_ngTitleHits; // the title pills as painted, and their sections
    QAbstractItemDelegate *m_ngRowDelegate = nullptr; // folded sections: set on each row (KDE's stays the view's own)
    void ngAnimateSection(const QString &key); // fold or open a section, sliding
    QString m_ngAnimKey; // the section sliding now
    qreal m_ngAnimFactor = 1.0;
    QVariantAnimation *m_ngAnim = nullptr;
    // the pill of the selected entry slides from the old entry to the new one
    QPersistentModelIndex m_ngPillIndex;
    QRectF m_ngPillRect;
    QVariantAnimation *m_ngPillAnim = nullptr;
    QString m_ngTitleHover;
    QPalette m_ngPalette;
    void connectDeviceSignals(const QModelIndex &idx);

    QList<QAction *> m_customContextMenuActions;

    QPersistentModelIndex m_indexToTearDown;

    std::unique_ptr<QAction> m_configureTrashAction;
    std::unique_ptr<QAction> m_openInSplitView;
    QAction *m_lockPanelsAction;
};

#endif // PLACESPANEL_H
