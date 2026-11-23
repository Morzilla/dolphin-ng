/*
 * SPDX-FileCopyrightText: 2012 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kstandarditemlistwidget.h"
#include "kitemviews/kitemlistwidget.h"

#include "dolphin_contentdisplaysettings.h"
#include "kfileitemlistview.h"
#include "kfileitemmodel.h"
#include "kitemlistcontroller.h"
#include "kitemlistselectionmanager.h"
#include "private/kfileitemclipboard.h"
#include "private/ngfacts.h"
#include "private/kitemlistroleeditor.h"
#include "private/kitemviewsutils.h"
#include "private/kpixmapmodifier.h"

#include <KIconEffect>
#include <KIconLoader>
#include <KIconUtils>
#include <KRatingPainter>
#include <KStandardGuiItem>
#include <KStringHandler>
#include <klocalizedstring.h>

#include <QApplication>
#include <QFontDatabase>
#include <QDateTime>
#include <QSet>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QGraphicsSceneResizeEvent>
#include <QGraphicsSceneMoveEvent>
#include <QTimer>
#include <QGraphicsView>
#include <QPixmapCache>
#include <QStyleOption>
#include <QTextBoundaryFinder>
#include <QVariantAnimation>
#include <QPointer>
#include <KFileItem>
#include <KIO/PreviewJob>
#include <QFileInfo>
#include <QDirIterator>
#include <QCollator>

#include <cmath>
#include <functional>

namespace
{
// dolphin-ng: how the file type is marked at the start of a row (setting MARK).
// Watermark is the faint icon and large abbreviation; the others pair a small line glyph with the abbreviation.
void ngPaintContentSketch(QPainter *painter, const QRectF &band, int category, const QString &sigla, const QColor &tint, bool dark, int count = -1,
                          const QString &sizeText = QString(), const QFont *font = nullptr, int special = 0, bool whole = false, const QStringList &tags = {},
                          bool openTabs = false, const QStringList &inside = {}, QList<QRectF> *insidePlaced = nullptr);

// dolphin-ng: the well-known folders of the home (xdg-user-dirs), recognised by where they are: 0 for any other
// folder, else 1 desktop, 2 downloads, 3 documents, 4 pictures, 5 music, 6 videos, 7 templates, 8 public
int ngSpecialFolder(const QString &path)
{
    static QHash<QString, int> known;
    static bool read = false;
    if (!read) {
        read = true;
        const QString home = QDir::homePath();
        auto add = [&](const QString &dir, int kind) {
            const QString clean = QDir::cleanPath(dir);
            if (!dir.isEmpty() && clean != home) { // an unset directory falls back to the home itself
                known.insert(clean, kind);
            }
        };
        add(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), 1);
        add(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), 2);
        add(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), 3);
        add(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), 4);
        add(QStandardPaths::writableLocation(QStandardPaths::MusicLocation), 5);
        add(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), 6);
        // templates and public share are not in QStandardPaths: read them from user-dirs.dirs
        QFile dirs(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/user-dirs.dirs"));
        if (dirs.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromUtf8(dirs.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                const int kind = line.startsWith(QLatin1String("XDG_TEMPLATES_DIR=")) ? 7 : line.startsWith(QLatin1String("XDG_PUBLICSHARE_DIR=")) ? 8 : 0;
                if (kind) {
                    QString value = line.section(QLatin1Char('='), 1).trimmed();
                    value.remove(QLatin1Char('"'));
                    value.replace(QLatin1String("$HOME"), home);
                    add(value, kind);
                }
            }
        }
    }
    return path.isEmpty() ? 0 : known.value(QDir::cleanPath(path), 0);
}

// ... and their colours
QColor ngSpecialFolderColour(int kind)
{
    static const QColor colours[] = {QColor(), QColor(0x6a, 0xa0, 0xb8), QColor(0x3f, 0xa7, 0xa0), QColor(0x4a, 0x8f, 0xe0), QColor(0x9b, 0x7b, 0xe0),
                                     QColor(0xe0, 0x7a, 0x9b), QColor(0xe0, 0x90, 0x4a), QColor(0x9a, 0xa3, 0xad), QColor(0x5b, 0xa3, 0xa3)};
    return kind > 0 && kind <= 8 ? colours[kind] : QColor();
}

// ... and their glyphs, in the NeoLine style (a 24 x 24 box, to be stroked)
QPainterPath ngSpecialFolderGlyph(int kind)
{
    QPainterPath p;
    auto lines = [&p](std::initializer_list<QPointF> points, bool close = false) {
        bool first = true;
        for (const QPointF &point : points) {
            if (first) {
                p.moveTo(point);
                first = false;
            } else {
                p.lineTo(point);
            }
        }
        if (close) {
            p.closeSubpath();
        }
    };
    switch (kind) {
    case 1: // desktop: a screen on its foot
        p.addRoundedRect(QRectF(3.5, 4.5, 17, 11), 1.5, 1.5);
        lines({{9, 20}, {15, 20}});
        lines({{12, 15.5}, {12, 20}});
        break;
    case 2: // downloads: an arrow into a tray
        lines({{12, 3.5}, {12, 14.5}});
        lines({{8, 10.5}, {12, 14.5}, {16, 10.5}});
        lines({{4, 15.5}, {4, 20}, {20, 20}, {20, 15.5}});
        break;
    case 3: // documents: a page with its corner and two lines
    case 7: // templates: the same, shorter lines
        lines({{6.5, 3.5}, {13.5, 3.5}, {17.5, 7.5}, {17.5, 20.5}, {6.5, 20.5}}, true);
        lines({{13.5, 3.5}, {13.5, 7.5}, {17.5, 7.5}});
        lines({{9, 12}, {kind == 7 ? 12.0 : 15.0, 12}});
        lines({{9, 15.5}, {15, 15.5}});
        break;
    case 4: // pictures: a frame, a sun, hills
        p.addRoundedRect(QRectF(3.5, 4.5, 17, 15), 1.5, 1.5);
        p.addEllipse(QPointF(9, 9.5), 1.6, 1.6);
        lines({{4, 17}, {9, 12}, {13, 16}, {16, 13}, {20, 17}});
        break;
    case 5: // music: two notes on a beam
        lines({{9, 18.5}, {9, 6}, {20, 3.5}, {20, 15.5}});
        p.addEllipse(QPointF(6.5, 18.5), 2.5, 2.5);
        p.addEllipse(QPointF(17.5, 15.5), 2.5, 2.5);
        break;
    case 6: // videos: a camera
        p.addRoundedRect(QRectF(3.5, 5.5, 13, 13), 1.5, 1.5);
        lines({{16.5, 10}, {20.5, 7.5}, {20.5, 16.5}, {16.5, 14}}, true);
        break;
    case 8: // public: a globe
        p.addEllipse(QPointF(12, 12), 8.5, 8.5);
        lines({{3.5, 12}, {20.5, 12}});
        p.addEllipse(QPointF(12, 12), 3.6, 8.5);
        break;
    default:
        break;
    }
    return p;
}

// Object (the default) draws the card's drawing of the file, small, with its type and size written in it.
enum class NgMark { Watermark, Tile, Pill, Signs, Header, Object };
NgMark ngMark()
{
    static NgMark mark = NgMark::Object;
    static int generation = -1;
    if (generation != ngSettingsGeneration()) {
        generation = ngSettingsGeneration();
        const QString name = ngSetting("MARK").toLower();
        mark = name == QLatin1String("tile") ? NgMark::Tile
            : name == QLatin1String("pill")  ? NgMark::Pill
            : name == QLatin1String("signs") ? NgMark::Signs
            : name == QLatin1String("header") ? NgMark::Header
            : name == QLatin1String("watermark") ? NgMark::Watermark
                                              : NgMark::Object;
    }
    return mark;
}


// dolphin-ng: the glass shadow under the text of a card. The card is a sheet of its own (frost on a light scheme,
// a veil on a dark one): the full shadow of the window's glass blurred its small text into a smudge. None on a light
// scheme, where the frost is bright enough; a light one on a dark scheme, where the veil is thin
static qreal ngCardHaloStrength(const QGraphicsItem *item)
{
    Q_UNUSED(item)
    return ngIsDark(QGuiApplication::palette()) ? 0.4 : 0.0;
}

QDateTime ngDateTimeOf(const QVariant &raw)
{
    // the model keeps the raw epoch seconds for the modification time (see KFileItemModel), a QDateTime for other roles
    if (raw.typeId() == QMetaType::QDateTime) {
        return raw.toDateTime();
    }
    return raw.toLongLong() > 0 ? QDateTime::fromSecsSinceEpoch(raw.toLongLong()) : QDateTime();
}

// A line drawing for each kind of file, in a 24 x 24 box, to be stroked.
QPainterPath ngGlyph(int category)
{
    QPainterPath p;
    auto lines = [&p](std::initializer_list<QPointF> points, bool close = false) {
        bool first = true;
        for (const QPointF &point : points) {
            if (first) {
                p.moveTo(point);
                first = false;
            } else {
                p.lineTo(point);
            }
        }
        if (close) {
            p.closeSubpath();
        }
    };
    switch (category) {
    case NgKind::Folder:
        lines({{3, 6}, {9, 6}, {11, 8.5}, {21, 8.5}, {21, 19}, {3, 19}}, true);
        break;
    case NgKind::Image:
        p.addRoundedRect(QRectF(4, 5, 16, 14), 1.5, 1.5);
        lines({{4.5, 16.5}, {8.5, 12.5}, {12, 16}, {15, 13}, {19.5, 17.5}});
        p.addEllipse(QPointF(15.5, 9.5), 1.3, 1.3);
        break;
    case NgKind::Sheet:
        p.addRoundedRect(QRectF(5, 4, 14, 16), 1.5, 1.5);
        lines({{5, 9.5}, {19, 9.5}});
        lines({{5, 14.5}, {19, 14.5}});
        lines({{11, 4}, {11, 20}});
        break;
    case NgKind::Archive:
        p.addRoundedRect(QRectF(5, 4, 14, 16), 1.5, 1.5);
        lines({{12, 4}, {12, 6}});
        lines({{12, 8}, {12, 10}});
        lines({{12, 12}, {12, 14}});
        p.addRect(QRectF(10.5, 15, 3, 2.5));
        break;
    case NgKind::Media:
        p.addEllipse(QPointF(12, 12), 8, 8);
        lines({{10, 8.5}, {16, 12}, {10, 15.5}}, true);
        break;
    case NgKind::Data:
        lines({{8.5, 7.5}, {4, 12}, {8.5, 16.5}});
        lines({{15.5, 7.5}, {20, 12}, {15.5, 16.5}});
        lines({{13.2, 6}, {10.8, 18}});
        break;
    case NgKind::Package:
        lines({{4, 8}, {12, 4}, {20, 8}, {20, 16}, {12, 20}, {4, 16}}, true);
        lines({{4, 8}, {12, 12}, {20, 8}});
        lines({{12, 12}, {12, 20}});
        break;
    case NgKind::Document:
        lines({{7, 3}, {14, 3}, {19, 8}, {19, 21}, {7, 21}}, true);
        lines({{14, 3}, {14, 8}, {19, 8}});
        lines({{10, 13}, {16, 13}});
        lines({{10, 17}, {16, 17}});
        break;
    default:
        lines({{7, 3}, {14, 3}, {19, 8}, {19, 21}, {7, 21}}, true);
        lines({{14, 3}, {14, 8}, {19, 8}});
        break;
    }
    return p;
}

// dolphin-ng: the main colour of an icon — the average of its pixels weighted by how saturated they are,
// lightened enough to read on a dark row. Cached per icon name. Invalid if the icon has no usable pixels.
QColor ngIconColor(const QString &iconName, const QPixmap &pixmap)
{
    static QHash<QString, QColor> cache;
    const auto cached = cache.constFind(iconName);
    if (cached != cache.constEnd()) {
        return *cached;
    }
    QColor result;
    if (!pixmap.isNull()) {
        const QImage image = pixmap.toImage().scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        double red = 0, green = 0, blue = 0, total = 0;
        for (int y = 0; y < image.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = QColor::fromRgba(line[x]);
                if (pixel.alpha() < 96) {
                    continue;
                }
                const double weight = 0.04 + pixel.hsvSaturationF() * pixel.valueF(); // colourful pixels count the most
                red += pixel.redF() * weight;
                green += pixel.greenF() * weight;
                blue += pixel.blueF() * weight;
                total += weight;
            }
        }
        if (total > 0) {
            const QColor average = QColor::fromRgbF(red / total, green / total, blue / total);
            float h, s, l;
            average.getHslF(&h, &s, &l);
            result = QColor::fromHslF(h < 0 ? 0 : h, s, qMax(l, 0.64f));
        }
    }
    cache.insert(iconName, result);
    return result;
}

// dolphin-ng: the two buttons shown on the hovered row, "Open" and a menu with everything else
class NgRowActions : public QGraphicsWidget
{
public:
    NgRowActions(QGraphicsItem *parent, std::function<void()> open, std::function<void(const QPoint &)> menu)
        : QGraphicsWidget(parent)
        , m_open(std::move(open))
        , m_menu(std::move(menu))
        , m_openText(KLocalizedString::removeAcceleratorMarker(KStandardGuiItem::open().text()))
    {
        while (m_openText.endsWith(QLatin1Char('.')) || m_openText.endsWith(QChar(0x2026))) {
            m_openText.chop(1); // "Open..." is a dialog caption; here it opens the item directly
        }
        setAcceptedMouseButtons(Qt::NoButton); // drawn here, clicked through KItemListController
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        Q_UNUSED(option)
        const QPalette palette = widget ? widget->palette() : QApplication::palette();
        const QColor text = palette.color(QPalette::WindowText);
        QColor fill = text;
        fill.setAlphaF(0.14);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(openRect(), 8, 8);
        painter->drawRoundedRect(previewRect(), 8, 8);
        painter->drawRoundedRect(menuRect(), 8, 8);
        painter->setPen(text);
        painter->setFont(font());
        painter->drawText(openRect(), Qt::AlignCenter, m_openText);
        painter->setPen(Qt::NoPen);
        painter->setBrush(text);
        for (int i = -1; i <= 1; ++i) {
            painter->drawEllipse(menuRect().center() + QPointF(i * 6.0, 0), 1.7, 1.7);
        }
        // preview: an eye
        const QPointF c = previewRect().center();
        painter->drawEllipse(c, 2.4, 2.4);
        QPainterPath eye;
        eye.moveTo(c + QPointF(-8, 0));
        eye.quadTo(c + QPointF(0, -8), c + QPointF(8, 0));
        eye.quadTo(c + QPointF(0, 8), c + QPointF(-8, 0));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(text, 1.5));
        painter->drawPath(eye);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        event->accept(); // keep the click away from the row underneath
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        event->accept();
        if (openRect().contains(event->pos())) {
            m_open();
        } else if (menuRect().contains(event->pos())) {
            m_menu(event->screenPos());
        }
    }

private:
    QRectF menuRect() const
    {
        const qreal h = size().height();
        return QRectF(size().width() - h, 0, h, h);
    }
    QRectF previewRect() const
    {
        const qreal h = size().height();
        return QRectF(size().width() - 2 * h - 6, 0, h, h);
    }
    QRectF openRect() const
    {
        const qreal h = size().height();
        return QRectF(0, 0, size().width() - 2 * h - 12, h);
    }

    std::function<void()> m_open;
    std::function<void(const QPoint &)> m_menu;
    QString m_openText;
};
}

// #define KSTANDARDITEMLISTWIDGET_DEBUG

KStandardItemListWidgetInformant::KStandardItemListWidgetInformant()
    : KItemListWidgetInformant()
{
}

KStandardItemListWidgetInformant::~KStandardItemListWidgetInformant() = default;

void KStandardItemListWidgetInformant::calculateItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                              qreal &logicalWidthHint,
                                                              const KItemListView *view) const
{
    switch (static_cast<const KStandardItemListView *>(view)->itemLayout()) {
    case KStandardItemListView::IconsLayout:
        calculateIconsLayoutItemSizeHints(logicalHeightHints, logicalWidthHint, view);
        break;

    case KStandardItemListView::CompactLayout:
        calculateCompactLayoutItemSizeHints(logicalHeightHints, logicalWidthHint, view);
        break;

    case KStandardItemListView::DetailsLayout:
        calculateDetailsLayoutItemSizeHints(logicalHeightHints, logicalWidthHint, view);
        break;

    case KStandardItemListView::CardsLayout:
        calculateCardsLayoutItemSizeHints(logicalHeightHints, logicalWidthHint, view);
        break;

    default:
        Q_ASSERT(false);
        break;
    }
}

void KStandardItemListWidgetInformant::calculateCardsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                                         qreal &logicalWidthHint,
                                                                         const KItemListView *view) const
{
    // dolphin-ng: every card has the size the view decided
    const QSizeF itemSize = view->itemSize();
    for (int index = 0; index < logicalHeightHints.count(); ++index) {
        if (logicalHeightHints.at(index).first > 0.0) {
            continue;
        }
        logicalHeightHints[index].first = itemSize.height();
        logicalHeightHints[index].second = false;
    }
    logicalWidthHint = itemSize.width();
}

qreal KStandardItemListWidgetInformant::preferredRoleColumnWidth(const QByteArray &role, int index, const KItemListView *view) const
{
    const QHash<QByteArray, QVariant> values = view->model()->data(index);
    const KItemListStyleOption &option = view->styleOption();

    const QString text = roleText(role, values);
    qreal width = KStandardItemListWidget::columnPadding(option);
    if (role != "text") {
        // dolphin-ng: extra room between the columns of the details view (DOLPHIN_NG_COLGAP, pixels)
        const int gap = ngSettingInt("COLGAP", 28);
        width += gap;
    }

    const QFontMetrics &normalFontMetrics = option.fontMetrics;
    const QFontMetrics linkFontMetrics(customizedFontForLinks(option.font));

    if (role == "rating") {
        width += KStandardItemListWidget::preferredRatingSize(option).width();
    } else {
        // If current item is a link, we use the customized link font metrics instead of the normal font metrics.
        const QFontMetrics &fontMetrics = itemIsLink(index, view) ? linkFontMetrics : normalFontMetrics;

        width += fontMetrics.horizontalAdvance(text);

        if (role == "text") {
            if (view->supportsItemExpanding()) {
                // Increase the width by the expansion-toggle and the current expansion level
                const int expandedParentsCount = values.value("expandedParentsCount", 0).toInt();
                const qreal height = option.padding * 2 + qMax(option.iconSize, fontMetrics.height());
                width += (expandedParentsCount + 1) * height;
            }

            // Increase the width by the required space for the icon
            width += option.padding * 2 + option.iconSize;
        }
    }

    return width;
}

QString KStandardItemListWidgetInformant::itemText(int index, const KItemListView *view) const
{
    return view->model()->data(index).value("text").toString();
}

bool KStandardItemListWidgetInformant::itemIsLink(int index, const KItemListView *view) const
{
    Q_UNUSED(index)
    Q_UNUSED(view)
    return false;
}

QString KStandardItemListWidgetInformant::roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values, ForUsageAs forUsageAs) const
{
    if (role == "rating") {
        if (forUsageAs == ForUsageAs::DisplayedText) {
            // Always use an empty text, as the rating is shown by the image m_rating.
            return QString();
        } else {
            const int rating{values.value(role).toInt()};
            // Check if there are half stars
            if (rating % 2) {
                return i18ncp("@accessible rating", "%1 and a half stars", "%1 and a half stars", rating / 2);
            }
            return i18ncp("@accessible rating", "%1 star", "%1 stars", rating / 2);
        }
    }
    return values.value(role).toString();
}

QFont KStandardItemListWidgetInformant::customizedFontForLinks(const QFont &baseFont) const
{
    return baseFont;
}

void KStandardItemListWidgetInformant::calculateIconsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                                         qreal &logicalWidthHint,
                                                                         const KItemListView *view) const
{
    const KItemListStyleOption &option = view->styleOption();
    const QFont &normalFont = option.font;
    const int additionalRolesCount = qMax(view->visibleRoles().count() - 1, 0);

    const qreal itemWidth = view->itemSize().width();
    const qreal maxWidth = itemWidth - 2 * option.padding;
    const qreal additionalRolesSpacing = additionalRolesCount * option.fontMetrics.lineSpacing();
    const qreal spacingAndIconHeight = option.iconSize + option.padding * 3;

    const QFont linkFont = customizedFontForLinks(normalFont);

    QTextOption textOption(Qt::AlignHCenter);
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    for (int index = 0; index < logicalHeightHints.count(); ++index) {
        if (logicalHeightHints.at(index).first > 0.0) {
            continue;
        }

        // If the current item is a link, we use the customized link font instead of the normal font.
        const QFont &font = itemIsLink(index, view) ? linkFont : normalFont;

        const QString &text = KStringHandler::preProcessWrap(itemText(index, view));

        // Calculate the number of lines required for wrapping the name
        qreal textHeight = 0;
        QTextLayout layout(text, font);
        layout.setTextOption(textOption);
        layout.beginLayout();
        QTextLine line;
        int lineCount = 0;
        bool isElided = false;
        while ((line = layout.createLine()).isValid()) {
            line.setLineWidth(maxWidth);
            line.naturalTextWidth();
            textHeight += line.height();

            ++lineCount;
            if (lineCount == option.maxTextLines) {
                isElided = layout.createLine().isValid();
                break;
            }
        }
        layout.endLayout();

        // Add one line for each additional information
        textHeight += additionalRolesSpacing;

        logicalHeightHints[index].first = textHeight + spacingAndIconHeight;
        logicalHeightHints[index].second = isElided;
    }

    logicalWidthHint = itemWidth;
}

void KStandardItemListWidgetInformant::calculateCompactLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                                           qreal &logicalWidthHint,
                                                                           const KItemListView *view) const
{
    const KItemListStyleOption &option = view->styleOption();
    const QFontMetrics &normalFontMetrics = option.fontMetrics;
    const int additionalRolesCount = qMax(view->visibleRoles().count() - 1, 0);

    const QList<QByteArray> &visibleRoles = view->visibleRoles();
    const bool showOnlyTextRole = (visibleRoles.count() == 1) && (visibleRoles.first() == "text");
    const qreal maxWidth = option.maxTextWidth;
    const qreal paddingAndIconWidth = option.padding * 4 + option.iconSize;
    const qreal height = option.padding * 2 + qMax(option.iconSize, (1 + additionalRolesCount) * normalFontMetrics.lineSpacing());

    const QFontMetrics linkFontMetrics(customizedFontForLinks(option.font));

    for (int index = 0; index < logicalHeightHints.count(); ++index) {
        if (logicalHeightHints.at(index).first > 0.0) {
            continue;
        }

        // If the current item is a link, we use the customized link font metrics instead of the normal font metrics.
        const QFontMetrics &fontMetrics = itemIsLink(index, view) ? linkFontMetrics : normalFontMetrics;

        // For each row exactly one role is shown. Calculate the maximum required width that is necessary
        // to show all roles without horizontal clipping.
        qreal maximumRequiredWidth = 0.0;

        if (showOnlyTextRole) {
            maximumRequiredWidth = fontMetrics.horizontalAdvance(itemText(index, view));
        } else {
            const QHash<QByteArray, QVariant> &values = view->model()->data(index);
            for (const QByteArray &role : visibleRoles) {
                const QString &text = roleText(role, values);
                const qreal requiredWidth = fontMetrics.horizontalAdvance(text);
                maximumRequiredWidth = qMax(maximumRequiredWidth, requiredWidth);
            }
        }

        qreal width = paddingAndIconWidth + maximumRequiredWidth;
        if (maxWidth > 0 && width > maxWidth) {
            width = maxWidth;
        }

        logicalHeightHints[index].first = width;
    }

    logicalWidthHint = height;
}

void KStandardItemListWidgetInformant::calculateDetailsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                                           qreal &logicalWidthHint,
                                                                           const KItemListView *view) const
{
    const KItemListStyleOption &option = view->styleOption();
    // dolphin-ng: two text lines per row
    const int textHeight = ngTwoLineList(view) ? 2 * option.fontMetrics.height() + 4 : option.fontMetrics.height();
    // dolphin-ng: the Details view (rows with columns) has the small tile for a mark; compact, one line as low as the text
    const bool details = ngRowColumns(view);
    const qreal height = details && ngDetailsCompact() ? option.padding * 2 + option.fontMetrics.height() + 10
        : details                                    ? option.padding * 2 + textHeight + 12
        : ngNoIcons(view) ? option.padding * 2 + textHeight + (ngMark() == NgMark::Object ? 44 : 24) // no icon: as tall as its two lines (a little more for the drawing)
                          : option.padding * 2 + qMax(option.iconSize, textHeight);
    logicalHeightHints.fill(std::make_pair(height, false));
    logicalWidthHint = -1.0;
}

KStandardItemListWidget::KStandardItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent)
    : KItemListWidget(informant, parent)
    , m_textInfo()
    , m_isCut(false)
    , m_isHidden(false)
    , m_customizedFont()
    , m_customizedFontMetrics(m_customizedFont)
    , m_isExpandable(false)
    , m_highlightEntireRow(false)
    , m_supportsItemExpanding(false)
    , m_dirtyLayout(true)
    , m_dirtyContent(true)
    , m_dirtyContentRoles()
    , m_layout(IconsLayout)
    , m_pixmapPos()
    , m_pixmap()
    , m_scaledPixmapSize()
    , m_columnWidthSum()
    , m_iconRect()
    , m_textRect()
    , m_sortedVisibleRoles()
    , m_expansionArea()
    , m_customTextColor()
    , m_additionalInfoTextColor()
    , m_overlays()
    , m_rating()
    , m_roleEditor(nullptr)
    , m_oldRoleEditor(nullptr)
{
    // dolphin-ng: repaint while work on this file is in progress, and once more when it ends
    connect(NgActivity::self(), &NgActivity::changed, this, [this] {
        const QUrl url = data().value("url").toUrl();
        if (m_ngShowedActivity || m_ngShowedFamily || NgActivity::self()->contains(url) || !NgActivity::self()->familyOf(url).isEmpty()) {
            update();
        }
    });
    // dolphin-ng: Ctrl or Shift down or up: the quick actions that come with the pointer hide or come back
    connect(NgModifiers::self(), &NgModifiers::changed, this, [this] {
        if (isHovered() && m_ngCardHoverPill) {
            update();
        }
    });
    // after a tie the files move next to each other: paint the bubble again once they are there
    connect(NgActivity::self(), &NgActivity::linksChanged, this, [this] {
        QTimer::singleShot(450, this, [this] {
            update();
        });
    });
}

KStandardItemListWidget::~KStandardItemListWidget()
{
    qDeleteAll(m_textInfo);
    m_textInfo.clear();

    if (m_roleEditor) {
        m_roleEditor->deleteLater();
    }

    if (m_oldRoleEditor) {
        m_oldRoleEditor->deleteLater();
    }
}

void KStandardItemListWidget::setLayout(Layout layout)
{
    if (m_layout != layout) {
        prepareGeometryChange(); // the cards' bounding rect is a little larger (see boundingRect())
        m_layout = layout;
        m_dirtyLayout = true;
        updateAdditionalInfoTextColor();
        update();
    }
}

void KStandardItemListWidget::setHighlightEntireRow(bool highlightEntireRow)
{
    if (m_highlightEntireRow != highlightEntireRow) {
        m_highlightEntireRow = highlightEntireRow;
        m_dirtyLayout = true;
        update();
    }
}

bool KStandardItemListWidget::highlightEntireRow() const
{
    return m_highlightEntireRow;
}

void KStandardItemListWidget::setSupportsItemExpanding(bool supportsItemExpanding)
{
    if (m_supportsItemExpanding != supportsItemExpanding) {
        m_supportsItemExpanding = supportsItemExpanding;
        m_dirtyLayout = true;
        update();
    }
}

bool KStandardItemListWidget::supportsItemExpanding() const
{
    return m_supportsItemExpanding;
}

namespace
{
// dolphin-ng: a horizontal fade of `tint` from alpha `start` through `middle` (at `middleAt` of the width) to nothing,
// as an 8 px high tile with ordered dithering on the alpha. Kept per colour and width.
QPixmap ngDitheredWash(const QColor &tint, int width, qreal start, qreal middle, qreal middleAt)
{
    static const int bayer[8][8] = {{0, 32, 8, 40, 2, 34, 10, 42},   {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
                                    {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},   {51, 19, 59, 27, 49, 17, 57, 25},
                                    {15, 47, 7, 39, 13, 45, 5, 37},   {63, 31, 55, 23, 61, 29, 53, 21}};
    static QHash<QString, QPixmap> cache;
    const QString key = QString::number(tint.rgb(), 16) + QLatin1Char('|') + QString::number(width) + QLatin1Char('|') + QString::number(start);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return *it;
    }
    if (cache.size() > 64) {
        cache.clear();
    }
    QImage image(width, 8, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < 8; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const qreal t = qreal(x) / width;
            const qreal alpha = t < middleAt ? start + (middle - start) * (t / middleAt) : middle * (1.0 - (t - middleAt) / (1.0 - middleAt));
            const int a = qBound(0, int(std::floor(alpha * 255.0 + (bayer[y][x & 7] + 0.5) / 64.0)), 255);
            line[x] = qPremultiply(qRgba(tint.red(), tint.green(), tint.blue(), a));
        }
    }
    return *cache.insert(key, QPixmap::fromImage(image));
}
}

void KStandardItemListWidget::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();

    // dolphin-ng: a cut file (waiting to be moved) is drawn whole in grey and see-through: the card or row is
    // painted into an image, which is turned grey and drawn at 45 %
    if (m_isCut && !m_ngCutPass) {
        const QRectF bounds = boundingRect();
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        QImage image((bounds.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        if (!image.isNull()) {
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::transparent);
            {
                QPainter offscreen(&image);
                offscreen.translate(-bounds.topLeft());
                m_ngCutPass = true;
                paint(&offscreen, option, widget);
                m_ngCutPass = false;
            }
            for (int y = 0; y < image.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    const QRgb p = line[x];
                    const int v = (qRed(p) * 11 + qGreen(p) * 16 + qBlue(p) * 5) / 32; // premultiplied stays valid
                    line[x] = qRgba(v, v, v, qAlpha(p));
                }
            }
            painter->save();
            painter->setOpacity(painter->opacity() * 0.45);
            painter->drawImage(bounds.topLeft(), image);
            painter->restore();
            return;
        }
    }

    // dolphin-ng: a file just tied to the one it is made from grows into its place
    painter->save();
    const auto restoreGrow = qScopeGuard([painter] {
        painter->restore();
    });
    if (NgActivity::self()->hasLinks()) {
        const QUrl url = data().value("url").toUrl();
        const qreal grow = NgActivity::self()->growth(url);
        if (grow < 1 && NgActivity::self()->parentOf(url).isValid()) {
            const qreal eased = 1 - std::pow(1 - grow, 3);
            const QPointF centre = rect().center();
            const qreal scale = (m_layout == CardsLayout ? 0.55 : 0.85) + (m_layout == CardsLayout ? 0.45 : 0.15) * eased;
            painter->translate(centre);
            painter->scale(scale, scale);
            painter->translate(-centre);
            painter->setOpacity(painter->opacity() * eased);
        }
    }

    if (m_layout == CardsLayout) {
        // dolphin-ng: the card draws everything itself; the base paints the selection and hover over its sheet
        paintNgFamily(painter);
        paintNgCardSheet(painter);
        KItemListWidget::paint(painter, option, widget);
        paintNgCard(painter, widget);
        paintNgFold(painter);
        paintNgPin(painter);
        ngPaintCheck(painter, widget);
        paintNgActivity(painter);
        return;
    }

    paintNgFamily(painter);
    KItemListWidget::paint(painter, option, widget);

    if (m_layout == IconsLayout && ngTiles()) {
        // dolphin-ng: a dark rounded tile with a hairline of light on top, like the cards of the list
        const QRectF tile = rect().adjusted(3, 3, -3, -3);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        const bool darkScheme = ngIsDark(styleOption().palette);
        painter->setBrush(ngAurora() ? ngAuroraVeil(darkScheme) : darkScheme ? QColor(0, 0, 0, 62) : QColor(0, 0, 0, 14));
        painter->drawRoundedRect(tile, 12, 12);
        if (!ngAurora()) { // Aurora has no lit edges
            painter->setPen(QPen(darkScheme ? QColor(255, 255, 255, 11) : QColor(255, 255, 255, 170), 1));
            painter->drawLine(QPointF(tile.left() + 12, tile.top() + 0.5), QPointF(tile.right() - 12, tile.top() + 0.5));
        }
        painter->restore();
    }

    const bool ngRow = m_layout == DetailsLayout && ngNoIcons(this);
    if (ngRow && m_ngCategory == int(NgKind::Folder) && ngSetting("FOLDER_TINT").toLower() != QLatin1String("off")) {
        // a folder's row wears the accent (its own colour for a well-known folder): told from a file at a glance
        const QRectF rowRect = ngRowStyle() == NgRowStyle::Open ? selectionRectFull() : selectionRectFull().adjusted(3, 3, -3, -3);
        QColor tint = m_ngTint;
        tint.setAlphaF(ngIsDark(styleOption().palette) ? 0.17 : 0.15);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRoundedRect(rowRect, 10, 10);
        painter->restore();
    }
    if (ngRow && !m_ngSigla.isEmpty()) {
        // the file type as a watermark: large, faint letters at the start of the row, fading out under the name
        const bool openRows = ngRowStyle() == NgRowStyle::Open;
        const QRectF rowRect = openRows ? selectionRectFull() : selectionRectFull().adjusted(3, 3, -3, -3);
        qreal x0 = rowRect.left() + (openRows ? 16 : 10);
        QFont watermarkFont(m_customizedFont);
        watermarkFont.setPixelSize(qMax(12, qRound(size().height() * 0.50)));
        watermarkFont.setWeight(QFont::Black);
        watermarkFont.setLetterSpacing(QFont::PercentageSpacing, 94);
        const qreal fade = QFontMetricsF(watermarkFont).horizontalAdvance(m_ngSigla) + m_customizedFontMetrics.height() * 1.5;
        QPainterPath clip;
        clip.addRoundedRect(rowRect, openRows ? 0 : 5, openRows ? 0 : 5);
        painter->save();
        painter->setClipPath(clip, Qt::IntersectClip);

        // the row takes the colour of the file's category, strongest at the start and gone by the middle
        // (dithered: so faint a gradient over so many pixels shows as bands when it is drawn plain)
        const int washWidth = qMax(8, qRound(rowRect.width() * (openRows ? 0.40 : 0.62)));
        if (ngSetting("ROW_WASH").toLower() != QLatin1String("hide")) {
            painter->setBrushOrigin(rowRect.topLeft());
            painter->fillRect(QRectF(rowRect.left(), rowRect.top(), washWidth, rowRect.height()),
                              QBrush(ngDitheredWash(m_ngTint, washWidth, openRows ? 0.14 : 0.22, openRows ? 0.05 : 0.06, 0.55)));
            painter->setBrushOrigin(QPointF(0, 0));
        }

        // a file with a preview (pictures, PDFs, videos) shows it, small, where the mark of its type would be;
        // resting on it opens the quick view, as on the cards
        m_ngRowThumb = QRectF();
        m_ngRowObject = QRectF();
        const QPixmap preview = data().value("isDir").toBool() ? QPixmap() : data().value("iconPixmap").value<QPixmap>();
        if (!preview.isNull() && !m_ngObjectRow) {
            const qreal rowHeight = size().height();
            const TextInfo *nameInfo = m_textInfo.value("text");
            const qreal room = (nameInfo ? nameInfo->pos.x() : x0 + 96) - x0 - 16;
            const qreal thumbHeight = rowHeight - 14;
            const qreal thumbWidth = qMin(thumbHeight * 1.35, room);
            if (thumbWidth >= 24 && thumbHeight >= 16) {
                m_ngRowThumb = QRectF(x0 + 6, (rowHeight - thumbHeight) / 2.0, thumbWidth, thumbHeight);
                const qreal dpr = KItemViewsUtils::devicePixelRatio(this);
                const QSize wanted = (m_ngRowThumb.size() * dpr).toSize();
                if (m_ngRowThumbKey != preview.cacheKey() || m_ngRowThumbPixmap.size() != wanted) {
                    // scaled to cover the frame, cut to it, once per picture
                    const QPixmap scaled = preview.scaled(wanted, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    m_ngRowThumbPixmap = scaled.copy((scaled.width() - wanted.width()) / 2, (scaled.height() - wanted.height()) / 2, wanted.width(), wanted.height());
                    m_ngRowThumbPixmap.setDevicePixelRatio(dpr);
                    m_ngRowThumbKey = preview.cacheKey();
                }
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                QPainterPath frame;
                frame.addRoundedRect(m_ngRowThumb, 7, 7);
                painter->setClipPath(frame, Qt::IntersectClip);
                painter->drawPixmap(m_ngRowThumb.topLeft(), m_ngRowThumbPixmap);
                painter->restore();
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                QColor rim = m_ngTint;
                rim.setAlphaF(0.45);
                painter->setPen(QPen(rim, 1));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(m_ngRowThumb.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
                painter->restore();
            }
        }
        const NgMark mark = m_ngDetailsRow ? NgMark::Tile : ngMark(); // (the Details view: the small tile)
        if (m_ngObjectRow) {
            // the drawing of the card, small: type and size written on it; a real preview becomes the object
            // (a print, a page, a film strip). Resting on a preview opens the quick view, as on the cards
            // (with expandable folders, after the expansion arrow and the tree lines of the parents, not under them)
            const QRectF band(rowRect.left() + 10 + ngExpansionIndent(), rowRect.top() + 1, qRound(size().height() * 1.75), rowRect.height() - 1);
            m_ngRowObject = band;
            painter->save();
            if (!preview.isNull()) {
                m_ngRowThumb = band;
                paintNgPreviewObject(painter, band, preview, false);
            } else {
                ngPaintContentSketch(painter, band, m_ngCategory, m_ngSigla, m_ngTint, ngIsDark(styleOption().palette), m_ngRowCount, m_ngInfoSize, &m_customizedFont,
                                     m_ngCategory == int(NgKind::Folder) ? ngSpecialFolder(data().value("url").toUrl().toLocalFile()) : 0);
            }
            painter->restore();
        } else if (!m_ngRowThumb.isEmpty()) {
            // the picture stands for the mark
        } else if (mark != NgMark::Watermark) {
            // Glyph and abbreviation as one mark, in the colour of the file's kind.
            const qreal h = size().height();
            const qreal fontPx = m_customizedFontMetrics.height();
            QColor soft = m_ngTint, edge = m_ngTint, block = m_ngTint;
            soft.setAlphaF(0.16);
            edge.setAlphaF(0.36);
            block.setAlphaF(0.20);
            painter->setRenderHint(QPainter::Antialiasing);
            auto glyph = [this, painter](const QRectF &box) {
                painter->save();
                painter->translate(box.topLeft());
                painter->scale(box.width() / 24.0, box.height() / 24.0);
                painter->setPen(QPen(m_ngTint, 1.7 * 24.0 / box.width(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(ngGlyph(m_ngCategory));
                painter->restore();
            };
            auto sigla = [this, painter](const QRectF &box, qreal pixelSize, int flags) {
                QFont font(m_customizedFont);
                font.setPixelSize(qMax(7, qRound(pixelSize)));
                font.setWeight(QFont::Bold);
                font.setLetterSpacing(QFont::PercentageSpacing, 106);
                painter->setFont(font);
                painter->setPen(m_ngTint);
                painter->drawText(box, flags, m_ngSigla);
            };
            if (mark == NgMark::Tile && m_ngCompactRow) {
                // one line: a small tile with the glyph alone
                const qreal side = h - 10;
                const QRectF tile(x0 + 3, (h - side) / 2.0, side, side);
                painter->setPen(QPen(edge, 1));
                painter->setBrush(soft);
                painter->drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), side / 3.8, side / 3.8);
                const qreal g = side * 0.62;
                glyph(QRectF(tile.center().x() - g / 2, tile.center().y() - g / 2, g, g));
            } else if (mark == NgMark::Tile) {
                const qreal side = h - 18;
                const QRectF tile(x0 + 3, (h - side) / 2.0, side, side);
                painter->setPen(QPen(edge, 1));
                painter->setBrush(soft);
                painter->drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), side / 3.8, side / 3.8);
                const qreal g = side * 0.40;
                glyph(QRectF(tile.center().x() - g / 2, tile.top() + side * 0.15, g, g));
                sigla(QRectF(tile.left(), tile.top() + side * 0.60, side, side * 0.30), fontPx * 0.46, Qt::AlignCenter);
            } else if (mark == NgMark::Pill) {
                const qreal pillHeight = qMin(h - 20, fontPx * 1.5), pillWidth = fontPx * 4.8;
                const QRectF pill(x0 + 3, (h - pillHeight) / 2.0, pillWidth, pillHeight);
                painter->setPen(QPen(edge, 1));
                painter->setBrush(soft);
                painter->drawRoundedRect(pill.adjusted(0.5, 0.5, -0.5, -0.5), pillHeight / 2.0, pillHeight / 2.0);
                const qreal g = pillHeight * 0.56;
                glyph(QRectF(pill.left() + pillHeight * 0.36, pill.center().y() - g / 2, g, g));
                sigla(QRectF(pill.left() + pillHeight * 0.36 + g + 6, pill.top(), pillWidth, pillHeight), fontPx * 0.56, Qt::AlignLeft | Qt::AlignVCenter);
            } else if (mark == NgMark::Signs) {
                const qreal g = 22;
                glyph(QRectF(x0 + 7, (h - g) / 2.0, g, g));
                sigla(QRectF(x0 + 7 + g + 9, 0, fontPx * 4, h), fontPx * 0.66, Qt::AlignLeft | Qt::AlignVCenter);
            } else { // Header: the start of the card takes the colour and carries the mark
                const QRectF head(rowRect.left(), rowRect.top(), 62, rowRect.height());
                painter->fillRect(head, block);
                const qreal g = 20;
                glyph(QRectF(head.center().x() - g / 2, head.top() + head.height() * 0.17, g, g));
                sigla(QRectF(head.left(), head.top() + head.height() * 0.60, head.width(), head.height() * 0.28), fontPx * 0.46, Qt::AlignCenter);
            }
        } else {
            // the type icon, faint, then the abbreviation
            if (!m_ngIcon.isNull()) {
                const qreal iconSide = m_ngIcon.width() / m_ngIcon.devicePixelRatio();
                painter->setOpacity(0.55); // the pixmap itself already fades from left to right
                painter->drawPixmap(QPointF(x0, (size().height() - iconSide) / 2.0), m_ngIcon);
                painter->setOpacity(1.0);
                x0 += iconSide + 8;
            }
            // The abbreviation. DOLPHIN_NG_SIGLA picks how it is drawn:
            //   fill (default)     large solid letters in the row colour, faint
            //   outline            hollow letters: a thin outline that thins out to the right, barely filled
            //   label              small, widely spaced capitals in the full row colour
            const QByteArray siglaStyle = ngSetting("SIGLA").toLower().toLatin1();
            const qreal rowHeight = size().height();
            if (siglaStyle == "label") {
                QFont labelFont(m_customizedFont);
                labelFont.setPixelSize(qMax(9, qRound(m_customizedFontMetrics.height() * 0.66)));
                labelFont.setWeight(QFont::DemiBold);
                labelFont.setLetterSpacing(QFont::PercentageSpacing, 118);
                QColor ink = m_ngTint;
                ink.setAlphaF(0.92);
                painter->setFont(labelFont);
                painter->setPen(ink);
                painter->drawText(QRectF(x0, 0, fade * 2, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, m_ngSigla);
            } else if (siglaStyle != "outline") {
                QColor ink = m_ngTint;
                ink.setAlphaF(ngIsDark(styleOption().palette) ? 0.36 : 0.30);
                painter->setFont(watermarkFont);
                painter->setPen(ink);
                painter->drawText(QRectF(x0, -rowHeight, fade * 2, 3 * rowHeight), Qt::AlignLeft | Qt::AlignVCenter, m_ngSigla);
            } else {
                const QFontMetricsF metrics(watermarkFont);
                QPainterPath letters;
                letters.addText(QPointF(x0, (rowHeight + metrics.capHeight()) / 2.0), watermarkFont, m_ngSigla);
                QLinearGradient outline(x0, 0, x0 + fade, 0);
                QColor strong = m_ngTint, weak = m_ngTint, body = m_ngTint;
                strong.setAlphaF(0.80);
                weak.setAlphaF(0.28);
                body.setAlphaF(0.07);
                outline.setColorAt(0.0, strong);
                outline.setColorAt(1.0, weak);
                painter->setRenderHint(QPainter::Antialiasing);
                painter->fillPath(letters, body);
                painter->strokePath(letters, QPen(QBrush(outline), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            }
        }
        painter->restore();

        if (m_ngBarFraction >= 0) {
            QColor groove = styleOption().palette.color(QPalette::Text);
            groove.setAlphaF(0.10);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(groove);
            painter->drawRoundedRect(m_ngBarRect, 2, 2);
            painter->setBrush(m_ngTint);
            const qreal filled = qMax(qreal(4), m_ngBarRect.width() * m_ngBarFraction);
            painter->drawRoundedRect(QRectF(m_ngBarRect.right() - filled, m_ngBarRect.top(), filled, m_ngBarRect.height()), 2, 2);
            painter->restore();
        }
    }

    if (!m_expansionArea.isEmpty()) {
        drawSiblingsInformation(painter);
    }

    // Overlays are not baked into the icon pixmap: it gets rescaled to its final
    // size in drawPixmap(), which would rescale them too. They are drawn at a
    // fixed size by drawOverlays() below. See bug 498211.
    const auto pixmap = ngRow ? QPixmap() : m_pixmap; // dolphin-ng: no icon in the icon-less row

    const KItemListStyleOption &itemListStyleOption = styleOption();
    if (isHovered() && !pixmap.isNull()) {
        if (hoverOpacity() < 1.0) {
            /*
             * Linear interpolation between pixmap and m_hoverPixmap.
             *
             * Note that this cannot be achieved by painting m_hoverPixmap over
             * pixmap, even if the opacities are adjusted. For details see
             * https://git.reviewboard.kde.org/r/109614/
             */
            // Paint pixmap1 so that pixmap1 = pixmap * (1.0 - hoverOpacity())
            QPixmap pixmap1(pixmap.size());
            pixmap1.setDevicePixelRatio(pixmap.devicePixelRatio());
            pixmap1.fill(Qt::transparent);
            {
                QPainter p(&pixmap1);
                p.setOpacity(1.0 - hoverOpacity());
                p.drawPixmap(0, 0, pixmap);
            }

            // Paint pixmap2 so that pixmap2 = m_hoverPixmap * hoverOpacity()
            QPixmap pixmap2(pixmap1.size());
            pixmap2.setDevicePixelRatio(pixmap1.devicePixelRatio());
            pixmap2.fill(Qt::transparent);
            {
                QPainter p(&pixmap2);
                p.setOpacity(hoverOpacity());
                p.drawPixmap(0, 0, m_pixmap);
            }

            // Paint pixmap2 on pixmap1 using CompositionMode_Plus
            // Now pixmap1 = pixmap2 + pixmap * (1.0 - hoverOpacity())
            //             = m_hoverPixmap * hoverOpacity() + pixmap * (1.0 - hoverOpacity())
            {
                QPainter p(&pixmap1);
                p.setCompositionMode(QPainter::CompositionMode_Plus);
                p.drawPixmap(0, 0, pixmap2);
            }

            // Finally paint pixmap1 on the widget
            drawPixmap(painter, pixmap1);
        } else {
            drawPixmap(painter, pixmap);
        }
    } else if (!pixmap.isNull()) {
        drawPixmap(painter, pixmap);
    }

    if (!m_overlays.isEmpty() && !pixmap.isNull()) {
        drawOverlays(painter);
    }

    painter->setFont(m_customizedFont);
    painter->setPen(textColor(*widget));
    const TextInfo *textInfo = m_textInfo.value("text");

    if (!textInfo) {
        // It seems that we can end up here even if m_textInfo does not contain
        // the key "text", see bug 306167. According to triggerCacheRefreshing(),
        // this can only happen if the index is negative. This can happen when
        // the item is about to be removed, see KItemListView::slotItemsRemoved().
        // TODO: try to reproduce the crash and find a better fix.
        return;
    }

    // dolphin-ng: on glass a soft shadow around the text (Breeze NG's "Shadow around text on glass")
    const bool ngHalo = ngTextHalo();
    if (ngHalo) {
        ngPaintTextHalo(painter, painter->pen().color(), [&](const QPointF &offset) {
            painter->drawStaticText(textInfo->pos + offset, textInfo->staticText);
        });
    }
    painter->drawStaticText(textInfo->pos, textInfo->staticText);

    bool clipAdditionalInfoBounds = false;
    if (m_supportsItemExpanding && m_sortedVisibleRoles.count() > 1 && !(m_layout == DetailsLayout && ngTwoLineList(this))) {
        // Prevent a possible overlapping of the additional-information-texts with the icon.
        // This will happen if the user has resized the width of the name-column to be very narrow while having folders expanded.
        // We only want to draw additional info text outside the area of the icon or expansion area, so we set a clip rect that does not contain the icon area.
        // This needs to work both for left-to-right as well as right-to-left layout directions.
        const TextInfo *potentiallyOverlappingRoleText = m_textInfo.value(m_sortedVisibleRoles[1]); // Only the first column after the name column can overlap.
        if (layoutDirection() == Qt::LeftToRight) { // In left-to-right languages the left end of text would overlap. This is mirrored for right-to-left.
            const qreal minX = m_iconRect.right() + 2 * itemListStyleOption.padding;
            if (potentiallyOverlappingRoleText->pos.x() < minX) {
                clipAdditionalInfoBounds = true;
                painter->save();
                painter->setClipRect(minX, 0, size().width() - minX, size().height(), Qt::IntersectClip);
            }
        } else {
            const qreal maxX = m_iconRect.left() - 2 * itemListStyleOption.padding;
            if (potentiallyOverlappingRoleText->pos.x() + m_customizedFontMetrics.horizontalAdvance(potentiallyOverlappingRoleText->staticText.text()) > maxX) {
                clipAdditionalInfoBounds = true;
                painter->save();
                painter->setClipRect(0, 0, maxX, size().height(), Qt::IntersectClip);
            }
        }
    }

    // dolphin-ng: the Details view's columns are read across the row, not glanced at under a name: nearly the text's own
    // tone (the dimmed mix of text and base lost its contrast on the veiled rows of a dark scheme)
    QColor infoInk = m_additionalInfoTextColor;
    if (m_ngDetailsRow && !m_customTextColor.isValid() && !isSelected()) {
        infoInk = styleOption().palette.color(QPalette::Text);
        infoInk.setAlphaF(0.88);
        infoInk = ngHaloInk(infoInk, styleOption().palette);
    }
    painter->setPen(infoInk);
    painter->setFont(m_customizedFont);

    const bool ngActionsShown = ngRow && m_ngActions && m_ngActions->isVisible();
    for (int i = 1; i < m_sortedVisibleRoles.count(); ++i) {
        if (ngActionsShown && m_sortedVisibleRoles[i] == m_ngDateRole) {
            continue; // dolphin-ng: the hover actions take the place of the date
        }
        const TextInfo *textInfo = m_textInfo.value(m_sortedVisibleRoles[i]);
        if (ngHalo) {
            ngPaintTextHalo(
                painter,
                infoInk,
                [&](const QPointF &offset) {
                    painter->drawStaticText(textInfo->pos + offset, textInfo->staticText);
                },
                0.5);
        }
        painter->drawStaticText(textInfo->pos, textInfo->staticText);
    }
    if (ngRow && (!m_ngAgo.isEmpty() || !m_ngCount.isEmpty())) {
        QColor dim = m_additionalInfoTextColor;
        dim.setAlpha(qRound(dim.alpha() * 0.6));
        painter->setPen(dim);
        if (!ngActionsShown && !m_ngAgo.isEmpty()) {
            painter->drawText(m_ngAgoPos, m_ngAgo);
        }
        if (!m_ngCount.isEmpty()) {
            painter->drawText(m_ngCountPos, m_ngCount);
        }
        painter->setPen(m_additionalInfoTextColor);
    }
    if (ngRow && !m_ngCardActions.isEmpty()) {
        paintNgActionPill(painter, styleOption().palette.color(QPalette::Text), ngIsDark(styleOption().palette));
    }
    if (ngRow && !m_ngChips.isEmpty()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QFont chipFont = m_customizedFont;
        chipFont.setPointSizeF(chipFont.pointSizeF() * 0.88);
        painter->setFont(chipFont);
        const bool dark = ngIsDark(styleOption().palette);
        const int ink = dark ? 255 : 0;
        for (const NgChip &chip : std::as_const(m_ngChips)) {
            if (m_ngObjectRow) {
                // drawn rows: plain words, a dot between them (the drawing carries the colour)
                static qreal wordsX = 0;
                if (&chip == &m_ngChips.first()) {
                    wordsX = chip.rect.left();
                }
                const QString words = (&chip == &m_ngChips.first() ? QString() : QStringLiteral("  ·  ")) + chip.text;
                painter->setPen(m_additionalInfoTextColor);
                painter->drawText(QRectF(wordsX, chip.rect.top(), chip.rect.width() + 40, chip.rect.height()), Qt::AlignLeft | Qt::AlignVCenter, words);
                wordsX += QFontMetricsF(painter->font()).horizontalAdvance(words);
                continue;
            }
            QColor fill = chip.tinted ? m_ngTint : QColor(ink, ink, ink);
            fill.setAlpha(chip.tinted ? (dark ? 34 : 40) : (dark ? 18 : 16));
            QColor edge = chip.tinted ? m_ngTint : QColor(ink, ink, ink);
            edge.setAlpha(chip.tinted ? 110 : 26);
            painter->setPen(QPen(edge, 1));
            painter->setBrush(fill);
            const qreal radius = chip.rect.height() / 2.0;
            painter->drawRoundedRect(chip.rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            QColor text = chip.tinted ? m_ngTint : m_additionalInfoTextColor;
            if (chip.tinted && !dark) {
                text = text.darker(135);
            }
            painter->setPen(text);
            painter->drawText(chip.rect, Qt::AlignCenter, chip.text);
        }
        painter->restore();
    }
    if (ngRow && !m_ngTime.isEmpty()) {
        painter->save();
        QFont small = m_customizedFont;
        small.setPointSizeF(small.pointSizeF() * 0.9);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSizeF(m_customizedFont.pointSizeF() * 0.8);
        mono.setBold(true);
        QColor dim = m_additionalInfoTextColor;
        dim.setAlpha(qRound(dim.alpha() * 0.55));
        for (const NgTimeLine &line : std::as_const(m_ngTime)) {
            painter->setFont(m_ngObjectRow ? small : mono);
            painter->setPen(dim);
            painter->drawText(line.letterPos, line.letter);
            painter->setFont(small);
            painter->setPen(m_additionalInfoTextColor);
            painter->drawText(line.textPos, line.text);
        }
        painter->restore();
    }

    if (!m_rating.isNull()) {
        const TextInfo *ratingTextInfo = m_textInfo.value("rating");
        QPointF pos = ratingTextInfo->pos;
        const Qt::Alignment align = ratingTextInfo->staticText.textOption().alignment();
        if (align & Qt::AlignHCenter) {
            pos.rx() += (size().width() - m_rating.width() / m_rating.devicePixelRatioF()) / 2 - 2;
        }
        painter->drawPixmap(pos, m_rating);
    }

    if (clipAdditionalInfoBounds) {
        painter->restore();
    }

    paintNgPin(painter);
    if (m_ngObjectRow) {
        ngPaintCheck(painter, widget); // the check of the ring selection, as on the cards
    }
    paintNgActivity(painter);

#ifdef KSTANDARDITEMLISTWIDGET_DEBUG
    painter->setBrush(Qt::NoBrush);
    painter->setPen(Qt::green);
    painter->drawRect(m_iconRect);

    painter->setPen(Qt::blue);
    painter->drawRect(m_textRect);

    painter->setPen(Qt::red);
    painter->drawText(QPointF(0, m_customizedFontMetrics.height()), QString::number(index()));
    painter->drawRect(rect());
#endif
}

