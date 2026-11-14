/*
 * SPDX-FileCopyrightText: 2008-2012 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2021 Kai Uwe Broulik <kde@broulik.de>
 *
 * Based on KFilePlacesView from kdelibs:
 * SPDX-FileCopyrightText: 2007 Kevin Ottens <ervin@kde.org>
 * SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "placespanel.h"

#include <QGraphicsOpacityEffect>
#include <QLinearGradient>
#include <QScrollBar>
#include <QPropertyAnimation>

#include "dolphindockwidget.h"

#include "kitemviews/kitemlistwidget.h"

#include "dolphin_generalsettings.h"
#include "dolphin_placespanelsettings.h"
#include "dolphinplacesmodelsingleton.h"
#include "settings/dolphinsettingsdialog.h"
#include "views/draganddrophelper.h"

#include <KFilePlacesModel>
#include <KIO/DropJob>
#include <KIO/Job>
#include <KLocalizedString>
#include <KProtocolManager>

#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionViewItem>
#include <QCursor>
#include <QShowEvent>
#include <QTimer>

#include <Solid/StorageAccess>
#include <QAbstractItemDelegate>
#include <QMouseEvent>
#include <QVariantAnimation>

namespace
{
// dolphin-ng: the key of a section of the places, for the folded sections (SIDEBAR_COLLAPSED)
QString ngGroupKey(int group)
{
    switch (group) {
    case KFilePlacesModel::PlacesType:
        return QStringLiteral("places");
    case KFilePlacesModel::RemoteType:
        return QStringLiteral("remote");
    case KFilePlacesModel::RecentlySavedType:
        return QStringLiteral("recent");
    case KFilePlacesModel::SearchForType:
        return QStringLiteral("search");
    case KFilePlacesModel::DevicesType:
        return QStringLiteral("devices");
    case KFilePlacesModel::RemovableDevicesType:
        return QStringLiteral("removable");
    case KFilePlacesModel::TagsType:
        return QStringLiteral("tags");
    default:
        return QStringLiteral("other");
    }
}

// dolphin-ng: in front of KFilePlacesView's own delegate: the first row of a folded section is only its title (the
// panel paints the title pill), everything else goes to the original delegate as before
class NgCollapseDelegate : public QAbstractItemDelegate
{
public:
    NgCollapseDelegate(QAbstractItemDelegate *inner, PlacesPanel *panel)
        : QAbstractItemDelegate(panel)
        , m_inner(inner)
        , m_panel(panel)
    {
        connect(inner, &QAbstractItemDelegate::sizeHintChanged, this, &QAbstractItemDelegate::sizeHintChanged);
        connect(inner, &QAbstractItemDelegate::commitData, this, &QAbstractItemDelegate::commitData);
        connect(inner, &QAbstractItemDelegate::closeEditor, this, &QAbstractItemDelegate::closeEditor);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (m_panel->ngHeaderOnly(index)) {
            return; // the panel paints the title pill
        }
        bool first = false;
        const qreal factor = m_panel->ngRowFactor(index, &first);
        if (factor >= 1.0) {
            m_inner->paint(painter, option, index);
            return;
        }
        // sliding: the row at its full height, moved up so that its bottom part shows, and fading
        const int full = m_inner->sizeHint(option, index).height();
        QStyleOptionViewItem moved(option);
        if (first) {
            // the entry under the title slides up behind it like the others (the panel paints the title over it)
            const int shift = qRound(plainHeight(option, index) * (1.0 - factor));
            moved.rect = QRect(option.rect.left(), option.rect.top() - shift, option.rect.width(), full);
        } else {
            moved.rect = QRect(option.rect.left(), option.rect.bottom() + 1 - full, option.rect.width(), full);
        }
        painter->save();
        painter->setClipRect(option.rect, Qt::IntersectClip);
        painter->setOpacity(painter->opacity() * factor);
        m_inner->paint(painter, moved, index);
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = m_inner->sizeHint(option, index);
        if (m_panel->ngHeaderOnly(index)) {
            size.setHeight(qMax(option.fontMetrics.height() + 6, size.height() - plainHeight(option, index)));
            return size;
        }
        bool first = false;
        const qreal factor = m_panel->ngRowFactor(index, &first);
        if (factor < 1.0) {
            const int plain = plainHeight(option, index);
            size.setHeight(first ? size.height() - qRound(plain * (1.0 - factor)) : qRound(size.height() * factor));
        }
        return size;
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return m_inner->createEditor(parent, option, index);
    }
    void destroyEditor(QWidget *editor, const QModelIndex &index) const override
    {
        m_inner->destroyEditor(editor, index);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        m_inner->setEditorData(editor, index);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        m_inner->setModelData(editor, model, index);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        m_inner->updateEditorGeometry(editor, option, index);
    }
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        return m_inner->editorEvent(event, model, option, index);
    }
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        return m_inner->helpEvent(event, view, option, index);
    }

private:
    // the height of a row without a title: a row that follows another one of its section
    int plainHeight(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        const QAbstractItemModel *model = index.model();
        for (int row = 1; model && row < model->rowCount(); ++row) {
            if (m_panel->isRowHidden(row) || m_panel->isRowHidden(row - 1)) {
                continue;
            }
            const QModelIndex here = model->index(row, 0);
            if (here.data(KFilePlacesModel::GroupRole) == model->index(row - 1, 0).data(KFilePlacesModel::GroupRole)) {
                return m_inner->sizeHint(option, here).height();
            }
        }
        return option.fontMetrics.height() + 14;
    }

    QAbstractItemDelegate *m_inner;
    PlacesPanel *m_panel;
};
}

PlacesPanel::PlacesPanel(QWidget *parent)
    : KFilePlacesView(parent)
{
    m_ngPalette = palette(); // the stock palette, restored when the glass panel is turned off
    ngApplyPalette();
    setViewportMargins(16, 12, 16, 12 + DolphinDockWidget::ngDiskRoom()); // dolphin-ng: keep the entries inside the glass card drawn by the dock, above its disk boxes
    // dolphin-ng: a tighter list than KIO's own, and icons moved in so the selection pill's round end clears them
    // (read by Dialog NG's places view; the stock KIO ignores them)
    setProperty("ngRowPadding", 2);
    setProperty("ngIconIndent", 7);
    // dolphin-ng: never fill the list background, whatever the style's base colour is, so the glass card shows through
    viewport()->setAutoFillBackground(false);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NoSystemBackground);
    viewport()->setAttribute(Qt::WA_NoSystemBackground);
    setDropOnPlaceEnabled(true);
    connect(this, &PlacesPanel::urlsDropped, this, &PlacesPanel::slotUrlsDropped);

    setAutoResizeItemsEnabled(false);

    setTeardownFunction([this](const QModelIndex &index) {
        slotTearDownRequested(index);
    });

    m_openInSplitView = std::make_unique<QAction>(QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18nc("@action:inmenu", "Open in Split View"));
    m_openInSplitView->setPriority(QAction::HighPriority);
    connect(m_openInSplitView.get(), &QAction::triggered, this, [this]() {
        const QUrl url = currentIndex().data(KFilePlacesModel::UrlRole).toUrl();
        Q_EMIT openInSplitViewRequested(url);
    });
    addAction(m_openInSplitView.get());

    m_configureTrashAction = std::make_unique<QAction>(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu", "Configure Trash…"));
    m_configureTrashAction->setPriority(QAction::HighPriority);
    connect(m_configureTrashAction.get(), &QAction::triggered, this, &PlacesPanel::slotConfigureTrash);
    addAction(m_configureTrashAction.get());

    connect(this, &PlacesPanel::contextMenuAboutToShow, this, &PlacesPanel::slotContextMenuAboutToShow);

    connect(this, &PlacesPanel::iconSizeChanged, this, [](const QSize &newSize) {
        int iconSize = qMin(newSize.width(), newSize.height());
        if (iconSize == 0) {
            // Don't store 0 size, let's keep -1 for default/small/automatic
            iconSize = -1;
        }
        PlacesPanelSettings *settings = PlacesPanelSettings::self();
        settings->setIconSize(iconSize);
        settings->save();
    });

    readSettings();

    // Set the model here so that it's loaded in time for the sizeHint to properly apply (setting it upon showEvent is too late)
    auto *placesModel = DolphinPlacesModelSingleton::instance().placesModel();
    setModel(placesModel);
    // dolphin-ng: folded sections. The delegate goes on each row, not on the view: KFilePlacesView's own code takes the
    // view's delegate and expects its own class (a click on the list crashed when it was replaced)
    m_ngRowDelegate = new NgCollapseDelegate(itemDelegate(), this);

    // dolphin-ng: disks and partitions (NG settings > Sidebar, or DOLPHIN_NG_DEVICES): by default boxes with their
    // usage at the foot of the card (the dock draws them; their rows here are hidden), "list" keeps KIO's own
    // sections, "hide" hides the internal disks. Single disks are hidden as any place is (its "Hide" entry, or the
    // NG settings).
    // KFilePlacesView decides row visibility itself on model changes, so hide the rows again right after it
    auto applyDevices = [this, placesModel] {
        // the rows at once (KFilePlacesView's own update for this change has run already: it was connected first),
        // and once more after the event loop, for the updates KFilePlacesView delays
        for (int row = 0; row < placesModel->rowCount(); ++row) {
            if (!isRowHidden(row) && ngRowHiddenByNg(placesModel, row)) {
                setRowHidden(row, true);
            }
        }
        QTimer::singleShot(0, this, [this, placesModel] {
            ngApplyDevices(placesModel);
        });
    };
    connect(placesModel, &QAbstractItemModel::rowsInserted, this, applyDevices);
    connect(placesModel, &QAbstractItemModel::rowsRemoved, this, applyDevices);
    connect(placesModel, &QAbstractItemModel::dataChanged, this, applyDevices);
    connect(placesModel, &QAbstractItemModel::modelReset, this, applyDevices);
    connect(placesModel, &KFilePlacesModel::groupHiddenChanged, this, applyDevices);
    connect(placesModel, &KFilePlacesModel::reloaded, this, applyDevices);
    connect(this, &KFilePlacesView::allPlacesShownChanged, this, applyDevices);
    // a folded section shows the entry of the open folder: follow the selection
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, applyDevices);
    applyDevices();
    connect(NgSettings::instance(), &NgSettings::changed, this, [this, placesModel] {
        ngApplyPalette();
        ngApplyDevices(placesModel);
        viewport()->update();
        update();
    });

    connect(placesModel, &KFilePlacesModel::errorMessage, this, &PlacesPanel::errorMessage);
    connect(placesModel, &KFilePlacesModel::teardownDone, this, &PlacesPanel::slotTearDownDone);

    connect(placesModel, &QAbstractItemModel::rowsInserted, this, &PlacesPanel::slotRowsInserted);
    connect(placesModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PlacesPanel::slotRowsAboutToBeRemoved);

    for (int i = 0; i < model()->rowCount(); ++i) {
        connectDeviceSignals(model()->index(i, 0, QModelIndex()));
    }
}

PlacesPanel::~PlacesPanel() = default;

// dolphin-ng: the selected entry sits on a glass pill like the section titles; the hovered entry gets a fainter one.
// The widget style frames and fills those rows in its own way whatever the palette says, so each such row is
// painted again from scratch: the panel's backdrop, the pill, then icon and text by the delegate without the
// selected/hovered state (it draws only the contents then).
void PlacesPanel::ngPaintPills()
{
    auto *dock = qobject_cast<DolphinDockWidget *>(parentWidget());
    if (!dock) {
        return;
    }
    const QModelIndexList selected = selectionModel() ? selectionModel()->selectedIndexes() : QModelIndexList();
    const int plainRow = ngPlainRowHeight();
    auto pillRectOf = [&](const QModelIndex &index) {
        QRect rect = visualRect(index);
        if (rect.isValid() && plainRow > 0 && rect.height() > plainRow + 2) {
            rect.setTop(rect.bottom() - plainRow + 1);
        }
        return rect;
    };
    // the selection slides from the old entry to the new one
    const QModelIndex current = selected.size() == 1 ? selected.first() : QModelIndex();
    const QRect target = current.isValid() ? pillRectOf(current) : QRect();
    if (current != m_ngPillIndex) {
        const bool slide = m_ngPillIndex.isValid() && current.isValid() && m_ngPillRect.isValid() && target.isValid() && isVisible()
            && style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this) > 0;
        m_ngPillIndex = current;
        if (m_ngPillAnim) {
            m_ngPillAnim->stop();
        }
        if (slide) {
            if (!m_ngPillAnim) {
                m_ngPillAnim = new QVariantAnimation(this);
                m_ngPillAnim->setEasingCurve(QEasingCurve::OutCubic);
                m_ngPillAnim->setDuration(260);
                connect(m_ngPillAnim, &QVariantAnimation::valueChanged, this, [this] {
                    viewport()->update();
                });
            }
            m_ngPillAnim->setStartValue(m_ngPillRect);
            m_ngPillAnim->setEndValue(QRectF(target));
            m_ngPillAnim->start();
        }
    }
    const bool sliding = m_ngPillAnim && m_ngPillAnim->state() == QAbstractAnimation::Running;
    m_ngPillRect = sliding ? m_ngPillAnim->currentValue().toRectF() : QRectF(target);

    QList<std::pair<QModelIndex, qreal>> rows;
    for (const QModelIndex &index : selected) {
        if (!sliding) {
            rows.append({index, 1.0});
        }
    }
    if (viewport()->underMouse()) {
        const QModelIndex hovered = indexAt(viewport()->mapFromGlobal(QCursor::pos()));
        if (hovered.isValid() && !selected.contains(hovered) && !ngHeaderOnly(hovered)) {
            rows.append({hovered, 0.5});
        }
    }
    if (rows.isEmpty() && !sliding) {
        return;
    }
    const bool dark = ngIsDark(palette());
    // the pill: a faint veil with a hairline edge, round ends; Aurora: the accent for the selected entry, the veil of
    // every other surface for the hovered one, no edge
    auto drawPill = [&](QPainter &painter, const QRectF &rect, qreal strength) {
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF pill = rect.adjusted(0.5, 2.5, -0.5, -2.5);
        const qreal radius = pill.height() / 2.0;
        if (ngAurora()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(strength >= 1.0 ? ngAuroraAccent(palette(), dark ? 0.34 : 0.30) : ngAuroraVeil(dark, true));
        } else {
            painter.setPen(QPen(dark ? QColor(255, 255, 255, qRound(46 * strength)) : QColor(0, 0, 0, qRound(30 * strength)), 1));
            painter.setBrush(dark ? QColor(255, 255, 255, qRound(30 * strength)) : QColor(255, 255, 255, qRound(150 * strength)));
        }
        painter.drawRoundedRect(pill, radius, radius);
    };
    auto drawContents = [&](QPainter &painter, const QModelIndex &index) {
        QStyleOptionViewItem option;
        initViewItemOption(&option);
        option.rect = visualRect(index); // the delegate lays the row out in its full rect (with the title space)
        option.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        option.showDecorationSelected = false;
        if (QAbstractItemDelegate *delegate = itemDelegateForIndex(index)) {
            delegate->paint(&painter, option, index);
        }
    };
    if (sliding) {
        // on its way the pill crosses other entries: each one it touches is painted again inside the pill's
        // rectangle (backdrop, pill, then its icon and text on top)
        QPainter painter(viewport());
        const QRect area = m_ngPillRect.toAlignedRect();
        QModelIndexList crossed;
        for (int y = area.top(); y <= area.bottom() + 1; y += qMax(4, area.height() / 3)) {
            const QModelIndex index = indexAt(QPoint(area.center().x(), qMin(y, area.bottom())));
            if (index.isValid() && !crossed.contains(index)) {
                crossed.append(index);
            }
        }
        painter.save();
        painter.setClipRect(area);
        DolphinDockWidget::ngPaintWindowGround(painter, viewport(), area);
        const QPoint dockOffset = viewport()->mapTo(dock, QPoint(0, 0));
        painter.save();
        painter.translate(-dockOffset);
        dock->paintNgGlass(painter);
        painter.restore();
        drawPill(painter, m_ngPillRect, 1.0);
        for (const QModelIndex &index : std::as_const(crossed)) {
            if (!ngHeaderOnly(index)) {
                drawContents(painter, index);
            }
        }
        painter.restore();
    }
    const QPoint offset = viewport()->mapTo(dock, QPoint(0, 0));
    // the first row of a group is taller: its rect holds the group title too; the pill keeps to the bottom part,
    // a plain row's height
    const int rowHeight = ngPlainRowHeight();
    QPainter painter(viewport());
    for (const auto &[index, strength] : std::as_const(rows)) {
        QRect rect = visualRect(index);
        if (!rect.isValid()) {
            continue;
        }
        if (rowHeight > 0 && rect.height() > rowHeight + 2) {
            rect.setTop(rect.bottom() - rowHeight + 1);
        }
        painter.save();
        painter.setClipRect(rect);
        // 1. the glass backdrop, exactly what lies behind the row
        // (with the live blur the glass is translucent: start again from the window colour, which is what the main
        // window paints under the panel before the glass thins it out. Starting from nothing left these rows fully
        // see-through, a bright band over a light desktop)
        DolphinDockWidget::ngPaintWindowGround(painter, viewport(), rect);
        painter.save();
        painter.translate(-offset);
        dock->paintNgGlass(painter);
        painter.restore();
        // 2. the pill, the same as the section titles
        drawPill(painter, QRectF(rect), strength);
        // 3. icon and text, by the delegate, as a plain row
        drawContents(painter, index);
        painter.restore();
    }
}

void PlacesPanel::ngApplyPalette()
{
    QPalette glass = m_ngPalette;
    if (ngSetting("SIDEBAR").toLower() != QLatin1String("plain")) {
        // the style highlights the selected and hovered entries with the palette's highlight colour;
        // a translucent white (black on light schemes) turns that into a glass pill over the backdrop
        // the pill itself is painted in paintEvent(); the style must not draw a second one
        glass.setColor(QPalette::Highlight, QColor(255, 255, 255, 0)); // white, not black: the style mixes the rgb with its own alpha
        glass.setColor(QPalette::HighlightedText, glass.color(QPalette::WindowText));
    }
    setPalette(glass);
    viewport()->setPalette(glass);
}

bool PlacesPanel::ngDeviceRowHidden(int group) const
{
    if (group != KFilePlacesModel::DevicesType && group != KFilePlacesModel::RemovableDevicesType) {
        return false; // only the rows of disks and removable devices are ever hidden here
    }
    if (ngSetting("SIDEBAR").toLower() == QLatin1String("plain")) {
        return false;
    }
    const QString mode = ngSetting("DEVICES").toLower();
    if (mode.isEmpty()) {
        return true; // boxes at the foot of the card
    }
    return mode == QLatin1String("hide") && group == KFilePlacesModel::DevicesType && !allPlacesShown();
}

bool PlacesPanel::ngRowShown(const KFilePlacesModel *placesModel, int row) const
{
    const QModelIndex index = placesModel->index(row, 0);
    const auto group = placesModel->groupType(index);
    if (!allPlacesShown() && (placesModel->isHidden(index) || placesModel->isGroupHidden(group))) {
        return false;
    }
    return !ngDeviceRowHidden(group);
}

bool PlacesPanel::ngFirstOfGroup(const KFilePlacesModel *placesModel, int row) const
{
    const auto group = placesModel->groupType(placesModel->index(row, 0));
    for (int before = row - 1; before >= 0; --before) {
        if (placesModel->groupType(placesModel->index(before, 0)) != group) {
            break;
        }
        if (ngRowShown(placesModel, before)) {
            return false;
        }
    }
    return true;
}

bool PlacesPanel::ngRowHiddenByNg(const KFilePlacesModel *placesModel, int row) const
{
    const QModelIndex index = placesModel->index(row, 0);
    const auto group = placesModel->groupType(index);
    if (ngDeviceRowHidden(group)) {
        return true;
    }
    if (!ngSectionCollapsed(ngGroupKey(group)) || (selectionModel() && selectionModel()->isSelected(index))) {
        return false; // an open section; or the entry of the open folder, which stays
    }
    return !ngFirstOfGroup(placesModel, row); // the first row stays: it carries the title
}

bool PlacesPanel::ngHeaderOnly(const QModelIndex &index) const
{
    auto *placesModel = qobject_cast<KFilePlacesModel *>(model());
    if (!placesModel || !index.isValid() || !ngSectionCollapsed(ngGroupKey(placesModel->groupType(index)))) {
        return false;
    }
    if (selectionModel() && selectionModel()->isSelected(index)) {
        return false;
    }
    return ngRowShown(placesModel, index.row()) && ngFirstOfGroup(placesModel, index.row());
}

void PlacesPanel::ngApplyDevices(KFilePlacesModel *placesModel)
{
    // every row: the disk rows, and the rows of folded sections. What KDE hides (hidden places, hidden sections) is
    // left as it is
    for (int row = 0; row < placesModel->rowCount(); ++row) {
        if (m_ngRowDelegate && itemDelegateForRow(row) != m_ngRowDelegate) {
            setItemDelegateForRow(row, m_ngRowDelegate); // rows come and go: every one has it
        }
        const QModelIndex index = placesModel->index(row, 0);
        if (!allPlacesShown() && (placesModel->isHidden(index) || placesModel->isGroupHidden(placesModel->groupType(index)))) {
            continue;
        }
        const bool hide = ngRowHiddenByNg(placesModel, row);
        if (hide != isRowHidden(row)) {
            setRowHidden(row, hide);
        }
    }
    scheduleDelayedItemsLayout(); // a title row of a folded section changed height
    if (!m_ngLayouting) {
        m_ngLayouting = true;
        ngLayoutBottom(); // the disk boxes may be more or fewer now
        m_ngLayouting = false;
    }
}

// dolphin-ng: over the glass card the section titles ("Places", "Remote"...) are too faint to read:
// draw each one again as a small dark pill with light text, on top of the original.
void PlacesPanel::paintEvent(QPaintEvent *event)
{
    const bool plain = ngSetting("SIDEBAR").toLower() == QLatin1String("plain");
    // KFilePlacesView shows rows again on its own (delayed updates when devices are found, when the URL changes, when a
    // section of the card comes or goes...): a device row that should be hidden is hidden here, before this frame is
    // painted, and the rows are laid out again at once, so it never shows, not even for a frame. What changes geometry
    // (the disk boxes, the sections) is left for after the paint: changing it while painting is not safe
    if (auto *devicesModel = qobject_cast<KFilePlacesModel *>(model())) {
        bool hidden = false;
        for (int row = 0; row < devicesModel->rowCount(); ++row) {
            if (!isRowHidden(row) && ngRowHiddenByNg(devicesModel, row)) {
                setRowHidden(row, true);
                hidden = true;
            }
        }
        if (hidden) {
            executeDelayedItemsLayout();
            if (!m_ngDevicesPending) {
                m_ngDevicesPending = true;
                QTimer::singleShot(0, this, [this, devicesModel] {
                    ngApplyDevices(devicesModel);
                    m_ngDevicesPending = false;
                });
            }
        }
    }
    KFilePlacesView::paintEvent(event);
    if (!plain) {
        ngPaintPills();
    }
    const auto *placesModel = qobject_cast<KFilePlacesModel *>(model());
    if (plain || !placesModel) {
        return;
    }

    // the first visible row of a group carries the title above it; a row that is not first tells the plain row height
    struct Title {
        QRect rect;
        QString text;
        QString key;
        bool headerOnly = false;
        int count = 0;
        qreal factor = 1.0; // while its section slides: how much of the entry under the title shows
    };
    QList<Title> titles;
    int rowHeight = -1;
    int previousGroup = -1;
    for (int row = 0; row < placesModel->rowCount(); ++row) {
        if (isRowHidden(row)) {
            continue;
        }
        const QModelIndex index = placesModel->index(row, 0);
        const int group = int(placesModel->groupType(index));
        const QRect rect = visualRect(index);
        if (group != previousGroup) {
            int count = 0;
            for (int other = 0; other < placesModel->rowCount(); ++other) {
                if (int(placesModel->groupType(placesModel->index(other, 0))) == group && ngRowShown(placesModel, other)) {
                    ++count;
                }
            }
            bool first = false;
            const qreal factor = ngRowFactor(index, &first);
            titles.append({rect, index.data(KFilePlacesModel::GroupRole).toString(), ngGroupKey(group), ngHeaderOnly(index), count, factor});
        } else if (rowHeight < 0) {
            bool first = false;
            if (ngRowFactor(index, &first) >= 1.0) { // a row sliding is lower than a plain row: not a measure
                rowHeight = rect.height();
            }
        }
        previousGroup = group;
    }
    if (rowHeight < 0) {
        rowHeight = ngPlainRowHeight(); // every section folded: only titles and the open folder's entry are shown
    }
    m_ngTitleHits.clear();
    if (titles.isEmpty()) {
        return;
    }

    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 0.84);
    titleFont.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(titleFont);

    // where the titles are, and the pills that replace them
    struct Pill {
        QRect header;
        QRect rect;
        QString text;
        QString key;
        bool folded = false;
        int count = 0;
    };
    QList<Pill> pills;
    QRegion headers;
    for (const Title &title : std::as_const(titles)) {
        // a folded section: its first row is only the title
        // (while its section slides, the entry under the title is that much lower)
        const int headerHeight = title.headerOnly ? title.rect.height()
            : rowHeight > 0                       ? title.rect.height() - qRound(rowHeight * title.factor)
                                                  : metrics.height() + 10;
        if (headerHeight < metrics.height() || title.text.isEmpty()) {
            continue;
        }
        const bool folded = ngSectionCollapsed(title.key);
        const QRect header(title.rect.left(), title.rect.top(), title.rect.width(), headerHeight);
        const int pillHeight = qMin(headerHeight - 2, metrics.height() + 8);
        // the arrow on the left, and when folded how many entries are inside
        const int extra = 14 + (folded ? metrics.horizontalAdvance(QStringLiteral(" %1").arg(title.count)) + 4 : 0);
        const QRect rect(header.left() + 2, header.bottom() - pillHeight, metrics.horizontalAdvance(title.text) + 22 + extra, pillHeight);
        pills.append({header, rect, title.text, title.key, folded, title.count});
        m_ngTitleHits.append({rect, title.key});
        headers += header;
    }
    if (pills.isEmpty()) {
        return;
    }

    QPainter painter(viewport());
    // Wipe the original titles: paint the panel's own backdrop (the dock widget draws it) again over the title
    // areas. The list has no background of its own, so this restores exactly what was behind the text.
    if (auto *dock = qobject_cast<DolphinDockWidget *>(parentWidget())) {
        const QPoint offset = viewport()->mapTo(dock, QPoint(0, 0));
        painter.save();
        painter.setClipRegion(headers);
        // with the live blur the glass is translucent: put the window colour back under the title areas first (the
        // old text would show through otherwise), then the glass thins it out as everywhere else
        DolphinDockWidget::ngPaintWindowGround(painter, viewport(), headers.boundingRect());
        painter.translate(-offset);
        dock->paintNgGlass(painter);
        painter.restore();
    }

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(titleFont);
    const bool dark = ngIsDark(palette());
    if (!viewport()->underMouse()) {
        m_ngTitleHover.clear();
    }
    QColor dim = palette().color(QPalette::WindowText);
    dim.setAlphaF(0.6);
    for (const Pill &pill : std::as_const(pills)) {
        // a light glass pill: a faint veil with a hairline edge, and the title in the normal text colour; it is a
        // button (a click folds the section), lit under the pointer
        const bool hovered = pill.key == m_ngTitleHover;
        const QRectF rect = QRectF(pill.rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(hovered ? QColor(95, 168, 216, 150) : dark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 30), 1));
        painter.setBrush(hovered ? QColor(95, 168, 216, 50) : dark ? QColor(255, 255, 255, 30) : QColor(255, 255, 255, 150));
        if (ngAurora()) { // no edges; the veil of every other surface, a little more under the pointer
            painter.setPen(Qt::NoPen);
            QColor veil = ngAuroraVeil(dark, true);
            veil.setAlphaF(veil.alphaF() + (hovered ? 0.06 : 0.0));
            painter.setBrush(veil);
        }
        painter.drawRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);
        ngDrawChevron(&painter, QRectF(pill.rect.left() + 6, pill.rect.top(), 12, pill.rect.height()), pill.folded, dim);
        const QRect textRect = pill.rect.adjusted(19, 0, -9, 0);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, pill.text);
        if (pill.folded) {
            painter.setPen(dim);
            painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(pill.count));
        }
    }
}

qreal PlacesPanel::ngRowFactor(const QModelIndex &index, bool *first) const
{
    auto *placesModel = qobject_cast<KFilePlacesModel *>(model());
    if (m_ngAnimKey.isEmpty() || !placesModel || !index.isValid() || ngGroupKey(placesModel->groupType(index)) != m_ngAnimKey) {
        return 1.0;
    }
    if (selectionModel() && selectionModel()->isSelected(index)) {
        return 1.0; // the entry of the open folder stays
    }
    *first = ngFirstOfGroup(placesModel, index.row());
    return m_ngAnimFactor;
}

int PlacesPanel::ngPlainRowHeight() const
{
    const QAbstractItemModel *placesModel = model();
    if (!placesModel) {
        return -1;
    }
    // a shown entry that follows another one of its section, not sliding: its height
    for (int row = 1; row < placesModel->rowCount(); ++row) {
        const QModelIndex here = placesModel->index(row, 0);
        bool first = false;
        if (isRowHidden(row) || isRowHidden(row - 1) || ngHeaderOnly(here) || ngRowFactor(here, &first) < 1.0
            || here.data(KFilePlacesModel::GroupRole) != placesModel->index(row - 1, 0).data(KFilePlacesModel::GroupRole)) {
            continue;
        }
        return visualRect(here).height();
    }
    // none (every section folded): KDE's delegate gives a hidden row no title, so its size is the plain one
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    for (int row = 0; row < placesModel->rowCount(); ++row) {
        if (isRowHidden(row)) {
            return itemDelegate()->sizeHint(option, placesModel->index(row, 0)).height();
        }
    }
    return -1;
}

void PlacesPanel::ngAnimateSection(const QString &key)
{
    if (m_ngAnim && m_ngAnim->state() == QAbstractAnimation::Running) {
        m_ngAnim->setCurrentTime(m_ngAnim->duration()); // the slide under way ends at once
    }
    const bool folding = !ngSectionCollapsed(key);
    m_ngAnimKey = key;
    m_ngAnimFactor = folding ? 1.0 : 0.0;
    if (!folding) {
        ngToggleSection(key); // the rows come back, at no height, then grow
    }
    if (!m_ngAnim) {
        m_ngAnim = new QVariantAnimation(this);
        m_ngAnim->setDuration(180);
        m_ngAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_ngAnim->disconnect(this, nullptr); // the connections of the previous slide
    connect(m_ngAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_ngAnimFactor = value.toReal();
        scheduleDelayedItemsLayout();
        viewport()->update();
    });
    connect(m_ngAnim, &QAbstractAnimation::finished, this, [this, key, folding] {
        m_ngAnimKey.clear();
        m_ngAnimFactor = 1.0;
        if (folding) {
            ngToggleSection(key); // now folded: the rows are hidden, the first one keeps the title
        }
        scheduleDelayedItemsLayout();
        viewport()->update();
    });
    m_ngAnim->setStartValue(folding ? 1.0 : 0.0);
    m_ngAnim->setEndValue(folding ? 0.0 : 1.0);
    m_ngAnim->start();
}

QString PlacesPanel::ngTitleAt(const QPoint &pos) const
{
    for (const auto &[rect, key] : m_ngTitleHits) {
        if (rect.contains(pos)) {
            return key;
        }
    }
    return QString();
}

void PlacesPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !ngTitleAt(event->pos()).isEmpty()) {
        event->accept(); // a section title: no selection, no drag
        return;
    }
    KFilePlacesView::mousePressEvent(event);
}

void PlacesPanel::mouseReleaseEvent(QMouseEvent *event)
{
    const QString key = ngTitleAt(event->pos());
    if (event->button() == Qt::LeftButton && !key.isEmpty()) {
        event->accept();
        ngAnimateSection(key); // the panel follows NgSettings::changed
        return;
    }
    KFilePlacesView::mouseReleaseEvent(event);
}

void PlacesPanel::mouseMoveEvent(QMouseEvent *event)
{
    const QString key = ngTitleAt(event->pos());
    if (key != m_ngTitleHover) {
        m_ngTitleHover = key;
        viewport()->setCursor(key.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        setToolTip(key.isEmpty() ? QString()
                       : ngSectionCollapsed(key) ? ngTr("Click to show this section", "Clic per mostrare la sezione")
                                                 : ngTr("Click to fold this section", "Clic per richiudere la sezione"));
        viewport()->update();
    }
    KFilePlacesView::mouseMoveEvent(event);
}

void PlacesPanel::setUrl(const QUrl &url)
{
    KFilePlacesView::setUrl(url);
}

QList<QAction *> PlacesPanel::customContextMenuActions() const
{
    return m_customContextMenuActions;
}

void PlacesPanel::setCustomContextMenuActions(const QList<QAction *> &actions)
{
    m_customContextMenuActions = actions;
}

void PlacesPanel::proceedWithTearDown()
{
    if (m_indexToTearDown.isValid()) {
        auto *placesModel = static_cast<KFilePlacesModel *>(model());
        placesModel->requestTeardown(m_indexToTearDown);
    } else {
        qWarning() << "Places entry to tear down is no longer valid";
    }
}

void PlacesPanel::readSettings()
{
    if (GeneralSettings::autoExpandFolders()) {
        setDragAutoActivationDelay(750);
    } else {
        setDragAutoActivationDelay(0);
    }

    const int iconSize = qMax(0, PlacesPanelSettings::iconSize());
    setIconSize(QSize(iconSize, iconSize));
}

static bool isInternalDrag(const QMimeData *mimeData)
{
    const auto formats = mimeData->formats();
    for (const auto &format : formats) {
        // from KFilePlacesModel::_k_internalMimetype
        if (format.startsWith(QLatin1String("application/x-kfileplacesmodel-"))) {
            return true;
        }
    }
    return false;
}

void PlacesPanel::dragMoveEvent(QDragMoveEvent *event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    if (index.isValid()) {
        auto *placesModel = static_cast<KFilePlacesModel *>(model());

        // Reject drag ontop of a non-writable protocol
        // We don't know whether we're dropping inbetween or ontop of a place
        // so still allow internal drag events so that re-arranging still works.
        if (!isInternalDrag(event->mimeData())) {
            const QUrl url = placesModel->url(index);
            if (!url.isValid() || !KProtocolManager::supportsWriting(url)) {
                event->setDropAction(Qt::IgnoreAction);
            } else {
                DragAndDropHelper::updateDropAction(event, url);
            }
        }
    }

    KFilePlacesView::dragMoveEvent(event);
}

void PlacesPanel::slotConfigureTrash()
{
    const QUrl url = currentIndex().data(KFilePlacesModel::UrlRole).toUrl();

    DolphinSettingsDialog *settingsDialog = new DolphinSettingsDialog(url, this);
    settingsDialog->setCurrentPage(settingsDialog->trashSettings);
    settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    settingsDialog->show();
}

void PlacesPanel::slotUrlsDropped(const QUrl &dest, QDropEvent *event, QWidget *parent)
{
    KIO::DropJob *job = DragAndDropHelper::dropUrls(dest, event, parent);
    if (job) {
        connect(job, &KIO::DropJob::result, this, [this](KJob *job) {
            if (job->error() && job->error() != KIO::ERR_USER_CANCELED) {
                Q_EMIT errorMessage(job->errorString());
            }
        });
    }
}

void PlacesPanel::slotContextMenuAboutToShow(const QModelIndex &index, QMenu *menu)
{
    Q_UNUSED(menu);

    auto *placesModel = static_cast<KFilePlacesModel *>(model());
    const QUrl url = placesModel->url(index);
    const Solid::Device device = placesModel->deviceForIndex(index);

    m_configureTrashAction->setVisible(url.scheme() == QLatin1String("trash"));
    m_openInSplitView->setVisible(url.isValid());

    // show customContextMenuActions only on the view's context menu
    if (!url.isValid() && !device.isValid()) {
        addActions(m_customContextMenuActions);
    } else {
        const auto actions = this->actions();
        for (QAction *action : actions) {
            if (m_customContextMenuActions.contains(action)) {
                removeAction(action);
            }
        }
    }
}

void PlacesPanel::slotTearDownRequested(const QModelIndex &index)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
    if (!storageAccess) {
        return;
    }

    m_indexToTearDown = QPersistentModelIndex(index);

    // disconnect the Solid::StorageAccess::teardownRequested
    // to prevent emitting PlacesPanel::storageTearDownExternallyRequested
    // after we have emitted PlacesPanel::storageTearDownRequested
    disconnect(storageAccess, &Solid::StorageAccess::teardownRequested, this, &PlacesPanel::slotTearDownRequestedExternally);
    Q_EMIT storageTearDownRequested(storageAccess->filePath());
}

void PlacesPanel::slotTearDownRequestedExternally(const QString &udi)
{
    Q_UNUSED(udi);
    auto *storageAccess = static_cast<Solid::StorageAccess *>(sender());

    Q_EMIT storageTearDownExternallyRequested(storageAccess->filePath());
}

void PlacesPanel::slotTearDownDone(const QModelIndex &index, Solid::ErrorType error, const QVariant &errorData)
{
    Q_UNUSED(errorData); // All error handling is currently done in frameworks.

    if (index == m_indexToTearDown) {
        if (error == Solid::ErrorType::NoError) {
            // No error; it must have been unmounted successfully
            Q_EMIT storageTearDownSuccessful();
        }
        m_indexToTearDown = QPersistentModelIndex();
    }
}

void PlacesPanel::slotRowsInserted(const QModelIndex &parent, int first, int last)
{
    for (int i = first; i <= last; ++i) {
        connectDeviceSignals(model()->index(i, 0, parent));
    }
}

void PlacesPanel::slotRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    for (int i = first; i <= last; ++i) {
        const QModelIndex index = placesModel->index(i, 0, parent);

        Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
        if (!storageAccess) {
            continue;
        }

        disconnect(storageAccess, &Solid::StorageAccess::teardownRequested, this, nullptr);
    }
}

void PlacesPanel::connectDeviceSignals(const QModelIndex &index)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
    if (!storageAccess) {
        return;
    }

    connect(storageAccess, &Solid::StorageAccess::teardownRequested, this, &PlacesPanel::slotTearDownRequestedExternally);
}

#include "moc_placespanel.cpp"

// dolphin-ng: the filters section sits at the bottom of the panel, above the disk usage box; the entries end above it
void PlacesPanel::ngSetBottomWidget(QWidget *widget)
{
    m_ngBottom = widget;
    if (widget) {
        widget->setParent(this);
        widget->installEventFilter(this);
        widget->raise();
    }
    ngLayoutBottom();
}

// dolphin-ng: when there are more places below (or above) than fit, the list fades out at that edge instead of
// ending on a row cut in half
namespace
{
// dolphin-ng: with the compositor's blur the glass is see-through, so the list cannot fade "to a colour". This strip
// lies over one end of the list and blends what is under it into the bare glass: the list is thinned out by a
// gradient and the glass ground (the window colour, thinned by the dock exactly as around the list) is added back
// by the same gradient.
class NgListFade : public QWidget
{
public:
    NgListFade(QWidget *parent, bool top)
        : QWidget(parent)
        , m_top(top)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        auto *dock = parentWidget() ? qobject_cast<DolphinDockWidget *>(parentWidget()->parentWidget()) : nullptr;
        if (!dock) {
            return;
        }
        QLinearGradient mask(0, 0, 0, height());
        mask.setColorAt(m_top ? 0.0 : 1.0, QColor(0, 0, 0, 255));
        mask.setColorAt(m_top ? 0.35 : 0.65, QColor(0, 0, 0, 150));
        mask.setColorAt(m_top ? 1.0 : 0.0, QColor(0, 0, 0, 0));
        const qreal dpr = devicePixelRatioF();
        QImage ground((QSizeF(size()) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        ground.setDevicePixelRatio(dpr);
        ground.fill(Qt::transparent);
        {
            QPainter painter(&ground);
            DolphinDockWidget::ngPaintWindowGround(painter, this, rect());
            painter.save();
            painter.translate(-mapTo(dock, QPoint(0, 0)));
            dock->paintNgGlass(painter);
            painter.restore();
            painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            painter.fillRect(rect(), mask);
        }
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.fillRect(rect(), mask);
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        painter.drawImage(QPointF(0, 0), ground);
    }

private:
    bool m_top;
};
}

void PlacesPanel::ngUpdateFade()
{
    QScrollBar *bar = verticalScrollBar();
    const bool below = bar->value() < bar->maximum();
    const bool above = bar->value() > bar->minimum();
    // (the effect renders the list off screen at every repaint; with the compositor's blur the window is
    // translucent and that is too slow, so there the list simply ends)
    const bool live = ngSetting("GLASS_MODE").toLower() == QLatin1String("live") && ngSetting("SIDEBAR").toLower() != QLatin1String("plain");
    if (live) {
        // two strips over the ends of the list do the fading (cheap: they repaint only their own few rows)
        if (!m_ngFadeTop) {
            m_ngFadeTop = new NgListFade(this, true);
            m_ngFadeBottom = new NgListFade(this, false);
        }
        const QRect list = viewport()->geometry();
        const int strip = qMin(30, list.height() / 3);
        m_ngFadeTop->setGeometry(list.left(), list.top(), list.width(), strip);
        m_ngFadeBottom->setGeometry(list.left(), list.bottom() + 1 - strip, list.width(), strip);
        m_ngFadeTop->setVisible(above);
        m_ngFadeBottom->setVisible(below);
        m_ngFadeTop->raise();
        m_ngFadeBottom->raise();
        if (m_ngOverlayBar) {
            m_ngOverlayBar->raise();
        }
    } else if (m_ngFadeTop) {
        m_ngFadeTop->hide();
        m_ngFadeBottom->hide();
    }
    if ((!below && !above) || live) {
        if (viewport()->graphicsEffect()) {
            viewport()->setGraphicsEffect(nullptr);
        }
        return;
    }
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(viewport()->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(viewport());
        effect->setOpacity(1.0);
        viewport()->setGraphicsEffect(effect);
    }
    const qreal height = qMax(1, viewport()->height());
    const qreal fade = qMin(36.0, height / 3.0) / height;
    QLinearGradient mask(0, 0, 0, height);
    mask.setColorAt(0.0, above ? Qt::transparent : Qt::black);
    mask.setColorAt(above ? fade : 0.0, Qt::black);
    mask.setColorAt(below ? 1.0 - fade : 1.0, Qt::black);
    mask.setColorAt(1.0, below ? Qt::transparent : Qt::black);
    effect->setOpacityMask(mask);
}

// dolphin-ng: Qt's own scroll bar takes a column at the right edge of the panel, outside the glass card. It is
// switched off; a thin bar of ours, tied to it, floats inside the card beside the places.
void PlacesPanel::ngPlaceScrollBar()
{
    QScrollBar *real = verticalScrollBar();
    if (!m_ngOverlayBar) {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_ngOverlayBar = new QScrollBar(Qt::Vertical, this);
        m_ngOverlayBar->setStyleSheet(QStringLiteral("QScrollBar:vertical { background: transparent; width: 7px; margin: 0; border: 0; }"
                                                     "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 0.34); border-radius: 3px; min-height: 28px; }"
                                                     "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 0.55); }"
                                                     "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                                                     "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
        auto sync = [this, real] {
            m_ngOverlayBar->setRange(real->minimum(), real->maximum());
            m_ngOverlayBar->setPageStep(real->pageStep());
            m_ngOverlayBar->setSingleStep(real->singleStep());
            m_ngOverlayBar->setValue(real->value());
            // hidden at once when there is nothing to scroll; shown only when there still is a moment later (choosing
            // a place lays the list out again, and for an instant it seemed to need the bar: it flashed)
            if (real->maximum() <= real->minimum()) {
                m_ngOverlayBar->hide();
            } else if (!m_ngOverlayBar->isVisible()) {
                QTimer::singleShot(250, m_ngOverlayBar, [this, real] {
                    m_ngOverlayBar->setVisible(real->maximum() > real->minimum());
                });
            }
        };
        connect(real, &QScrollBar::rangeChanged, m_ngOverlayBar, sync);
        connect(real, &QScrollBar::valueChanged, m_ngOverlayBar, &QScrollBar::setValue);
        connect(m_ngOverlayBar, &QScrollBar::valueChanged, real, &QScrollBar::setValue);
        sync();
        // out of sight until the pointer is over the panel: it fades in and out
        m_ngBarOpacity = new QGraphicsOpacityEffect(m_ngOverlayBar);
        m_ngBarOpacity->setOpacity(underMouse() ? 1.0 : 0.0);
        m_ngOverlayBar->setGraphicsEffect(m_ngBarOpacity);
        m_ngBarFade = new QPropertyAnimation(m_ngBarOpacity, "opacity", this);
        m_ngBarFade->setDuration(220);
        m_ngBarFade->setEasingCurve(QEasingCurve::InOutQuad);
    }
    const QRect list = viewport()->geometry();
    m_ngOverlayBar->setGeometry(width() - 7 - 5, list.top() + 4, 7, qMax(20, list.height() - 8));
    m_ngOverlayBar->raise();
}

void PlacesPanel::ngFadeBar(bool in)
{
    if (!m_ngBarFade) {
        return;
    }
    m_ngBarFade->stop();
    m_ngBarFade->setStartValue(m_ngBarOpacity->opacity());
    m_ngBarFade->setEndValue(in ? 1.0 : 0.0);
    m_ngBarFade->start();
}

void PlacesPanel::enterEvent(QEnterEvent *event)
{
    ngFadeBar(true);
    KFilePlacesView::enterEvent(event);
}

void PlacesPanel::leaveEvent(QEvent *event)
{
    ngFadeBar(false);
    KFilePlacesView::leaveEvent(event);
}

void PlacesPanel::ngSetSections(QWidget *pinned, QWidget *filters)
{
    m_ngPinned = pinned;
    m_ngFilters = filters;
    for (QWidget *widget : {pinned, filters}) {
        if (widget) {
            widget->setParent(this);
            widget->installEventFilter(this);
            widget->raise();
        }
    }
    ngLayoutBottom();
}

// dolphin-ng: the order of the sidebar's sections (NG settings > Sidebar): before "places", fixed at the top; after
// it, fixed at the foot; the places scroll in between
QStringList ngSidebarOrder()
{
    QStringList order;
    const QStringList known{QStringLiteral("places"), QStringLiteral("pinned"), QStringLiteral("filters"), QStringLiteral("disks")};
    for (const QString &name : ngSetting("SIDEBAR_ORDER").toLower().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (known.contains(name.trimmed()) && !order.contains(name.trimmed())) {
            order.append(name.trimmed());
        }
    }
    if (order.isEmpty()) {
        // the earlier setting: the disks above the filters, or at the foot
        order = ngSetting("DISKS_POS").toLower() == QLatin1String("above")
            ? QStringList{QStringLiteral("places"), QStringLiteral("disks"), QStringLiteral("pinned"), QStringLiteral("filters")}
            : QStringList{QStringLiteral("places"), QStringLiteral("pinned"), QStringLiteral("filters"), QStringLiteral("disks")};
    }
    for (const QString &name : known) {
        if (!order.contains(name)) {
            order.append(name);
        }
    }
    return order;
}

void PlacesPanel::ngLayoutBottom()
{
    if (!m_ngBarWatched) {
        m_ngBarWatched = true;
        verticalScrollBar()->installEventFilter(this);
    }
    if (!m_ngFadeWatched) {
        m_ngFadeWatched = true;
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &PlacesPanel::ngUpdateFade);
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &PlacesPanel::ngUpdateFade);
    }
    // the old single section (pinned items and filters together), when the window set that one
    if (m_ngBottom && !m_ngPinned && !m_ngFilters) {
        m_ngFilters = m_ngBottom;
    }
    const QStringList order = ngSidebarOrder();
    const int placesAt = order.indexOf(QStringLiteral("places"));
    const int diskRoom = DolphinDockWidget::ngDiskRoom();
    auto widgetOf = [this](const QString &name) -> QWidget * {
        return name == QLatin1String("pinned") ? m_ngPinned : name == QLatin1String("filters") ? m_ngFilters : nullptr;
    };
    bool sectionsShown = true;
    auto heightOf = [&](const QString &name) {
        if (name == QLatin1String("disks")) {
            return diskRoom;
        }
        QWidget *widget = widgetOf(name);
        return sectionsShown && widget && !widget->isHidden() ? widget->height() + 8 : 0;
    };
    auto total = [&](int from, int to) {
        int sum = 0;
        for (int i = from; i < to; ++i) {
            sum += heightOf(order.at(i));
        }
        return sum;
    };
    int topTotal = total(0, placesAt);
    int bottomTotal = total(placesAt + 1, order.count());
    if (height() - 24 - topTotal - bottomTotal < 150) {
        // a short window: the places come first, the pinned items and the filters step aside (parked below)
        sectionsShown = false;
        topTotal = total(0, placesAt);
        bottomTotal = total(placesAt + 1, order.count());
    }
    for (QWidget *widget : {m_ngPinned, m_ngFilters}) {
        if (widget && !sectionsShown) {
            widget->move(14, height() + 20);
        }
    }
    setViewportMargins(16, 12 + topTotal, 16, 12 + bottomTotal); // (16 at the sides: the pill of the selected entry keeps clear of the card's edges)
    ngPlaceScrollBar();

    // top down, then bottom up; the disks are drawn by the dock where they are given room
    int disksAt = -1;
    int y = 12;
    for (int i = 0; i < placesAt; ++i) {
        const QString &name = order.at(i);
        if (name == QLatin1String("disks")) {
            disksAt = y;
        } else if (QWidget *widget = widgetOf(name); widget && sectionsShown && !widget->isHidden()) {
            widget->setGeometry(18, y, width() - 36, widget->height());
        }
        y += heightOf(name);
    }
    y = height() - 12;
    for (int i = order.count() - 1; i > placesAt; --i) {
        const QString &name = order.at(i);
        y -= heightOf(name);
        if (name == QLatin1String("disks")) {
            disksAt = y;
        } else if (QWidget *widget = widgetOf(name); widget && sectionsShown && !widget->isHidden()) {
            widget->setGeometry(18, y + 8, width() - 36, widget->height());
        }
    }
    if (property("ngDisksY").toInt() != disksAt) {
        setProperty("ngDisksY", disksAt);
        if (parentWidget()) {
            parentWidget()->update();
        }
    }
}

bool PlacesPanel::eventFilter(QObject *watched, QEvent *event)
{
    const bool barToggled = m_ngBarWatched && watched == verticalScrollBar() && (event->type() == QEvent::Show || event->type() == QEvent::Hide);
    const bool section = watched == m_ngBottom || watched == m_ngPinned || watched == m_ngFilters;
    if (barToggled || (section && (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide))) {
        if (!m_ngLayouting) {
            m_ngLayouting = true;
            ngLayoutBottom();
            m_ngLayouting = false;
        }
    }
    return KFilePlacesView::eventFilter(watched, event);
}

void PlacesPanel::resizeEvent(QResizeEvent *event)
{
    KFilePlacesView::resizeEvent(event);
    ngLayoutBottom();
    ngUpdateFade();
    ngPlaceScrollBar();
}
