/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinitemlistview.h"

#include "dolphin_cardsmodesettings.h"
#include "dolphin_compactmodesettings.h"
#include "dolphin_contentdisplaysettings.h"
#include "dolphin_detailsmodesettings.h"
#include "dolphin_generalsettings.h"
#include "dolphin_iconsmodesettings.h"
#include "dolphinfileitemlistwidget.h"
#include "kitemviews/kitemlistwidget.h"
#include "settings/viewmodes/viewmodesettings.h"
#include "zoomlevelinfo.h"

#include <KIO/PreviewJob>
#include <QGraphicsSceneResizeEvent>
#include <QtMath>

DolphinItemListView::DolphinItemListView(QGraphicsWidget *parent)
    : KFileItemListView(parent)
    , m_zoomLevel(0)
{
    // dolphin-ng: the NG settings apply without a restart
    connect(NgSettings::instance(), &NgSettings::changed, this, [this] {
        setHeaderVisible(itemLayout() == DetailsLayout && (!ngTwoLineList() || ngRowColumns()));
        updateFont();
        updateGridSize();
        ngRefresh();
    });
    DolphinItemListView::updateFont();
    updateGridSize();
}

void DolphinItemListView::ngSetCardLook(int look)
{
    const QVariant before = property("_ng_card_style");
    if (before.isValid() && before.toInt() == look) {
        return;
    }
    setProperty("_ng_card_style", look);
    updateFont();
    updateGridSize();
    ngRefresh();
}

DolphinItemListView::~DolphinItemListView()
{
    writeSettings();
}

void DolphinItemListView::setZoomLevel(int level)
{
    if (level < ZoomLevelInfo::minimumLevel()) {
        level = ZoomLevelInfo::minimumLevel();
    } else if (level > ZoomLevelInfo::maximumLevel()) {
        level = ZoomLevelInfo::maximumLevel();
    }

    const bool useGlobalViewProps = GeneralSettings::globalViewProps();
    ViewModeSettings settings(itemLayout());

    // The size belonging to the requested zoom level must be applied even when the zoom level
    // itself did not change: m_iconSize/m_previewSize may still be unset, e.g. because this is
    // the first call for the current view mode. Otherwise the view would keep using a stale size.
    const int size = ZoomLevelInfo::iconSizeForZoomLevel(level);
    bool changed = (level != m_zoomLevel);

    if (previewsShown()) {
        changed = changed || (m_previewSize != size);
        m_previewSize = size;
        // Only update the icon size settings if we're using global view props
        // to prevent inconsistent state on zoom level changes
        if (useGlobalViewProps) {
            settings.setPreviewSize(m_previewSize);
        }
    } else {
        // Same as above
        changed = changed || (m_iconSize != size);
        m_iconSize = size;
        if (useGlobalViewProps) {
            settings.setIconSize(m_iconSize);
        }
    }

    if (!changed) {
        return;
    }

    m_zoomLevel = level;

    updateGridSize();
}

int DolphinItemListView::zoomLevel() const
{
    return m_zoomLevel;
}

void DolphinItemListView::setEnabledSelectionToggles(DolphinItemListView::SelectionTogglesEnabled selectionTogglesEnabled)
{
    m_selectionTogglesEnabled = selectionTogglesEnabled;
    switch (m_selectionTogglesEnabled) {
    case True: {
        setEnabledSelectionToggles(true);
        return;
    }
    case False: {
        setEnabledSelectionToggles(false);
        return;
    }
    case FollowSetting: {
        setEnabledSelectionToggles(GeneralSettings::showSelectionToggle());
        return;
    }
    }
}

void DolphinItemListView::readSettings()
{
    // We load the settings for all view modes now because we don't load them when the view mode changes.
    IconsModeSettings::self()->load();
    CompactModeSettings::self()->load();
    DetailsModeSettings::self()->load();
    CardsModeSettings::self()->load();

    ContentDisplaySettings::self()->load();

    beginTransaction();

    setEnabledSelectionToggles(m_selectionTogglesEnabled);
    setHighlightEntireRow(itemLayoutHighlightEntireRow(itemLayout()));
    setSupportsItemExpanding(itemLayoutSupportsItemExpanding(itemLayout()));

    updateFont();
    updateGridSize();

    const KConfigGroup globalConfig(KSharedConfig::openConfig(), QStringLiteral("PreviewSettings"));
    setEnabledPlugins(globalConfig.readEntry("Plugins", KIO::PreviewJob::defaultPlugins()));
    endTransaction();
}