QRectF KStandardItemListWidget::textRect() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
    return m_textRect;
}

QRectF KStandardItemListWidget::textFocusRect() const
{
    // In the compact- and details-layout a larger textRect() is returned to be aligned
    // with the iconRect(). This is useful to have a larger selection/hover-area
    // when having a quite large icon size but only one line of text. Still the
    // focus rectangle should be shown as narrow as possible around the text.

    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();

    switch (m_layout) {
    case CompactLayout: {
        QRectF rect = m_textRect;
        const TextInfo *topText = m_textInfo.value(m_sortedVisibleRoles.first());
        const TextInfo *bottomText = m_textInfo.value(m_sortedVisibleRoles.last());
        rect.setTop(topText->pos.y());
        rect.setBottom(bottomText->pos.y() + bottomText->staticText.size().height());
        return rect;
    }

    case DetailsLayout: {
        QRectF rect = m_textRect;
        const TextInfo *textInfo = m_textInfo.value(m_sortedVisibleRoles.first());
        rect.setTop(textInfo->pos.y());
        rect.setBottom(textInfo->pos.y() + textInfo->staticText.size().height());

        const KItemListStyleOption &option = styleOption();
        if (option.extendedSelectionRegion) {
            const QString text = textInfo->staticText.text();
            rect.setWidth(m_customizedFontMetrics.horizontalAdvance(text) + 2 * option.padding);
        }

        return rect;
    }

    default:
        break;
    }

    return m_textRect;
}

int KStandardItemListWidget::ngActionAt(const QPointF &pos) const
{
    // 6: the stop button on a file at work
    if (!m_ngCancelRect.isEmpty() && m_ngCancelRect.adjusted(-3, -3, 3, 3).contains(pos)) {
        return 6;
    }
    if (NgActivity::self()->contains(data().value("url").toUrl())) {
        return 0; // a file at work: its quick actions, preview and menu buttons are hidden, so not clickable
    }
    if (m_ngCardHoverPill && NgModifiers::selecting()) {
        return 0; // Ctrl or Shift held: the hidden actions are not clickable, the click selects
    }
    // 5: the pointer is on a name that was shortened to fit (hover only: the full name is shown as a tip)
    auto onShortenedName = [this, &pos] {
        const TextInfo *name = m_textInfo.value("text");
        if (!name || !QRectF(name->pos, name->staticText.size()).contains(pos)) {
            return false;
        }
        const QChar ellipsis(0x2026);
        return name->staticText.text().contains(ellipsis) && !data().value("text").toString().contains(ellipsis);
    };
    if (m_layout == DetailsLayout && ngTwoLineList(this) && m_ngCardActionsPill.contains(pos)) {
        for (int i = 0; i < m_ngCardActions.count(); ++i) {
            if (m_ngCardActions.at(i).rect.contains(pos)) {
                return 10 + m_ngCardActionIndex.value(i, i);
            }
        }
        return 0; // on the pill, between two buttons: not the row's own buttons
    }
    if (m_layout == DetailsLayout && ngTwoLineList(this) && m_ngRowThumb.contains(pos)) {
        return 4; // the small preview of the row: quick view on hover, never a click
    }
    if (m_layout == CardsLayout) {
        const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
        for (int i = 0; i < m_ngCardActions.count(); ++i) {
            const QRectF &rect = m_ngCardActions.at(i).rect;
            if (m_ngCardHoverPill && !isHovered() && !m_ngCardFab.contains(rect.center())) {
                continue; // in the pill that is not shown now
            }
            if (rect.contains(pos)) {
                return 10 + m_ngCardActionIndex.value(i, i);
            }
        }
        if (!isHovered()) {
            return 0;
        }
        if (m_ngCardEye.contains(pos)) {
            return 3;
        }
        if (m_ngCardMenu.contains(pos)) {
            return 2;
        }
        if (m_ngCardActionsPill.contains(pos)) {
            return 0; // on the pill, in the gap between two buttons: not the picture under it (no quick view)
        }
        // 4: the pointer rests on the picture of an image or a PDF (quick view; hover only, never a click)
        if (m_ngCardThumb.contains(pos)) {
            const QString name = data().value("text").toString();
            static const QStringList moving{QStringLiteral("pdf"), QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"), QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("m4v")};
            if (m_ngCategory == int(NgKind::Image) || moving.contains(name.section(QLatin1Char('.'), -1).toLower())) {
                return 4;
            }
        }
        return onShortenedName() ? 5 : 0;
    }
    if (!m_ngActions || !m_ngActions->isVisible() || !m_ngActionsRect.contains(pos)) {
        return m_layout == DetailsLayout && ngTwoLineList(this) && onShortenedName() ? 5 : 0;
    }
    // same split as NgRowActions: "Open", then two square buttons: preview and menu
    const qreal side = m_ngActionsRect.height();
    const qreal menuLeft = m_ngActionsRect.right() - side;
    const qreal previewLeft = menuLeft - 6 - side;
    if (pos.x() >= menuLeft) {
        return 2;
    }
    if (pos.x() >= previewLeft && pos.x() <= menuLeft - 6) {
        return 3;
    }
    return pos.x() <= previewLeft - 6 ? 1 : 0;
}

QRectF KStandardItemListWidget::selectionRectFull() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
    const int padding = styleOption().padding;
    if (m_layout == DetailsLayout && ngTwoLineList(this)) {
        // dolphin-ng: there are no columns, the whole row is the item
        return QRectF(leftPadding(), 0, size().width() - leftPadding(), size().height());
    }
    if (m_layout == DetailsLayout) {
        auto rect = m_iconRect | m_textRect;
        if (m_highlightEntireRow) {
            if (layoutDirection() == Qt::LeftToRight) {
                rect.setRight(leftPadding() + m_columnWidthSum);
            } else {
                rect.setLeft(size().width() - m_columnWidthSum - rightPadding());
            }
        }
        return rect.adjusted(-padding, 0, padding, 0);
    } else {
        if (m_layout == CompactLayout) {
            return rect().adjusted(-padding, 0, padding, 0);
        }
        return rect();
    }
}

QRectF KStandardItemListWidget::selectionRectCore() const
{
    // Allow dragging from selection area in details view.
    if (m_layout == DetailsLayout && highlightEntireRow() && !isSelected()) {
        QRectF result = m_iconRect | m_textRect;
        return result;
    }
    return selectionRectFull();
}

QRectF KStandardItemListWidget::expansionToggleRect() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
    return m_isExpandable ? m_expansionArea : QRectF();
}

QRectF KStandardItemListWidget::selectionToggleRect() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
    if (m_layout == CardsLayout) {
        return QRectF(); // dolphin-ng: no "+" toggle over the cards; Ctrl+click selects
    }

    const int widgetIconSize = styleOption().iconSize;
    int toggleSize = KIconLoader::SizeSmall;
    if (widgetIconSize >= KIconLoader::SizeEnormous) {
        toggleSize = KIconLoader::SizeMedium;
    } else if (widgetIconSize >= KIconLoader::SizeLarge) {
        toggleSize = KIconLoader::SizeSmallMedium;
    }

    const int padding = styleOption().padding;
    const QRectF selectionRectMinusPadding = selectionRectFull().adjusted(padding, padding, -padding, -padding);
    QPointF pos = selectionRectMinusPadding.topLeft();
    if (QApplication::isRightToLeft()) {
        pos.setX(selectionRectMinusPadding.right() - (pos.x() + toggleSize - selectionRectMinusPadding.left()));
    }

    return QRectF(pos, QSizeF(toggleSize, toggleSize));
}

QPixmap KStandardItemListWidget::createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QPixmap pixmap = KItemListWidget::createDragPixmap(option, widget);
    if (m_layout != DetailsLayout) {
        return pixmap;
    }

    // Only return the content of the text-column as pixmap
    const int leftClip = m_pixmapPos.x();

    const TextInfo *textInfo = m_textInfo.value("text");
    const int rightClip = textInfo->pos.x() + textInfo->staticText.size().width() + 2 * styleOption().padding;

    QPixmap clippedPixmap(rightClip - leftClip + 1, pixmap.height());
    clippedPixmap.fill(Qt::transparent);

    QPainter painter(&clippedPixmap);
    painter.drawPixmap(-leftClip, 0, pixmap);

    return clippedPixmap;
}

void KStandardItemListWidget::startActivateSoonAnimation(int timeUntilActivation)
{
    if (m_activateSoonAnimation) {
        m_activateSoonAnimation->stop(); // automatically DeleteWhenStopped
    }

    m_activateSoonAnimation = new QVariantAnimation{this};
    m_activateSoonAnimation->setStartValue(0.0);
    m_activateSoonAnimation->setEndValue(1.0);
    m_activateSoonAnimation->setDuration(timeUntilActivation);

    const QVariant originalIconName{value("iconName")};
    connect(m_activateSoonAnimation, &QVariantAnimation::valueChanged, this, [originalIconName, this](const QVariant &value) {
        auto progress = value.toFloat();

        QVariant wantedIconName;
        if (progress < 0.333) {
            wantedIconName = "folder-open";
        } else if (progress < 0.666) {
            wantedIconName = originalIconName;
        } else {
            wantedIconName = "folder-open";
        }

        QHash<QByteArray, QVariant> itemData{data()};
        if (itemData["iconName"] != wantedIconName) {
            itemData.insert("iconName", wantedIconName);
            setData(itemData);
            invalidateIconCache();
        }
    });

    connect(m_activateSoonAnimation, &QObject::destroyed, this, [originalIconName, this]() {
        QHash<QByteArray, QVariant> itemData{data()};
        if (itemData["iconName"] == "folder-open") {
            itemData.insert("iconName", originalIconName);
            setData(itemData);
            invalidateIconCache();
        }
    });

    m_activateSoonAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

bool KStandardItemListWidget::isIconControlledByActivateSoonAnimation() const
{
    return m_activateSoonAnimation && value("iconName") == "folder-open";
}

KItemListWidgetInformant *KStandardItemListWidget::createInformant()
{
    return new KStandardItemListWidgetInformant();
}

void KStandardItemListWidget::ngSettingsChanged()
{
    m_ngIconFaded = false; // the icon may be needed un-faded again (tile mark / icons shown)
    m_ngIcon = QPixmap();
    invalidateCache();
    update();
}

void KStandardItemListWidget::invalidateCache()
{
    m_dirtyLayout = true;
    m_dirtyContent = true;
}

void KStandardItemListWidget::invalidateIconCache()
{
    m_dirtyContent = true;
    m_dirtyContentRoles.insert("iconPixmap");
}

void KStandardItemListWidget::refreshCache()
{
}

bool KStandardItemListWidget::isRoleRightAligned(const QByteArray &role) const
{
    Q_UNUSED(role)
    return false;
}

bool KStandardItemListWidget::isHidden() const
{
    return false;
}

QFont KStandardItemListWidget::customizedFont(const QFont &baseFont) const
{
    return baseFont;
}

QPalette::ColorRole KStandardItemListWidget::normalTextColorRole() const
{
    if (isPressed()) {
        return QPalette::HighlightedText;
    } else {
        return QPalette::Text;
    }
}

void KStandardItemListWidget::setTextColor(const QColor &color)
{
    if (color != m_customTextColor) {
        m_customTextColor = color;
        updateAdditionalInfoTextColor();
        update();
    }
}

QColor KStandardItemListWidget::textColor(const QWidget &widget) const
{
    if (!isSelected()) {
        if (m_isHidden) {
            return m_additionalInfoTextColor;
        } else if (m_customTextColor.isValid()) {
            return m_customTextColor;
        }
    }

    // dolphin-ng: the drawn rows keep the active text colour when the view has no focus: the colour scheme's inactive
    // effect faded the names, and on the veiled rows of a dark scheme they were hard to read
    const bool ngRows = m_layout == DetailsLayout && ngTwoLineList(this);
    const QPalette::ColorGroup group = (isActiveWindow() && widget.hasFocus()) || ngRows ? QPalette::Active : QPalette::Inactive;
    const QPalette::ColorRole role = normalTextColorRole();
    return styleOption().palette.color(group, role);
}

void KStandardItemListWidget::setOverlays(QHash<Qt::Corner, QString> &overlays)
{
    if (overlays == m_overlays) {
        return;
    }

    m_overlays = overlays;
    m_dirtyContent = true;
    m_dirtyContentRoles.insert("iconOverlays");
    update();
}

QHash<Qt::Corner, QString> KStandardItemListWidget::overlays() const
{
    return m_overlays;
}

QString KStandardItemListWidget::roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values) const
{
    return static_cast<const KStandardItemListWidgetInformant *>(informant())->roleText(role, values);
}

void KStandardItemListWidget::dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles)
{
    Q_UNUSED(current)

    m_dirtyContent = true;

    QSet<QByteArray> dirtyRoles;
    if (roles.isEmpty()) {
        const auto visibleRoles = this->visibleRoles();
        dirtyRoles = QSet<QByteArray>(visibleRoles.constBegin(), visibleRoles.constEnd());
    } else {
        dirtyRoles = roles;
    }

    // The URL might have changed (i.e., if the sort order of the items has
    // been changed). Therefore, the "is cut" state must be updated.
    KFileItemClipboard *clipboard = KFileItemClipboard::instance();
    const QUrl itemUrl = data().value("url").toUrl();
    m_isCut = clipboard->isCut(itemUrl);

    // The icon-state might depend from other roles and hence is
    // marked as dirty whenever a role has been changed
    dirtyRoles.insert("iconPixmap");
    dirtyRoles.insert("iconName");

    QSetIterator<QByteArray> it(dirtyRoles);
    while (it.hasNext()) {
        const QByteArray &role = it.next();
        m_dirtyContentRoles.insert(role);
    }
}

void KStandardItemListWidget::visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous)
{
    Q_UNUSED(previous)
    m_sortedVisibleRoles = current;
    m_dirtyLayout = true;
}

void KStandardItemListWidget::columnWidthChanged(const QByteArray &role, qreal current, qreal previous)
{
    Q_UNUSED(role)
    Q_UNUSED(current)
    Q_UNUSED(previous)
    m_dirtyLayout = true;
}

void KStandardItemListWidget::sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth)
{
    Q_UNUSED(leftPaddingWidth)
    Q_UNUSED(rightPaddingWidth)
    m_dirtyLayout = true;
}

void KStandardItemListWidget::styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous)
{
    KItemListWidget::styleOptionChanged(current, previous);

    updateAdditionalInfoTextColor();
    m_dirtyLayout = true;
}

void KStandardItemListWidget::hoveredChanged(bool hovered)
{
    if (m_layout == CardsLayout || m_ngObjectRow) {
        // dolphin-ng: the pill of actions fades in and out with the hover
        if (!m_ngRevealAnimation) {
            m_ngRevealAnimation = new QVariantAnimation(this);
            m_ngRevealAnimation->setEasingCurve(QEasingCurve::OutCubic);
            connect(m_ngRevealAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
                m_ngReveal = value.toReal();
                update();
            });
        }
        m_ngRevealAnimation->stop();
        m_ngRevealAnimation->setDuration(hovered ? 180 : 220);
        m_ngRevealAnimation->setStartValue(m_ngReveal);
        m_ngRevealAnimation->setEndValue(hovered ? 1.0 : 0.0);
        m_ngRevealAnimation->start();
    }
    if (!hovered && m_activateSoonAnimation) {
        m_activateSoonAnimation->stop(); // automatically DeleteWhenStopped
    }
    m_dirtyLayout = true;

    if (m_layout == CardsLayout) {
        update(); // dolphin-ng: the card shows its actions while hovered
    }

    // dolphin-ng: actions of the hovered row (not on rows drawn like the cards: they have the pill of quick actions)
    if (m_layout == DetailsLayout && ngNoIcons(this) && !m_ngObjectRow) {
        if (hovered) {
            if (!m_ngActions) {
                auto controllerOf = [this]() -> KItemListController * {
                    const auto *view = dynamic_cast<KItemListView *>(parentItem());
                    return view ? view->controller() : nullptr;
                };
                m_ngActions = new NgRowActions(
                    this,
                    [this, controllerOf] {
                        if (KItemListController *controller = controllerOf()) {
                            Q_EMIT controller->itemActivated(index());
                        }
                    },
                    [this, controllerOf](const QPoint &screenPos) {
                        if (KItemListController *controller = controllerOf()) {
                            // the context menu acts on the selection: make it this item
                            controller->selectionManager()->clearSelection();
                            controller->selectionManager()->setSelected(index(), 1);
                            controller->selectionManager()->setCurrentItem(index());
                            Q_EMIT controller->itemContextMenuRequested(index(), QPointF(screenPos));
                        }
                    });
                m_ngActions->setFont(m_customizedFont);
            }
            triggerCacheRefreshing();
            m_ngActions->setGeometry(m_ngActionsRect);
            m_ngActions->setVisible(!m_ngActionsRect.isEmpty());
        } else if (m_ngActions) {
            m_ngActions->hide();
        }
        update();
    }
}

void KStandardItemListWidget::selectedChanged(bool selected)
{
    Q_UNUSED(selected)
    updateAdditionalInfoTextColor();
    m_dirtyContent = true;
}

void KStandardItemListWidget::pressedChanged(bool pressed)
{
    Q_UNUSED(pressed)
    updateAdditionalInfoTextColor();
    m_dirtyContent = true;
}

void KStandardItemListWidget::siblingsInformationChanged(const QBitArray &current, const QBitArray &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
    m_dirtyLayout = true;
}

int KStandardItemListWidget::numberOfUnicodeCharactersIn(const QString &text)
{
    int count = 0;
    QTextBoundaryFinder boundaryFinder(QTextBoundaryFinder::Grapheme, text);
    while (boundaryFinder.toNextBoundary() != -1) {
        ++count;
    }
    return count;
}

int KStandardItemListWidget::selectionLength(const QString &text) const
{
    return numberOfUnicodeCharactersIn(text);
}

void KStandardItemListWidget::editedRoleChanged(const QByteArray &current, const QByteArray &previous)
{
    Q_UNUSED(previous)

    QGraphicsView *parent = !scene() || scene()->views().isEmpty() ? nullptr : scene()->views()[0];
    if (current.isEmpty() || !parent || current != "text") {
        if (m_roleEditor) {
            Q_EMIT roleEditingCanceled(index(), current, data().value(current));
            closeRoleEditor();
        }
        return;
    }

    Q_ASSERT(!m_roleEditor);

    m_roleEditor = new KItemListRoleEditor(parent);
    m_roleEditor->setRole(current);
    m_roleEditor->setAllowUpDownKeyChainEdit(m_layout != IconsLayout);
    m_roleEditor->setFont(styleOption().font);

    const QString text = data().value(current).toString();
    m_roleEditor->setPlainText(text);

    const TextInfo *textInfo = m_textInfo.value("text");
    if (textInfo) {
        QTextOption textOption = textInfo->staticText.textOption();
        m_roleEditor->document()->setDefaultTextOption(textOption);
    }

    const int textSelectionLength = selectionLength(text);

    if (textSelectionLength > 0) {
        QTextCursor cursor = m_roleEditor->textCursor();
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, textSelectionLength);
        m_roleEditor->setTextCursor(cursor);
    }

    connect(m_roleEditor, &KItemListRoleEditor::roleEditingCanceled, this, &KStandardItemListWidget::slotRoleEditingCanceled);
    connect(m_roleEditor, &KItemListRoleEditor::roleEditingFinished, this, &KStandardItemListWidget::slotRoleEditingFinished);

    updateRoleEditorGeometry();
    m_roleEditor->show();
    m_roleEditor->setFocus();
    setHovered(false);
    update();
}

void KStandardItemListWidget::iconSizeChanged(int current, int previous)
{
    KItemListWidget::iconSizeChanged(current, previous);

    invalidateIconCache();
    triggerCacheRefreshing();
    update();
}

void KStandardItemListWidget::resizeEvent(QGraphicsSceneResizeEvent *event)
{
    if (m_roleEditor) {
        setEditedRole(QByteArray());
        Q_ASSERT(!m_roleEditor);
    }

    KItemListWidget::resizeEvent(event);

    m_dirtyLayout = true;
}

void KStandardItemListWidget::showEvent(QShowEvent *event)
{
    KItemListWidget::showEvent(event);

    if (m_roleEditor) {
        m_roleEditor->setFinishedSignalBlocked(false);
        updateRoleEditorGeometry();
        m_roleEditor->show();
        m_roleEditor->setFocus();
    }

    // Listen to changes of the clipboard to mark the item as cut/uncut
    KFileItemClipboard *clipboard = KFileItemClipboard::instance();

    const QUrl itemUrl = data().value("url").toUrl();
    m_isCut = clipboard->isCut(itemUrl);

    connect(clipboard, &KFileItemClipboard::cutItemsChanged, this, &KStandardItemListWidget::slotCutItemsChanged);
}

void KStandardItemListWidget::hideEvent(QHideEvent *event)
{
    if (m_roleEditor) {
        m_roleEditor->setFinishedSignalBlocked(true);
        m_roleEditor->hide();
    }

    disconnect(KFileItemClipboard::instance(), &KFileItemClipboard::cutItemsChanged, this, &KStandardItemListWidget::slotCutItemsChanged);

    KItemListWidget::hideEvent(event);
}

bool KStandardItemListWidget::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::WindowActivate || event->type() == QEvent::PaletteChange) {
        m_dirtyContent = true;
    }

    return KItemListWidget::event(event);
}

void KStandardItemListWidget::finishRoleEditing()
{
    if (!editedRole().isEmpty() && m_roleEditor) {
        slotRoleEditingFinished(editedRole(), KIO::encodeFileName(m_roleEditor->toPlainText()));
    }
}

void KStandardItemListWidget::cancelRoleEditing()
{
    if (!editedRole().isEmpty() && m_roleEditor) {
        slotRoleEditingCanceled(editedRole(), KIO::encodeFileName(m_roleEditor->toPlainText()));
    }
}

void KStandardItemListWidget::updateRoleEditorGeometry()
{
    if (!m_roleEditor || editedRole().isEmpty() || !scene() || scene()->views().isEmpty()) {
        return;
    }

    auto *parent = scene()->views()[0];
    QRectF rect = roleEditingRect(editedRole());
    const int frameWidth = m_roleEditor->frameWidth();
    rect.adjust(-frameWidth, -frameWidth, frameWidth, frameWidth);
    rect.translate(pos());
    if (rect.right() > parent->width()) {
        rect.setWidth(parent->width() - rect.left());
    }
    m_roleEditor->setGeometry(rect.toRect());
    m_roleEditor->autoAdjustSize();
}

void KStandardItemListWidget::slotCutItemsChanged()
{
    const QUrl itemUrl = data().value("url").toUrl();
    const bool isCut = KFileItemClipboard::instance()->isCut(itemUrl);
    if (m_isCut != isCut) {
        m_isCut = isCut;
        m_pixmap = QPixmap();
        m_dirtyContent = true;
        update();
    }
}

void KStandardItemListWidget::slotRoleEditingCanceled(const QByteArray &role, const QVariant &value)
{
    closeRoleEditor();
    Q_EMIT roleEditingCanceled(index(), role, value);
    setEditedRole(QByteArray());
}

void KStandardItemListWidget::slotRoleEditingFinished(const QByteArray &role, const QVariant &value)
{
    closeRoleEditor();
    Q_EMIT roleEditingFinished(index(), role, value);
    setEditedRole(QByteArray());
}

void KStandardItemListWidget::triggerCacheRefreshing()
{
    if ((!m_dirtyContent && !m_dirtyLayout) || index() < 0) {
        return;
    }

    refreshCache();

    const QHash<QByteArray, QVariant> values = data();
    m_isExpandable = m_supportsItemExpanding && values["isExpandable"].toBool();
    m_isHidden = isHidden();
    m_customizedFont = customizedFont(styleOption().font);
    m_customizedFontMetrics = QFontMetrics(m_customizedFont);
    m_columnWidthSum = std::accumulate(m_sortedVisibleRoles.begin(), m_sortedVisibleRoles.end(), qreal(), [this](qreal sum, const auto &role) {
        return sum + columnWidth(role);
    });

    updateExpansionArea();
    updateTextsCache();
    updatePixmapCache();
    clearHoverCache();

    m_dirtyLayout = false;
    m_dirtyContent = false;
    m_dirtyContentRoles.clear();
}

// dolphin-ng: in drawn rows the drawing starts after the expansion arrow (and the tree lines of the parents), when the
// view expands folders in place: they were drawn over the folder
qreal KStandardItemListWidget::ngExpansionIndent() const
{
    if (!m_supportsItemExpanding || m_expansionArea.isEmpty() || layoutDirection() != Qt::LeftToRight) {
        return 0;
    }
    return qMax<qreal>(0, m_expansionArea.right() + 4 - (leftPadding() + 3 + 10));
}

void KStandardItemListWidget::updateExpansionArea()
{
    if (m_supportsItemExpanding) {
        const QHash<QByteArray, QVariant> values = data();
        const int expandedParentsCount = values.value("expandedParentsCount", 0).toInt();
        if (expandedParentsCount >= 0) {
            const int widgetIconSize = styleOption().iconSize;
            const qreal widgetHeight = size().height();
            const qreal inc = (widgetHeight - widgetIconSize) / 2;
            const qreal x = expandedParentsCount * widgetHeight + inc;
            const qreal y = inc;
            if (layoutDirection() == Qt::LeftToRight) {
                const qreal leftPaddingWidth = m_highlightEntireRow ? leftPadding() : 0;
                m_expansionArea = QRectF(leftPaddingWidth + x, y, widgetIconSize, widgetIconSize);
                return;
            }
            const qreal rightPaddingWidth = m_highlightEntireRow ? rightPadding() : 0;
            m_expansionArea = QRectF(size().width() - rightPaddingWidth - x - widgetIconSize, y, widgetIconSize, widgetIconSize);
            return;
        }
    }

    m_expansionArea = QRectF();
}

void KStandardItemListWidget::updatePixmapCache()
{
    // Precondition: Requires already updated m_textPos values to calculate
    // the remaining height when the alignment is vertical.

    const QSizeF widgetSize = size();
    const bool iconOnTop = (m_layout == IconsLayout);
    const KItemListStyleOption &option = styleOption();
    const qreal padding = option.padding;
    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);

    const int widgetIconSize = option.iconSize;
    const int maxIconWidth = iconOnTop ? widgetSize.width() - 2 * padding : widgetIconSize;
    const int maxIconHeight = widgetIconSize;

    const QHash<QByteArray, QVariant> values = data();

    bool updatePixmap = (m_pixmap.width() != maxIconWidth || m_pixmap.height() != maxIconHeight);
    if (!updatePixmap && m_dirtyContent) {
        updatePixmap = m_dirtyContentRoles.isEmpty() || m_dirtyContentRoles.contains("iconPixmap") || m_dirtyContentRoles.contains("iconName")
            || m_dirtyContentRoles.contains("iconOverlays");
    }

    if (updatePixmap) {
        m_pixmap = QPixmap();

        int sequenceIndex = hoverSequenceIndex();

        if (values.contains("hoverSequencePixmaps") && !isIconControlledByActivateSoonAnimation()) {
            // Use one of the hover sequence pixmaps instead of the default
            // icon pixmap.

            const QVector<QPixmap> pixmaps = values["hoverSequencePixmaps"].value<QVector<QPixmap>>();

            if (values.contains("hoverSequenceWraparoundPoint")) {
                const float wap = values["hoverSequenceWraparoundPoint"].toFloat();
                if (wap >= 1.0f) {
                    sequenceIndex %= static_cast<int>(wap);
                }
            }

            const int loadedIndex = qMax(qMin(sequenceIndex, pixmaps.size() - 1), 0);

            if (loadedIndex != 0) {
                m_pixmap = pixmaps[loadedIndex];
            }
        }

        if (m_pixmap.isNull() && !isIconControlledByActivateSoonAnimation()) {
            m_pixmap = values["iconPixmap"].value<QPixmap>();
        }

        if (m_pixmap.isNull()) {
            // Use the icon that fits to the MIME-type
            QString iconName = values["iconName"].toString();
            if (iconName.isEmpty()) {
                // The icon-name has not been not resolved by KFileItemModelRolesUpdater,
                // use a generic icon as fallback
                iconName = QStringLiteral("unknown");
            }
            const bool hasFocus = scene()->views()[0]->parentWidget()->hasFocus();
            m_pixmap = pixmapForIcon(iconName,
                                     QSize(maxIconWidth, maxIconHeight),
                                     m_layout != IconsLayout && isActiveWindow() && isPressed() && hasFocus ? QIcon::Selected : QIcon::Normal);

        } else {
            // dolphin-ng: the preview of a card is scaled once to the place it is shown in (the band of a Tessera, the
            // column of a Scheda), not down to the icon size and up again when painted
            QSizeF cardPicture;
            if (m_layout == CardsLayout && ngCardStyle(this) == NgCardStyle::Tessera) {
                cardPicture = QSizeF(widgetSize.width(), qRound(qMin(widgetSize.width() * 0.40, QFontMetricsF(m_customizedFont).height() * 9.0)));
            } else if (m_layout == CardsLayout && ngCardStyle(this) == NgCardStyle::Scheda) {
                // the object's column: a page as wide as it (see updateCardsLayoutTextCache())
                const qreal column = qMin(widgetSize.height() * 1.3, widgetSize.width() * 0.38);
                cardPicture = QSizeF(column, column);
            } else if (m_layout == CardsLayout && ngCardStyle(this) == NgCardStyle::Dossier) {
                const qreal column = qMin(qRound(widgetSize.height() * 0.38) * 1.6, widgetSize.width() * 0.34);
                cardPicture = QSizeF(column, column);
            }
            if (cardPicture.isValid()) {
                // covering: the narrower side of the preview fills the place (a page its width), the rest is clipped
                const QSize target = (cardPicture * dpr).toSize();
                const Qt::AspectRatioMode mode = Qt::KeepAspectRatioByExpanding;
                m_pixmap = m_pixmap.scaled(target, mode, Qt::SmoothTransformation);
                m_pixmap.setDevicePixelRatio(dpr);
            } else if (m_pixmap.width() / m_pixmap.devicePixelRatio() != maxIconWidth || m_pixmap.height() / m_pixmap.devicePixelRatio() != maxIconHeight) {
                // A custom pixmap has been applied. Assure that the pixmap
                // is scaled to the maximum available size.
                KPixmapModifier::scale(m_pixmap, QSize(maxIconWidth, maxIconHeight) * dpr);
            }
        }

        if (m_pixmap.isNull()) {
            return;
        }

        if (m_isCut) {
            KIconEffect::toDisabled(m_pixmap);
        }

        if (m_isHidden) {
            KIconEffect::semiTransparent(m_pixmap);
        }
    }

    if (m_layout == CardsLayout) {
        ngPlaceCardPixmap(); // dolphin-ng
        return;
    }

    int scaledIconSize = 0;
    if (iconOnTop) {
        const TextInfo *textInfo = m_textInfo.value("text");
        scaledIconSize = static_cast<int>(textInfo->pos.y() - 2 * padding);
    } else {
        const int textRowsCount = (m_layout == CompactLayout) ? visibleRoles().count() : 1;
        const qreal requiredTextHeight = textRowsCount * m_customizedFontMetrics.height();
        scaledIconSize = (requiredTextHeight < maxIconHeight) ? widgetSize.height() - 2 * padding : maxIconHeight;
    }

    const int maxScaledIconWidth = iconOnTop ? widgetSize.width() - 2 * padding : scaledIconSize;
    const int maxScaledIconHeight = scaledIconSize;

    m_scaledPixmapSize = m_pixmap.size();
    m_scaledPixmapSize.scale(maxScaledIconWidth * dpr, maxScaledIconHeight * dpr, Qt::KeepAspectRatio);
    m_scaledPixmapSize = m_scaledPixmapSize / dpr;

    if (iconOnTop) {
        // Center horizontally and align on bottom within the icon-area
        m_pixmapPos.setX((widgetSize.width() - m_scaledPixmapSize.width()) / 2.0);
        m_pixmapPos.setY(padding + scaledIconSize - m_scaledPixmapSize.height());
    } else {
        // Center horizontally and vertically within the icon-area
        const TextInfo *textInfo = m_textInfo.value("text");
        if (QApplication::isRightToLeft()) {
            m_pixmapPos.setX(m_textRect.right() + 2.0 * padding);
        } else {
            m_pixmapPos.setX(textInfo->pos.x() - 2.0 * padding - (scaledIconSize + m_scaledPixmapSize.width()) / 2.0);
        }

        // Derive icon's vertical center from the center of the text frame, including
        // any necessary adjustment if the font's midline is offset from the frame center
        const qreal midlineShift = m_customizedFontMetrics.height() / 2.0 - m_customizedFontMetrics.descent() - m_customizedFontMetrics.capHeight() / 2.0;
        m_pixmapPos.setY(m_textRect.center().y() + midlineShift - m_scaledPixmapSize.height() / 2.0);
    }

    if (m_layout == IconsLayout) {
        m_iconRect = QRectF(m_pixmapPos, QSizeF(m_scaledPixmapSize));
    } else {
        const qreal widthOffset = widgetIconSize - m_scaledPixmapSize.width();
        const qreal heightOffset = widgetIconSize - m_scaledPixmapSize.height();
        const QPointF squareIconPos(m_pixmapPos.x() - 0.5 * widthOffset, m_pixmapPos.y() - 0.5 * heightOffset);
        const QSizeF squareIconSize(widgetIconSize, widgetIconSize);
        m_iconRect = QRectF(squareIconPos, squareIconSize);
    }
}

void KStandardItemListWidget::updateTextsCache()
{
    QTextOption textOption;
    switch (m_layout) {
    case IconsLayout:
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textOption.setAlignment(Qt::AlignHCenter);
        break;
    case CompactLayout:
        textOption.setAlignment(QApplication::isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft);
        textOption.setWrapMode(QTextOption::NoWrap);
        break;
    case DetailsLayout:
    case CardsLayout:
        textOption.setAlignment(Qt::AlignLeft);
        textOption.setWrapMode(QTextOption::NoWrap);
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    qDeleteAll(m_textInfo);
    m_textInfo.clear();
    for (int i = 0; i < m_sortedVisibleRoles.count(); ++i) {
        TextInfo *textInfo = new TextInfo();
        textInfo->staticText.setTextFormat(Qt::PlainText);
        textInfo->staticText.setPerformanceHint(QStaticText::AggressiveCaching);
        textInfo->staticText.setTextOption(textOption);
        m_textInfo.insert(m_sortedVisibleRoles[i], textInfo);
    }

    switch (m_layout) {
    case IconsLayout:
        updateIconsLayoutTextCache();
        break;
    case CompactLayout:
        updateCompactLayoutTextCache();
        break;
    case DetailsLayout:
        updateDetailsLayoutTextCache();
        break;
    case CardsLayout:
        updateCardsLayoutTextCache();
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    const TextInfo *ratingTextInfo = m_textInfo.value("rating");
    if (ratingTextInfo) {
        // The text of the rating-role has been set to empty to get
        // replaced by a rating-image showing the rating as stars.
        const KItemListStyleOption &option = styleOption();
        QSizeF ratingSize = preferredRatingSize(option);

        const qreal availableWidth = (m_layout == DetailsLayout) ? columnWidth("rating") - columnPadding(option) : size().width();
        if (ratingSize.width() > availableWidth) {
            ratingSize.rwidth() = availableWidth;
        }
        const qreal dpr = KItemViewsUtils::devicePixelRatio(this);
        m_rating = QPixmap(ratingSize.toSize() * dpr);
        m_rating.setDevicePixelRatio(dpr);
        m_rating.fill(Qt::transparent);

        QPainter painter(&m_rating);
        const QRect rect(QPoint(0, 0), ratingSize.toSize());
        const int rating = data().value("rating").toInt();
        KRatingPainter::paintRating(&painter, rect, Qt::AlignJustify | Qt::AlignVCenter, rating);
    } else if (!m_rating.isNull()) {
        m_rating = QPixmap();
    }
}

QString KStandardItemListWidget::elideText(QString text, qreal maxWidth) const
{
    if (ContentDisplaySettings::elidingMode() == ContentDisplaySettings::ElidingMode::Middle) {
        return m_customizedFontMetrics.elidedText(text, Qt::ElideMiddle, maxWidth);
    }

    if (ContentDisplaySettings::elidingMode() == ContentDisplaySettings::ElidingMode::Right) {
        qsizetype lastDotPosition = text.lastIndexOf(".");
        QString extension = text.mid(lastDotPosition);

        if (m_customizedFontMetrics.horizontalAdvance(QStringLiteral("…") + extension) > maxWidth) {
            extension = "";
            lastDotPosition = text.size();
        }

        maxWidth -= m_customizedFontMetrics.horizontalAdvance(extension);
        QString leftPart = m_customizedFontMetrics.elidedText(text.left(lastDotPosition), Qt::ElideRight, maxWidth);

        return leftPart + extension;
    }

    Q_UNREACHABLE();
    return text;
}

QString KStandardItemListWidget::escapeString(const QString &text) const
{
    QString escaped(text);

    const QChar returnSymbol(0x21b5);
    escaped.replace('\n', returnSymbol);

    return escaped;
}

void KStandardItemListWidget::updateIconsLayoutTextCache()
{
    //      +------+
    //      | Icon |
    //      +------+
    //
    //    Name role that
    // might get wrapped above
    //    several lines.
    //  Additional role 1
    //  Additional role 2

    const QHash<QByteArray, QVariant> values = data();

    const KItemListStyleOption &option = styleOption();
    const qreal padding = option.padding;
    const qreal maxWidth = size().width() - 2 * padding;
    const qreal lineSpacing = m_customizedFontMetrics.lineSpacing();

    // Initialize properties for the "text" role. It will be used as anchor
    // for initializing the position of the other roles.
    TextInfo *nameTextInfo = m_textInfo.value("text");
    const QString nameText = KStringHandler::preProcessWrap(escapeString(values["text"].toString()));
    nameTextInfo->staticText.setText(nameText);

    // Calculate the number of lines required for the name and the required width
    qreal nameWidth = 0;
    qreal nameHeight = 0;
    QTextLine line;

    QTextLayout layout(nameTextInfo->staticText.text(), m_customizedFont);
    layout.setTextOption(nameTextInfo->staticText.textOption());
    layout.beginLayout();
    int nameLineIndex = 0;
    while ((line = layout.createLine()).isValid()) {
        line.setLineWidth(maxWidth);
        nameWidth = qMax(nameWidth, line.naturalTextWidth());
        nameHeight += line.height();

        ++nameLineIndex;
        if (nameLineIndex == option.maxTextLines) {
            // The maximum number of textlines has been reached. If this is
            // the case provide an elided text if necessary.
            const int textLength = line.textStart() + line.textLength();
            if (textLength < nameText.length()) {
                // Elide the last line of the text
                qreal elidingWidth = maxWidth;
                qreal lastLineWidth;
                do {
                    QString lastTextLine = nameText.mid(line.textStart());
                    lastTextLine = elideText(lastTextLine, elidingWidth);
                    const QString elidedText = nameText.left(line.textStart()) + lastTextLine;
                    nameTextInfo->staticText.setText(elidedText);

                    lastLineWidth = m_customizedFontMetrics.size(0, lastTextLine).width();

                    // We do the text eliding in a loop with decreasing width (1 px / iteration)
                    // to avoid problems related to different width calculation code paths
                    // within Qt. (see bug 337104)
                    elidingWidth -= 1.0;
                } while (lastLineWidth > maxWidth);

                nameWidth = qMax(nameWidth, lastLineWidth);
            }
            break;
        }
    }
    layout.endLayout();

    // Use one line for each additional information
    nameTextInfo->staticText.setTextWidth(maxWidth);
    nameTextInfo->pos = QPointF(padding, styleOption().iconSize + 2 * padding);
    m_textRect = QRectF(padding + (maxWidth - nameWidth) / 2, nameTextInfo->pos.y(), nameWidth, nameHeight);

    // Calculate the position for each additional information
    qreal y = nameTextInfo->pos.y() + nameHeight;
    for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
        if (role == "text") {
            continue;
        }

        const QString text = roleText(role, values);
        TextInfo *textInfo = m_textInfo.value(role);
        textInfo->staticText.setText(text);

        qreal requiredWidth = 0;

        QTextLayout layout(text, m_customizedFont);
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(textOption);

        layout.beginLayout();
        QTextLine textLine = layout.createLine();
        if (textLine.isValid()) {
            textLine.setLineWidth(maxWidth);
            requiredWidth = textLine.naturalTextWidth();
            if (requiredWidth > maxWidth) {
                const QString elidedText = m_customizedFontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
                textInfo->staticText.setText(elidedText);
                requiredWidth = m_customizedFontMetrics.horizontalAdvance(elidedText);
            } else if (role == "rating") {
                // Use the width of the rating pixmap, because the rating text is empty.
                requiredWidth = m_rating.width() / m_rating.devicePixelRatioF();
            }
        }
        layout.endLayout();

        textInfo->pos = QPointF(padding, y);
        textInfo->staticText.setTextWidth(maxWidth);

        const QRectF textRect(padding + (maxWidth - requiredWidth) / 2, y, requiredWidth, lineSpacing);

        // Ignore empty roles. Avoids a text rect taller than the area that actually contains text.
        if (!textRect.isEmpty()) {
            m_textRect |= textRect;
        }

        y += lineSpacing;
    }

    // Add a padding to the text rectangle
    m_textRect.adjust(-padding, -padding, padding, padding);
}

void KStandardItemListWidget::updateCompactLayoutTextCache()
{
    // +------+  Name role
    // | Icon |  Additional role 1
    // +------+  Additional role 2

    const QHash<QByteArray, QVariant> values = data();

    const KItemListStyleOption &option = styleOption();
    const qreal widgetHeight = size().height();
    const qreal lineSpacing = m_customizedFontMetrics.lineSpacing();
    const qreal textLinesHeight = qMax(visibleRoles().count(), 1) * lineSpacing;

    qreal maximumRequiredTextWidth = 0;
    const qreal x = QApplication::isRightToLeft() ? option.padding : option.padding * 3 + styleOption().iconSize;
    qreal y = qRound((widgetHeight - textLinesHeight) / 2);
    const qreal maxWidth = size().width() - styleOption().iconSize - 4 * option.padding;
    for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
        const QString text = escapeString(roleText(role, values));
        TextInfo *textInfo = m_textInfo.value(role);
        textInfo->staticText.setText(text);

        qreal requiredWidth = m_customizedFontMetrics.horizontalAdvance(text);
        if (requiredWidth > maxWidth) {
            requiredWidth = maxWidth;
            if (role == "text") {
                const QString elidedText = elideText(text, maxWidth);
                textInfo->staticText.setText(elidedText);
            } else {
                const QString elidedText = m_customizedFontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
                textInfo->staticText.setText(elidedText);
            }
        }

        textInfo->pos = QPointF(x, y);
        textInfo->staticText.setTextWidth(maxWidth);

        maximumRequiredTextWidth = qMax(maximumRequiredTextWidth, requiredWidth);

        y += lineSpacing;
    }

    m_textRect = QRectF(x - option.padding, 0, maximumRequiredTextWidth + 2 * option.padding, widgetHeight);
}