void DolphinItemListView::writeSettings()
{
    IconsModeSettings::self()->save();
    CompactModeSettings::self()->save();
    DetailsModeSettings::self()->save();
    CardsModeSettings::self()->save();
}

void DolphinItemListView::resizeEvent(QGraphicsSceneResizeEvent *event)
{
    KFileItemListView::resizeEvent(event);
    if (itemLayout() == CardsLayout && ngCardsStretch() && event->newSize().width() != event->oldSize().width()) {
        updateGridSize();
    }
}

KItemListWidgetCreatorBase *DolphinItemListView::defaultWidgetCreator() const
{
    return new KItemListWidgetCreator<DolphinFileItemListWidget>();
}

bool DolphinItemListView::itemLayoutHighlightEntireRow(ItemLayout layout) const
{
    return layout == DetailsLayout && DetailsModeSettings::highlightEntireRow();
}

bool DolphinItemListView::itemLayoutSupportsItemExpanding(ItemLayout layout) const
{
    return layout == DetailsLayout && DetailsModeSettings::expandableFolders();
}

void DolphinItemListView::onItemLayoutChanged(ItemLayout current, ItemLayout previous)
{
    setHeaderVisible(current == DetailsLayout && (!ngTwoLineList() || ngRowColumns())); // dolphin-ng: the two-line list has no columns, unless asked

    updateFont();
    updateGridSize();

    KFileItemListView::onItemLayoutChanged(current, previous);
}

void DolphinItemListView::onPreviewsShownChanged(bool shown)
{
    Q_UNUSED(shown)
    updateGridSize();
}

void DolphinItemListView::onVisibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous)
{
    KFileItemListView::onVisibleRolesChanged(current, previous);
    updateGridSize();
}

void DolphinItemListView::updateFont()
{
    const ViewModeSettings settings(itemLayout());
    KItemListStyleOption option = styleOption();

    if (settings.useSystemFont()) {
        KItemListView::updateFont();
        option.font = font();
    } else {
        option.font = settings.viewFont();
    }

    option.fontMetrics = QFontMetrics(option.font);
    setStyleOption(option);
}