void KStandardItemListWidget::updateDetailsLayoutTextCache()
{
    // Precondition: Requires already updated m_expansionArea
    // to determine the left position.

    // +------+
    // | Icon |  Name role   Additional role 1   Additional role 2
    // +------+
    // Mirror the above for right-to-left languages.
    const bool isLeftToRight = QApplication::layoutDirection() == Qt::LeftToRight;
    m_textRect = QRectF();

    const KItemListStyleOption &option = styleOption();
    const QHash<QByteArray, QVariant> values = data();

    const qreal widgetHeight = size().height();
    const int fontHeight = m_customizedFontMetrics.height();

    const qreal columnWidthInc = columnPadding(option);
    qreal firstColumnInc = styleOption().iconSize;
    if (m_supportsItemExpanding) {
        firstColumnInc += isLeftToRight ? (m_expansionArea.left() + m_expansionArea.right() + widgetHeight) / 2
                                        : ((size().width() - m_expansionArea.left()) + (size().width() - m_expansionArea.right()) + widgetHeight) / 2;
    } else {
        firstColumnInc += option.padding + (isLeftToRight ? leftPadding() : rightPadding());
    }

    qreal x = firstColumnInc;
    const qreal standardY = qMax(qreal(option.padding), (widgetHeight - fontHeight) / 2.0);

    if (ngTwoLineList(this)) {
        // dolphin-ng, with icons:   +------+  Name                                            Date
        //                            | Icon |  Size · Type · other roles
        // dolphin-ng, icon-less:    TYPE (watermark)  Name                        Size        Date / actions on hover
        //                                              Type · other roles          [bar]
        static const QSet<QByteArray> dateRoles{"modificationtime", "creationtime", "accesstime", "deletiontime"};
        const bool noIcons = ngNoIcons(this);
        QByteArray dateRole;
        for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
            if (dateRoles.contains(role)) {
                dateRole = role;
                break;
            }
        }
        m_ngDateRole = dateRole;
        const qreal gap = columnWidthInc / 2;
        // the Details view: the drawn rows with columns, the small tile for a mark, one line when compact
        const bool columns = noIcons && ngRowColumns(this);
        const bool compact = columns && ngDetailsCompact();
        m_ngDetailsRow = columns;
        m_ngCompactRow = compact;
        const NgMark rowMark = columns ? NgMark::Tile : ngMark();
        qreal left = firstColumnInc + gap;
        if (noIcons) {
            switch (rowMark) {
            case NgMark::Tile:
                left = leftPadding() + 13 + (compact ? widgetHeight - 10 : widgetHeight - 18) + (compact ? 12 : 16);
                break;
            case NgMark::Pill:
                left = leftPadding() + 13 + fontHeight * 4.8 + 16;
                break;
            case NgMark::Signs:
                left = leftPadding() + 17 + 22 + 9 + fontHeight * 2.7 + 14;
                break;
            case NgMark::Header:
                left = leftPadding() + 3 + 62 + 16;
                break;
            case NgMark::Object:
                left = leftPadding() + 3 + 10 + ngExpansionIndent() + qRound(widgetHeight * 1.75) + 18;
                break;
            default:
                left = leftPadding() + 14 + widgetHeight * 0.62 + 8 + fontHeight * 5.4;
                break;
            }
        }
        const qreal right = size().width() - 18;
        const int lineGap = 2;
        const qreal firstY = compact ? standardY : qMax(qreal(option.padding), (widgetHeight - (2 * fontHeight + lineGap)) / 2.0);
        const qreal secondY = firstY + fontHeight + lineGap;

        // a narrow view drops the date first, then the size column
        // dolphin-ng: rows drawn like the cards: the drawing says type and size, the dates are the two lines at the end
        m_ngObjectRow = noIcons && rowMark == NgMark::Object;
        // with columns: the name and its line end where the header's name column ends; every other role goes in its
        // column, at the place of the header's (no dates block, no chips for them)
        if (columns) {
            dateRole.clear();
            m_ngDateRole.clear();
        }
        const bool objectDates = m_ngObjectRow && !columns && ngSetting("ROW_DATES").toLower() != QLatin1String("hide");
        const bool roomForDate = (!noIcons || size().width() >= fontHeight * 36) && !objectDates;
        const bool roomForSize = size().width() >= fontHeight * 26;
        if (!dateRole.isEmpty() && !roomForDate) {
            m_textInfo.value(dateRole)->staticText.setText(QString());
        }

        // ---- right column: how long ago (big), the date and time under it (small); DATE_AGO=hide keeps the plain date
        qreal dateWidth = 0;
        m_ngAgo.clear();
        const bool showAgo = ngSetting("DATE_AGO").toLower() != QLatin1String("hide");
        const QLocale locale;
        if (!dateRole.isEmpty() && roomForDate) {
            TextInfo *textInfo = m_textInfo.value(dateRole);
            const QDateTime when = ngDateTimeOf(values.value(dateRole));
            QString text = roleText(dateRole, values);
            if (showAgo && when.isValid()) {
                text = ngAgoText(when);
                m_ngAgo = QStringLiteral("%1 · %2").arg(locale.toString(when.date(), QStringLiteral("dd/MM")), locale.toString(when.time(), QLocale::ShortFormat));
                m_ngAgoPos = QPointF(right - m_customizedFontMetrics.horizontalAdvance(m_ngAgo), secondY + m_customizedFontMetrics.ascent());
            }
            dateWidth = m_customizedFontMetrics.horizontalAdvance(text);
            textInfo->staticText.setText(text);
            textInfo->pos = QPointF(right - dateWidth, m_ngAgo.isEmpty() ? standardY : firstY);
        }
        qreal blockLeft = right;
        m_ngActionsRect = QRectF();
        if (noIcons) {
            const qreal dateColumn = roomForDate ? qMax(qreal(fontHeight * 8), qreal(150)) : qreal(fontHeight * 8.2); // fixed, so that the sizes line up
            const qreal actionsWidth = qMin(dateColumn, qreal(fontHeight * 8.2));
            const qreal actionsHeight = qMin(widgetHeight - 12, qreal(fontHeight * 1.7));
            m_ngActionsRect = QRectF(right - actionsWidth, (widgetHeight - actionsHeight) / 2.0, actionsWidth, actionsHeight);
            blockLeft = objectDates ? right + gap : right - dateColumn;
        } else if (dateWidth > 0) {
            blockLeft = right - dateWidth - 3 * gap;
        }
        if (m_ngActions) {
            m_ngActions->setGeometry(m_ngActionsRect);
        }

        // ---- size column, with a bar on a logarithmic scale (1 KiB ... 10 GiB); folders: size above, item count below
        m_ngBarFraction = -1;
        m_ngBarRect = QRectF();
        m_ngCount.clear();
        const bool sizeColumn = noIcons && !m_ngObjectRow && !columns && m_sortedVisibleRoles.contains("size");
        // the type and the size written in the drawing
        m_ngRowCount = values.contains("count") ? values.value("count").toInt() : -1;
        m_ngInfoSize = values.value("isDir").toBool() ? (values.value("size").toLongLong() > 0 ? roleText("size", values) : QString()) : roleText("size", values);
        const bool isDir = values.value("isDir").toBool();
        const qlonglong bytes = values.value("size").toLongLong();
        const bool measured = isDir && bytes >= 0 && values.contains("count") && ContentDisplaySettings::directorySizeMode() == ContentDisplaySettings::EnumDirectorySizeMode::ContentSize;
        if (sizeColumn && !roomForSize) {
            m_textInfo.value("size")->staticText.setText(QString());
        }
        if (sizeColumn && roomForSize) {
            const qreal columnWidth = qMax(qreal(fontHeight * 6.5), qreal(130));
            const qreal sizeRight = blockLeft - gap;
            QString text = roleText("size", values);
            // a folder whose content is not measured (or empty) says how many items it holds, not "0 B"
            const bool unmeasured = isDir && bytes <= 0 && values.contains("count");
            if (unmeasured) {
                text = i18ncp("@item:intable", "%1 item", "%1 items", values.value("count").toInt());
            }
            const qreal width = m_customizedFontMetrics.horizontalAdvance(text);
            TextInfo *textInfo = m_textInfo.value("size");
            textInfo->staticText.setText(text);
            textInfo->pos = QPointF(sizeRight - width, firstY);
            if (!isDir && bytes > 0) {
                m_ngBarFraction = qBound(0.03, (std::log10(double(bytes)) - 3.0) / 7.0, 1.0);
                const qreal barWidth = columnWidth - 10;
                m_ngBarRect = QRectF(sizeRight - barWidth, secondY + fontHeight / 2.0 - 2, barWidth, 4);
            }
            if (measured && !unmeasured) {
                m_ngCount = i18ncp("@item:intable", "%1 item", "%1 items", values.value("count").toInt());
                m_ngCountPos = QPointF(sizeRight - m_customizedFontMetrics.horizontalAdvance(m_ngCount), secondY + m_customizedFontMetrics.ascent());
            }
            blockLeft = sizeRight - columnWidth;
        }

        // ---- the dates of the item (the creation time is not a requested role: read it from the item itself)
        QDateTime created;
        QDateTime modified = ngDateTimeOf(values.value("modificationtime"));
        const bool wantDates = noIcons && !columns && ngSetting("ROW_DATES").toLower() != QLatin1String("hide");
        if (wantDates) {
            if (values.contains("creationtime")) {
                created = ngDateTimeOf(values.value("creationtime"));
            } else if (auto *fileView = qobject_cast<KFileItemListView *>(parentWidget())) {
                if (auto *fileModel = qobject_cast<KFileItemModel *>(fileView->model())) {
                    const KFileItem fileItem = fileModel->fileItem(index());
                    if (!fileItem.isNull()) {
                        created = fileItem.time(KFileItem::CreationTime);
                        if (!modified.isValid()) {
                            modified = fileItem.time(KFileItem::ModificationTime);
                        }
                    }
                }
            }
        }

        // in Recent Files the first date is when the file was last used (what the view is sorted by)
        bool usedFirst = false;
        if (wantDates && ngSortedByUse()) {
            const QDateTime used = ngDateTimeOf(values.value("accesstime"));
            if (used.isValid()) {
                created = used;
                usedFirst = true;
            }
        }

        // ---- time column: "C  12/10/26 · 29 giorni fa" over "M  09/11/26 · ieri"; dropped on a narrow view (then chips)
        m_ngTime.clear();
        const bool roomForTime = size().width() >= fontHeight * 42;
        QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        monoFont.setPointSizeF(m_customizedFont.pointSizeF() * 0.8);
        monoFont.setBold(true);
        const QFontMetricsF monoMetrics(monoFont);
        if (wantDates && roomForTime && modified.isValid()) {
            const qreal timeWidth = qMax(qreal(fontHeight * 12.5), qreal(205));
            const qreal timeRight = blockLeft - gap;
            const qreal timeLeft = timeRight - timeWidth;
            QFont small = m_customizedFont;
            small.setPointSizeF(small.pointSizeF() * 0.9);
            const QFontMetricsF smallMetrics(small);
            const QString createdLabel = usedFirst ? (m_ngObjectRow ? ngTr("Used", "Usato") : QStringLiteral("U"))
                                                   : (m_ngObjectRow ? ngTr("Created", "Creato") : QStringLiteral("C"));
            const QString modifiedLabel = m_ngObjectRow ? ngTr("Modified", "Modificato") : QStringLiteral("M");
            const qreal letterWidth = m_ngObjectRow ? qMax(smallMetrics.horizontalAdvance(createdLabel), smallMetrics.horizontalAdvance(modifiedLabel)) + 10
                                                    : monoMetrics.horizontalAdvance(QStringLiteral("M")) + 8;
            auto line = [&](const QString &letter, const QDateTime &when, qreal y) {
                NgTimeLine entry;
                entry.letter = letter;
                entry.text = QStringLiteral("%1 · %2").arg(locale.toString(when.date(), QLocale::ShortFormat), ngAgoText(when));
                entry.text = smallMetrics.elidedText(entry.text, Qt::ElideRight, timeWidth - letterWidth);
                entry.letterPos = m_ngObjectRow ? QPointF(timeLeft, y + smallMetrics.ascent())
                                                : QPointF(timeLeft, y + monoMetrics.ascent() + (smallMetrics.height() - monoMetrics.height()) / 2);
                entry.textPos = QPointF(timeLeft + letterWidth, y + smallMetrics.ascent());
                m_ngTime.append(entry);
            };
            const qreal rowsHeight = 2 * smallMetrics.height() + 1;
            const qreal top = (widgetHeight - rowsHeight) / 2.0;
            if (created.isValid()) {
                line(createdLabel, created, top);
                line(modifiedLabel, modified, top + smallMetrics.height() + 1);
            } else {
                line(modifiedLabel, modified, top + (smallMetrics.height() + 1) / 2.0);
            }
            blockLeft = timeLeft;
        }
        if (columns) {
            blockLeft = qMin(blockLeft, leftPadding() + columnWidth("text"));
            m_ngActionsRect = QRectF();
        }
        qreal textRight = blockLeft - 2 * gap;

        // ---- the quick actions of the file, as on the cards: one pill at the end of the text column, before the dates
        m_ngCardActions.clear();
        m_ngCardActionIndex.clear();
        m_ngCardFab = QRectF();
        m_ngCardHoverPill = m_ngObjectRow || columns; // drawn rows and Details: the actions come with the pointer, as on the cards
        m_ngCardActionIcons.clear();
        m_ngCardActionSeps.clear();
        m_ngCardActionsPill = QRectF();
        if (ngSetting("ROW_ACTIONS").toLower() != QLatin1String("hide")) {
            QList<NgQuickAction> rowActions;
            if (auto *fileView = qobject_cast<KFileItemListView *>(parentWidget())) {
                if (auto *fileModel = qobject_cast<KFileItemModel *>(fileView->model())) {
                    const KFileItem fileItem = fileModel->fileItem(index());
                    if (!fileItem.isNull() && fileItem.isLocalFile()) {
                        rowActions = ngQuickActionsFor(fileItem.localPath(), fileItem.mimetype());
                    }
                }
            }
            QFont pillFont = m_customizedFont;
            pillFont.setPointSizeF(pillFont.pointSizeF() * 0.88);
            const QFontMetricsF pillMetrics(pillFont);
            const qreal pillHeight = qMin(qreal(fontHeight + 2), pillMetrics.height() + 5) + 12;
            const qreal earliest = left + fontHeight * 14; // the name keeps at least this much
            if (!rowActions.isEmpty() && textRight - earliest > 60 && widgetHeight >= pillHeight + 4) {
                ngLayoutActionPill(rowActions, earliest, (widgetHeight - pillHeight) / 2.0, textRight, pillHeight, pillMetrics, true);
                if (!m_ngCardActionsPill.isEmpty()) {
                    textRight = m_ngCardActionsPill.left() - 12;
                }
            }
        }

        if (noIcons) {
            const NgKind kind = ngKindFor(values.value("text").toString(), isDir);
            m_ngSigla = kind.sigla;
            m_ngTint = kind.tint;
            m_ngCategory = int(kind.category);
            const QString iconName = values.value("iconName").toString();
            const int iconSide = qMax(16, qRound(widgetHeight * 0.62));
            if (iconName != m_ngIconName || m_ngIcon.isNull() || qRound(m_ngIcon.width() / m_ngIcon.devicePixelRatio()) != iconSide) {
                m_ngIconName = iconName;
                m_ngIcon = iconName.isEmpty() ? QPixmap() : QIcon::fromTheme(iconName).pixmap(QSize(iconSide, iconSide), KItemViewsUtils::devicePixelRatio(this));
                m_ngIconFaded = false;
            }
            // the row, the abbreviation and the size bar take the main colour of the type icon
            const bool darkScheme = ngIsDark(styleOption().palette);
            const QColor iconColor = ngIconColor(iconName, m_ngIcon);
            m_ngTint = ngAccent(iconColor.isValid() ? iconColor : m_ngTint, darkScheme);
            if (isDir) {
                if (const int special = ngSpecialFolder(values.value("url").toUrl().toLocalFile())) {
                    m_ngTint = ngAccent(ngSpecialFolderColour(special), darkScheme);
                }
            }
            if (!m_ngIcon.isNull() && !m_ngIconFaded) {
                // watermark: the icon fades out from left to right (done once, after its colour was read)
                QPainter fade(&m_ngIcon);
                fade.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                const qreal side = m_ngIcon.width() / m_ngIcon.devicePixelRatio();
                QLinearGradient mask(0, 0, side, 0);
                mask.setColorAt(0.0, QColor(0, 0, 0, 255));
                mask.setColorAt(0.45, QColor(0, 0, 0, 200));
                mask.setColorAt(1.0, QColor(0, 0, 0, 30));
                fade.fillRect(QRectF(0, 0, side, side), mask);
                fade.end();
                m_ngIconFaded = true;
            }
        } else {
            m_ngSigla.clear();
        }

        // ---- first line: the name; second line: chips (type first, in the row's colour, then the other details)
        m_ngChips.clear();
        static const QSet<QByteArray> allDateRoles{"modificationtime", "creationtime", "accesstime", "deletiontime"};
        QFont chipFont = m_customizedFont;
        chipFont.setPointSizeF(chipFont.pointSizeF() * 0.88);
        const QFontMetricsF chipMetrics(chipFont);
        const qreal chipHeight = qMin(qreal(fontHeight + 2), chipMetrics.height() + 5);
        const qreal chipY = secondY + (fontHeight - chipHeight) / 2.0;
        qreal chipX = left;
        auto chip = [&](const QString &text, bool tinted) {
            if (text.isEmpty() || compact) {
                return; // (compact: one line, the name alone)
            }
            const qreal width = chipMetrics.horizontalAdvance(text) + 16;
            if (chipX + width > textRight) {
                return; // no room: the chips to the right fall off
            }
            m_ngChips.append({text, QRectF(chipX, chipY, width, chipHeight), tinted});
            chipX += width + 5;
        };
        QString typeText;
        QStringList plainChips;
        qreal columnX = leftPadding() + columnWidth("text"); // where the first column after the name starts
        for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
            TextInfo *textInfo = m_textInfo.value(role);
            if (columns && role != "text") {
                // in its column, vertically centred, elided to it; sizes and counts to the right, as in Dolphin
                const qreal roleWidth = columnWidth(role);
                QString text = roleText(role, values);
                const qreal available = qMax(qreal(0), roleWidth - columnWidthInc);
                qreal width = m_customizedFontMetrics.horizontalAdvance(text);
                if (width > available) {
                    text = m_customizedFontMetrics.elidedText(text, Qt::ElideRight, available);
                    width = m_customizedFontMetrics.horizontalAdvance(text);
                }
                textInfo->staticText.setText(text);
                const qreal x = isRoleRightAligned(role) ? columnX + roleWidth - columnWidthInc / 2 - width : columnX + columnWidthInc / 2;
                textInfo->pos = QPointF(x, (widgetHeight - fontHeight) / 2.0);
                columnX += roleWidth;
                continue;
            }
            if (role == "text") {
                QString text = escapeString(roleText(role, values));
                qreal width = m_customizedFontMetrics.horizontalAdvance(text);
                const qreal available = qMax(qreal(0), textRight - left);
                if (width > available) {
                    text = elideText(text, available);
                    width = m_customizedFontMetrics.horizontalAdvance(text);
                }
                textInfo->staticText.setText(text);
                textInfo->pos = QPointF(left, firstY);
                m_textRect = QRectF(left - option.padding, 0, width + 2 * option.padding, widgetHeight);
                continue;
            }
            if (!noIcons) {
                // with icons the second line stays plain text
                if (role != dateRole && !(sizeColumn && role == "size")) {
                    QString text = roleText(role, values);
                    if (!text.isEmpty() && chipX > left) {
                        text.prepend(QStringLiteral("·  "));
                    }
                    qreal width = m_customizedFontMetrics.horizontalAdvance(text);
                    const qreal available = textRight - chipX;
                    if (width > available) {
                        text = available > 3 * fontHeight ? m_customizedFontMetrics.elidedText(text, Qt::ElideRight, available) : QString();
                        width = m_customizedFontMetrics.horizontalAdvance(text);
                    }
                    textInfo->staticText.setText(text);
                    textInfo->pos = QPointF(chipX, secondY);
                    if (!text.isEmpty()) {
                        chipX += width + gap;
                    }
                }
                continue;
            }
            if (role == dateRole || (sizeColumn && role == "size")) {
                continue; // laid out above, in their columns
            }
            textInfo->staticText.setText(QString()); // drawn as a chip instead (or not at all)
            if (wantDates && allDateRoles.contains(role)) {
                continue;
            }
            if (role == "type") {
                typeText = roleText(role, values);
            } else if (role == "size" && m_ngObjectRow) {
                // written in the drawing
            } else if (role == "size" && isDir && !sizeColumn) {
                plainChips << roleText(role, values); // item count, when there is no size column
            } else {
                plainChips << roleText(role, values);
            }
        }
        if (noIcons) {
            if (typeText.isEmpty() && m_ngObjectRow) {
                // the drawing shows the extension: say the kind in words
                if (auto *fileView = qobject_cast<KFileItemListView *>(parentWidget())) {
                    if (auto *fileModel = qobject_cast<KFileItemModel *>(fileView->model())) {
                        const KFileItem fileItem = fileModel->fileItem(index());
                        if (!fileItem.isNull()) {
                            typeText = fileItem.mimeComment();
                        }
                    }
                }
            }
            if (typeText.isEmpty()) {
                typeText = isDir ? ngTr("Folder", "Cartella") : values.value("text").toString().section(QLatin1Char('.'), -1).toUpper();
            }
            chip(typeText, !m_ngObjectRow);
            if (m_ngObjectRow && isDir) {
                // the folder's drawing is small in a row: its item count and size are said in words too
                if (values.contains("count") && values.value("count").toInt() >= 0) {
                    chip(i18ncp("@item:intable", "%1 item", "%1 items", values.value("count").toInt()), false);
                }
                if (!m_ngInfoSize.isEmpty()) {
                    chip(m_ngInfoSize, false);
                }
            }
            if (wantDates && m_ngTime.isEmpty()) {
                // no time column: the dates become chips
                if (modified.isValid()) {
                    chip(ngTr("modified %1", "modificato %1").arg(ngAgoText(modified)), false);
                }
                if (created.isValid()) {
                    chip(ngTr("created %1", "creato %1").arg(ngAgoText(created)), false);
                }
            }
            if (measured && !roomForSize && !m_ngObjectRow) {
                chip(i18ncp("@item:intable", "%1 item", "%1 items", values.value("count").toInt()), false);
            }
            for (const QString &text : std::as_const(plainChips)) {
                chip(text, false);
            }
        }
        return;
    }

    for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
        QString text = roleText(role, values);

        // Elide the text in case it does not fit into the available column-width
        qreal requiredWidth = m_customizedFontMetrics.horizontalAdvance(text);
        const qreal roleWidth = columnWidth(role);
        qreal availableTextWidth = roleWidth - columnWidthInc;

        const bool isTextRole = (role == "text");
        if (isTextRole) {
            text = escapeString(text);
            availableTextWidth -= firstColumnInc - (isLeftToRight ? leftPadding() : rightPadding());
        }

        if (requiredWidth > availableTextWidth) {
            if (isTextRole) {
                text = elideText(text, availableTextWidth);
            } else {
                text = m_customizedFontMetrics.elidedText(text, Qt::ElideRight, availableTextWidth);
            }
            requiredWidth = m_customizedFontMetrics.horizontalAdvance(text);
        }

        TextInfo *textInfo = m_textInfo.value(role);
        textInfo->staticText.setText(text);
        qreal y = standardY;
        if (isTextRole) {
            QTextLayout layout(text, m_customizedFont);
            layout.beginLayout();
            QTextLine line = layout.createLine();
            layout.endLayout();
            if (line.isValid()) {
                y = standardY + (m_customizedFontMetrics.ascent() - line.ascent());
            }
        }
        textInfo->pos = QPointF(isLeftToRight ? (x + columnWidthInc / 2) : (size().width() - (x + columnWidthInc / 2) - requiredWidth), y);
        x += roleWidth;

        if (isTextRole) {
            m_textRect = QRectF(textInfo->pos.x() - option.padding, 0, requiredWidth + 2 * option.padding, size().height());

            // The column after the name should always be aligned on the same x-position independent
            // from the expansion-level shown in the name column
            x -= firstColumnInc - (isLeftToRight ? leftPadding() : rightPadding());
        } else if (isRoleRightAligned(role) && isLeftToRight) {
            textInfo->pos.rx() += roleWidth - requiredWidth - columnWidthInc;
        }
    }
}

void KStandardItemListWidget::updateAdditionalInfoTextColor()
{
    QColor c1;
    const bool hasFocus = scene()->views()[0]->parentWidget()->hasFocus();
    if (m_customTextColor.isValid()) {
        c1 = m_customTextColor;
    } else if (isSelected() && hasFocus && (m_layout != DetailsLayout || m_highlightEntireRow)) {
        // The detail text color needs to match the main text (HighlightedText) for the same level
        // of readability. We short circuit early here to avoid interpolating with another color.
        m_additionalInfoTextColor = styleOption().palette.color(normalTextColorRole());
        return;
    } else {
        c1 = styleOption().palette.text().color();
    }

    // For the color of the additional info the inactive text color
    // is not used as this might lead to unreadable text for some color schemes. Instead
    // the text color c1 is slightly mixed with the background color.
    const QColor c2 = styleOption().palette.base().color();
    const int p1 = 70;
    const int p2 = 100 - p1;
    m_additionalInfoTextColor =
        QColor((c1.red() * p1 + c2.red() * p2) / 100, (c1.green() * p1 + c2.green() * p2) / 100, (c1.blue() * p1 + c2.blue() * p2) / 100);
}

void KStandardItemListWidget::drawOverlays(QPainter *painter) const
{
    // similar to KIconUtils::addOverlays, keep in sync preferably
    if (m_overlays.isEmpty()) {
        return;
    }

    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);

    // Size depends only on the zoom-level icon size, so it is consistent between
    // files regardless of thumbnail aspect ratio.
    const int iconSize = styleOption().iconSize;
    int overlaySize;
    if (iconSize < 32) {
        overlaySize = 8;
    } else if (iconSize <= 48) {
        overlaySize = 16;
    } else if (iconSize <= 96) {
        overlaySize = 22;
    } else if (iconSize < 256) {
        overlaySize = 32;
    } else {
        overlaySize = 64;
    }

    // Anchor to the corners of the icon as actually drawn on screen.
    QRectF iconRect(m_pixmapPos, QSizeF(m_scaledPixmapSize));

    // Clamp to the thumbnail height so the overlay never spills onto the file name
    // below a short thumbnail.
    overlaySize = qMin<int>(overlaySize, qFloor(iconRect.height()));
    if (overlaySize <= 0) {
        return;
    }

    // For a thumbnail narrower than the overlay, grow the anchor into the side
    // margins so the overlays sit beside it rather than on top of it.
    if (iconRect.width() < overlaySize) {
        iconRect.adjust(-overlaySize, 0, overlaySize, 0);
    }

    for (const auto &[corner, overlay] : m_overlays.asKeyValueRange()) {
        const QPixmap overlayPixmap = QIcon::fromTheme(overlay).pixmap(QSize{overlaySize, overlaySize}, dpr);
        if (overlayPixmap.isNull()) {
            continue;
        }

        QPointF startPoint;
        switch (corner) {
        case Qt::BottomLeftCorner:
            startPoint = QPointF{iconRect.left(), iconRect.bottom() - overlaySize};
            break;
        case Qt::BottomRightCorner:
            startPoint = QPointF{iconRect.right() - overlaySize, iconRect.bottom() - overlaySize};
            break;
        case Qt::TopRightCorner:
            startPoint = QPointF{iconRect.right() - overlaySize, iconRect.top()};
            break;
        case Qt::TopLeftCorner:
            startPoint = iconRect.topLeft();
            break;
        }
        painter->drawPixmap(startPoint, overlayPixmap);
    }
}

void KStandardItemListWidget::drawPixmap(QPainter *painter, const QPixmap &pixmap)
{
    // dolphin-ng: in the tiles of the icons view the picture gets rounded corners
    const bool rounded = m_layout == IconsLayout && ngTiles();
    if (rounded) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(m_pixmapPos, QSizeF(m_scaledPixmapSize)), 9, 9);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipPath(clip, Qt::IntersectClip);
    }
    auto done = qScopeGuard([painter, rounded] {
        if (rounded) {
            painter->restore();
        }
    });

    if (m_scaledPixmapSize != pixmap.size() / pixmap.devicePixelRatio()) {
        const qreal dpr = KItemViewsUtils::devicePixelRatio(this);
        QPixmap scaledPixmap = pixmap;
        KPixmapModifier::scale(scaledPixmap, m_scaledPixmapSize * dpr);
        scaledPixmap.setDevicePixelRatio(dpr);
        painter->drawPixmap(m_pixmapPos, scaledPixmap);

#ifdef KSTANDARDITEMLISTWIDGET_DEBUG
        painter->setPen(Qt::blue);
        painter->drawRect(QRectF(m_pixmapPos, QSizeF(m_scaledPixmapSize)));
#endif
    } else {
        painter->drawPixmap(m_pixmapPos, pixmap);
    }
}

void KStandardItemListWidget::drawSiblingsInformation(QPainter *painter)
{
    const int siblingSize = size().height();
    const int x = (m_expansionArea.left() + m_expansionArea.right() - siblingSize) / 2;
    QRect siblingRect(x, 0, siblingSize, siblingSize);

    bool isItemSibling = true;

    const QBitArray siblings = siblingsInformation();
    QStyleOption option;
    for (int i = siblings.count() - 1; i >= 0; --i) {
        option.rect = siblingRect;
        option.state = siblings.at(i) ? QStyle::State_Sibling : QStyle::State_None;
        if (isItemSibling) {
            option.state |= QStyle::State_Item;
            if (m_isExpandable) {
                option.state |= QStyle::State_Children;
            }
            if (data().value("isExpanded").toBool()) {
                option.state |= QStyle::State_Open;
            }
            isItemSibling = false;
        }

        style()->drawPrimitive(QStyle::PE_IndicatorBranch, &option, painter);

        siblingRect.translate(layoutDirection() == Qt::LeftToRight ? -siblingRect.width() : siblingRect.width(), 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// dolphin-ng: the cards view. Every item is a card of a fixed size (the view sets it) that holds all the details
// the list row shows. Three looks, chosen with ngCardStyle():
//   Tessera   a thumbnail band on top, then the name, chips and a label/value list
//   Scheda    the icon in a column on the left, the name and two lines of text on the right
//   Dossier   the identity card: a header with the object and the name, then what is inside the file (three facts,
//             or the kinds a folder holds), where it came from, and the dates on one line
// The base class paints the selection and the hover over the sheet drawn by paintNgCardSheet().

namespace
{
// dolphin-ng: the first entries of a folder, for its card: folders first, then by name; hidden ones left out. Read once
// per folder and kept until the folder changes (its modification time). A huge folder is not read to the end.
struct NgFolderPeek {
    QStringList names; // a trailing "/" marks a folder
    QStringList pictures; // the first files that have a picture to show (images, PDFs, videos)
    int more = 0; // entries beyond `names`; -1: many, not counted
    bool cover = false; // `pictures` is the folder's own cover
};
NgFolderPeek ngFolderPeek(const QString &path)
{
    struct Cached {
        qint64 modified;
        NgFolderPeek peek;
    };
    static QHash<QString, Cached> cache;
    const qint64 modified = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    const auto found = cache.constFind(path);
    if (found != cache.constEnd() && found->modified == modified) {
        return found->peek;
    }
    if (cache.size() > 600) {
        cache.clear();
    }
    const int limit = 400;
    QStringList folders, files;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot);
    int seen = 0;
    while (it.hasNext() && seen < limit) {
        it.next();
        ++seen;
        (it.fileInfo().isDir() ? folders : files).append(it.fileName());
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(folders.begin(), folders.end(), collator);
    std::sort(files.begin(), files.end(), collator);
    NgFolderPeek peek;
    // an album or a collection with its own cover (folder.jpg, cover.png, front.jpg...): the cover alone stands for it
    static const QSet<QString> coverNames{QStringLiteral("folder"), QStringLiteral("cover"), QStringLiteral("front"), QStringLiteral("albumart")};
    static const QSet<QString> coverSuffixes{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp")};
    QString cover;
    for (const QString &name : std::as_const(files)) {
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        if (dot > 0 && coverNames.contains(name.left(dot).toLower()) && coverSuffixes.contains(name.mid(dot + 1).toLower())) {
            cover = name;
            break;
        }
    }
    if (!cover.isEmpty()) {
        peek.pictures.append(cover);
        peek.cover = true;
    }
    for (const QString &name : std::as_const(folders)) {
        if (peek.names.count() < 5) {
            peek.names.append(name + QLatin1Char('/'));
        }
    }
    for (const QString &name : std::as_const(files)) {
        if (peek.names.count() < 5) {
            peek.names.append(name);
        }
        static const QSet<QString> moving{QStringLiteral("PDF"), QStringLiteral("MP4"), QStringLiteral("MKV"), QStringLiteral("WEBM"), QStringLiteral("MOV"), QStringLiteral("AVI")};
        const NgKind kind = ngKindFor(name, false);
        if (cover.isEmpty() && peek.pictures.count() < 4 && (kind.category == NgKind::Image || moving.contains(kind.sigla))) {
            peek.pictures.append(name);
        }
    }
    peek.more = it.hasNext() ? -1 : seen - int(peek.names.count());
    cache.insert(path, {modified, peek});
    return peek;
}

// dolphin-ng: the pictures of a folder, for the band of its card: previews of its first few pictures, asked for
// once per folder and kept. Until they arrive the list is empty; `waiting` is repainted when one does.
QList<QPixmap> ngFolderPictures(const QString &folder, const QStringList &names, QGraphicsWidget *waiting)
{
    static QHash<QString, QList<QPixmap>> cache;
    static QSet<QString> asked;
    const QString key = folder + QLatin1Char('|') + names.join(QLatin1Char('|'));
    if (names.isEmpty()) {
        return {};
    }
    if (!asked.contains(key)) {
        if (asked.size() > 800) {
            asked.clear();
            cache.clear();
        }
        asked.insert(key);
        KFileItemList items;
        for (const QString &name : names) {
            items.append(KFileItem(QUrl::fromLocalFile(folder + QLatin1Char('/') + name)));
        }
        auto *job = new KIO::PreviewJob(items, QSize(512, 512));
        job->setIgnoreMaximumSize(false);
        const QPointer<QGraphicsWidget> widget(waiting);
        QObject::connect(job, &KIO::PreviewJob::gotPreview, qApp, [key, widget](const KFileItem &, const QPixmap &pixmap) {
            cache[key].append(pixmap);
            if (widget) {
                widget->update();
            }
        });
    }
    return cache.value(key);
}

// dolphin-ng: the band of a card whose file has no preview shows a small drawing of the kind of content: a page of
// text, a table, lines of code, a waveform... The kind comes from the category of the file and, where the category
// is too wide (audio or video, text or slides), from its extension.
// tagged files inside a folder: sheets of paper in their tags' colours rising out of it from the right of @p top (the
// folder's front edge), within @p room, at most four; drawn before the front, which covers their lower part
void ngPaintInsideSheets(QPainter *painter, qreal right, qreal top, qreal room, qreal rise, const QStringList &inside, QList<QRectF> *placed = nullptr)
{
    // (only in the room right of the folder's tabs, never over their names: fewer sheets where it is short)
    const qreal least = rise * 1.1;
    int count = qMin<int>(4, inside.size());
    while (count > 0 && room < least * (count * 0.6 + 0.4)) {
        --count;
    }
    if (count == 0) {
        return;
    }
    const qreal width = qMin(room / (count * 0.6 + 0.4), rise * 2.6), lift = qMax(4.0, rise * 0.85);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    for (int i = count - 1; i >= 0; --i) {
        // (from the right, each a little lower and to the left of the one behind it)
        const qreal x = right - width - i * width * 0.6;
        const QRectF sheet(x, top - lift + i * 1.5, width, lift + rise * 2);
        painter->setPen(QPen(QColor(0, 0, 0, 50), 1));
        painter->setBrush(QColor(250, 250, 248));
        painter->drawRoundedRect(sheet, 2.5, 2.5);
        painter->setPen(Qt::NoPen);
        painter->setBrush(ngTagColour(inside.at(i)));
        painter->drawRoundedRect(QRectF(sheet.left(), sheet.top(), sheet.width(), qMax(3.0, lift * 0.45)), 2.5, 2.5);
        if (placed) {
            placed->prepend(sheet); // (in the order of the tags: the first is the rightmost)
        }
    }
    painter->restore();
}

// a folder's tags as bookmark ribbons hanging from its top edge on the right, as a book's. At rest a ribbon of each
// tag's colour (as many as fit in @p room, leftwards from @p right); @p open (the pointer on the card) unrolls them into
// flags with their names, one under the other from the edge, down to @p bottom. Drawn over the folder's front.
// unrolled: with the pointer on the card (or, for tests, DOLPHIN_NG_TEST_TABS_OPEN)
bool ngRibbonsOpen(bool hovered)
{
    static const bool testOpen = qEnvironmentVariableIsSet("DOLPHIN_NG_TEST_TABS_OPEN");
    return hovered || testOpen;
}

qreal ngRibbonWidth(qreal depth)
{
    return qBound(6.0, depth * 0.6, 11.0);
}

void ngPaintRibbons(QPainter *painter, qreal right, qreal top, qreal room, qreal bottom, qreal depth, const QStringList &tags, bool open, const QFont &font)
{
    if (tags.isEmpty()) {
        return;
    }
    open = ngRibbonsOpen(open) || tags.size() == 1; // (a single tag always with its name)
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor(0, 0, 0, 70), 1));
    if (!open) {
        const qreal w = ngRibbonWidth(depth), length = qMax(depth * 1.6, 14.0), notch = w * 0.5;
        const int fit = qMax(1, int((room + 3) / (w + 3)));
        for (int i = 0; i < tags.size() && i < fit; ++i) {
            const qreal x = right - (i + 1) * w - i * 3;
            QPainterPath ribbon;
            ribbon.moveTo(x, top - 2);
            ribbon.lineTo(x + w, top - 2);
            ribbon.lineTo(x + w, top + length);
            ribbon.lineTo(x + w / 2, top + length - notch);
            ribbon.lineTo(x, top + length);
            ribbon.closeSubpath();
            painter->setBrush(ngTagColour(tags.at(i)));
            painter->drawPath(ribbon);
        }
    } else {
        // unrolled: a flag each, its point to the left, the name in it
        const QFontMetricsF fm(font);
        const qreal h = fm.height() + 5, point = h * 0.4;
        painter->setFont(font);
        qreal y = top + 1;
        for (const QString &tag : tags) {
            if (y + h > bottom) {
                break;
            }
            const qreal w = qMin(fm.horizontalAdvance(tag) + 22 + point, room);
            const qreal x = right - w;
            QPainterPath flag;
            flag.moveTo(x, y);
            flag.lineTo(right, y);
            flag.lineTo(right, y + h);
            flag.lineTo(x, y + h);
            flag.lineTo(x + point, y + h / 2);
            flag.closeSubpath();
            painter->setPen(QPen(QColor(0, 0, 0, 70), 1));
            painter->setBrush(ngTagColour(tag));
            painter->drawPath(flag);
            painter->setPen(Qt::white);
            painter->drawText(QRectF(x + point + 5, y, w - point - 10, h), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(tag, Qt::ElideRight, w - point - 10));
            y += h + 3;
        }
    }
    painter->restore();
}

QFont ngTagRibbonFont(const QFont *base)
{
    QFont font = base ? *base : QGuiApplication::font();
    font.setPointSizeF(font.pointSizeF() * 0.74);
    font.setWeight(QFont::DemiBold);
    return font;
}

void ngPaintContentSketch(QPainter *painter, const QRectF &band, int category, const QString &sigla, const QColor &tint, bool dark, int count, const QString &sizeText,
                          const QFont *font, int special, bool whole, const QStringList &tags, bool openTabs, const QStringList &inside,
                          QList<QRectF> *insidePlaced)
{
    painter->setRenderHint(QPainter::Antialiasing);
    // CARDS_BADGE=inside: the type and the size are part of the drawing (printed on the box, the type as the page's
    // title, the size as its last line...), in the drawing's own colours
    const bool info = font != nullptr;
    auto write = [&](const QRectF &r, const QString &t, qreal px, QFont::Weight weight, const QColor &colour, Qt::Alignment align) {
        if (!info || t.isEmpty()) {
            return;
        }
        QFont f(*font);
        f.setPixelSize(qMax(7, qRound(px)));
        f.setWeight(weight);
        if (weight >= QFont::Black) {
            f.setLetterSpacing(QFont::PercentageSpacing, 104);
        }
        // a word that does not fit shrinks (down to 70 %) before it is cut: "DOCX" on a narrow page stays whole
        const qreal needed = QFontMetricsF(f).horizontalAdvance(t);
        if (needed > r.width() && needed > 0) {
            f.setPixelSize(qMax(7, int(px * qMax(0.6, 0.95 * r.width() / needed))));
        }
        painter->setFont(f);
        painter->setPen(colour);
        painter->drawText(r, align, QFontMetricsF(f).elidedText(t, Qt::ElideRight, r.width()));
    };
    // the writing takes the colour of the drawing: a shade of the type colour, lighter or darker
    auto shade = [&](qreal lightness, qreal saturation) {
        const qreal hue = tint.hslHueF();
        return hue < 0 ? QColor::fromHslF(0, 0, lightness) : QColor::fromHslF(hue, qBound<qreal>(0, tint.hslSaturationF() * saturation, 1), lightness);
    };
    const QString ext = sigla.toLower();
    static const QSet<QString> audio{QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("wav"), QStringLiteral("opus"), QStringLiteral("m4a"),
                                     QStringLiteral("aac"), QStringLiteral("wma")};
    static const QSet<QString> slides{QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("odp"), QStringLiteral("key")};
    const qreal u = band.height() / 100.0; // the drawing is laid out for a band 100 high
    const QColor paper = dark ? QColor(232, 234, 238) : QColor(255, 255, 255);
    const QColor paperEdge = dark ? QColor(0, 0, 0, 0) : QColor(0, 0, 0, 40);
    const QColor grey(150, 156, 166);
    const QColor faint(198, 202, 209);
    auto bar = [painter](qreal x, qreal y, qreal width, qreal height, const QColor &colour) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(colour);
        painter->drawRoundedRect(QRectF(x, y, width, height), height / 2.0, height / 2.0);
    };
    // a sheet of paper in the middle of the band, running off its bottom edge
    auto sheet = [&](qreal widthFactor) {
        const qreal width = qMin(band.width() * (whole ? qMin(1.0, widthFactor * 1.25) : widthFactor), band.height() * 2.1);
        // whole (the Scheda): the page stands entire in the middle of its column; else it runs off the bottom of the band
        const QRectF page = whole ? QRectF(band.center().x() - width / 2.0, band.top() + 6 * u, width, band.height() - 12 * u)
                                  : QRectF(band.center().x() - width / 2.0, band.top() + 12 * u, width, band.height());
        painter->setPen(paperEdge.alpha() ? QPen(paperEdge, 1) : QPen(Qt::NoPen));
        painter->setBrush(paper);
        painter->drawRoundedRect(page, 5 * u, 5 * u);
        if (whole) {
            // what is drawn on the page stays on it (a table would run past its foot)
            QPainterPath sheetClip;
            sheetClip.addRoundedRect(page, 5 * u, 5 * u);
            painter->setClipPath(sheetClip, Qt::IntersectClip);
        }
        return page;
    };

    if (category == int(NgKind::Folder)) {
        // a flat folder: a darker back whose tab slopes down into its top edge, a lighter front over nearly all of
        // it. The count and the size are written at the bottom left of the front, large; a well-known folder of the
        // home (Downloads, Documents...) carries its own colour (the tint) and a line glyph at the top right
        const qreal width = whole ? qMin(band.width() * 0.92, (band.height() - 10 * u) / 0.76) : qMin(band.width() * 0.68, 128 * u); // about 3 : 2, not wider
        const qreal height = qMin(band.height() - (whole ? 8 : 12) * u, width * 0.76);
        const QRectF body(band.center().x() - width / 2.0, band.center().y() - height / 2.0 + 2 * u, width, height);
        const qreal hue = tint.hslHueF() < 0 ? 0 : tint.hslHueF();
        const qreal sat = tint.hslHueF() < 0 ? 0 : tint.hslSaturationF();
        const QColor back = QColor::fromHslF(hue, sat * 0.62, dark ? 0.40 : 0.56);
        // (light schemes: paler all over, a folder of light on the frost)
        const QColor front = QColor::fromHslF(hue, sat * 0.78, dark ? 0.70 : 0.81);
        const QColor frontTop = QColor::fromHslF(hue, sat * 0.78, dark ? 0.74 : 0.85);
        // what is written and drawn on the folder: the accent colour of the desktop, in a deep tone that reads on the
        // pale front, on either scheme
        const QColor highlight = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
        const qreal accentHue = highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF();
        const qreal accentSat = (highlight.hslSaturationF() < 0.15 ? highlight.hslSaturationF() : qBound(0.45, highlight.hslSaturationF() * 1.2, 0.90));
        const QColor ink = QColor::fromHslF(accentHue, accentSat, dark ? 0.24 : 0.38); // (light schemes: a lighter tone, the accent itself more than a navy)
        const QColor ink2 = QColor::fromHslF(accentHue, accentSat * 0.9, dark ? 0.33 : 0.46);
        const qreal radius = qMin(width, height) * 0.09;
        const qreal tabDepth = height * 0.16;
        QPainterPath backShape;
        backShape.addRoundedRect(QRectF(body.left(), body.top() + tabDepth * 0.6, body.width(), body.height() - tabDepth * 0.6), radius, radius);
        QPainterPath tab;
        tab.addRoundedRect(QRectF(body.left(), body.top(), body.width() * 0.34, tabDepth * 1.8), radius * 0.8, radius * 0.8);
        QPolygonF slope;
        slope << QPointF(body.left() + body.width() * 0.30, body.top()) << QPointF(body.left() + body.width() * 0.34, body.top())
              << QPointF(body.left() + body.width() * 0.43, body.top() + tabDepth * 0.95) << QPointF(body.left() + body.width() * 0.30, body.top() + tabDepth * 0.95);
        // the three parts filled one by one (the same opaque colour, so where they overlap does not show): their union
        // as one path lost the tab on some Qt versions, and the folder showed only the slope, a fin on a bare edge
        painter->setPen(Qt::NoPen);
        painter->setBrush(back);
        painter->drawPath(backShape);
        const qreal tabsRight = body.left() + body.width() * 0.43;
        painter->drawPath(tab);
        painter->drawPolygon(slope);
        painter->setBrush(back);
        const QRectF cover(body.left(), body.top() + tabDepth, body.width(), body.height() - tabDepth);
        // the tags hang from the front's top edge on the right as ribbons (drawn over it, below); tagged files inside
        // are sheets peeking out of the folder, left of the ribbons, behind the front
        const qreal ribbonsRight = cover.right() - cover.width() * 0.08;
        const qreal ribbonsRoom = ribbonsRight - tabsRight - 4;
        const int ribbonCount = qMin<int>(tags.size(), qMax(1, int((ribbonsRoom * 0.6 + 3) / (ngRibbonWidth(tabDepth) + 3))));
        const qreal sheetsRight = tags.isEmpty() ? ribbonsRight : ribbonsRight - ribbonCount * (ngRibbonWidth(tabDepth) + 3) - 2;
        ngPaintInsideSheets(painter, sheetsRight, cover.top(), sheetsRight - tabsRight - 3, tabDepth, inside, insidePlaced);
        painter->setBrush(back);
        QLinearGradient shine(cover.topLeft(), cover.bottomLeft());
        shine.setColorAt(0.0, frontTop);
        shine.setColorAt(1.0, front);
        painter->setBrush(shine);
        painter->drawRoundedRect(cover, radius, radius);
        if (!tags.isEmpty()) {
            ngPaintRibbons(painter, ribbonsRight, cover.top(), (ngRibbonsOpen(openTabs) || tags.size() == 1) ? band.right() - band.left() - 8 : ribbonsRoom * 0.6, cover.bottom() - 4, tabDepth, tags, openTabs,
                           ngTagRibbonFont(font));
        }
        if (special > 0) {
            const qreal side = qMin(cover.height() * 0.36, 30 * u);
            const QRectF box(cover.right() - side - cover.width() * 0.07, cover.top() + cover.height() * 0.11, side, side);
            QColor mark = ink;
            mark.setAlphaF(0.5);
            painter->save();
            painter->translate(box.topLeft());
            painter->scale(side / 24.0, side / 24.0);
            painter->setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(ngSpecialFolderGlyph(special));
            painter->restore();
        }
        if (info && (count >= 0 || !sizeText.isEmpty())) {
            // the count large, its word smaller on the same baseline ("19 items"), the size under them; when the word
            // does not fit, the number alone (never an ellipsis)
            const QRectF text = cover.adjusted(cover.width() * 0.09, cover.height() * 0.10, -cover.width() * 0.07, -cover.height() * 0.12);
            QFont numberFont(*font), wordFont(*font), sizeFont(*font);
            numberFont.setPixelSize(qMax(10, qRound(cover.height() * 0.30)));
            numberFont.setWeight(QFont::Black);
            wordFont.setPixelSize(qMax(8, qRound(cover.height() * 0.15)));
            wordFont.setWeight(QFont::DemiBold);
            sizeFont.setPixelSize(qMax(8, qRound(cover.height() * 0.165)));
            sizeFont.setWeight(QFont::Bold);
            const QFontMetricsF numberFm(numberFont), wordFm(wordFont), sizeFm(sizeFont);
            qreal bottom = text.bottom();
            if (!sizeText.isEmpty()) {
                painter->setFont(sizeFont);
                painter->setPen(ink2);
                painter->drawText(QPointF(text.left(), bottom - sizeFm.descent()), sizeFm.elidedText(sizeText, Qt::ElideRight, text.width()));
                bottom -= sizeFm.height() + cover.height() * 0.02;
            }
            if (count >= 0) {
                const QString number = QString::number(count);
                const qreal baseline = bottom - numberFm.descent();
                painter->setFont(numberFont);
                painter->setPen(ink);
                painter->drawText(QPointF(text.left(), baseline), number);
                const QString word = count == 1 ? ngTr("item", "elemento") : ngTr("items", "elementi");
                const qreal wordLeft = text.left() + numberFm.horizontalAdvance(number) + wordFm.height() * 0.3;
                if (wordLeft + wordFm.horizontalAdvance(word) <= text.right()) {
                    painter->setFont(wordFont);
                    painter->setPen(ink2);
                    painter->drawText(QPointF(wordLeft, baseline), word);
                }
            }
        }
    } else if (category == int(NgKind::Sheet)) {
        const QRectF page = sheet(0.62);
        const qreal left = page.left() + 9 * u, right = page.right() - 9 * u, rowHeight = 15 * u;
        qreal top = page.top() + (info ? 32 * u : 10 * u);
        if (info) {
            // the size beside the title, or under it when the page is too narrow for both (a small drawing)
            QFont titleFont(*font), sizeFont(*font);
            titleFont.setPixelSize(qMax(7, qRound(22 * u)));
            titleFont.setWeight(QFont::Black);
            titleFont.setLetterSpacing(QFont::PercentageSpacing, 104);
            sizeFont.setPixelSize(qMax(7, qRound(12 * u)));
            sizeFont.setWeight(QFont::Bold);
            const bool beside = QFontMetricsF(titleFont).horizontalAdvance(sigla) + QFontMetricsF(sizeFont).horizontalAdvance(sizeText) + 8 * u <= right - left;
            write(QRectF(left, page.top() + 5 * u, right - left, 24 * u), sigla, 22 * u, QFont::Black, shade(0.42, 1.0), Qt::AlignLeft | Qt::AlignVCenter);
            if (beside) {
                write(QRectF(left, page.top() + 5 * u, right - left, 24 * u), sizeText, 12 * u, QFont::Bold, shade(0.4, 0.4), Qt::AlignRight | Qt::AlignVCenter);
            } else {
                write(QRectF(left, page.top() + 27 * u, right - left, 15 * u), sizeText, 12 * u, QFont::Bold, shade(0.4, 0.4), Qt::AlignLeft | Qt::AlignVCenter);
                top += 14 * u;
            }
        }
        QColor head = tint;
        head.setAlphaF(0.55);
        painter->fillRect(QRectF(left, top, right - left, rowHeight), head);
        painter->setPen(QPen(faint, 1));
        for (int row = 0; row <= 6; ++row) {
            painter->drawLine(QPointF(left, top + row * rowHeight), QPointF(right, top + row * rowHeight));
        }
        for (int column = 0; column <= 4; ++column) {
            const qreal x = left + (right - left) * column / 4.0;
            painter->drawLine(QPointF(x, top), QPointF(x, page.bottom()));
        }
        for (int row = 1; row < 6; ++row) {
            for (int column = 0; column < 4; ++column) {
                const qreal x = left + (right - left) * column / 4.0;
                bar(x + 4 * u, top + row * rowHeight + 5.5 * u, (right - left) / 4.0 * (0.35 + 0.12 * ((row * 3 + column) % 4)), 4 * u, column == 0 ? grey : faint);
            }
        }
    } else if (category == int(NgKind::Data)) {
        // lines of code on a dark editor
        const qreal width = qMin(band.width() * 0.7, band.height() * 2.4);
        const QRectF editor(band.center().x() - width / 2.0, band.top() + 12 * u, width, band.height());
        painter->setPen(QPen(QColor(255, 255, 255, 28), 1));
        painter->setBrush(QColor(20, 22, 27));
        painter->drawRoundedRect(editor, 5 * u, 5 * u);
        static const qreal indent[] = {0, 1, 1, 2, 2, 1, 0}, length[] = {0.34, 0.52, 0.30, 0.44, 0.26, 0.38, 0.2};
        QColor bright = tint.lighter(125), soft(120, 128, 140);
        for (int i = 0; i < 7; ++i) {
            const qreal x = editor.left() + 10 * u + indent[i] * 12 * u, y = editor.top() + 11 * u + i * 12 * u;
            if (info && (i == 0 || i == 5)) {
                const QRectF line(editor.left() + 10 * u, y - 4.5 * u, editor.width() - 20 * u, 13 * u);
                if (i == 0) {
                    write(line.adjusted(0, -2 * u, 0, 2 * u), QStringLiteral("#!") + sigla, 14 * u, QFont::Black, bright, Qt::AlignLeft | Qt::AlignVCenter);
                } else {
                    write(line.adjusted(0, -2 * u, 0, 2 * u), QStringLiteral("# ") + sizeText, 12 * u, QFont::Bold, shade(0.62, 0.35), Qt::AlignLeft | Qt::AlignVCenter);
                }
                continue;
            }
            if (info && i == 6) {
                continue;
            }
            bar(x, y, editor.width() * 0.16, 4.5 * u, i % 3 == 0 ? bright : soft);
            bar(x + editor.width() * 0.16 + 5 * u, y, editor.width() * length[i], 4.5 * u, i % 2 ? QColor(170, 178, 190) : soft);
        }
    } else if (category == int(NgKind::Media) && audio.contains(ext)) {
        // a waveform across the band
        const int count = 34;
        const qreal step = band.width() * 0.8 / count, x0 = band.left() + band.width() * 0.1;
        QColor wave = tint.lighter(120);
        const qreal middle = info ? band.top() + band.height() * 0.38 : band.center().y();
        for (int i = 0; i < count; ++i) {
            const qreal amount = 0.18 + 0.82 * qAbs(std::sin(i * 0.55) * std::cos(i * 0.23 + 1.0));
            const qreal height = band.height() * (info ? 0.5 : 0.62) * amount;
            wave.setAlphaF(0.55 + 0.4 * amount);
            bar(x0 + i * step, middle - height / 2.0, qMax(2.0, step * 0.55), height, wave);
        }
        if (info) {
            const qreal baseline = band.bottom() - 26 * u;
            QColor rule = tint;
            rule.setAlphaF(0.5);
            painter->setPen(QPen(rule, 1.5));
            painter->drawLine(QPointF(x0, baseline), QPointF(x0 + count * step, baseline));
            const QRectF text(x0, baseline + 2 * u, count * step, 22 * u);
            write(text, sigla, 15 * u, QFont::Black, shade(0.7, 1.0), Qt::AlignLeft | Qt::AlignVCenter);
            write(text, sizeText, 12.5 * u, QFont::Bold, shade(0.8, 0.6), Qt::AlignRight | Qt::AlignVCenter);
        }
    } else if (category == int(NgKind::Media)) {
        // a frame of film with the play mark
        const qreal width = qMin(band.width() * 0.56, band.height() * 1.5), height = band.height() * 0.72;
        const QRectF frame(band.center().x() - width / 2.0, band.center().y() - height / 2.0, width, height);
        painter->setPen(QPen(QColor(255, 255, 255, 40), 1));
        painter->setBrush(QColor(14, 15, 19));
        painter->drawRoundedRect(frame, 6 * u, 6 * u);
        for (int i = 0; i < 5; ++i) {
            const qreal y = frame.top() + frame.height() * (i + 0.5) / 5.0 - 3 * u;
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 60));
            painter->drawRoundedRect(QRectF(frame.left() + 5 * u, y, 6 * u, 6 * u), 1.5 * u, 1.5 * u);
            painter->drawRoundedRect(QRectF(frame.right() - 11 * u, y, 6 * u, 6 * u), 1.5 * u, 1.5 * u);
        }
        QPainterPath play;
        const qreal side = height * 0.36;
        play.moveTo(frame.center().x() - side * 0.38, frame.center().y() - side / 2.0);
        play.lineTo(frame.center().x() + side * 0.55, frame.center().y());
        play.lineTo(frame.center().x() - side * 0.38, frame.center().y() + side / 2.0);
        play.closeSubpath();
        painter->setPen(QPen(tint.lighter(125), 2 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(tint.lighter(125));
        painter->drawPath(play);
        if (info) {
            const QRectF text = QRectF(frame.left() + 15 * u, frame.bottom() - 21 * u, frame.width() - 30 * u, 18 * u);
            write(text, sigla, 13 * u, QFont::Black, shade(0.72, 1.0), Qt::AlignLeft | Qt::AlignVCenter);
            write(text, sizeText, 11 * u, QFont::Bold, shade(0.82, 0.5), Qt::AlignRight | Qt::AlignVCenter);
        }
    } else if (category == int(NgKind::Archive) || category == int(NgKind::Package)) {
        // a page like the others (the type as its heading, the size under it), with a zip down its right side in the
        // type's colour: an archive among documents, not a coloured box that stood apart from every other drawing.
        // A package has a strip of tape instead of the zip.
        const QRectF page = sheet(0.56);
        const qreal zipWidth = qMax(6.0, 12 * u);
        const QRectF zip(page.right() - zipWidth - 8 * u, page.top() - 1, zipWidth, page.height() + 2);
        QColor zipColour = tint;
        zipColour.setAlphaF(1.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(zipColour);
        painter->drawRect(zip);
        if (category == int(NgKind::Archive)) {
            const qreal tooth = qMax(2.0, 4 * u);
            for (qreal y = zip.top() + 6 * u; y < page.bottom(); y += tooth * 2) {
                const bool left = int((y - zip.top()) / (tooth * 2)) % 2 == 0;
                painter->setBrush(QColor(255, 255, 255, 200));
                painter->drawRect(QRectF(left ? zip.left() + zipWidth * 0.18 : zip.center().x(), y, zipWidth * 0.32, tooth));
            }
            // the pull of the zip
            painter->setBrush(zipColour.darker(130));
            painter->drawRoundedRect(QRectF(zip.left() - 2 * u, page.top() + 20 * u, zipWidth + 4 * u, 14 * u), 3 * u, 3 * u);
        } else {
            painter->setBrush(QColor(255, 255, 255, 70));
            painter->drawRect(QRectF(zip.left(), zip.top(), zipWidth * 0.3, zip.height()));
        }
        const qreal left = page.left() + 11 * u, width = zip.left() - 7 * u - left;
        if (info) {
            write(QRectF(left, page.top() + 4 * u, width, 26 * u), sigla, 23 * u, QFont::Black, shade(0.48, 1.0), Qt::AlignLeft | Qt::AlignVCenter);
            write(QRectF(left, page.top() + 28 * u, width, 16 * u), sizeText, 12.5 * u, QFont::Bold, shade(0.42, 0.4), Qt::AlignLeft | Qt::AlignVCenter);
        } else {
            bar(left, page.top() + 14 * u, width * 0.62, 5 * u, tint);
        }
        static const qreal lines[] = {1.0, 0.8, 1.0, 0.6};
        for (int i = 0; i < 4; ++i) {
            bar(left, page.top() + 50 * u + i * 10 * u, width * lines[i], 4 * u, i < 2 ? grey : faint);
        }
    } else if (category == int(NgKind::Image)) {
        // a picture that could not be previewed: sky, sun and hills
        const QRectF page = sheet(0.6);
        const QRectF view = page.adjusted(7 * u, 7 * u, -7 * u, 0);
        QColor sky = tint;
        sky.setAlphaF(0.30);
        painter->fillRect(view, sky);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(240, 196, 92));
        painter->drawEllipse(QPointF(view.right() - 22 * u, view.top() + 20 * u), 8 * u, 8 * u);
        QPainterPath hills;
        hills.moveTo(view.left(), view.bottom());
        hills.lineTo(view.left(), view.top() + 62 * u);
        hills.lineTo(view.left() + view.width() * 0.3, view.top() + 34 * u);
        hills.lineTo(view.left() + view.width() * 0.52, view.top() + 58 * u);
        hills.lineTo(view.left() + view.width() * 0.7, view.top() + 44 * u);
        hills.lineTo(view.right(), view.top() + 68 * u);
        hills.lineTo(view.right(), view.bottom());
        hills.closeSubpath();
        painter->setClipRect(view, Qt::IntersectClip);
        painter->fillPath(hills, tint.darker(115));
        if (info) {
            const QRectF text(view.left() + 7 * u, view.top() + 64 * u, view.width() - 14 * u, 20 * u);
            write(text, sigla, 13 * u, QFont::Black, shade(0.93, 0.6), Qt::AlignLeft | Qt::AlignVCenter);
            write(text, sizeText, 11 * u, QFont::Bold, shade(0.88, 0.5), Qt::AlignRight | Qt::AlignVCenter);
        }
    } else if (slides.contains(ext)) {
        // a slide: a title and a small chart
        const qreal width = qMin(band.width() * 0.66, band.height() * 2.2);
        const QRectF slide(band.center().x() - width / 2.0, band.top() + 14 * u, width, band.height());
        painter->setPen(paperEdge.alpha() ? QPen(paperEdge, 1) : QPen(Qt::NoPen));
        painter->setBrush(paper);
        painter->drawRoundedRect(slide, 5 * u, 5 * u);
        if (info) {
            write(QRectF(slide.left() + 10 * u, slide.top() + 4 * u, slide.width() * 0.6, 24 * u), sigla, 21 * u, QFont::Black, shade(0.48, 1.0), Qt::AlignLeft | Qt::AlignVCenter);
            write(QRectF(slide.left() + 10 * u, slide.top() + 27 * u, slide.width() * 0.6, 15 * u), sizeText, 12 * u, QFont::Bold, shade(0.42, 0.4), Qt::AlignLeft | Qt::AlignVCenter);
        } else {
            bar(slide.left() + 10 * u, slide.top() + 11 * u, slide.width() * 0.42, 6 * u, tint);
            bar(slide.left() + 10 * u, slide.top() + 24 * u, slide.width() * 0.30, 4 * u, faint);
        }
        static const qreal heights[] = {0.30, 0.52, 0.40, 0.70};
        for (int i = 0; i < 4; ++i) {
            const qreal h = band.height() * heights[i] * 0.7;
            painter->setPen(Qt::NoPen);
            painter->setBrush(i == 3 ? tint : faint);
            painter->drawRoundedRect(QRectF(slide.right() - (4 - i) * 16 * u - 6 * u, slide.bottom() - h, 11 * u, h + 4 * u), 2 * u, 2 * u);
        }
        bar(slide.left() + 10 * u, slide.top() + 40 * u, slide.width() * 0.26, 4 * u, faint);
        bar(slide.left() + 10 * u, slide.top() + 52 * u, slide.width() * 0.20, 4 * u, faint);
    } else {
        // a page of text: a heading in the colour of the type, then lines
        const QRectF page = sheet(0.56);
        const qreal left = page.left() + 11 * u, width = page.width() - 22 * u;
        if (info) {
            write(QRectF(left, page.top() + 4 * u, width, 26 * u), sigla, 23 * u, QFont::Black, shade(0.48, 1.0), Qt::AlignLeft | Qt::AlignVCenter);
        } else {
            bar(left, page.top() + 14 * u, width * 0.62, 5 * u, tint);
        }
        static const qreal lines[] = {1.0, 1.0, 0.82, 0.0, 1.0, 0.94, 0.6};
        for (int i = 0; i < 7; ++i) {
            if (info && i == 4) {
                write(QRectF(left, page.top() + 30 * u + i * 10 * u - 6 * u, width, 16 * u), sizeText, 12.5 * u, QFont::Bold, shade(0.42, 0.4), Qt::AlignLeft | Qt::AlignVCenter);
                break;
            }
            if (lines[i] > 0) {
                bar(left, page.top() + 30 * u + i * 10 * u, width * lines[i], 4 * u, i < 3 ? grey : faint);
            }
        }
    }
}
}

// dolphin-ng: lays out the pill of quick actions (cards and two-line rows). With alignRight the pill ends at
// `right` and `x` is the leftmost place it may start from.
qreal KStandardItemListWidget::ngLayoutActionPill(const QList<NgQuickAction> &quickActions, qreal x, qreal y, qreal right, qreal actionHeight, const QFontMetricsF &chipFm, bool alignRight,
                                                  const QList<int> &origin)
{
    const KItemListStyleOption &option = styleOption();
    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);
    const qreal inner = 3, button = 30, divider = 7; // 22 px application icons with a little air
    struct Item {
        int index;
        qreal width;
        bool divided;
    };
    QList<Item> items;
    auto build = [&](bool labelled, const QList<int> &shown) {
        items.clear();
        qreal total = 2 * inner;
        int previous = -1;
        for (const int i : shown) {
            const NgQuickAction &action = quickActions.at(i);
            // cut, copy and pin: the tools of the file itself, in one group at the end
            const bool own = action.program == QLatin1String("@cut") || action.program == QLatin1String("@copy") || action.program == QLatin1String("@pin");
            const int group = action.main ? 0 : own ? 3 : action.program.startsWith(QLatin1Char('@')) ? 1 : 2;
            const qreal width = (action.main && labelled) ? 8 + 18 + 6 + chipFm.horizontalAdvance(action.label) + 12 : button + 2;
            const bool divided = previous >= 0 && group != previous;
            total += width + (divided ? divider : 0);
            items.append({i, width, divided});
            previous = group;
        }
        return total;
    };
    // what gives way when there is no room, in this order: the extra actions (from the last), then the
    // applications and "open with", then — after the label of the main one — cut, copy and pin; the main action stays
    auto isOwn = [&](int i) {
        const QString &program = quickActions.at(i).program;
        return program == QLatin1String("@cut") || program == QLatin1String("@copy") || program == QLatin1String("@pin");
    };
    QList<int> extras, apps, own;
    for (int i = quickActions.count() - 1; i >= 0; --i) {
        if (quickActions.at(i).main) {
            continue;
        }
        if (isOwn(i)) {
            own.append(i);
        } else if (quickActions.at(i).program.startsWith(QLatin1Char('@'))) {
            apps.append(i);
        } else {
            extras.append(i);
        }
    }
    QList<int> shown;
    for (int i = 0; i < quickActions.count(); ++i) {
        shown.append(i);
    }
    bool labelled = true;
    qreal total = build(true, shown);
    for (const int i : std::as_const(extras)) {
        if (total <= right - x) {
            break;
        }
        shown.removeOne(i);
        total = build(true, shown);
    }
    if (total > right - x) {
        labelled = false;
        total = build(false, shown);
        for (const int i : apps + own) {
            if (total <= right - x) {
                break;
            }
            shown.removeOne(i);
            total = build(false, shown);
        }
    }
    const int count = shown.count();
    if (count == 0 || total > right - x) {
        return y;
    }
    if (alignRight) {
        x = right - total;
    }
    m_ngCardActionsPill = QRectF(x, y, total, actionHeight);
    qreal cx = x + inner;
    for (const Item &item : std::as_const(items)) {
        const NgQuickAction &action = quickActions.at(item.index);
        if (item.divided) {
            m_ngCardActionSeps.append(cx + divider / 2.0);
            cx += divider;
        }
        NgChip chip;
        chip.text = (action.main && labelled) ? action.label : QString();
        chip.rect = QRectF(cx, y + inner, item.width, actionHeight - 2 * inner);
        chip.tinted = action.main || (action.program == QLatin1String("@pin") && data().value("ngPinned").toBool()); // a pinned item: its pin lit
        m_ngCardActions.append(chip);
        m_ngCardActionIndex.append(origin.isEmpty() ? item.index : origin.value(item.index, item.index)); // a part of the list keeps its numbering
        // rendered once per icon and kept: this runs for every card at every layout
        static QHash<QString, QPixmap> iconCache;
        const QColor glyphColor = option.palette.color(QPalette::Text);
        const bool isApp = action.program.startsWith(QLatin1String("@app"));
        const bool isPin = action.program == QLatin1String("@pin");
        const bool pinned = isPin && data().value("ngPinned").toBool();
        const QString cacheKey = action.icon + (pinned ? QStringLiteral("-on") : QString()) + QLatin1Char('|') + QString::number(dpr) + QLatin1Char('|') + (isApp ? QString() : glyphColor.name());
        auto cached = iconCache.constFind(cacheKey);
        if (cached == iconCache.constEnd() && isPin) {
            // drawn here in the NeoLine style: an outline pin, a filled red one when the item is pinned
            QPixmap pixmap(QSize(18, 18) * dpr);
            pixmap.setDevicePixelRatio(dpr);
            pixmap.fill(Qt::transparent);
            QPainter pinPainter(&pixmap);
            ngDrawPin(&pinPainter, QRectF(0, 0, 18, 18), glyphColor, pinned, pinned ? 1.5 : 1.8); // filled when pinned
            pinPainter.end();
            cached = iconCache.insert(cacheKey, pixmap);
        }
        if (cached == iconCache.constEnd()) {
            QPixmap pixmap = QIcon::fromTheme(action.icon, QIcon::fromTheme(QStringLiteral("application-x-executable"))).pixmap(isApp ? QSize(22, 22) : QSize(18, 18), dpr);
            if (!isApp) {
                // an action glyph: in the text colour, whatever the icon theme draws it in (applications keep their colours)
                QPainter tint(&pixmap);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(pixmap.rect(), glyphColor);
            }
            cached = iconCache.insert(cacheKey, pixmap);
        }
        m_ngCardActionIcons.append(*cached);
        cx += item.width;
    }
    return y + actionHeight;
}