void DolphinItemListView::updateGridSize()
{
    const ViewModeSettings settings(itemLayout());
    const bool useGlobalViewProps = GeneralSettings::globalViewProps();

    // Calculate the size of the icon
    // Only use zoom stored in settings if we're using global view props
    const int configuredSize = previewsShown() ? settings.previewSize() : settings.iconSize();
    // The cached size is the per-folder zoom, shared by every view mode, so the fallback below must
    // not be stored: it would reach the other modes as though the folder carried it.
    const int cachedSize = previewsShown() ? m_previewSize : m_iconSize;
    const int iconSize = (useGlobalViewProps || cachedSize <= 0) ? configuredSize : cachedSize;
    m_zoomLevel = ZoomLevelInfo::zoomLevelForIconSize(QSize(iconSize, iconSize));
    KItemListStyleOption option = styleOption();

    const int padding = 2;
    int horizontalMargin = 0;
    int verticalMargin = 0;

    // Calculate the item-width and item-height
    int itemWidth;
    int itemHeight;
    int maxTextLines = 0;
    int maxTextWidth = 0;

    switch (itemLayout()) {
    case KFileItemListView::IconsLayout: {
        // an exponential factor based on zoom, 0 -> 1, 4 -> 1.36 8 -> ~1.85, 16 -> 3.4
        auto zoomFactor = qExp(m_zoomLevel / 13.0);
        // 9 is the average char width for 10pt Noto Sans, making fontFactor =1
        // by each pixel the font gets larger the factor increases by 1/9
        auto fontFactor = option.fontMetrics.averageCharWidth() / 9.0;
        // 48 is chosen as the magic number here to give more room for the text to be readable
        itemWidth = 48 + IconsModeSettings::textWidthIndex() * 64 * fontFactor * zoomFactor;

        if (itemWidth < iconSize + padding * 2 * zoomFactor) {
            itemWidth = iconSize + padding * 2 * zoomFactor;
        }

        itemHeight = padding * 3 + iconSize + option.fontMetrics.lineSpacing();

        const auto margin = style()->pixelMetric(QStyle::PM_SizeGripSize);
        horizontalMargin = margin;
        verticalMargin = margin;
        maxTextLines = IconsModeSettings::maximumTextLines();
        break;
    }
    case KFileItemListView::CompactLayout: {
        itemWidth = padding * 4 + iconSize + option.fontMetrics.height() * 5;
        const int textLinesCount = visibleRoles().count();
        itemHeight = padding * 2 + qMax(iconSize, textLinesCount * option.fontMetrics.lineSpacing());

        if (CompactModeSettings::maximumTextWidthIndex() > 0) {
            // A restriction is given for the maximum width of the text (0 means
            // having no restriction)
            maxTextWidth = option.fontMetrics.height() * 10 * CompactModeSettings::maximumTextWidthIndex();
        }

        horizontalMargin = 8;
        break;
    }
    case KFileItemListView::DetailsLayout: {
        itemWidth = -1;
        itemHeight = padding * 2 + qMax(iconSize, option.fontMetrics.lineSpacing());
        if (ngNoIcons()) {
            // dolphin-ng: the icon-less row is two lines of text plus its own padding, whatever the icon size
            itemHeight = padding * 2 + 2 * option.fontMetrics.height() + 4 + 24;
        }
        break;
    }
    case KFileItemListView::CardsLayout: {
        // dolphin-ng: a card of a fixed size per look, scaled with the zoom and the font. The zoom is read
        // against the default size of the mode (48 px icons, 128 px previews) so that the default level is 1:1.
        static const QSizeF base[] = {{236, 262}, {312, 108}, {320, 200}}; // Tessera, Scheda, Dossier
        const int look = qBound(0, int(ngCardStyle(this)), 2);
        const qreal zoom = qBound(0.7, 0.6 + 0.4 * (iconSize / (previewsShown() ? 128.0 : 48.0)), 1.6);
        const qreal scale = zoom * qBound(0.8, option.fontMetrics.height() / 19.0, 2.0); // 19 px: a 10 pt Noto Sans line
        itemWidth = qRound(base[look].width() * scale);
        itemHeight = qRound(base[look].height() * scale);
        horizontalMargin = 10;
        verticalMargin = 10;
        if (ngCardsStretch()) {
            // as many cards of at least that width as fit, then widened so that the row is full
            const qreal available = size().width() - horizontalMargin;
            if (available > itemWidth + horizontalMargin) {
                const int columns = qMax(1, int(available / (itemWidth + horizontalMargin)));
                itemWidth = int(available / columns) - horizontalMargin - 1;
            }
        }
        if (look == 0) {
            // a Tessera is as tall as what it shows: the band (which follows the width), the name, the chips and
            // the lines of details. The same measures as KStandardItemListWidget::updateCardsLayoutTextCache().
            QFont nameFont = option.font;
            nameFont.setWeight(QFont::DemiBold);
            QFont small = option.font;
            small.setPointSizeF(small.pointSizeF() * 0.9);
            QFont chipFont = option.font;
            chipFont.setPointSizeF(chipFont.pointSizeF() * 0.88);
            const qreal fh = QFontMetricsF(option.font).height();
            const qreal pad = qBound(8.0, itemWidth * 0.05, 14.0);
            const qreal band = qRound(qMin(itemWidth * 0.40, fh * 9.0));
            const int lines = ngSetting("CARDS_INFO").toLower() == QLatin1String("full") ? 4 : 2;
            // the chips are on the band, unless they were given a row of their own
            const qreal chipRow = ngSetting("CARDS_BADGE").toLower() == QLatin1String("row") ? qRound(QFontMetricsF(chipFont).height() + 4) + 8 : 0;
            itemHeight = qRound(band + pad * 0.8 + QFontMetricsF(nameFont).height() + 6 + chipRow
                                + lines * (QFontMetricsF(small).height() + 2) + pad + 3); // 3: the widget rounds its own steps
        }
        break;
    }
    default:
        itemWidth = -1;
        itemHeight = -1;
        Q_ASSERT(false);
        break;
    }

    // Apply the calculated values
    option.padding = padding;
    option.horizontalMargin = horizontalMargin;
    option.verticalMargin = verticalMargin;
    option.iconSize = iconSize;
    option.maxTextLines = maxTextLines;
    option.maxTextWidth = maxTextWidth;
    beginTransaction();
    setStyleOption(option);
    setItemSize(QSizeF(itemWidth, itemHeight));
    endTransaction();
}

#include "moc_dolphinitemlistview.cpp"