void KStandardItemListWidget::paintNgActionPill(QPainter *painter, const QColor &text, bool dark)
{
    if (NgActivity::self()->contains(data().value("url").toUrl())) {
        return; // a file at work (or being made) shows only its progress: no quick actions over it
    }
    if (m_ngCardHoverPill && NgModifiers::selecting()) {
        return; // Ctrl or Shift held: a selection is being made, the actions that come with the pointer stay away
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // ---- the quick actions: icons in one pill, the main one with its label on a tint of the type
    if (!m_ngCardActions.isEmpty()) {
        // the pill of a Tessera comes in and goes out with the hover animation of the card
        const qreal reveal = m_ngCardHoverPill ? (ngDemoHovered() ? 1.0 : qBound(0.0, m_ngReveal, 1.0)) : 1.0;
        const bool pillShown = !m_ngCardActionsPill.isEmpty() && reveal > 0.01;
        painter->setOpacity(reveal);
        const QRectF pill = m_ngCardActionsPill.adjusted(0.5, 0.5, -0.5, -0.5);
        QColor edge = text;
        edge.setAlphaF(m_ngCardHoverPill ? 0.22 : 0.13);
        if (pillShown) {
            painter->setPen(QPen(edge, 1));
            // over a picture the pill needs a darker ground than over the sheet of the card
            // (nearly opaque: over a light picture a see-through black reads as grey)
            painter->setBrush(m_ngCardHoverPill ? (dark ? QColor(10, 11, 13, 238) : QColor(252, 252, 252, 240)) : (dark ? QColor(0, 0, 0, 72) : QColor(255, 255, 255, 130)));
            painter->drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
            QColor hairline = text;
            hairline.setAlphaF(0.18);
            painter->setPen(QPen(hairline, 1));
            for (const qreal x : std::as_const(m_ngCardActionSeps)) {
                painter->drawLine(QPointF(x, pill.top() + 6), QPointF(x, pill.bottom() - 6));
            }
        }
        if (!m_ngCardFab.isEmpty()) {
            // the main action: an opaque pill of its own, half on the picture and half on the sheet
            const QRectF fab = m_ngCardFab.adjusted(0.5, 0.5, -0.5, -0.5);
            QColor rim = text;
            rim.setAlphaF(0.26);
            painter->setPen(QPen(rim, 1));
            painter->setBrush(dark ? QColor(22, 23, 27) : QColor(250, 250, 250));
            painter->drawRoundedRect(fab, fab.height() / 2.0, fab.height() / 2.0);
        }
        QFont chipFont = m_customizedFont;
        chipFont.setPointSizeF(chipFont.pointSizeF() * 0.88);
        painter->setFont(chipFont);
        painter->setOpacity(m_ngCardHoverPill ? reveal : isHovered() ? 1.0 : 0.82);
        for (int i = 0; i < m_ngCardActions.count(); ++i) {
            const NgChip &chip = m_ngCardActions.at(i);
            const QPixmap &icon = m_ngCardActionIcons.at(i);
            if (!pillShown && !m_ngCardFab.contains(chip.rect.center())) {
                continue; // a button of the pill that is hidden now
            }
            if (!chip.text.isEmpty()) {
                QColor tint = m_ngTint;
                tint.setAlpha(m_ngCardHoverPill ? 135 : dark ? 64 : 70); // over a picture the colour has to hold its own
                painter->setPen(Qt::NoPen);
                painter->setBrush(tint);
                painter->drawRoundedRect(chip.rect, chip.rect.height() / 2.0, chip.rect.height() / 2.0);
                painter->drawPixmap(QPointF(chip.rect.left() + 8, chip.rect.center().y() - 9), icon);
                painter->setPen(text);
                painter->drawText(chip.rect.adjusted(8 + 18 + 6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, chip.text);
            } else {
                if (chip.tinted) {
                    // the main action without room for its label: it keeps the colour of the type behind its glyph
                    QColor tint = m_ngTint;
                    tint.setAlpha(m_ngCardHoverPill ? 135 : dark ? 64 : 70);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(tint);
                    painter->drawRoundedRect(chip.rect, chip.rect.height() / 2.0, chip.rect.height() / 2.0);
                }
                const qreal side = icon.width() / icon.devicePixelRatio(); // 22 for applications, 18 for glyphs
                painter->drawPixmap(QPointF(chip.rect.center().x() - side / 2.0, chip.rect.center().y() - side / 2.0), icon);
            }
        }
        painter->setOpacity(1.0);
    }
    painter->restore();
}

// dolphin-ng: a text whole in \a width, its type made smaller down to \a least of its size; cut short only past that
static std::pair<QString, QFont> ngFitText(const QString &text, const QFont &font, qreal width, qreal least)
{
    QFont fitted = font;
    QFontMetricsF fm(fitted);
    for (qreal k = 0.95; fm.horizontalAdvance(text) > width && k >= least - 0.001; k -= 0.05) {
        fitted.setPointSizeF(font.pointSizeF() * k);
        fm = QFontMetricsF(fitted);
    }
    return {fm.horizontalAdvance(text) > width ? fm.elidedText(text, Qt::ElideRight, width) : text, fitted};
}

void KStandardItemListWidget::updateCardsLayoutTextCache()
{
    const KItemListStyleOption &option = styleOption();
    const QHash<QByteArray, QVariant> values = data();
    const NgCardStyle look = ngCardStyle(this);
    const bool hovered = isHovered() || ngDemoHovered();
    const QLocale locale;
    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);

    m_ngChips.clear();
    m_ngTime.clear();
    m_ngCardTexts.clear();
    m_ngBarRect = QRectF();
    m_ngBarFraction = -1;
    m_ngDossierShapes.clear();
    m_ngDossierRules.clear();
    m_ngDossierSignRect = QRectF();
    m_ngDossierSign.clear();
    m_ngCount.clear();
    m_ngAgo.clear();
    m_ngCardEye = QRectF();
    m_ngFoldType.clear();
    m_ngInfoInside = false;
    m_ngCardEyeInPill = false;
    m_ngCardMenu = QRectF();
    m_ngCardActions.clear();
    m_ngCardActionIndex.clear();
    m_ngCardFab = QRectF();
    m_ngCardHoverPill = false;
    m_ngCardActionIcons.clear();
    m_ngCardActionSeps.clear();
    m_ngCardActionsPill = QRectF();
    m_ngCardThumb = QRectF();
    m_ngCardIconSide = 0;
    m_textRect = QRectF();

    // every role but the name is drawn by the card itself
    for (auto it = m_textInfo.begin(); it != m_textInfo.end(); ++it) {
        if (it.key() != "text") {
            it.value()->staticText.setText(QString());
        }
    }
    TextInfo *nameInfo = m_textInfo.value("text");
    if (!nameInfo) {
        return;
    }

    // ---- the data
    const bool isDir = values.value("isDir").toBool();
    const qlonglong bytes = values.value("size").toLongLong();
    const QString name = values.value("text").toString();
    const NgKind kind = ngKindFor(name, isDir);
    m_ngSigla = kind.sigla;
    m_ngCategory = int(kind.category);
    const bool darkScheme = ngIsDark(option.palette);
    m_ngCardPreview = !values.value("iconPixmap").value<QPixmap>().isNull();
    QColor iconColor;
    if (!m_ngCardPreview) {
        // the card takes the main colour of the type icon, as the list rows do
        const QString iconName = values.value("iconName").toString();
        if (!iconName.isEmpty() && (iconName != m_ngIconName || m_ngIcon.isNull())) {
            m_ngIconName = iconName;
            m_ngIcon = QIcon::fromTheme(iconName).pixmap(QSize(32, 32), dpr);
            m_ngIconFaded = false;
        }
        iconColor = ngIconColor(m_ngIconName, m_ngIcon);
    }
    m_ngTint = ngAccent(iconColor.isValid() ? iconColor : kind.tint, darkScheme);
    if (isDir) {
        if (const int special = ngSpecialFolder(values.value("url").toUrl().toLocalFile())) {
            m_ngTint = ngAccent(ngSpecialFolderColour(special), darkScheme); // a well-known folder: its own colour
        } else {
            // any other folder: the accent colour of the desktop (the icon theme's own grey-blue said nothing)
            // (its hue made vivid as Aurora's accent is: a scheme's own highlight is often a dull blue, which on a
            // dark scheme gave grey folders)
            m_ngTint = ngAccent(ngAuroraAccent(QGuiApplication::palette(), 1.0), darkScheme);
        }
    }

    QString sizeText = roleText("size", values);
    if (isDir && bytes <= 0) {
        sizeText.clear(); // the content is not measured: "0 B" says nothing (the item count is a chip of its own)
    }
    QString countText;
    if (isDir && values.contains("count")) {
        countText = i18ncp("@item:intable", "%1 item", "%1 items", values.value("count").toInt());
        if (countText == sizeText) {
            countText.clear();
        }
    }
    QString typeText = roleText("type", values);
    if (typeText.isEmpty()) {
        typeText = isDir ? ngTr("Folder", "Cartella") : name.section(QLatin1Char('.'), -1).toUpper();
    }
    const QString permissions = roleText("permissions", values); // the role holds a list: the string and the bits
    const QString owner = values.value("owner").toString();
    QDateTime modified = ngDateTimeOf(values.value("modificationtime"));
    QDateTime created = ngDateTimeOf(values.value("creationtime"));
    // in Recent Files the first date is when the file was last used (what the view is sorted by)
    QString createdCaption = ngTr("Created", "Creato");
    if (ngSortedByUse()) {
        const QDateTime used = ngDateTimeOf(values.value("accesstime"));
        if (used.isValid()) {
            created = used;
            createdCaption = ngTr("Used", "Usato");
        }
    }
    QList<NgQuickAction> quickActions;
    QString kindWords; // dolphin-ng: "PDF document", "Folder"... (Scheda and Dossier)
    const NgFileFacts *facts = nullptr; // Dossier: what is inside the file, where it came from
    QStringList tags; // dolphin-ng: the file's tags (Scheda and Tessera; the Dossier has them in its facts)
    m_ngCardTags.clear();
    m_ngFileRibbons.clear();
    m_ngCardHeader = QRectF();
    if (auto *fileView = qobject_cast<KFileItemListView *>(parentWidget())) {
        if (auto *fileModel = qobject_cast<KFileItemModel *>(fileView->model())) {
            const KFileItem fileItem = fileModel->fileItem(index());
            if (!fileItem.isNull()) {
                if (!created.isValid()) {
                    created = fileItem.time(KFileItem::CreationTime);
                }
                if (!modified.isValid()) {
                    modified = fileItem.time(KFileItem::ModificationTime);
                }
                if (fileItem.isLocalFile()) {
                    quickActions = ngQuickActionsFor(fileItem.localPath(), fileItem.mimetype());
                }
                kindWords = fileItem.isDir() ? ngTr("Folder", "Cartella") : fileItem.mimeComment();
                m_ngTagsPath = fileItem.isLocalFile() ? fileItem.localPath() : QString();
                if (look != NgCardStyle::Dossier) {
                    // drawn over the picture (see paintNgCardTags), not as a line of their own: they add to what the card
                    // says, they never take the place of a date
                    tags = ngFileTags(m_ngTagsPath);
                    m_ngCardTags = tags;
                }
                if (!m_ngTagsHooked) {
                    // a tag added or taken away (the Information panel): the card is laid out again
                    m_ngTagsHooked = true;
                    connect(NgFacts::instance(), &NgFacts::changed, this, [this](const QString &path) {
                        if (!m_ngTagsPath.isEmpty() && path == m_ngTagsPath) {
                            invalidateCache();
                            update();
                        }
                    });
                }
                if (look == NgCardStyle::Dossier && fileItem.isLocalFile()) {
                    // what the Dossier tells beyond the dates: read in the background, the card is laid out again then
                    const qint64 newest = qMax(fileItem.time(KFileItem::ModificationTime).toSecsSinceEpoch(), values.value("ngNewest").toLongLong());
                    m_ngFactsPath = fileItem.localPath();
                    facts = NgFacts::instance()->facts(m_ngFactsPath, fileItem.mimetype(), fileItem.isDir(), newest, fileItem.isDir() ? 0 : qint64(fileItem.size()));
                    if (!facts && !m_ngFactsHooked) {
                        m_ngFactsHooked = true;
                        connect(NgFacts::instance(), &NgFacts::ready, this, [this](const QString &path) {
                            if (path == m_ngFactsPath) {
                                invalidateCache();
                                update();
                            }
                        });
                    }
                }
                m_ngCardInnerTags.clear();
                m_ngCardInnerLabels.clear();
                m_ngInsideSheets.clear();
                if (fileItem.isDir() && fileItem.isLocalFile()) {
                    // a folder: the tags of what is inside, as sheets peeking out of it (read in the background with
                    // its other facts; the card is drawn again when they come)
                    const qint64 newest = qMax(fileItem.time(KFileItem::ModificationTime).toSecsSinceEpoch(), values.value("ngNewest").toLongLong());
                    m_ngFactsPath = fileItem.localPath();
                    const NgFileFacts *inside = facts ? facts : NgFacts::instance()->facts(m_ngFactsPath, fileItem.mimetype(), true, newest, 0);
                    if (inside) {
                        for (const auto &tag : inside->innerTags) {
                            m_ngCardInnerTags.append(tag.first);
                            m_ngCardInnerLabels.append(QStringLiteral("%1 · %2").arg(tag.first).arg(tag.second));
                        }
                    } else if (!m_ngFactsHooked) {
                        m_ngFactsHooked = true;
                        connect(NgFacts::instance(), &NgFacts::ready, this, [this](const QString &path) {
                            if (path == m_ngFactsPath) {
                                invalidateCache();
                                update();
                            }
                        });
                    }
                }
                m_ngCardPeek.clear();
                m_ngCardPeekMore = 0;
                m_ngCardFolderPath.clear();
                m_ngCardFolderPictures.clear();
                m_ngCardFolderCover = false;
                if (fileItem.isDir() && fileItem.isLocalFile() && ngCardStyle(this) == NgCardStyle::Tessera) {
                    const NgFolderPeek peek = ngFolderPeek(fileItem.localPath());
                    m_ngCardPeek = peek.names;
                    m_ngCardPeekMore = peek.more;
                    m_ngCardFolderPath = fileItem.localPath();
                    m_ngCardFolderPictures = peek.pictures;
                    m_ngCardFolderCover = peek.cover;
                }
            }
        }
    }
    const bool fullInfo = ngSetting("CARDS_INFO").toLower() == QLatin1String("full"); // type and permissions too
    // CARDS_MARK=type: no icon, the type is the background of the card (its colour and a big faint abbreviation);
    // pictures keep their preview in any case, folders do not: a folder's preview is the folder icon (a collage at
    // most) and only some folders get one, so a view mixed folder icons and DIR marks
    const bool markType = ngSetting("CARDS_MARK").toLower() == QLatin1String("type");
    const bool keepsPicture = m_ngCardPreview && !isDir;
    const bool showPicture = !markType || keepsPicture;
    m_ngCardWatermark = markType && !keepsPicture;
    auto dateLine = [&locale](const QDateTime &when) {
        // the "ago" part sits between two markers: putRich() draws it in bold
        return QStringLiteral("%1 · \x01%2\x02").arg(locale.toString(when.date(), QStringLiteral("dd/MM/yy")), ngAgoText(when));
    };

    // ---- fonts and measures
    const QFontMetricsF fm(m_customizedFont);
    const qreal fh = fm.height();
    QFont nameFont = m_customizedFont;
    nameFont.setWeight(QFont::DemiBold);
    const QFontMetricsF nameFm(nameFont);
    QFont small = m_customizedFont;
    small.setPointSizeF(small.pointSizeF() * 0.9);
    const QFontMetricsF smallFm(small);
    QFont chipFont = m_customizedFont;
    chipFont.setPointSizeF(chipFont.pointSizeF() * 0.88);
    const QFontMetricsF chipFm(chipFont);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(m_customizedFont.pointSizeF() * 0.85);
    const QFontMetricsF monoFm(mono);
    const QRectF card = rect();
    const qreal pad = qBound(8.0, card.width() * 0.05, 14.0);
    const qreal actionSide = qRound(fh * 1.25);
    const qreal chipHeight = qRound(chipFm.height() + 4);

    auto putText = [this](const QString &text, const QPointF &baseline, const QFont &font, int tone) {
        if (text.isEmpty()) {
            return;
        }
        NgCardText entry;
        entry.text = text;
        entry.pos = baseline;
        entry.font = font;
        entry.tone = tone;
        m_ngCardTexts.append(entry);
    };
    // a line of text where the parts between \x01 and \x02 are bold and in the text colour; elided at maxWidth
    auto putRich = [&putText](const QString &text, qreal x, qreal baseline, qreal maxWidth, const QFont &font, int tone) {
        QFont bold = font;
        bold.setWeight(QFont::DemiBold);
        const qreal right = x + maxWidth;
        const QStringList parts = text.split(QRegularExpression(QStringLiteral("[\x01\x02]")));
        for (int i = 0; i < parts.count() && x < right; ++i) {
            if (parts.at(i).isEmpty()) {
                continue;
            }
            const bool strong = i % 2 == 1;
            const QFontMetricsF metrics(strong ? bold : font);
            const QString shown = metrics.elidedText(parts.at(i), Qt::ElideRight, right - x);
            putText(shown, QPointF(x, baseline), strong ? bold : font, strong ? 1 : tone);
            if (shown != parts.at(i)) {
                break;
            }
            x += metrics.horizontalAdvance(shown);
        }
    };
    // chips left to right from (x, y); those that do not fit are dropped. Returns the bottom edge.
    auto chipRow = [this, &chipFm, chipHeight](qreal x, qreal y, qreal right, const QStringList &texts, int tintedCount, QList<NgChip> *into = nullptr) {
        int i = 0;
        for (const QString &text : texts) {
            if (text.isEmpty()) {
                ++i;
                continue;
            }
            const qreal width = chipFm.horizontalAdvance(text) + 14;
            if (x + width > right) {
                break;
            }
            NgChip chip;
            chip.text = text;
            chip.rect = QRectF(x, y, width, chipHeight);
            chip.tinted = i < tintedCount;
            (into ? *into : m_ngChips).append(chip);
            x += width + 4;
            ++i;
        }
        return y + chipHeight;
    };
    // the tags as chips in their colours: as many as fit, at most two, then "+N" for the rest
    // (lower and tighter than the type's chips: a tag is a small label beside the words, not a button)
    const qreal tagHeight = qRound(chipFm.height() + 1);
    auto tagRow = [this, &chipFm, tagHeight](qreal x, qreal y, qreal right, const QStringList &tags) {
        for (int i = 0; i < tags.count(); ++i) {
            const int rest = tags.count() - i;
            const QString more = QStringLiteral("+%1").arg(rest);
            const qreal moreWidth = chipFm.horizontalAdvance(more) + 10;
            const qreal width = chipFm.horizontalAdvance(tags.at(i)) + 10;
            const bool last = rest == 1;
            if (i == 2 || x + width + (last ? 0 : moreWidth + 4) > right) {
                if (x + moreWidth <= right) {
                    NgChip chip;
                    chip.text = more;
                    chip.rect = QRectF(x, y, moreWidth, tagHeight);
                    m_ngChips.append(chip);
                }
                break;
            }
            NgChip chip;
            chip.text = tags.at(i);
            chip.rect = QRectF(x, y, width, tagHeight);
            chip.colour = ngTagColour(tags.at(i));
            m_ngChips.append(chip);
            x += width + 4;
        }
        return y + tagHeight;
    };
    // while hovered the card shows the preview eye in its top right corner (the popup opens on a click or, with the
    // hover preview on, when the pointer rests on it); the menu stays on the right click
    auto actionsAt = [](qreal, qreal) {};
    // The quick actions: one pill. First the typical action of the kind, with its label; then the two other
    // applications and "open with"; then the remaining actions of the kind. Groups are divided by a hairline.
    // Too narrow: the label goes first, then the actions at the end.
    const qreal actionHeight = chipHeight + 12; // room for 22 px application icons
    auto actionRow = [&](qreal x, qreal y, qreal right) {
        return ngLayoutActionPill(quickActions, x, y, right, actionHeight, chipFm, false);
    };
    if (hovered) {
        // a pinned card keeps its pin in that corner (paintNgPin): the eye stands to its left
        const qreal pinRoom = data().value("ngPinned").toBool() ? qMax(0.0, card.right() - rect().right() + 25) : 0;
        m_ngCardEye = QRectF(card.right() - 6 - pinRoom - (actionSide + 2), card.top() + 6, actionSide + 2, actionSide + 2);
    }
    auto setName = [this, nameInfo, &nameFm, &name](qreal x, qreal top, qreal width) {
        if (!m_ngCardEye.isEmpty() && top < m_ngCardEye.bottom() && x + width > m_ngCardEye.left() - 6) {
            width = m_ngCardEye.left() - 6 - x; // the name makes room for the eye while the card is hovered
        }
        nameInfo->staticText.setText(nameFm.elidedText(name, Qt::ElideMiddle, qMax(10.0, width)));
        nameInfo->pos = QPointF(x, top);
        m_textRect = QRectF(x, top, qMax(10.0, width), nameFm.height());
    };
    auto joined = [](const QStringList &parts) {
        QStringList kept;
        for (const QString &part : parts) {
            if (!part.isEmpty()) {
                kept << part;
            }
        }
        return kept.join(QStringLiteral(" · "));
    };

    if (look == NgCardStyle::Tessera) {
        // +--------------------+
        // |     thumbnail      |
        // +--------------------+
        // | Name          👁 ⋯ |
        // | [PNG] [2,4 MiB]    |
        // | Creato     12/01/26 · 302 giorni fa
        // | Modificato ...
        // | Tipo       ...
        // | Permessi   -rw-r--r-- · daniele
        const qreal bandHeight = qRound(qMin(card.width() * 0.40, fh * 9.0)); // follows the width; the view sizes the card around it
        m_ngCardThumb = QRectF(card.left(), card.top(), card.width(), bandHeight);
        m_ngCardIconSide = showPicture ? qMin<qreal>(option.iconSize, bandHeight * 0.56) : 0;
        const qreal x0 = card.left() + pad;
        const qreal right = card.right() - pad;
        // The actions: one pill over the top left of the band, shown while the card is hovered. The main action comes
        // first, with its label on the colour of the type; the preview eye is the last button of the same pill.
        if (!quickActions.isEmpty()) {
            const qreal eyeSide = actionHeight - 6;
            ngLayoutActionPill(quickActions, card.left() + 8, card.top() + 8, card.right() - 8 - (eyeSide + 9), actionHeight, chipFm, false);
            m_ngCardHoverPill = true;
            if (!m_ngCardActionsPill.isEmpty()) { // (also while not hovered: the pill keeps its width as it fades out)
                m_ngCardActionSeps.append(m_ngCardActionsPill.right() + 0.5);
                m_ngCardEye = QRectF(m_ngCardActionsPill.right() + 4, m_ngCardActionsPill.top() + 3, eyeSide, eyeSide);
                m_ngCardActionsPill.setRight(m_ngCardEye.right() + 3);
                m_ngCardEyeInPill = true;
            }
        }
        qreal y = m_ngCardThumb.bottom() + (m_ngCardFab.isEmpty() ? pad * 0.8 : qMax(pad * 0.8, actionHeight / 2.0 + 5));
        actionsAt(right, y - 2);
        setName(x0, y, right - x0);
        y += nameFm.height() + 6;
        if (ngSetting("CARDS_BADGE").toLower() == QLatin1String("row")) {
            // the chips in a row of their own under the name
            y = chipRow(x0, y, right, {m_ngSigla, sizeText, countText}, 1) + 8;
        } else if (ngSetting("CARDS_BADGE").toLower() == QLatin1String("inside")) {
            // no badge: the type and the size are written into the drawing of the file (ngPaintContentSketch); a
            // real preview has nothing written on it, the card's first line says them instead
            m_ngInfoInside = true;
            m_ngInfoSize = sizeText;
        } else if (ngSetting("CARDS_BADGE").toLower() == QLatin1String("fold")) {
            // no chips: the corner of the band folds over, with the type and the size on its back (paintNgFold)
            m_ngFoldType = m_ngSigla;
            m_ngFoldSize = !sizeText.isEmpty() ? sizeText : countText;
            const int count = values.value("count").toInt();
            m_ngFoldFill = bytes > 0 ? qBound(0.04, (std::log10(qreal(bytes)) - 2) / 7.0, 1.0)
                                     : (isDir ? qBound(0.04, std::log10(qreal(count) + 1) / 3.0, 1.0) : 0.04);
        } else {
            // the chips (type, size, items) sit on the band, at its bottom right: laid out from the left, then moved
            const int first = m_ngChips.count();
            chipRow(card.left() + 8, m_ngCardThumb.bottom() - chipHeight - 7, card.right() - 8, {m_ngSigla, sizeText, countText}, 1);
            if (m_ngChips.count() > first) {
                // they are the parts of one badge: side by side without a gap, with the padding of the mockup
                // (6 px above and below the text, 10 px at the ends)
                const qreal badgeHeight = chipFm.height() + 11;
                qreal next = m_ngChips.at(first).rect.left();
                for (int i = first; i < m_ngChips.count(); ++i) {
                    NgChip &chip = m_ngChips[i];
                    // (the type is bold; the glass badge has a dot of the type colour before it)
                    const QString badgeStyle = ngSetting("CARDS_BADGE").toLower();
                    const bool glassBadge = badgeStyle == QLatin1String("glass") || badgeStyle == QLatin1String("center");
                    const qreal width = chipFm.horizontalAdvance(chip.text) * (chip.tinted ? 1.12 : 1.0) + 19 + (glassBadge && chip.tinted ? 11 : 0);
                    chip.rect = QRectF(next, m_ngCardThumb.bottom() - 8 - badgeHeight, width, badgeHeight);
                    next = chip.rect.right();
                }
                // at the bottom right; CARDS_BADGE=center: in the middle of the band's foot
                const qreal shift = ngSetting("CARDS_BADGE").toLower() == QLatin1String("center")
                    ? qRound(m_ngCardThumb.center().x() - (m_ngChips.at(first).rect.left() + m_ngChips.constLast().rect.right()) / 2.0)
                    : card.right() - 8 - m_ngChips.constLast().rect.right();
                for (int i = first; i < m_ngChips.count(); ++i) {
                    m_ngChips[i].rect.translate(shift, 0);
                }
            }
        }

        struct Line {
            QString label;
            QString value;
            bool monospace;
        };
        QList<Line> lines;
        if (created.isValid()) {
            lines.append({createdCaption, dateLine(created), false});
        }
        if (modified.isValid()) {
            lines.append({ngTr("Modified", "Modificato"), dateLine(modified), false});
        }
        if (fullInfo) {
            lines.append({ngTr("Type", "Tipo"), typeText, false});
            if (!permissions.isEmpty()) {
                lines.append({ngTr("Permissions", "Permessi"), joined({permissions, owner}), true});
            }
        }
        const qreal lineHeight = smallFm.height() + 2;
        qreal labelWidth = 0;
        for (const Line &line : std::as_const(lines)) {
            labelWidth = qMax(labelWidth, smallFm.horizontalAdvance(line.label));
        }
        labelWidth += 10;
        const qreal actionsHeight = 0; // the actions are on the band now: the lines have the card down to its foot
        const int fit = qBound(0, int((card.bottom() - pad - actionsHeight - y) / lineHeight), int(lines.count()));
        qreal lineY = y; // the lines follow the chips
        for (int i = 0; i < fit; ++i) {
            const Line &line = lines.at(i);
            putText(line.label, QPointF(x0, lineY + smallFm.ascent()), small, 2);
            const QFont &valueFont = line.monospace ? mono : small;
            const QFontMetricsF &valueFm = line.monospace ? monoFm : smallFm;
            Q_UNUSED(valueFm)
            putRich(line.value, x0 + labelWidth, lineY + smallFm.ascent(), right - x0 - labelWidth, valueFont, 0);
            lineY += lineHeight;
        }
    } else if (look == NgCardStyle::Scheda) {
        // +----------+  Name
        // |  object  |  Kind in words
        // |  (whole) |  Created    12/11/26 · today
        // +----------+  Modified   12/11/26 · today          [actions on hover]
        // The object is the drawing of the file (type and size written in it) or its preview as an object, whole
        // and centred in its column; the quick actions come with the pointer, as on the Tessera
        m_ngInfoInside = true;
        m_ngInfoSize = sizeText;
        const qreal column = showPicture ? qRound(qMin(card.height() * 1.3, card.width() * 0.38)) : 0;
        if (showPicture) {
            m_ngCardThumb = QRectF(card.left() + 4, card.top() + 4, column, card.height() - 8);
            m_ngCardIconSide = qMax(16.0, qMin(column, m_ngCardThumb.height()) - 10);
        }
        const qreal x1 = showPicture ? m_ngCardThumb.right() + pad * 0.6 : card.left() + pad;
        const qreal right = card.right() - pad;
        QList<std::pair<QString, QString>> lines;
        if (created.isValid()) {
            lines.append({createdCaption, dateLine(created)});
        }
        if (modified.isValid()) {
            lines.append({ngTr("Modified", "Modificato"), dateLine(modified)});
        }
        const qreal lineHeight = smallFm.height() + 1;
        const qreal blockHeight = nameFm.height() + 2 + smallFm.height() + 5 + lines.count() * lineHeight;
        qreal y = card.top() + qMax(pad * 0.7, (card.height() - blockHeight) / 2.0);
        setName(x1, y, right - x1);
        y += nameFm.height() + 2;
        const QString kind = !kindWords.isEmpty() ? kindWords : typeText;
        if (!tags.isEmpty() && m_ngCategory != int(NgKind::Folder)) {
            // a tagged file: its tags on the second line, in place of its kind in words (the drawing beside already says
            // the type); a folder keeps its line, its tags are its ribbons
            tagRow(x1, y + (smallFm.height() - tagHeight) / 2.0, right, tags);
        } else {
            putText(smallFm.elidedText(kind, Qt::ElideRight, right - x1), QPointF(x1, y + smallFm.ascent()), small, 2);
        }
        y += smallFm.height() + 5;
        qreal labelWidth = 0;
        for (const auto &line : std::as_const(lines)) {
            labelWidth = qMax(labelWidth, smallFm.horizontalAdvance(line.first));
        }
        labelWidth += 9;
        const bool shortDates = right - x1 - labelWidth < smallFm.horizontalAdvance(QStringLiteral("00/00/00 · ") + ngAgoText(QDateTime::currentDateTime().addDays(-12)));
        int lineIndex = 0;
        for (const auto &line : std::as_const(lines)) {
            if (y + smallFm.height() > card.bottom() - 3) {
                break;
            }
            putText(line.first, QPointF(x1, y + smallFm.ascent()), small, 2);
            const QDateTime &when = (lineIndex == 0 && created.isValid()) ? created : modified;
            if (shortDates) {
                // the value in the column of the longest label when it fits there; else right after its own label (a
                // short "Created" left room that the column of "Modified" took, and the date was cut)
                const QString ago = ngAgoText(when);
                const qreal ownLabel = smallFm.horizontalAdvance(line.first) + 7;
                const qreal at = smallFm.horizontalAdvance(ago) <= right - x1 - labelWidth ? labelWidth : qMin(labelWidth, ownLabel);
                putText(smallFm.elidedText(ago, Qt::ElideRight, right - x1 - at), QPointF(x1 + at, y + smallFm.ascent()), small, 0);
            } else {
                putRich(line.second, x1 + labelWidth, y + smallFm.ascent(), right - x1 - labelWidth, small, 0);
            }
            y += lineHeight;
            ++lineIndex;
        }
        if (!quickActions.isEmpty()) {
            ngLayoutActionPill(quickActions, x1, card.bottom() - 8 - actionHeight, card.right() - 8, actionHeight, chipFm, true);
            m_ngCardHoverPill = true;
        }
    } else {
        // +--------------------------------------+
        // | object  Name                         |  (the header, in the colour of the type)
        // |         Kind in words    [actions]   |
        // +--------------------------------------+
        //  24       | 5.312      | 9 min            what is inside: three facts, large, with their word
        //  pages    | words      | to read          (folders: a bar of the kinds they hold and its legend)
        //  ⤓ Downloaded from example.com  ★★★★      where it came from, who made it, tags and rating
        //  ------------------------------------
        //  Modified today 14:02      created 3 Nov
        m_ngInfoInside = true;
        m_ngInfoSize = sizeText;
        const qreal x0 = card.left() + pad;
        const qreal right = card.right() - pad;
        const qreal headerHeight = qRound(card.height() * 0.38);
        m_ngCardHeader = QRectF(card.left(), card.top(), card.width(), headerHeight);
        const qreal column = showPicture ? qRound(qMin(headerHeight * 1.6, card.width() * 0.34)) : 0;
        if (showPicture) {
            m_ngCardThumb = QRectF(card.left() + 6, card.top() + 4, column, headerHeight - 4);
            m_ngCardIconSide = qMax(16.0, qMin(column, m_ngCardThumb.height()) - 8);
        }
        const qreal x1 = showPicture ? m_ngCardThumb.right() + 6 : x0;
        const qreal titleBlock = nameFm.height() + 2 + smallFm.height();
        qreal y = card.top() + (headerHeight - titleBlock) / 2.0;
        setName(x1, y, right - x1);
        y += nameFm.height() + 2;
        const QString kind = !kindWords.isEmpty() ? kindWords : typeText;
        putText(smallFm.elidedText(kind, Qt::ElideRight, right - x1), QPointF(x1, y + smallFm.ascent()), small, 2);
        if (!quickActions.isEmpty()) {
            // with the pointer, on the lower edge of the header (never over the facts)
            ngLayoutActionPill(quickActions, x1, m_ngCardHeader.bottom() - actionHeight / 2.0, card.right() - 8, actionHeight, chipFm, true);
            m_ngCardHoverPill = true;
        }

        // ---- the dates, one small line at the foot under a hairline
        const qreal bottom = card.bottom() - pad * 0.75;
        const qreal datesTop = bottom - smallFm.height();
        if (modified.isValid() || created.isValid()) {
            m_ngDossierRules.append(QLineF(x0, datesTop - 5.5, right, datesTop - 5.5));
            const QDate today = QDate::currentDate();
            auto when = [&locale, &today](const QDateTime &time) {
                QString text = ngAgoText(time);
                if (time.date().daysTo(today) <= 1) {
                    text += QLatin1Char(' ') + locale.toString(time.time(), QStringLiteral("HH:mm"));
                }
                return text;
            };
            QString createdText = created.isValid() ? ngTr("created %1", "creato %1").arg(ngAgoText(created)) : QString();
            const QString modifiedText = modified.isValid() ? ngTr("Modified", "Modificato") + QLatin1Char(' ') + when(modified) : QString();
            // both whole: in a smaller type when the card is narrow, and without the creation when even that is not enough
            QFont datesFont = small;
            auto needed = [&](const QFont &font) {
                const QFontMetricsF fm(font);
                return fm.horizontalAdvance(modifiedText) + (createdText.isEmpty() ? 0 : 12 + fm.horizontalAdvance(createdText));
            };
            for (qreal k = 0.95; needed(datesFont) > right - x0 && k >= 0.8; k -= 0.05) {
                datesFont.setPointSizeF(small.pointSizeF() * k);
            }
            if (needed(datesFont) > right - x0 && !modifiedText.isEmpty()) {
                createdText.clear();
            }
            const qreal createdWidth = QFontMetricsF(datesFont).horizontalAdvance(createdText);
            if (!createdText.isEmpty()) {
                putText(createdText, QPointF(right - createdWidth, datesTop + smallFm.ascent()), datesFont, 2);
            }
            if (modified.isValid()) {
                putRich(ngTr("Modified", "Modificato") + QStringLiteral(" \x01") + when(modified) + QStringLiteral("\x02"), x0, datesTop + smallFm.ascent(),
                        right - x0 - createdWidth - (createdText.isEmpty() ? 0 : 12), datesFont, 2);
            }
        }

        // ---- what is inside
        y = m_ngCardHeader.bottom() + pad * 0.75;
        QFont valueFont = m_customizedFont;
        valueFont.setPointSizeF(m_customizedFont.pointSizeF() * 1.3);
        valueFont.setWeight(QFont::Bold);
        const QFontMetricsF valueFm(valueFont);
        QFont wordFont = m_customizedFont;
        wordFont.setPointSizeF(m_customizedFont.pointSizeF() * 0.82);
        const QFontMetricsF wordFm(wordFont);
        const qreal originHeight = qMax(smallFm.height(), chipHeight);
        // the facts and the origin line sit in the middle of the room between the header and the dates
        const bool hasOrigin = facts
            && ((!facts->lastName.isEmpty() && facts->lastWhen.isValid()) || !facts->origin.isEmpty() || !facts->who.isEmpty() || facts->rating > 0
                || !facts->tags.isEmpty());
        {
            const qreal factsHeight = !facts ? 0 : !facts->mix.isEmpty() ? 2 + 7 + 6 + smallFm.height() : valueFm.height() + wordFm.height();
            const qreal content = factsHeight + (hasOrigin ? 6 + originHeight : 0);
            const qreal room = (datesTop - 6) - m_ngCardHeader.bottom();
            y = m_ngCardHeader.bottom() + qMax(pad * 0.5, (room - content) / 2.0);
        }
        qreal factsBottom = y;
        if (facts && !facts->mix.isEmpty()) {
            // a folder: a bar of the kinds it holds, the legend below
            int total = 0;
            for (const NgMixPart &part : facts->mix) {
                total += part.count;
            }
            const qreal barHeight = 7;
            const qreal gap = 2;
            const qreal usable = right - x0 - gap * (facts->mix.count() - 1);
            qreal bx = x0;
            for (const NgMixPart &part : facts->mix) {
                const qreal width = qMax(3.0, usable * part.count / total);
                m_ngDossierShapes.append({QRectF(bx, y + 2, qMin(width, right - bx), barHeight), part.colour});
                bx += width + gap;
                if (bx >= right) {
                    break;
                }
            }
            qreal lx = x0;
            const qreal ly = y + 2 + barHeight + 6;
            const qreal dot = qRound(wordFm.height() * 0.42);
            for (const NgMixPart &part : facts->mix) {
                const qreal width = dot + 5 + smallFm.horizontalAdvance(part.label);
                if (lx + width > right) {
                    break;
                }
                m_ngDossierShapes.append({QRectF(lx, ly + (smallFm.height() - dot) / 2.0, dot, dot), part.colour});
                putText(part.label, QPointF(lx + dot + 5, ly + smallFm.ascent()), small, 0);
                lx += width + 12;
            }
            factsBottom = ly + smallFm.height();
        } else if (facts && !facts->facts.isEmpty()) {
            // three columns whatever the count, so that the facts line up from card to card
            const qreal columnWidth = (right - x0) / 3.0;
            for (int i = 0; i < facts->facts.count() && i < 3; ++i) {
                const qreal cx = x0 + i * columnWidth + (i > 0 ? 10 : 0);
                const qreal width = columnWidth - (i > 0 ? 14 : 4);
                if (i > 0) {
                    m_ngDossierRules.append(QLineF(x0 + i * columnWidth, y + 3, x0 + i * columnWidth, y + valueFm.height() + wordFm.height() - 2));
                }
                const NgFact &fact = facts->facts.at(i);
                // whole, in a smaller type when it is long (a resolution, a size): cut short, a fact says nothing
                const auto [value, valueFit] = ngFitText(fact.value, valueFont, width, 0.6);
                const auto [word, wordFit] = ngFitText(fact.word, wordFont, width, 0.7);
                putText(value, QPointF(cx, y + valueFm.ascent()), valueFit, 1);
                putText(word, QPointF(cx, y + valueFm.height() + wordFm.ascent()), wordFit, 2);
            }
            factsBottom = y + valueFm.height() + wordFm.height();
        }

        // ---- where it came from: the site, or who made it; then the rating and the tags while they fit
        const qreal originTop = factsBottom + 6;
        if (hasOrigin && originTop + originHeight <= datesTop - 4) {
            const qreal baseline = originTop + (originHeight - smallFm.height()) / 2.0 + smallFm.ascent();
            QString line;
            QString sign;
            if (!facts->lastName.isEmpty() && facts->lastWhen.isValid()) {
                line = ngTr("Last changed: ", "Ultimo toccato: ") + QStringLiteral("\x01") + facts->lastName + QStringLiteral("\x02 · ") + ngAgoText(facts->lastWhen);
                sign = QStringLiteral("chronometer");
            } else if (!facts->origin.isEmpty()) {
                line = ngTr("Downloaded from ", "Scaricato da ") + QStringLiteral("\x01") + facts->origin + QStringLiteral("\x02");
                sign = QStringLiteral("download");
            } else if (!facts->who.isEmpty()) {
                const QString lead = facts->whoKind == NgFileFacts::Author ? ngTr("By ", "Di ") : facts->whoKind == NgFileFacts::Program ? ngTr("Made with ", "Creato con ") : QString();
                line = lead + QStringLiteral("\x01") + facts->who + QStringLiteral("\x02");
                sign = facts->whoKind == NgFileFacts::Camera ? QStringLiteral("camera-photo") : facts->whoKind == NgFileFacts::Artist ? QStringLiteral("view-media-artist") : QStringLiteral("user");
            }
            qreal lineRight = right;
            // the rating: five stars on the right
            if (facts->rating > 0) {
                QString stars;
                for (int i = 0; i < 5; ++i) {
                    stars += facts->rating >= (i + 1) * 2 ? QChar(0x2605) : QChar(0x2606);
                }
                const qreal width = smallFm.horizontalAdvance(stars);
                putText(stars, QPointF(lineRight - width, baseline), small, 3);
                lineRight -= width + 8;
            }
            qreal lx = x0;
            if (!line.isEmpty()) {
                const qreal side = qRound(smallFm.height() * 0.85);
                m_ngDossierSign = sign;
                m_ngDossierSignRect = QRectF(lx, originTop + (originHeight - side) / 2.0, side, side);
                lx += side + 6;
                const qreal room = facts->tags.isEmpty() ? lineRight - lx : qMin(lineRight - lx, smallFm.horizontalAdvance(line) + 30);
                putRich(line, lx, baseline, room, small, 2);
                QString plain = line;
                plain.remove(QLatin1Char('\x01')).remove(QLatin1Char('\x02'));
                lx += qMin(room, smallFm.horizontalAdvance(plain)) + 10;
            }
            if (!facts->tags.isEmpty() && lx < lineRight - 30) {
                if (line.isEmpty()) {
                    const qreal side = qRound(smallFm.height() * 0.85);
                    m_ngDossierSign = QStringLiteral("tag");
                    m_ngDossierSignRect = QRectF(lx, originTop + (originHeight - side) / 2.0, side, side);
                    lx += side + 6;
                }
                tagRow(lx, originTop + (originHeight - chipHeight) / 2.0, lineRight, facts->tags);
            }
        }
}
}

void KStandardItemListWidget::ngPlaceCardPixmap()
{
    // Precondition: updateCardsLayoutTextCache() decided m_ngCardThumb and m_ngCardIconSide
    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);
    const QRectF area = m_ngCardThumb;
    if (m_pixmap.isNull() || area.isEmpty()) {
        m_scaledPixmapSize = QSize();
        m_iconRect = area;
        return;
    }
    QSize pixelSize = m_pixmap.size();
    if (ngCardStyle(this) == NgCardStyle::Tessera && m_ngCardPreview) {
        // the picture covers the band; paintNgCard() clips it
        pixelSize.scale((area.size() * dpr).toSize(), Qt::KeepAspectRatioByExpanding);
    } else {
        const int side = qRound(m_ngCardIconSide * dpr);
        pixelSize.scale(QSize(side, side), Qt::KeepAspectRatio);
    }
    m_scaledPixmapSize = pixelSize / dpr;
    m_pixmapPos = QPointF(area.center().x() - m_scaledPixmapSize.width() / 2.0, area.center().y() - m_scaledPixmapSize.height() / 2.0);
    if (ngCardStyle(this) == NgCardStyle::Tessera && m_ngCardPreview && m_scaledPixmapSize.height() > area.height() * 1.6) {
        m_pixmapPos.setY(area.top()); // a page: the band shows its top, where the heading is, not its middle
    }
    m_iconRect = QRectF(m_pixmapPos, QSizeF(m_scaledPixmapSize)).intersected(area);
}

bool KStandardItemListWidget::ngRingSelection() const
{
    return m_layout == CardsLayout || (m_layout == DetailsLayout && m_ngObjectRow);
}

qreal KStandardItemListWidget::ngBlockInset() const
{
    // drawn rows fill the width of the view and the first one touches its top: the ring is drawn on the edge of
    // the row's card (3 px inside the item), all of it inside the item, or the view would cut it off
    return m_layout == DetailsLayout && m_ngObjectRow ? 7 : 0;
}

bool KStandardItemListWidget::ngNoHover() const
{
    const QUrl url = data().value("url").toUrl();
    return NgActivity::self()->contains(url) || (NgActivity::self()->hasLinks() && !NgActivity::self()->familyOf(url).isEmpty());
}

bool KStandardItemListWidget::ngOwnSelection() const
{
    return m_layout == CardsLayout || KItemListWidget::ngOwnSelection();
}

qreal KStandardItemListWidget::ngSelectionRadius() const
{
    return m_layout == CardsLayout ? 12 : 9;
}

QRectF KStandardItemListWidget::boundingRect() const
{
    const QRectF r = KItemListWidget::boundingRect();
    return m_layout == CardsLayout || m_ngObjectRow ? r.adjusted(-6, -6, 6, 6) : r;
}

void KStandardItemListWidget::moveEvent(QGraphicsSceneMoveEvent *event)
{
    KItemListWidget::moveEvent(event);
    // the bubble and the outline of a block of selected cards depend on where the neighbours are: when the
    // cards reflow (the window is resized, files come and go) they are drawn again where they land
    if (m_ngShowedFamily || isSelected()) {
        update();
    }
}

void KStandardItemListWidget::paintNgPreviewObject(QPainter *painter, const QRectF &band, const QPixmap &pixmap, bool whole)
{
    // CARDS_BADGE=inside, a card with a real preview: the preview becomes the thing itself and the type and the
    // size go on its border. A picture is a print with its white margin (slightly askew, each its own way), a
    // video a frame on a strip of film, a page (PDF, documents) a sheet with a footer.
    const QFontMetricsF base(m_customizedFont);
    const qreal hue = m_ngTint.hslHueF() < 0 ? 0 : m_ngTint.hslHueF();
    const qreal sat = m_ngTint.hslHueF() < 0 ? 0 : m_ngTint.hslSaturationF();
    auto shade = [&](qreal lightness, qreal saturation) {
        return QColor::fromHslF(hue, qBound<qreal>(0, sat * saturation, 1), lightness);
    };
    // the preview, cropped to fill a rectangle
    auto picture = [&](const QRectF &target) {
        const QSizeF size = QSizeF(pixmap.size()) / pixmap.devicePixelRatio();
        if (size.isEmpty()) {
            return;
        }
        const qreal scale = qMax(target.width() / size.width(), target.height() / size.height());
        const QSizeF shown = target.size() / scale;
        const QRectF source(QPointF((size.width() - shown.width()) / 2.0, m_ngCategory == int(NgKind::Image) ? (size.height() - shown.height()) / 2.0 : 0) * pixmap.devicePixelRatio(),
                            shown * pixmap.devicePixelRatio());
        painter->drawPixmap(target, pixmap, source);
    };
    auto writeLine = [&](const QRectF &r, qreal height, const QColor &typeColour, const QColor &sizeColour) {
        QFont typeFont(m_customizedFont);
        typeFont.setPixelSize(qMax(10, qRound(height * 0.58)));
        typeFont.setWeight(QFont::Black);
        typeFont.setLetterSpacing(QFont::PercentageSpacing, 104);
        QFont sizeFont(m_customizedFont);
        sizeFont.setPixelSize(qMax(9, qRound(height * 0.45)));
        sizeFont.setWeight(QFont::Bold);
        // too wide for the border: both shrink together (down to 65 %) before the size is cut
        const qreal total = QFontMetricsF(typeFont).horizontalAdvance(m_ngSigla) + 8 + QFontMetricsF(sizeFont).horizontalAdvance(m_ngInfoSize);
        if (total > r.width() && total > 0) {
            const qreal k = qMax(0.65, 0.97 * r.width() / total);
            typeFont.setPixelSize(qMax(8, int(typeFont.pixelSize() * k)));
            sizeFont.setPixelSize(qMax(7, int(sizeFont.pixelSize() * k)));
        }
        painter->setFont(typeFont);
        painter->setPen(typeColour);
        painter->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, m_ngSigla);
        const qreal typeWidth = QFontMetricsF(typeFont).horizontalAdvance(m_ngSigla) + 8;
        painter->setFont(sizeFont);
        painter->setPen(sizeColour);
        painter->drawText(r.adjusted(typeWidth, 0, 0, 0), Qt::AlignRight | Qt::AlignVCenter,
                          QFontMetricsF(sizeFont).elidedText(m_ngInfoSize, Qt::ElideLeft, r.width() - typeWidth));
    };
    // how wide the line of type and size is, at a given line height: the object is made at least that wide
    auto lineWidth = [&](qreal height) {
        QFont typeFont(m_customizedFont);
        typeFont.setPixelSize(qMax(10, qRound(height * 0.58)));
        typeFont.setWeight(QFont::Black);
        typeFont.setLetterSpacing(QFont::PercentageSpacing, 104);
        QFont sizeFont(m_customizedFont);
        sizeFont.setPixelSize(qMax(9, qRound(height * 0.45)));
        sizeFont.setWeight(QFont::Bold);
        return QFontMetricsF(typeFont).horizontalAdvance(m_ngSigla) + 10 + QFontMetricsF(sizeFont).horizontalAdvance(m_ngInfoSize) + 14;
    };
    const QString name = data().value("text").toString();
    const QString mime = data().value("mimetype").toString();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_ngCategory == int(NgKind::Image)) {
        // thin margins and a low foot: the picture takes most of the print
        const qreal foot = qRound(qMax(17.0, base.height() * 1.3));
        const qreal margin = qMax(3.0, band.width() * 0.014);
        const qreal printHeight = band.height() - 8;
        qreal width = qMin(band.width() * 0.88, (printHeight - foot - margin) * 1.7 + 2 * margin);
        width = qMin(band.width() * 0.94, qMax(width, lineWidth(foot) + 2 * margin + 4));
        const QRectF print(-width / 2.0, -printHeight / 2.0, width, printHeight);
        painter->save();
        painter->translate(band.center().x(), band.center().y() + 1);
        painter->rotate(((int(qHash(name) % 7)) - 3) * 0.5);
        painter->setPen(Qt::NoPen);
        for (int i = 4; i >= 1; --i) { // its shadow on the band
            painter->setBrush(QColor(0, 0, 0, 22));
            painter->drawRoundedRect(print.translated(0, 2).adjusted(-i, -i + 1, i, i), 3 + i, 3 + i);
        }
        painter->setBrush(QColor(244, 244, 241));
        painter->drawRoundedRect(print, 3, 3);
        picture(print.adjusted(margin, margin, -margin, -foot));
        writeLine(QRectF(print.left() + margin + 2, print.bottom() - foot, print.width() - 2 * margin - 4, foot), foot * 1.08, shade(0.32, 0.9), shade(0.45, 0.35));
        painter->restore();
    } else if (m_ngCategory == int(NgKind::Media) || mime.startsWith(QLatin1String("video/"))) {
        // a frame on a strip of film; type and size printed along its edge. The strip takes almost the whole band
        // and its holes and edge stay thin, so the picture keeps as much height as it can
        const qreal edge = qRound(qMax(15.0, base.height() * 1.1));
        const QRectF strip(band.left() + band.width() * 0.06, band.top() + 4, band.width() * 0.88, band.height() - 6);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(17, 18, 21));
        painter->drawRoundedRect(strip, 4, 4);
        const qreal hole = qMax(4.0, strip.height() * 0.045);
        for (qreal x = strip.left() + 7; x + hole * 1.4 < strip.right() - 4; x += hole * 2.6) {
            painter->setBrush(QColor(255, 255, 255, 50));
            painter->drawRoundedRect(QRectF(x, strip.top() + hole * 0.5, hole * 1.4, hole), 1.5, 1.5);
        }
        const QRectF frame = strip.adjusted(6, hole * 2.0, -6, -edge);
        picture(frame);
        writeLine(QRectF(frame.left() + 2, frame.bottom(), frame.width() - 4, edge), edge, shade(0.72, 1.0), QColor(205, 209, 216));
    } else {
        // a sheet: the page itself, with a footer like a printed page
        const qreal foot = qRound(qMax(20.0, base.height() * 1.5));
        const qreal width = qMin(band.width() * 0.94, qMax(qMin(band.width() * 0.56, band.height() * 1.05), lineWidth(foot) + 16));
        const QRectF page = whole ? QRectF(band.center().x() - width / 2.0, band.top() + 4, width, band.height() - 8)
                                  : QRectF(band.center().x() - width / 2.0, band.top() + 10, width, band.height());
        painter->setPen(Qt::NoPen);
        for (int i = 3; i >= 1; --i) {
            painter->setBrush(QColor(0, 0, 0, 22));
            painter->drawRoundedRect(page.adjusted(-i, -i + 1, i, i), 3 + i, 3 + i);
        }
        painter->setBrush(QColor(255, 255, 255));
        painter->drawRoundedRect(page, 3, 3);
        const QRectF footer(page.left(), (whole ? page.bottom() : band.bottom()) - foot - 3, page.width(), foot + 3);
        painter->save();
        painter->setClipRect(QRectF(page.left(), page.top(), page.width(), footer.top() - page.top()), Qt::IntersectClip);
        picture(QRectF(page.left(), page.top(), page.width(), page.width() * 1.414));
        painter->restore();
        painter->fillRect(footer, QColor(255, 255, 255));
        painter->setPen(QPen(QColor(226, 228, 233), 1));
        painter->drawLine(QPointF(footer.left() + 6, footer.top() + 0.5), QPointF(footer.right() - 6, footer.top() + 0.5));
        writeLine(QRectF(footer.left() + 8, footer.top(), footer.width() - 16, foot), foot, shade(0.45, 1.0), QColor(107, 114, 128));
    }
}

// for screenshots: DOLPHIN_NG_DEMO_HOVER names the item drawn as if the pointer were on it (no input is sent)
// dolphin-ng: Recent Files is sorted by when each file was last used; then that is the first date shown, not the creation
bool KStandardItemListWidget::ngSortedByUse() const
{
    if (auto *fileView = qobject_cast<KFileItemListView *>(parentWidget())) {
        if (auto *fileModel = qobject_cast<KFileItemModel *>(fileView->model())) {
            return fileModel->sortRole() == "accesstime";
        }
    }
    return false;
}

bool KStandardItemListWidget::ngDemoHovered() const
{
    static const QString name = qEnvironmentVariable("DOLPHIN_NG_DEMO_HOVER");
    return !name.isEmpty() && data().value("text").toString() == name;
}

void KStandardItemListWidget::paintNgPin(QPainter *painter)
{
    if (!data().value("ngPinned").toBool()) {
        return;
    }
    // discreet: a small see-through disc with a hairline, the NeoLine pushpin filled in a light shade of the
    // type's colour. At the top right corner of a card, on the corner of the preview of a two-line row, on
    // the corner of the icon elsewhere.
    const bool dark = ngIsDark(styleOption().palette);
    qreal radius;
    QPointF centre;
    if (m_layout == CardsLayout) {
        radius = 11;
        centre = QPointF(rect().right() - 16, rect().top() + 16);
    } else if (ngTwoLineList(this)) {
        radius = 9.5;
        centre = !m_ngRowObject.isEmpty() ? QPointF(m_ngRowObject.left() + 9, m_ngRowObject.top() + 11)
               : m_ngRowThumb.isEmpty() ? QPointF(rect().left() + 13, rect().top() + 13) : QPointF(m_ngRowThumb.left() + 3, m_ngRowThumb.top() + 3);
    } else {
        const QRectF icon = m_iconRect;
        radius = qBound(7.0, icon.height() * 0.22, 10.0);
        centre = QPointF(icon.right() - radius * 0.35, icon.top() + radius * 0.35);
    }
    QColor tint = m_ngTint.isValid() ? m_ngTint : styleOption().palette.color(QPalette::Highlight);
    tint = dark ? QColor::fromHslF(tint.hslHueF() < 0 ? 0 : tint.hslHueF(), qMin(tint.hslSaturationF(), 0.55), 0.78)
                : QColor::fromHslF(tint.hslHueF() < 0 ? 0 : tint.hslHueF(), qMin(tint.hslSaturationF(), 0.65), 0.38);
    QColor hairline = styleOption().palette.color(QPalette::Text);
    hairline.setAlphaF(dark ? 0.16 : 0.14);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(hairline, 1));
    painter->setBrush(dark ? QColor(14, 15, 18, 150) : QColor(255, 255, 255, 190));
    painter->drawEllipse(centre, radius, radius);
    const qreal glyph = radius * 1.35;
    ngDrawPin(painter, QRectF(centre.x() - glyph / 2, centre.y() - glyph / 2, glyph, glyph), tint, true, 1.6);
    painter->restore();
}

void KStandardItemListWidget::paintNgFold(QPainter *painter)
{
    if (m_ngFoldType.isEmpty() || m_layout != CardsLayout || m_ngCardThumb.height() < 40) {
        return;
    }
    const QRectF band = m_ngCardThumb;
    // the type and the size first: the fold is made large enough to hold them
    QFont typeFont(m_customizedFont);
    typeFont.setPixelSize(qMax(11, qRound(m_customizedFontMetrics.height() * 0.78)));
    typeFont.setWeight(QFont::Black);
    QFont sizeFont(m_customizedFont);
    sizeFont.setPixelSize(qMax(9, qRound(m_customizedFontMetrics.height() * 0.6)));
    sizeFont.setWeight(QFont::DemiBold);
    const QFontMetricsF typeFm(typeFont), sizeFm(sizeFont);
    const qreal textWidth = qMax(typeFm.horizontalAdvance(m_ngFoldType), m_ngFoldSize.isEmpty() ? 0.0 : sizeFm.horizontalAdvance(m_ngFoldSize));
    const qreal textHeight = typeFm.height() + (m_ngFoldSize.isEmpty() ? 0 : sizeFm.height()) + 4;
    // the fold grows with the weight of the file
    const qreal k = 0.8 + 0.5 * m_ngFoldFill;
    qreal h = qMax(textHeight * 2.0, band.height() * 0.46) * k;
    h = qMin(h, band.height() * 0.8);
    // at the height of the text the fold must still be wide enough for it
    qreal w = qMax(h * 1.3, (textWidth + 10) / qMax(0.2, 1 - textHeight / h));
    w = qMin(w, band.width() * 0.62);
    const QPointF corner = band.bottomRight();
    const QPointF a(corner.x() - w, corner.y()), b(corner.x(), corner.y() - h);
    QPainterPath fold;
    fold.moveTo(corner);
    fold.lineTo(a);
    fold.lineTo(b);
    fold.closeSubpath();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(band, Qt::IntersectClip);
    // the shadow it casts on the page, along the crease
    const QLineF crease(a, b);
    const QPointF away = QPointF(-crease.dy(), crease.dx()) / crease.length(); // perpendicular, towards the page
    for (int i = 1; i <= 6; ++i) {
        QPainterPath shade;
        const QPointF d = away * (i * 1.4);
        shade.moveTo(corner);
        shade.lineTo(a + d * (a.y() == corner.y() ? 0 : 1) + QPointF(-i * 1.4, 0));
        shade.lineTo(b + QPointF(0, -i * 1.4));
        shade.closeSubpath();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 16));
        painter->drawPath(shade);
    }
    // frosted glass: the piece of the picture under the fold, blurred, tinted with the type and lit
    painter->setClipPath(fold, Qt::IntersectClip);
    const QRectF box = fold.boundingRect();
    if (m_ngCardPreview && !m_pixmap.isNull()) {
        const qreal dpr = m_pixmap.devicePixelRatio();
        const QRect source = QRectF((box.topLeft() - m_pixmapPos) * dpr, box.size() * dpr).toAlignedRect().intersected(m_pixmap.rect());
        if (m_ngFoldGlassKey != m_pixmap.cacheKey() || m_ngFoldGlassSource != source) {
            const QPixmap part = m_pixmap.copy(source);
            m_ngFoldGlass = part.isNull() ? QPixmap()
                                          : part.scaled(qMax(1, part.width() / 9), qMax(1, part.height() / 9), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                                .scaled(part.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_ngFoldGlassKey = m_pixmap.cacheKey();
            m_ngFoldGlassSource = source;
        }
        if (!m_ngFoldGlass.isNull()) {
            painter->drawPixmap(box, m_ngFoldGlass, QRectF(m_ngFoldGlass.rect()));
        }
        painter->fillRect(box, QColor(0, 0, 0, 40));
    } else {
        painter->fillRect(box, QColor(22, 24, 28, 170));
    }
    QColor tint = m_ngTint;
    tint.setAlpha(70);
    painter->fillRect(box, tint);
    QLinearGradient light(b, corner);
    light.setColorAt(0, QColor(255, 255, 255, 80));
    light.setColorAt(0.6, QColor(255, 255, 255, 18));
    light.setColorAt(1, QColor(255, 255, 255, 6));
    painter->fillRect(box, light);
    painter->setClipping(false);
    painter->setClipRect(band, Qt::IntersectClip);
    // the crease, a line of light
    painter->setPen(QPen(QColor(255, 255, 255, 190), 1.5, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(a, b);
    // the type and the size, on the back of the fold
    const qreal right = corner.x() - 7;
    const qreal sizeBase = corner.y() - 6 - sizeFm.descent();
    const qreal typeBase = sizeBase - sizeFm.ascent() - 1 - typeFm.descent();
    auto shadowed = [&](const QFont &font, const QFontMetricsF &fm, qreal baseline, const QString &text, const QColor &colour) {
        painter->setFont(font);
        const QPointF at(right - fm.horizontalAdvance(text), baseline);
        painter->setPen(QColor(0, 0, 0, 140));
        painter->drawText(at + QPointF(0, 1), text);
        painter->setPen(colour);
        painter->drawText(at, text);
    };
    QColor typeColour = QColor::fromHslF(m_ngTint.hslHueF() < 0 ? 0 : m_ngTint.hslHueF(), qMin<qreal>(1, m_ngTint.hslSaturationF()), 0.86);
    if (m_ngTint.hslHueF() < 0) {
        typeColour = QColor(240, 242, 245);
    }
    shadowed(typeFont, typeFm, typeBase, m_ngFoldType, typeColour);
    if (!m_ngFoldSize.isEmpty()) {
        shadowed(sizeFont, sizeFm, sizeBase, m_ngFoldSize, QColor(246, 247, 249));
    }
    painter->restore();
}

void KStandardItemListWidget::paintNgFamily(QPainter *painter)
{
    NgActivity *activity = NgActivity::self();
    const QString family = activity->hasLinks() ? activity->familyOf(data().value("url").toUrl()) : QString();
    m_ngShowedFamily = !family.isEmpty();
    if (!m_ngShowedFamily) {
        return;
    }
    // a file and the ones made from it stand side by side (the model sorts them so): one tinted ground with a
    // thin border around all of them, like a selection of several files. Every card paints its own piece: up
    // to half of the gap towards a member of the family next to it, else its own rounded end.
    const bool cards = m_layout == CardsLayout;
    const qreal pad = cards ? 4 : -2; // cards: around the card, into the gap; rows: inside the row
    const QRectF own = rect();
    QRectF bubble = own;
    qreal clipLeft = own.left() - pad - 2, clipRight = own.right() + pad + 2, clipTop = own.top() - pad - 2, clipBottom = own.bottom() + pad + 2;
    if (QGraphicsItem *container = parentItem()) {
        const auto siblings = container->childItems();
        for (QGraphicsItem *item : siblings) {
            auto *other = dynamic_cast<KItemListWidget *>(item);
            if (!other || other == this || !other->isVisible() || activity->familyOf(other->data().value("url").toUrl()) != family) {
                continue;
            }
            const QRectF r = other->geometry().translated(-pos());
            const bool sameRow = qAbs(r.top() - own.top()) < 1 && qAbs(r.height() - own.height()) < 1;
            const bool sameColumn = qAbs(r.left() - own.left()) < 1 && qAbs(r.width() - own.width()) < 1;
            if (sameRow && r.left() >= own.right() - 1 && r.left() - own.right() < 40) {
                bubble = bubble.united(r);
                clipRight = (own.right() + r.left()) / 2;
            } else if (sameRow && r.right() <= own.left() + 1 && own.left() - r.right() < 40) {
                bubble = bubble.united(r);
                clipLeft = (r.right() + own.left()) / 2;
            } else if (sameColumn && r.top() >= own.bottom() - 1 && r.top() - own.bottom() < 40) {
                bubble = bubble.united(r);
                clipBottom = (own.bottom() + r.top()) / 2;
            } else if (sameColumn && r.bottom() <= own.top() + 1 && own.top() - r.bottom() < 40) {
                bubble = bubble.united(r);
                clipTop = (r.bottom() + own.top()) / 2;
            } else if (cards && r.top() >= own.bottom() - 1 && r.left() < own.left()) {
                // the family goes on at the start of the next row: this piece stays open towards the right edge
                bubble = bubble.adjusted(0, 0, 80, 0);
                clipRight = own.right() + pad + 1;
            } else if (cards && r.bottom() <= own.top() + 1 && r.right() > own.right()) {
                // it began at the end of the row above: open towards the left edge
                bubble = bubble.adjusted(-80, 0, 0, 0);
                clipLeft = own.left() - pad - 1;
            }
        }
    }
    // the colour of the file at work (the parent of the family), the same on every piece of the bubble: the parent
    // tells the colour of its own card, the others take it (until it has painted, the colour of its kind)
    const QUrl parentUrl(family);
    const QUrl ownUrl = data().value("url").toUrl();
    if (ownUrl.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments).toString() == family && m_ngTint.isValid()) {
        activity->setFamilyColour(family, m_ngTint);
    }
    QColor kindColour = activity->familyColour(family);
    if (!kindColour.isValid()) {
        const bool parentDir = parentUrl.isLocalFile() && QFileInfo(parentUrl.toLocalFile()).isDir();
        kindColour = ngKindFor(parentUrl.adjusted(QUrl::StripTrailingSlash).fileName(), parentDir).tint;
    }
    const qreal hue = kindColour.hslHueF() < 0 ? 0.55 : kindColour.hslHueF();
    const qreal saturation = (kindColour.hslHueF() < 0 || kindColour.hslSaturationF() < 0.15) ? kindColour.hslSaturationF() : qMax<qreal>(kindColour.hslSaturationF(), 0.55);
    QColor accent = QColor::fromHslF(hue, saturation, ngIsDark(styleOption().palette) ? 0.64 : 0.5);
    QColor fill = accent, border = accent;
    const qreal in = (1 - std::pow(1 - activity->growth(data().value("url").toUrl()), 3)) * activity->fading(data().value("url").toUrl());
    fill.setAlphaF((cards ? 0.18 : 0.13) * in);
    border.setAlphaF(0.60 * in);
    const qreal radius = cards ? 16 : 7;
    QPainterPath path;
    path.addRoundedRect(bubble.adjusted(-pad, -pad, pad, pad).adjusted(0.75, 0.75, -0.75, -0.75), radius, radius);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(QRectF(QPointF(clipLeft, clipTop), QPointF(clipRight, clipBottom)), Qt::IntersectClip);
    painter->setPen(QPen(border, 1.5));
    painter->setBrush(fill);
    painter->drawPath(path);
    painter->restore();
}

void KStandardItemListWidget::paintNgActivity(QPainter *painter)
{
    const QUrl url = data().value("url").toUrl();
    NgActivity *activity = NgActivity::self();
    m_ngShowedActivity = activity->contains(url);
    m_ngCancelRect = QRectF();
    if (!m_ngShowedActivity) {
        return;
    }
    const NgActivity::Entry e = activity->entry(url);
    // the accent of the desktop, lifted: drawn over pictures and dark previews it has to stand out
    QColor accent = styleOption().palette.color(QPalette::Highlight);
    accent = QColor::fromHslF(accent.hslHueF() < 0 ? 0.55 : accent.hslHueF(), accent.hslSaturationF() < 0.15 ? accent.hslSaturationF() : qMax<qreal>(accent.hslSaturationF(), 0.65), 0.62);
    const bool busy = e.percent < 0;
    const bool waiting = busy && activity->parentOf(url).isValid(); // a file being made from another one: it waits, quietly
    const bool stoppable = activity->canCancel(url);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // the ring: a track, the part done (or a turning arc), the figure in the middle
    auto ring = [&](const QPointF &centre, qreal radius, qreal width) {
        const QRectF box(centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius);
        painter->setBrush(QColor(18, 20, 24, 235));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(box.adjusted(-width, -width, width, width));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255, 40), width, Qt::SolidLine, Qt::RoundCap));
        painter->drawEllipse(box);
        painter->setPen(QPen(accent, width, Qt::SolidLine, Qt::RoundCap));
        if (busy) {
            painter->drawArc(box, qRound(90 * 16 - activity->phase() * 360 * 16), -100 * 16);
        } else {
            painter->drawArc(box, 90 * 16, -qRound(e.percent * 3.6 * 16));
            QFont f(m_customizedFont);
            f.setPixelSize(qMax(10, qRound(radius * 0.62)));
            f.setWeight(QFont::Bold);
            painter->setFont(f);
            painter->setPen(QColor(240, 242, 245));
            painter->drawText(box, Qt::AlignCenter, QString::number(e.percent) + QLatin1Char('%'));
        }
    };

    if (m_layout == CardsLayout) {
        // over the preview: darkened, the ring in the middle, what is going on underneath
        const QRectF card = rect();
        // the preview band of the card (the Tessera computes it), else the icon area, else the upper half
        QRectF area = m_ngCardThumb.isValid() && m_ngCardThumb.height() > 24 ? m_ngCardThumb
                    : (m_iconRect.isValid() && m_iconRect.height() > 40 ? m_iconRect : QRectF(card.left(), card.top(), card.width(), card.height() * 0.55));
        area = area.intersected(card);
        QPainterPath clip;
        clip.addRoundedRect(card, 12, 12);
        painter->setClipPath(clip, Qt::IntersectClip);
        if (waiting) {
            // only a veil, what is coming, and a short light running along the foot of the preview
            painter->fillRect(area, QColor(10, 11, 14, 70));
            QFont f(m_customizedFont);
            f.setPixelSize(qMax(9, qRound(m_customizedFontMetrics.height() * 0.62)));
            f.setWeight(QFont::DemiBold);
            const QFontMetricsF fm(f);
            const QString shown = fm.elidedText(e.text, Qt::ElideRight, area.width() - 28);
            const qreal w = fm.horizontalAdvance(shown) + 18;
            const QRectF label(area.center().x() - w / 2, area.top() + (area.height() - 34 - fm.height() - 6) / 2, w, fm.height() + 6);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(18, 20, 24, 215));
            painter->drawRoundedRect(label, label.height() / 2, label.height() / 2);
            painter->setFont(f);
            painter->setPen(QColor(235, 238, 242));
            painter->drawText(label, Qt::AlignCenter, shown);
            const qreal runner = area.width() * 0.3;
            const qreal x = area.left() - runner + (area.width() + runner) * activity->phase();
            QLinearGradient light(x, 0, x + runner, 0);
            light.setColorAt(0, Qt::transparent);
            light.setColorAt(0.5, accent);
            light.setColorAt(1, Qt::transparent);
            painter->fillRect(QRectF(x, area.bottom() - 3, runner, 3), light);
            painter->restore();
            return;
        }
        painter->fillRect(area, QColor(10, 11, 14, 120));
        // one capsule in the middle of the preview: a small ring with the figure, what is going on and the time
        // left, the stop button at its end (red under the pointer). Its size: ACTIVITY_SIZE small, medium, large.
        const QString sizeChoice = ngSetting("ACTIVITY_SIZE").toLower();
        const qreal k = sizeChoice == QLatin1String("small") ? 0.85 : sizeChoice == QLatin1String("large") ? 1.25 : 1.0;
        // responsive: as tall as the card allows (about a seventh of its width, a third of the preview), never
        // smaller than a line of text needs; the setting scales it
        const qreal fromCard = qMin(area.width() * 0.15, area.height() * 0.36);
        const qreal h = qRound(qBound<qreal>(m_customizedFontMetrics.height() * 1.7, fromCard, 64) * k);
        const qreal inset = qRound(h * 0.13);
        const qreal d = h - 2 * inset; // the ring and the stop button
        const qreal gap = qRound(h * 0.26);
        QFont labelFont(m_customizedFont);
        labelFont.setPixelSize(qMax(9, qRound(h * 0.4)));
        labelFont.setWeight(QFont::DemiBold);
        QFont leftFont(labelFont);
        leftFont.setWeight(QFont::Normal);
        const QFontMetricsF fm(labelFont), fmLeft(leftFont);
        QString timeLeft;
        if (!busy && e.percent >= 3 && e.percent < 100 && e.startedAt) {
            const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - e.startedAt) / 1000 * (100 - e.percent) / e.percent;
            timeLeft = secs >= 3600 ? QStringLiteral("%1 h").arg(secs / 3600) : QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0'));
        }
        const qreal leftWidth = timeLeft.isEmpty() ? 0 : fmLeft.horizontalAdvance(timeLeft) + gap * 0.6;
        const qreal fixed = inset + d + gap + leftWidth + (stoppable ? gap * 0.7 + d : inset * 2.5);
        const QString shown = fm.elidedText(e.text, Qt::ElideRight, qMax<qreal>(20, area.width() - 16 - fixed));
        const qreal w = fixed + fm.horizontalAdvance(shown);
        const qreal centreY = qBound(area.top() + h / 2 + 4, area.top() + area.height() * 0.42, area.bottom() - 34 - h / 2);
        const QRectF capsule(qRound(area.center().x() - w / 2), qRound(centreY - h / 2), qRound(w), h);
        painter->setPen(Qt::NoPen);
        for (int i = 3; i >= 1; --i) { // a soft shadow under it
            painter->setBrush(QColor(0, 0, 0, 26));
            painter->drawRoundedRect(capsule.adjusted(-i, -i + 2, i, i + 2), h / 2 + i, h / 2 + i);
        }
        painter->setPen(QPen(QColor(255, 255, 255, 22), 1));
        painter->setBrush(QColor(18, 20, 24, 238));
        painter->drawRoundedRect(capsule.adjusted(0.5, 0.5, -0.5, -0.5), h / 2, h / 2);
        // the ring
        const qreal stroke = qMax<qreal>(2.2, d * 0.11);
        const QRectF ringBox(capsule.left() + inset + stroke / 2, capsule.top() + inset + stroke / 2, d - stroke, d - stroke);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255, 38), stroke));
        painter->drawEllipse(ringBox);
        painter->setPen(QPen(accent, stroke, Qt::SolidLine, Qt::RoundCap));
        if (busy) {
            painter->drawArc(ringBox, qRound(90 * 16 - activity->phase() * 360 * 16), -100 * 16);
        } else {
            painter->drawArc(ringBox, 90 * 16, -qRound(e.percent * 3.6 * 16));
            QFont figure(m_customizedFont);
            figure.setPixelSize(qMax(7, qRound(d * (e.percent >= 100 ? 0.27 : 0.33))));
            figure.setWeight(QFont::Bold);
            painter->setFont(figure);
            painter->setPen(QColor(240, 242, 245));
            painter->drawText(ringBox.adjusted(-2, 0, 2, 0), Qt::AlignCenter, QString::number(e.percent) + QLatin1Char('%'));
        }
        // what is going on, and the time left
        qreal x = capsule.left() + inset + d + gap;
        painter->setFont(labelFont);
        painter->setPen(QColor(236, 239, 243));
        painter->drawText(QRectF(x, capsule.top(), fm.horizontalAdvance(shown) + 1, h), Qt::AlignLeft | Qt::AlignVCenter, shown);
        x += fm.horizontalAdvance(shown);
        if (!timeLeft.isEmpty()) {
            painter->setFont(leftFont);
            painter->setPen(QColor(168, 174, 184));
            painter->drawText(QRectF(x + gap * 0.6, capsule.top(), leftWidth, h), Qt::AlignLeft | Qt::AlignVCenter, timeLeft);
        }
        // the stop button
        if (stoppable) {
            const QRectF stop(capsule.right() - inset - d, capsule.top() + inset, d, d);
            const bool hot = activity->isStopHovered(url);
            painter->setPen(Qt::NoPen);
            painter->setBrush(hot ? QColor(214, 72, 66) : QColor(255, 255, 255, 18));
            painter->drawEllipse(stop);
            const qreal c = d * 0.17;
            painter->setPen(QPen(hot ? QColor(255, 255, 255) : QColor(222, 226, 232), qMax<qreal>(1.5, d * 0.08), Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(stop.center() + QPointF(-c, -c), stop.center() + QPointF(c, c));
            painter->drawLine(stop.center() + QPointF(c, -c), stop.center() + QPointF(-c, c));
            m_ngCancelRect = stop;
        }
        // and a line of the same colour along the bottom edge of the preview
        if (!busy) {
            painter->fillRect(QRectF(area.left(), area.bottom() - 3, area.width() * e.percent / 100.0, 3), accent);
        }
    } else {
        // rows and icons: the part done fills the row from the left, a small ring and the figure at the end
        const QRectF row = rect().adjusted(3, 3, -3, -3);
        QPainterPath clip;
        clip.addRoundedRect(row, 6, 6);
        painter->setClipPath(clip, Qt::IntersectClip);
        QColor fill = accent;
        fill.setAlphaF(0.20);
        if (busy && waiting) {
            // a file being made: no sweep, the caption says it
        } else if (busy) {
            // a soft band sweeping across
            const qreal w = row.width() * 0.25;
            const qreal x = row.left() - w + (row.width() + w) * activity->phase();
            QLinearGradient sweep(x, 0, x + w, 0);
            sweep.setColorAt(0, Qt::transparent);
            sweep.setColorAt(0.5, fill);
            sweep.setColorAt(1, Qt::transparent);
            painter->fillRect(QRectF(x, row.top(), w, row.height()), sweep);
        } else {
            const qreal done = row.width() * e.percent / 100.0;
            painter->fillRect(QRectF(row.left(), row.top(), done, row.height()), fill);
            painter->fillRect(QRectF(row.left() + done - 2, row.top(), 2, row.height()), accent);
        }
        if (m_layout == DetailsLayout) {
            QFont f(m_customizedFont);
            f.setPixelSize(qMax(9, qRound(m_customizedFontMetrics.height() * 0.72)));
            f.setWeight(QFont::DemiBold);
            const QString figure = QString::number(e.percent) + QLatin1Char('%');
            const QString caption = busy ? e.text : (e.text.isEmpty() ? figure : QString(e.text + QStringLiteral(" · ") + figure));
            const QFontMetricsF fm(f);
            const qreal pillHeight = fm.height() * 1.7;
            const qreal stopRoom = stoppable ? pillHeight - 4 + 4 : 0;
            const qreal w = qMin(fm.horizontalAdvance(caption) + 22 + stopRoom, row.width() * 0.45);
            // solid, over the end of the row: it covers the date while the work runs
            // (drawn rows: before the dates, where the quick actions come)
            const qreal pillRight = m_ngObjectRow && !m_ngTime.isEmpty() ? m_ngTime.first().letterPos.x() - 16 : row.right() - 8;
            const QRectF pill(pillRight - w, row.center().y() - pillHeight / 2, w, pillHeight);
            painter->setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 110), 1));
            painter->setBrush(QColor(16, 17, 21));
            painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            painter->setFont(f);
            painter->setPen(accent.lighter(135));
            const QRectF textBox = pill.adjusted(0, 0, -stopRoom, 0);
            painter->drawText(textBox, Qt::AlignCenter, fm.elidedText(caption, Qt::ElideRight, textBox.width() - 14));
            if (stoppable) {
                const QRectF stop(pill.right() - pill.height() + 2, pill.top() + 2, pill.height() - 4, pill.height() - 4);
                const bool hot = activity->isStopHovered(url);
                painter->setPen(Qt::NoPen);
                painter->setBrush(hot ? QColor(214, 72, 66) : QColor(255, 255, 255, 18));
                painter->drawEllipse(stop);
                const qreal c = stop.width() * 0.18;
                painter->setPen(QPen(hot ? QColor(255, 255, 255) : QColor(222, 226, 232), 1.6, Qt::SolidLine, Qt::RoundCap));
                painter->drawLine(stop.center() + QPointF(-c, -c), stop.center() + QPointF(c, c));
                painter->drawLine(stop.center() + QPointF(c, -c), stop.center() + QPointF(-c, c));
                m_ngCancelRect = stop;
            }
        }
    }
    painter->restore();
}

void KStandardItemListWidget::paintNgCardSheet(QPainter *painter)
{
    // a dark rounded sheet with a hairline of light along its top edge, like the tiles of the icons view
    const QRectF card = rect();
    const bool dark = ngIsDark(styleOption().palette);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    if (ngAurora()) {
        // Breeze NG Aurora: a light veil on the window's glass, no lit edge
        painter->setBrush(ngAuroraVeil(dark));
        painter->drawRoundedRect(card, 12, 12);
    } else {
        QColor sheet = dark ? QColor(0, 0, 0, 62) : QColor(0, 0, 0, 14);
        if (isSelected()) {
            // a selected card is tinted with the accent (lifted on a dark scheme, as the ring around it), not grey
            QColor accent = styleOption().palette.color(QPalette::Highlight);
            if (dark && accent.lightness() < 150) {
                accent = QColor::fromHsl(accent.hslHue() < 0 ? 210 : accent.hslHue(), qMax(accent.hslSaturation(), 150), 150);
            }
            accent.setAlphaF(dark ? 0.26 : 0.20);
            sheet = accent;
        }
        painter->setBrush(sheet);
        painter->drawRoundedRect(card, 12, 12);
        painter->setPen(QPen(dark ? QColor(255, 255, 255, 11) : QColor(255, 255, 255, 170), 1));
        painter->drawLine(QPointF(card.left() + 12, card.top() + 0.5), QPointF(card.right() - 12, card.top() + 0.5));
    }
    painter->restore();
}

void KStandardItemListWidget::paintNgCard(QPainter *painter, QWidget *widget)
{
    const KItemListStyleOption &option = styleOption();
    const bool dark = ngIsDark(option.palette);
    const QColor text = widget ? textColor(*widget) : option.palette.color(QPalette::Text);
    QColor dim = m_additionalInfoTextColor;
    dim.setAlpha(qRound(dim.alpha() * 0.62));
    const NgCardStyle look = ngCardStyle(this);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // ---- a folder's card wears the accent (its own colour for a well-known folder): told from a file at a glance
    if (m_ngCategory == int(NgKind::Folder) && !m_ngCardWatermark && ngSetting("FOLDER_TINT").toLower() != QLatin1String("off")) {
        QPainterPath sheet;
        sheet.addRoundedRect(rect(), 12, 12);
        QColor tint = m_ngTint;
        tint.setAlphaF(dark ? 0.17 : 0.15);
        painter->fillPath(sheet, tint);
    }

    // ---- the type as the background: its colour washes into the card, its abbreviation stays in watermark
    if (m_ngCardWatermark && !m_ngSigla.isEmpty()) {
        const QRectF card = rect();
        QPainterPath clip;
        clip.addRoundedRect(card, 12, 12);
        painter->save();
        painter->setClipPath(clip, Qt::IntersectClip);
        QColor washStart = m_ngTint, washMid = m_ngTint, washEnd = m_ngTint;
        washStart.setAlphaF(dark ? 0.24 : 0.20);
        washMid.setAlphaF(0.07);
        washEnd.setAlphaF(0.0);
        QLinearGradient wash;
        if (look == NgCardStyle::Tessera) {
            wash = QLinearGradient(card.topLeft(), QPointF(card.left(), card.top() + card.height() * 0.7));
        } else if (look == NgCardStyle::Scheda) {
            wash = QLinearGradient(card.topLeft(), QPointF(card.left() + card.width() * 0.75, card.top()));
        } else {
            wash = QLinearGradient(card.topLeft(), QPointF(card.left() + card.width() * 0.6, card.top() + card.height() * 0.6));
        }
        wash.setColorAt(0.0, washStart);
        wash.setColorAt(0.45, washMid);
        wash.setColorAt(1.0, washEnd);
        painter->fillRect(card, wash);
        QFont watermarkFont(m_customizedFont);
        watermarkFont.setWeight(QFont::Black);
        watermarkFont.setLetterSpacing(QFont::PercentageSpacing, 96);
        QColor ink = m_ngTint;
        ink.setAlphaF(dark ? 0.15 : 0.13);
        painter->setPen(ink);
        if (look == NgCardStyle::Tessera) {
            watermarkFont.setPixelSize(qMax(14, qRound(m_ngCardThumb.height() * 0.66)));
            painter->setFont(watermarkFont);
            painter->drawText(m_ngCardThumb.adjusted(10, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter, m_ngSigla);
        } else if (look == NgCardStyle::Scheda) {
            watermarkFont.setPixelSize(qMax(14, qRound(card.height() * 0.40)));
            painter->setFont(watermarkFont);
            painter->drawText(card.adjusted(8, 0, 0, 6), Qt::AlignLeft | Qt::AlignBottom, m_ngSigla);
        } else {
            watermarkFont.setPixelSize(qMax(14, qRound(card.height() * 0.36)));
            painter->setFont(watermarkFont);
            painter->drawText(card.adjusted(6, card.height() * 0.22, 0, 0), Qt::AlignLeft | Qt::AlignTop, m_ngSigla);
        }
        painter->restore();
    }

    // ---- the picture: a band across the top (Tessera) or the icon in its area
    if (m_ngCardWatermark) {
        // no icon: the watermark says the type
    } else if (look == NgCardStyle::Tessera && !m_ngCardThumb.isEmpty()) {
        QPainterPath rounded;
        rounded.addRoundedRect(m_ngCardThumb.adjusted(0, 0, 0, 14), 12, 12);
        QPainterPath box;
        box.addRect(m_ngCardThumb);
        const QPainterPath band = rounded.intersected(box);
        {
            // the whole card takes the colour of the type, faintly; the band (below) a little more
            QPainterPath sheet;
            sheet.addRoundedRect(rect(), 12, 12);
            QColor body = m_ngTint;
            body.setAlphaF(dark ? 0.12 : 0.10);
            painter->fillPath(sheet, body);
        }
        if (!m_ngCardPreview) {
            QColor wash = m_ngTint;
            wash.setAlphaF(dark ? 0.16 : 0.14);
            painter->fillPath(band, wash);
        }
        // a folder: its own pictures side by side when it has some (the folder preview of the system is a small
        // collage inside a folder shape, of which the band would show a slice), else the folder and its first entries
        const QList<QPixmap> folderPictures =
            m_ngCategory == int(NgKind::Folder) && !ngSpecialFolder(data().value("url").toUrl().toLocalFile())
            ? ngFolderPictures(m_ngCardFolderPath, m_ngCardFolderPictures, this)
            : QList<QPixmap>();
        if (!folderPictures.isEmpty()) {
            painter->save();
            painter->setClipPath(band, Qt::IntersectClip);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->setRenderHint(QPainter::Antialiasing);
            // It has to read as a folder, not as a picture: the pictures stand inside a folder drawn across the band,
            // its back with the tab behind them and its front flap over their lower part.
            const QRectF &b = m_ngCardThumb;
            const QColor back = m_ngTint.darker(dark ? 165 : 125), front = m_ngTint.darker(dark ? 112 : 104);
            const qreal tabStrip = 9;
            const QRectF backPanel(b.left() + 8, b.top() + 4 + tabStrip, b.width() - 16, b.height());
            painter->setPen(Qt::NoPen);
            painter->setBrush(back);
            const qreal tabsRight = backPanel.left() + backPanel.width() * 0.30;
            painter->drawRoundedRect(QRectF(backPanel.left(), b.top() + 4, backPanel.width() * 0.30, 20), 5, 5);
            painter->drawRoundedRect(backPanel, 7, 7);
            // tagged files inside: their sheets peeking out, left of the ribbons of the folder's own tags, behind the pictures
            const qreal ribbonDepth = 16;
            const qreal ribbonsRight = backPanel.right() - 10, ribbonsRoom = (ribbonsRight - tabsRight - 4) * 0.6;
            const int ribbonCount = qMin<int>(m_ngCardTags.size(), qMax(1, int((ribbonsRoom + 3) / (ngRibbonWidth(ribbonDepth) + 3))));
            const qreal sheetsRight = m_ngCardTags.isEmpty() ? ribbonsRight : ribbonsRight - ribbonCount * (ngRibbonWidth(ribbonDepth) + 3) - 2;
            ngPaintInsideSheets(painter, sheetsRight, backPanel.top() + 2, sheetsRight - tabsRight - 3, tabStrip, m_ngCardInnerTags, &m_ngInsideSheets);
            const QRectF inside = backPanel.adjusted(7, 7, -7, 0);
            const int count = folderPictures.count();
            const qreal gap = 4, tileWidth = (inside.width() - (count - 1) * gap) / count;
            for (int i = 0; i < count; ++i) {
                const QPixmap &picture = folderPictures.at(i);
                QRectF tile(inside.left() + i * (tileWidth + gap), inside.top(), tileWidth, inside.height());
                if (m_ngCardFolderCover && picture.height() > 0) {
                    // a cover is shown whole across, in the middle of the folder, as a record in its sleeve
                    const qreal height = b.bottom() - inside.top();
                    const qreal width = qMin(inside.width(), height * picture.width() / picture.height());
                    tile = QRectF(inside.center().x() - width / 2, inside.top(), width, height);
                }
                // cover the tile: the middle of a wide picture, the top of a tall one
                const qreal scale = qMax(tile.width() / picture.width(), tile.height() / picture.height());
                const QSizeF part(tile.width() / scale, tile.height() / scale);
                const QRectF source((picture.width() - part.width()) / 2.0, picture.height() > picture.width() * 1.3 ? 0 : (picture.height() - part.height()) / 2.0,
                                    part.width(), part.height());
                QPainterPath sheet;
                sheet.addRoundedRect(tile.adjusted(0, 0, 0, 6), 3, 3);
                painter->save();
                painter->setClipPath(sheet, Qt::IntersectClip);
                painter->drawPixmap(tile, picture, source);
                painter->restore();
            }
            const qreal flapHeight = qMax(16.0, b.height() * 0.26);
            const QRectF flap(b.left() + 3, b.bottom() - flapHeight, b.width() - 6, flapHeight + 10);
            painter->setPen(Qt::NoPen);
            painter->setBrush(front);
            painter->drawRoundedRect(flap, 7, 7);
            painter->setPen(QPen(QColor(255, 255, 255, dark ? 70 : 160), 1));
            painter->drawLine(QPointF(flap.left() + 7, flap.top() + 0.5), QPointF(flap.right() - 7, flap.top() + 0.5));
            if (m_ngInfoInside) {
                // written on the front flap: how many items, and how much they take when it is measured
                const QVariant items = data().value("count");
                const int itemCount = items.isValid() && items.typeId() != QMetaType::QString ? items.toInt() : -1;
                QString words = itemCount >= 0 ? i18ncp("@item:intable", "%1 item", "%1 items", itemCount) : QString();
                if (!m_ngInfoSize.isEmpty()) {
                    words += (words.isEmpty() ? QString() : QStringLiteral(" · ")) + m_ngInfoSize;
                }
                if (!words.isEmpty()) {
                    QFont flapFont(m_customizedFont);
                    flapFont.setPixelSize(qMax(11, qRound(flapHeight * 0.58)));
                    flapFont.setWeight(QFont::Bold);
                    painter->setFont(flapFont);
                    painter->setPen(front.lightnessF() > 0.6 ? QColor(45, 49, 56) : QColor(240, 242, 245));
                    const QRectF line(flap.left() + 11, flap.top(), flap.width() - 22, flapHeight);
                    painter->drawText(line, Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(flapFont).elidedText(words, Qt::ElideRight, line.width()));
                }
            }
            // the folder's own tags: ribbons from the back's top edge, over the pictures
            ngPaintRibbons(painter, ribbonsRight, backPanel.top(), (ngRibbonsOpen(isHovered()) || m_ngCardTags.size() == 1) ? backPanel.width() - 16 : ribbonsRoom, b.bottom() - 6, ribbonDepth, m_ngCardTags, isHovered(),
                           ngTagRibbonFont(&m_customizedFont));
            painter->restore();
        } else if (m_ngCategory == int(NgKind::Folder) || !m_ngCardPreview) {
            // no preview: a sketch of what a file of this kind holds (or of the folder), instead of its icon
            painter->save();
            painter->setClipPath(band, Qt::IntersectClip);
            const QVariant items = data().value("count");
            int itemCount = items.isValid() && items.typeId() != QMetaType::QString ? items.toInt() : -1;
            if (itemCount < 0 && m_ngCategory == int(NgKind::Folder) && m_ngCardPeek.isEmpty() && m_ngCardPeekMore == 0) {
                itemCount = 0; // nothing was found in it: drawn as an empty folder
            }
            // (the list of the first entries beside a small folder is kept behind CARDS_PEEK=show: by default a folder
            // is drawn whole, like every other kind)
            if (m_ngCategory == int(NgKind::Folder) && !m_ngCardPeek.isEmpty() && ngSetting("CARDS_PEEK").toLower() == QLatin1String("show")
                && !ngSpecialFolder(data().value("url").toUrl().toLocalFile())) {
                // a folder with something in it: the folder on the left, its first entries listed beside it
                const QRectF left(m_ngCardThumb.left(), m_ngCardThumb.top(), m_ngCardThumb.width() * 0.40, m_ngCardThumb.height());
                ngPaintContentSketch(painter, left, m_ngCategory, m_ngSigla, m_ngTint, dark, itemCount, m_ngInfoSize, m_ngInfoInside ? &m_customizedFont : nullptr);
                QFont peekFont = m_customizedFont;
                peekFont.setPointSizeF(peekFont.pointSizeF() * 0.84);
                const QFontMetricsF peekFm(peekFont);
                const qreal rowHeight = peekFm.height() + 3;
                // (the chips of the card sit at the bottom right of the band: the list stays above them)
                const qreal listHeight = m_ngCardThumb.height() - 12 - (peekFm.height() + 12);
                const int room = qMax(1, int(listHeight / rowHeight));
                const bool more = m_ngCardPeekMore != 0 || m_ngCardPeek.count() > room;
                const int shown = qMin<int>(m_ngCardPeek.count(), more ? room - 1 : room);
                const qreal x = left.right() + 2, right = m_ngCardThumb.right() - 10;
                qreal y = m_ngCardThumb.top() + 6 + qMax(0.0, (listHeight - (shown + (more ? 1 : 0)) * rowHeight) / 2.0);
                painter->setFont(peekFont);
                for (int i = 0; i < shown; ++i) {
                    QString name = m_ngCardPeek.at(i);
                    const bool folder = name.endsWith(QLatin1Char('/'));
                    if (folder) {
                        name.chop(1);
                    }
                    const NgKind kind = ngKindFor(name, folder);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(kind.tint);
                    painter->drawRoundedRect(QRectF(x, y + rowHeight / 2.0 - 3.5, 7, 7), folder ? 2 : 3.5, folder ? 2 : 3.5);
                    QColor ink = styleOption().palette.color(QPalette::Text);
                    ink.setAlphaF(0.82);
                    painter->setPen(ink);
                    painter->drawText(QRectF(x + 13, y, right - x - 13, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, peekFm.elidedText(name, Qt::ElideMiddle, right - x - 13));
                    y += rowHeight;
                }
                if (more) {
                    QColor dim = styleOption().palette.color(QPalette::Text);
                    dim.setAlphaF(0.5);
                    painter->setPen(dim);
                    const int rest = itemCount > shown ? itemCount - shown : m_ngCardPeekMore > 0 ? m_ngCardPeekMore + m_ngCardPeek.count() - shown : 0;
                    painter->drawText(QRectF(x + 13, y, right - x - 13, rowHeight), Qt::AlignLeft | Qt::AlignVCenter,
                                      rest > 0 ? ngTr("+ %1 more", "+ altri %1").arg(rest) : QStringLiteral("…"));
                }
            } else {
                ngPaintContentSketch(painter, m_ngCardThumb, m_ngCategory, m_ngSigla, m_ngTint, dark, itemCount, m_ngInfoSize, m_ngInfoInside ? &m_customizedFont : nullptr,
                                     m_ngCategory == int(NgKind::Folder) ? ngSpecialFolder(data().value("url").toUrl().toLocalFile()) : 0, false, m_ngCardTags,
                                     isHovered(), m_ngCardInnerTags, &m_ngInsideSheets);
            }
            painter->restore();
        } else if (!m_pixmap.isNull() && m_ngInfoInside) {
            // the preview as an object, its type and size written on its border
            painter->save();
            painter->setClipPath(band, Qt::IntersectClip);
            paintNgPreviewObject(painter, m_ngCardThumb, m_pixmap, false);
            painter->restore();
        } else if (!m_pixmap.isNull()) {
            painter->save();
            painter->setClipPath(band, Qt::IntersectClip);
            drawPixmap(painter, m_pixmap);
            painter->restore();
        }
    } else if ((look == NgCardStyle::Scheda || look == NgCardStyle::Dossier) && !m_ngCardThumb.isEmpty() && m_ngInfoInside) {
        // the drawing of the file (type and size in it) or its preview as an object: whole in the Scheda's column,
        // rising from the foot of the Dossier's header
        const bool whole = look == NgCardStyle::Scheda;
        QPainterPath clip;
        clip.addRoundedRect(rect(), 12, 12);
        painter->save();
        painter->setClipPath(clip, Qt::IntersectClip);
        if (!m_ngCardHeader.isEmpty()) {
            QColor head = m_ngTint;
            head.setAlphaF(dark ? 0.18 : 0.16);
            painter->fillRect(m_ngCardHeader, head);
            painter->setClipRect(m_ngCardHeader, Qt::IntersectClip);
        }
        if (m_ngCardPreview && !m_pixmap.isNull() && m_ngCategory != int(NgKind::Folder)) {
            paintNgPreviewObject(painter, m_ngCardThumb, m_pixmap, whole);
        } else {
            const QVariant items = data().value("count");
            const int itemCount = items.isValid() && items.typeId() != QMetaType::QString ? items.toInt() : -1;
            ngPaintContentSketch(painter, m_ngCardThumb, m_ngCategory, m_ngSigla, m_ngTint, dark, itemCount, m_ngInfoSize, &m_customizedFont,
                                 m_ngCategory == int(NgKind::Folder) ? ngSpecialFolder(data().value("url").toUrl().toLocalFile()) : 0, whole, m_ngCardTags,
                                 isHovered(), m_ngCardInnerTags, &m_ngInsideSheets);
        }
        painter->restore();
    } else if (!m_pixmap.isNull()) {
        drawPixmap(painter, m_pixmap);
    }
    if (m_layout == CardsLayout && !m_ngCardTags.isEmpty() && !m_ngCardThumb.isEmpty() && m_ngCategory != int(NgKind::Folder)
        && ngCardStyle(this) == NgCardStyle::Tessera) {
        paintNgCardTags(painter); // (a folder's tags are its ribbons, see ngPaintContentSketch; in the Scheda a file's are its second line)
    }
    if (!m_overlays.isEmpty() && !m_pixmap.isNull()) {
        drawOverlays(painter);
    }

    // ---- the name
    const TextInfo *nameInfo = m_textInfo.value("text");
    if (nameInfo && editedRole() != "text") {
        QFont nameFont = m_customizedFont;
        nameFont.setWeight(QFont::DemiBold);
        painter->setFont(nameFont);
        painter->setPen(text);
        if (ngTextHalo() && ngCardHaloStrength(this) > 0) {
            ngPaintTextHalo(
                painter,
                text,
                [&](const QPointF &offset) {
                    painter->drawStaticText(nameInfo->pos + offset, nameInfo->staticText);
                },
                ngCardHaloStrength(this));
        }
        painter->drawStaticText(nameInfo->pos, nameInfo->staticText);
    }

    // ---- chips
    if (!m_ngChips.isEmpty()) {
        QFont chipFont = m_customizedFont;
        chipFont.setPointSizeF(chipFont.pointSizeF() * 0.88);
        painter->setFont(chipFont);
        const int ink = dark ? 255 : 0;
        // Tessera: the chips on the band are one badge in two halves: the type on its full colour, the rest on dark
        QRectF badge;
        if (look == NgCardStyle::Tessera) {
            for (const NgChip &chip : std::as_const(m_ngChips)) {
                if (m_ngCardThumb.contains(chip.rect.center())) {
                    badge = badge.isNull() ? chip.rect : badge.united(chip.rect);
                }
            }
        }
        const QString badgeStyle = ngSetting("CARDS_BADGE").toLower();
        // CARDS_BADGE=center: the glass badge in the middle, flatter (no shadow, a fainter edge): part of the picture
        const bool flat = badgeStyle == QLatin1String("center");
        const bool glass = badgeStyle == QLatin1String("glass") || flat;
        if (!badge.isNull()) {
            const qreal radius = badge.height() / 2.0;
            QPainterPath shape;
            shape.addRoundedRect(badge, radius, radius);
            painter->save();
            painter->setPen(Qt::NoPen);
            for (int i = 3; i >= 1 && !flat; --i) { // a soft shadow under it, so that it lifts off a picture
                painter->setBrush(QColor(0, 0, 0, 26));
                painter->drawRoundedRect(badge.translated(0, 1.5).adjusted(-i, -i + 1, i, i), radius + i, radius + i);
            }
            painter->setClipPath(shape, Qt::IntersectClip);
            if (glass) {
                // frosted glass: the part of the picture under the badge, blurred, under a pale veil; without a
                // picture (a sketch) a dark glass
                if (m_ngCardPreview && !m_pixmap.isNull()) {
                    const qreal dpr = m_pixmap.devicePixelRatio();
                    const QRect source = QRectF((badge.topLeft() - m_pixmapPos) * dpr, badge.size() * dpr).toAlignedRect().intersected(m_pixmap.rect());
                    if (m_ngBadgeGlassKey != m_pixmap.cacheKey() || m_ngBadgeGlass.size() != source.size()) {
                        const QPixmap part = m_pixmap.copy(source);
                        m_ngBadgeGlass = part.scaled(qMax(1, part.width() / 10), qMax(1, part.height() / 10), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                             .scaled(part.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        m_ngBadgeGlassKey = m_pixmap.cacheKey();
                        const int lightness = m_ngBadgeGlass.isNull() ? 0 : m_ngBadgeGlass.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).toImage().pixelColor(0, 0).lightness();
                        m_ngBadgeGlassShade = lightness > 190 ? 150 : lightness > 130 ? 115 : 70;
                    }
                    painter->drawPixmap(badge, m_ngBadgeGlass, QRectF(m_ngBadgeGlass.rect()));
                    painter->fillRect(badge, QColor(0, 0, 0, flat ? m_ngBadgeGlassShade * 3 / 4 : m_ngBadgeGlassShade));
                    painter->fillRect(badge, QColor(255, 255, 255, flat ? 22 : 40));
                } else {
                    painter->fillRect(badge, QColor(20, 22, 26, 150));
                }
            } else {
                painter->fillRect(badge, dark ? QColor(14, 15, 18, 240) : QColor(252, 252, 252, 242));
                for (const NgChip &chip : std::as_const(m_ngChips)) {
                    if (chip.tinted && badge.contains(chip.rect.center())) {
                        painter->fillRect(chip.rect, m_ngTint);
                    }
                }
            }
            painter->restore();
            if (glass) {
                painter->setPen(QPen(QColor(255, 255, 255, flat ? 34 : 96), 1));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(badge.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            }
        }
        for (const NgChip &chip : std::as_const(m_ngChips)) {
            if (!badge.isNull() && badge.contains(chip.rect.center())) {
                QFont badgeFont = chipFont;
                badgeFont.setWeight(chip.tinted ? QFont::Bold : QFont::Normal);
                if (chip.tinted) {
                    badgeFont.setLetterSpacing(QFont::PercentageSpacing, 104);
                }
                painter->setFont(badgeFont);
                if (glass) {
                    // white on the glass, with a thin shadow; the type has a dot of its colour before it
                    QRectF textRect = chip.rect.adjusted(chip.tinted ? 1 : -1, 0, chip.tinted ? 1 : -1, 0);
                    if (chip.tinted) {
                        painter->setPen(QPen(QColor(0, 0, 0, 64), 2));
                        painter->setBrush(m_ngTint);
                        painter->drawEllipse(QPointF(chip.rect.left() + 13, chip.rect.center().y()), 4, 4);
                        textRect.adjust(11, 0, 0, 0);
                    }
                    painter->setPen(QColor(0, 0, 0, 120));
                    painter->drawText(textRect.translated(0, 1), Qt::AlignCenter, chip.text);
                    painter->setPen(QColor(255, 255, 255, chip.tinted ? 255 : 235));
                    painter->drawText(textRect, Qt::AlignCenter, chip.text);
                    painter->setFont(chipFont);
                    continue;
                }
                painter->setPen(chip.tinted ? QColor(20, 22, 26) : text);
                // the two halves lean towards the join: 10 px at the outer end, 8 at the inner one
                painter->drawText(chip.rect.adjusted(chip.tinted ? 1 : -1, 0, chip.tinted ? 1 : -1, 0), Qt::AlignCenter, chip.text);
                painter->setFont(chipFont);
                continue;
            }
            const bool tinted = chip.tinted || chip.colour.isValid();
            const QColor tint = chip.colour.isValid() ? chip.colour : m_ngTint;
            QColor fill = tinted ? tint : QColor(ink, ink, ink);
            fill.setAlpha(tinted ? (dark ? 34 : 40) : (dark ? 18 : 16));
            QColor edge = tinted ? tint : QColor(ink, ink, ink);
            edge.setAlpha(tinted ? 110 : 26);
            const qreal radius = chip.rect.height() / 2.0;
            const bool onBand = look == NgCardStyle::Tessera && m_ngCardThumb.contains(chip.rect.center());
            if (onBand) {
                // over the picture a chip needs a ground of its own, or it is lost in it
                painter->setPen(Qt::NoPen);
                painter->setBrush(dark ? QColor(12, 13, 16, 228) : QColor(252, 252, 252, 232));
                painter->drawRoundedRect(chip.rect, radius, radius);
                edge.setAlpha(tinted ? 150 : 60);
            }
            painter->setPen(QPen(edge, 1));
            painter->setBrush(fill);
            painter->drawRoundedRect(chip.rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            QColor chipText = tinted ? tint : onBand ? text : m_additionalInfoTextColor;
            if (tinted && !dark) {
                chipText = chipText.darker(135);
            } else if (chip.colour.isValid()) {
                chipText = chipText.lighter(125);
            }
            if (chip.colour.isValid()) {
                // a tag: a solid chip in its colour, white text, in every look (as the Information panel shows it; a
                // pastel see-through one read as a different, fainter thing)
                QColor solid = chip.colour;
                solid.setAlphaF(0.80);
                painter->setPen(Qt::NoPen);
                painter->setBrush(solid);
                painter->drawRoundedRect(chip.rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
                chipText = QColor(255, 255, 255);
            }
            painter->setPen(chipText);
            painter->drawText(chip.rect, Qt::AlignCenter, chip.text);
        }
    }

    // ---- the size bar (Dossier, files)
    if (m_ngBarFraction >= 0 && !m_ngBarRect.isEmpty()) {
        QColor track = text;
        track.setAlphaF(0.10);
        painter->setPen(Qt::NoPen);
        painter->setBrush(track);
        painter->drawRoundedRect(m_ngBarRect, 2, 2);
        QColor fill = m_ngTint;
        fill.setAlphaF(0.9);
        painter->setBrush(fill);
        painter->drawRoundedRect(QRectF(m_ngBarRect.left(), m_ngBarRect.top(), qMax(4.0, m_ngBarRect.width() * m_ngBarFraction), m_ngBarRect.height()), 2, 2);
    }

    // ---- the Dossier's hairlines, the folder's bar and the sign before the origin line
    if (!m_ngDossierRules.isEmpty()) {
        QColor rule = text;
        rule.setAlphaF(0.10);
        painter->setPen(QPen(rule, 1));
        painter->drawLines(m_ngDossierRules);
    }
    if (!m_ngDossierShapes.isEmpty()) {
        painter->setPen(Qt::NoPen);
        for (const auto &shape : std::as_const(m_ngDossierShapes)) {
            painter->setBrush(shape.second);
            const qreal radius = qMin(shape.first.height(), shape.first.width()) / 2.5;
            painter->drawRoundedRect(shape.first, radius, radius);
        }
    }
    if (!m_ngDossierSignRect.isEmpty()) {
        const QIcon sign = QIcon::fromTheme(m_ngDossierSign);
        if (!sign.isNull()) {
            painter->setOpacity(0.6);
            sign.paint(painter, m_ngDossierSignRect.toAlignedRect());
            painter->setOpacity(1.0);
        }
    }

    // ---- the texts
    // what stands out on a card (the facts, the bold words of a line): the desktop's accent, a little warmer and at half
    // its colour, between the accent and the text: a mid tone on light schemes, a pastel on dark ones. A selection that
    // has its own text colour (the full accent) keeps it
    QColor strong = text;
    if (text == option.palette.color(QPalette::Text)) {
        const QColor highlight = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
        const qreal hue = std::fmod((highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF()) + 4.0 / 360.0, 1.0);
        const qreal tone = highlight.hslSaturationF() < 0.15 ? 0.0 : 1.0; // (a grey accent: a grey ink)
        strong = dark ? QColor::fromHslF(hue, 0.40 * tone, 0.84) : QColor::fromHslF(hue, 0.28 * tone, 0.44);
    }
    const bool ngHaloCards = ngTextHalo() && ngCardHaloStrength(this) > 0;
    for (const NgCardText &entry : std::as_const(m_ngCardTexts)) {
        painter->setFont(entry.font);
        QColor ink = entry.tone == 1 ? strong : entry.tone == 2 ? dim : entry.tone == 3 ? QColor(232, 192, 72) : m_additionalInfoTextColor;
        if (ngHaloCards) {
            // the details (kind, dates, facts): solid, and a shadow nearly as full as the names' (see-through and
            // half shaded, they vanished over a white window behind the glass)
            ink = ngHaloInk(ink, styleOption().palette);
            ngPaintTextHalo(
                painter,
                ink,
                [&](const QPointF &offset) {
                    painter->drawText(entry.pos + offset, entry.text);
                },
                ngCardHaloStrength(this));
        }
        painter->setPen(ink);
        painter->drawText(entry.pos, entry.text);
    }

    // ---- the dates with their letters (Dossier)
    if (!m_ngTime.isEmpty()) {
        QFont small = m_customizedFont;
        small.setPointSizeF(small.pointSizeF() * 0.9);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSizeF(m_customizedFont.pointSizeF() * 0.8);
        mono.setBold(true);
        for (const NgTimeLine &line : std::as_const(m_ngTime)) {
            painter->setFont(mono);
            painter->setPen(dim);
            painter->drawText(line.letterPos, line.letter);
            painter->setFont(small);
            painter->setPen(m_additionalInfoTextColor);
            painter->drawText(line.textPos, line.text);
        }
    }

    // ---- the quick actions, over everything written on the card (drawn earlier, the dates showed through the pill)
    paintNgActionPill(painter, text, dark);

    // ---- a folder's tagged files inside, with the pointer on the card: each sheet peeking out of the folder hangs down
    // as a ribbon over everything, its tag and how many items carry it written along it
    // (a single tag is always shown with its name: there is room for it, and nothing to unfold)
    if (m_layout == CardsLayout) {
        if (ngRibbonsOpen(isHovered()) || m_ngCardInnerTags.size() == 1) {
            paintNgHangingRibbons(painter, m_ngInsideSheets, m_ngCardInnerTags, m_ngCardInnerLabels);
        }
        if (ngRibbonsOpen(isHovered()) || m_ngCardTags.size() == 1) {
            paintNgHangingRibbons(painter, m_ngFileRibbons, m_ngCardTags, m_ngCardTags); // (a file's own tags)
        }
    }

    // ---- the preview eye, on a dark disc so that it reads over a picture too
    if (!m_ngCardEye.isEmpty() && (m_ngCardEyeInPill ? (m_ngReveal > 0.01 || ngDemoHovered()) : (isHovered() || ngDemoHovered())) && !NgActivity::self()->contains(data().value("url").toUrl())
        && !NgModifiers::selecting()) {
        painter->setOpacity(m_ngCardEyeInPill ? (ngDemoHovered() ? 1.0 : qBound(0.0, m_ngReveal, 1.0)) : 1.0);
        if (!m_ngCardEyeInPill) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 130));
            painter->drawRoundedRect(m_ngCardEye, 7, 7);
        }
        // in the pill the eye takes the colour of the other glyphs; alone, over a picture, it is white on its disc
        const QColor glyph = m_ngCardEyeInPill ? styleOption().palette.color(QPalette::Text) : QColor(255, 255, 255, 235);
        const QPointF c = m_ngCardEye.center();
        const qreal r = m_ngCardEye.width() * 0.32;
        painter->setBrush(glyph);
        painter->drawEllipse(c, r * 0.32, r * 0.32);
        QPainterPath eye;
        eye.moveTo(c + QPointF(-r, 0));
        eye.quadTo(c + QPointF(0, -r), c + QPointF(r, 0));
        eye.quadTo(c + QPointF(0, r), c + QPointF(-r, 0));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(glyph, 1.4));
        painter->drawPath(eye);
    }

    painter->restore();
}

QRectF KStandardItemListWidget::roleEditingRect(const QByteArray &role) const
{
    const TextInfo *textInfo = m_textInfo.value(role);
    if (!textInfo) {
        return QRectF();
    }

    QRectF rect(textInfo->pos, textInfo->staticText.size());
    if (m_layout == DetailsLayout) {
        rect.setWidth(columnWidth(role) - rect.x());
    }

    return rect;
}

void KStandardItemListWidget::closeRoleEditor()
{
    disconnect(m_roleEditor, &KItemListRoleEditor::roleEditingCanceled, this, &KStandardItemListWidget::slotRoleEditingCanceled);
    disconnect(m_roleEditor, &KItemListRoleEditor::roleEditingFinished, this, &KStandardItemListWidget::slotRoleEditingFinished);

    if (m_roleEditor->hasFocus()) {
        // If the editing was not ended by a FocusOut event, we have
        // to transfer the keyboard focus back to the KItemListContainer.
        scene()->views()[0]->parentWidget()->setFocus();
    }

    if (m_oldRoleEditor) {
        m_oldRoleEditor->deleteLater();
    }
    m_oldRoleEditor = m_roleEditor;
    m_roleEditor->hide();
    m_roleEditor = nullptr;
}

QPixmap KStandardItemListWidget::pixmapForIcon(const QString &name, const QSize &size, QIcon::Mode mode) const
{
    static const QIcon fallbackIcon = QIcon::fromTheme(QStringLiteral("unknown"));
    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);

    int iconHeight = size.height();
    QSize iconSize = QSize(iconHeight, iconHeight);

    const QString key = "KStandardItemListWidget:" % name % ":" % QString::number(iconHeight) % "@" % QString::number(dpr) % ":" % QString::number(mode);
    QPixmap pixmap;

    if (!QPixmapCache::find(key, &pixmap)) {
        QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull()) {
            icon = QIcon(name);
        }
        if (!icon.isNull()) {
            pixmap = icon.pixmap(iconSize, dpr, mode);
        }
        if (pixmap.isNull()) {
            icon = fallbackIcon;
            pixmap = icon.pixmap(iconSize, dpr, mode);
        }
        if (pixmap.width() != iconHeight * dpr || pixmap.height() != iconHeight * dpr) {
            KPixmapModifier::scale(pixmap, iconSize * dpr);
        }

        QPixmapCache::insert(key, pixmap);
    }
    pixmap.setDevicePixelRatio(dpr);

    return pixmap;
}

QSizeF KStandardItemListWidget::preferredRatingSize(const KItemListStyleOption &option)
{
    const qreal height = option.fontMetrics.ascent();
    return QSizeF(height * 5, height);
}

qreal KStandardItemListWidget::columnPadding(const KItemListStyleOption &option)
{
    return option.padding * 6;
}

// dolphin-ng: a file card's tags over its picture (a folder's are its tabs, see ngPaintContentSketch): pills at the top
// left of the picture. When they do
// not fit, dots of their colours; with the pointer on the card they open into pills, over the picture
void KStandardItemListWidget::paintNgCardTags(QPainter *painter)
{
    // a file's tags: short ribbons hanging from the top right corner of its picture, as a folder's (the type's name is
    // written at its top left, its size at its foot: the pills of before covered them). With the pointer on the card they
    // hang down with their names, over everything (see paintNgHangingRibbons)
    m_ngFileRibbons.clear();
    const QRectF thumb = m_ngCardThumb.adjusted(4, 2, -4, -4);
    const qreal width = qBound(7.0, thumb.width() * 0.09, 10.0), length = qMax(14.0, thumb.height() * 0.22), notch = width * 0.5, gap = 3;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor(0, 0, 0, 70), 1));
    qreal x = thumb.right() - width;
    for (const QString &tag : std::as_const(m_ngCardTags)) {
        if (x < thumb.left() + thumb.width() * 0.45) {
            break; // (the left of the picture, where the type is written, stays clear)
        }
        QPainterPath ribbon;
        ribbon.moveTo(x, thumb.top());
        ribbon.lineTo(x + width, thumb.top());
        ribbon.lineTo(x + width, thumb.top() + length);
        ribbon.lineTo(x + width / 2, thumb.top() + length - notch);
        ribbon.lineTo(x, thumb.top() + length);
        ribbon.closeSubpath();
        painter->setBrush(ngTagColour(tag));
        painter->drawPath(ribbon);
        m_ngFileRibbons.append(QRectF(x, thumb.top(), width, length));
        x -= width + gap;
    }
    painter->restore();
}

// dolphin-ng: tags hanging down as vertical ribbons over everything on the card, with the pointer on it: from @p anchors
// (the sheets of a folder's tagged files, a file's short ribbons), the tag's colour, @p labels written along it, a
// swallowtail at the foot; side by side from the rightmost leftwards, kept inside the card
void KStandardItemListWidget::paintNgHangingRibbons(QPainter *painter, const QList<QRectF> &anchors, const QStringList &tags, const QStringList &labels)
{
    if (anchors.isEmpty()) {
        return;
    }
    QFont font = m_customizedFont;
    font.setPointSizeF(font.pointSizeF() * 0.78);
    font.setWeight(QFont::DemiBold);
    const QFontMetricsF fm(font);
    const qreal width = fm.height() + 6, notch = width * 0.45, gap = 3;
    const QRectF card = rect().adjusted(4, 4, -4, -4);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipping(false);
    painter->setFont(font);
    qreal right = anchors.first().right();
    for (int i = 0; i < anchors.size() && i < tags.size() && i < labels.size(); ++i) {
        const qreal left = qMin(right - width, anchors.at(i).center().x() - width / 2);
        if (left < card.left()) {
            break;
        }
        const qreal top = anchors.at(i).top();
        const QString label = labels.at(i);
        const qreal length = qMin(fm.horizontalAdvance(label) + 16 + notch, card.bottom() - top);
        QPainterPath ribbon;
        ribbon.moveTo(left, top);
        ribbon.lineTo(left + width, top);
        ribbon.lineTo(left + width, top + length);
        ribbon.lineTo(left + width / 2, top + length - notch);
        ribbon.lineTo(left, top + length);
        ribbon.closeSubpath();
        // a soft shadow, then the ribbon
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 55));
        painter->drawPath(ribbon.translated(1.5, 1.5));
        painter->setPen(QPen(QColor(0, 0, 0, 70), 1));
        painter->setBrush(ngTagColour(tags.at(i)));
        painter->drawPath(ribbon);
        // the words along it, read from the top down
        painter->save();
        painter->translate(left + width / 2, top + 7);
        painter->rotate(90);
        painter->setPen(Qt::white);
        const qreal room = length - notch - 10;
        painter->drawText(QRectF(0, -width / 2, room, width), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(label, Qt::ElideRight, room));
        painter->restore();
        right = left - gap;
    }
    painter->restore();
}

#include "moc_kstandarditemlistwidget.cpp"
