/*
 * SPDX-FileCopyrightText: 2007 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinviewcontainer.h"

#include "admin/bar.h"
#include "admin/workerintegration.h"
#include "dolphin_compactmodesettings.h"
#include "dolphin_contentdisplaysettings.h"
#include "dolphin_detailsmodesettings.h"
#include "dolphin_generalsettings.h"
#include "dolphin_iconsmodesettings.h"
#include "dolphindebug.h"
#include "dolphinplacesmodelsingleton.h"
#include "filterbar/filterbar.h"
#include "global.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistwidget.h"
#include "search/bar.h"
#include "selectionmode/topbar.h"
#include "statusbar/dolphinstatusbar.h"

#include <KActionCollection>
#include <KApplicationTrader>
#include <KFileItemActions>
#include <KFilePlacesModel>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkpathJob>
#include <KIO/ApplicationLauncherJob>
#include <QFileInfo>
#include <KMessageBox>
#include <KIO/CommandLauncherJob>
#include <KIO/OpenUrlJob>
#include <KIO/PreviewJob>
#include <KLocalizedString>
#include <KProtocolManager>
#include <KShell>
#include <kio_version.h>

#ifndef QT_NO_ACCESSIBILITY
#include <QAccessible>
#endif
#include <QApplication>
#include <QDesktopServices>
#include <QDropEvent>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <memory>
#include <QLabel>
#include <QLineEdit>
#include <QActionGroup>
#include <functional>
#include <QPointer>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QToolButton>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>

#include <cmath>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QLocale>
#include <QStackedLayout>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMimeDatabase>
#include <QScreen>
#include <QCursor>
#include <QVideoSink>
#include <QVideoFrame>
#include <QAudioOutput>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QSlider>
#include <QMovie>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QScopeGuard>
#include <QTimer>
#include <QVariantAnimation>
#include <QUrl>
#include <QUrlQuery>

bool isSearchUrl(const QUrl &url)
{
    return url.scheme().contains(QLatin1String("search"));
}

// An overview of the widgets contained by this ViewContainer
struct LayoutStructure {
    int searchBar = 0;
    int adminBar = 1;
    int messageWidget = 2;
    int selectionModeTopBar = 3;
    int sortBar = 4; // dolphin-ng
    int view = 5;
    int selectionModeBottomBar = 6;
    int filterBar = 7;
    int statusBar = 8;
};
constexpr LayoutStructure positionFor;

// dolphin-ng: a layout that puts its items one after the other and wraps to a new line when the width runs out
class NgFlowLayout : public QLayout
{
public:
    NgFlowLayout(QWidget *parent, int hSpacing, int vSpacing)
        : QLayout(parent)
        , m_hSpacing(hSpacing)
        , m_vSpacing(vSpacing)
    {
    }
    ~NgFlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0)) {
            delete item;
        }
    }
    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
    }
    int count() const override
    {
        return m_items.size();
    }
    QLayoutItem *itemAt(int index) const override
    {
        return m_items.value(index);
    }
    QLayoutItem *takeAt(int index) override
    {
        return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override
    {
        return {};
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, width, 0), true);
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override
    {
        return minimumSize();
    }
    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : m_items) {
            size = size.expandedTo(item->minimumSize());
        }
        const QMargins margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

private:
    int doLayout(const QRect &rect, bool testOnly) const
    {
        const QMargins margins = contentsMargins();
        const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
        int y = area.y();
        int i = 0;
        while (i < m_items.size()) {
            // take as many items as fit on this line (at least one)
            int x = area.x();
            int lineHeight = 0;
            int last = i;
            while (last < m_items.size()) {
                if (m_items.at(last)->isEmpty()) {
                    ++last; // a hidden widget: no room, no spacing
                    continue;
                }
                const QSize hint = m_items.at(last)->sizeHint();
                if (last > i && x + hint.width() > area.right() + 1) {
                    break;
                }
                x += hint.width() + m_hSpacing;
                lineHeight = qMax(lineHeight, hint.height());
                ++last;
            }
            if (!testOnly) {
                x = area.x();
                for (int k = i; k < last; ++k) {
                    if (m_items.at(k)->isEmpty()) {
                        continue;
                    }
                    const QSize hint = m_items.at(k)->sizeHint();
                    m_items.at(k)->setGeometry(QRect(x, y + (lineHeight - hint.height()) / 2, hint.width(), hint.height()));
                    x += hint.width() + m_hSpacing;
                }
            }
            y += lineHeight + m_vSpacing;
            i = last;
        }
        return y - m_vSpacing - rect.y() + margins.bottom();
    }

    QList<QLayoutItem *> m_items;
    int m_hSpacing;
    int m_vSpacing;
};

// dolphin-ng: the bar above the two-line list, which has no column header. On one line (wrapping when narrow):
// "Sort by" — one button per visible role, clicking the active one reverses the order;
// "Filtri" — a button with a menu of filters: all, files only, and one per kind of file, each with its count.
// dolphin-ng: the quick view. While the pointer rests on the picture of a card, a larger rendition floats next to
// the card: the picture itself for images, the first page for PDFs. It never takes the mouse, so moving away
// (or scrolling, or clicking) simply ends it.
class NgQuickView : public QWidget
{
public:
    explicit NgQuickView(QWidget *host)
        : QWidget(host)
        , m_host(host)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
        // The view says when the pointer leaves the picture, but only while the pointer moves over the view: leaving
        // towards a panel or the bar, a list that changes under a still pointer, a switch to another window say
        // nothing, and the quick view stayed open. So while it is up it checks for itself that the pointer is still
        // on the item it was opened for.
        m_watch.setInterval(140);
        connect(&m_watch, &QTimer::timeout, this, [this] {
            const QPoint pointer = m_host->mapFromGlobal(QCursor::pos());
            if (m_url.isEmpty() || !m_host->isVisible() || !m_host->window()->isActiveWindow() || !m_anchor.adjusted(-2, -2, 2, 2).contains(pointer)) {
                end();
            }
        });
    }

    void showFor(const KFileItem &item, const QRect &itemRectGlobal)
    {
        if (item.isNull() || !item.isLocalFile()) {
            return;
        }
        m_watch.start();
        m_url = item.url();
        m_anchor = QRect(m_host->mapFromGlobal(itemRectGlobal.topLeft()), itemRectGlobal.size());
        const QSize limit(qMin(560, m_host->width() - 40), qMin(480, m_host->height() - 40));
        const qreal dpr = devicePixelRatioF();
        stopMotion();
        const QString mime = item.mimetype();
        if (mime == QLatin1String("image/gif") || mime == QLatin1String("image/apng") || mime == QLatin1String("image/webp")) {
            // animated pictures play
            m_movie = new QMovie(item.localPath(), QByteArray(), this);
            if (m_movie->isValid()) {
                connect(m_movie, &QMovie::frameChanged, this, [this, limit] {
                    frame(m_movie->currentPixmap(), limit);
                });
                m_movie->start();
                return;
            }
            stopMotion();
        }
        if (mime.startsWith(QLatin1String("video/"))) {
            // videos play, muted, in a loop
            m_player = new QMediaPlayer(this);
            m_sink = new QVideoSink(this);
            m_player->setVideoSink(m_sink);
            m_player->setLoops(QMediaPlayer::Infinite);
            connect(m_sink, &QVideoSink::videoFrameChanged, this, [this, limit](const QVideoFrame &videoFrame) {
                const QImage image = videoFrame.toImage();
                if (!image.isNull()) {
                    frame(QPixmap::fromImage(image), limit);
                }
            });
            m_player->setSource(item.url());
            m_player->play();
            return;
        }
        if (mime.startsWith(QLatin1String("image/"))) {
            QImageReader reader(item.localPath());
            reader.setAutoTransform(true);
            QSize size = reader.size();
            if (size.isValid() && (size.width() > limit.width() * dpr || size.height() > limit.height() * dpr)) {
                size.scale(limit * dpr, Qt::KeepAspectRatio);
                reader.setScaledSize(size); // decoded at the size shown: quick even for large photos
            }
            setPicture(QPixmap::fromImage(reader.read()), limit);
        } else {
            // a PDF (or anything else with a thumbnailer): the preview job renders the first page
            KIO::PreviewJob *job = KIO::filePreview(KFileItemList{item}, limit * dpr);
            job->setScaleType(KIO::PreviewJob::Scaled);
            const QUrl url = m_url;
            connect(job, &KIO::PreviewJob::gotPreview, this, [this, url, limit](const KFileItem &, const QPixmap &pixmap) {
                if (url == m_url) {
                    setPicture(pixmap, limit);
                }
            });
        }
    }

    void end()
    {
        m_watch.stop();
        m_url.clear();
        stopMotion();
        hide();
    }

protected:
    void stopMotion()
    {
        if (m_movie) {
            m_movie->stop();
            m_movie->deleteLater();
            m_movie = nullptr;
        }
        if (m_player) {
            m_player->stop();
            m_player->deleteLater();
            m_player = nullptr;
        }
        if (m_sink) {
            m_sink->deleteLater();
            m_sink = nullptr;
        }
        m_playing = false;
    }

    // a frame of a movie or a video: the first one places the view, the others only replace the picture
    void frame(const QPixmap &pixmap, const QSize &limit)
    {
        if (pixmap.isNull() || m_url.isEmpty()) {
            return;
        }
        if (!m_playing) {
            m_playing = true;
            QPixmap first = pixmap;
            first.setDevicePixelRatio(1.0);
            setPicture(first, limit);
        } else {
            m_pixmap = pixmap;
            update();
        }
    }

    void setPicture(QPixmap pixmap, const QSize &limit)
    {
        if (pixmap.isNull() || m_url.isEmpty()) {
            return;
        }
        const qreal dpr = devicePixelRatioF();
        QSize shown = pixmap.size() / pixmap.devicePixelRatio();
        if (pixmap.devicePixelRatio() == 1.0) {
            shown = (QSizeF(pixmap.size()) / dpr).toSize();
            pixmap.setDevicePixelRatio(dpr);
        }
        if (shown.width() > limit.width() || shown.height() > limit.height()) {
            shown.scale(limit, Qt::KeepAspectRatio);
        }
        m_pixmap = pixmap;
        const int pad = 8;
        const QSize size = shown + QSize(2 * pad, 2 * pad);
        // beside the card: to its right, else to its left; never over it, so that the pointer stays on the card
        int x = m_anchor.right() + 10;
        if (x + size.width() > m_host->width() - 6) {
            x = m_anchor.left() - 10 - size.width();
        }
        if (x < 6) {
            x = qBound(6, m_anchor.center().x() - size.width() / 2, qMax(6, m_host->width() - 6 - size.width()));
        }
        int y = m_anchor.center().y() - size.height() / 2;
        if (x < m_anchor.right() && x + size.width() > m_anchor.left()) {
            y = m_anchor.bottom() + 10; // no room at either side: below the card, or above it
            if (y + size.height() > m_host->height() - 6) {
                y = m_anchor.top() - 10 - size.height();
            }
        }
        // CARDS_QUICKVIEW_SIDE: left (default) or right of the pointer, or "auto": beside the card, as computed above
        const QString side = ngSetting("CARDS_QUICKVIEW_SIDE", QStringLiteral("left")).toLower();
        const bool wideAnchor = m_anchor.width() > m_host->width() * 0.6; // a row, not a card
        if (side != QLatin1String("auto") && !wideAnchor) {
            // a card: on the chosen side of the card itself, never over it. Its actions and its eye are on the picture,
            // and a view that opened at the pointer covered them
            const QPoint pointer = m_host->mapFromGlobal(QCursor::pos());
            const int leftX = m_anchor.left() - 10 - size.width(), rightX = m_anchor.right() + 10;
            const bool fitsLeft = leftX >= 6, fitsRight = rightX + size.width() <= m_host->width() - 6;
            const bool wantRight = side == QLatin1String("right");
            if (fitsLeft || fitsRight) {
                x = (wantRight ? fitsRight : !fitsLeft) ? rightX : leftX;
                y = pointer.y() - size.height() / 2;
            } // neither side has room: below or above the card, as computed above
        } else if (side != QLatin1String("auto")) {
            const QPoint pointer = m_host->mapFromGlobal(QCursor::pos());
            // on the chosen side of the pointer; when it does not fit there, on the other one, so that it never
            // covers what the pointer is on (the small previews of the rows sit at the very left of the view)
            const int leftX = pointer.x() - 18 - size.width(), rightX = pointer.x() + 18;
            const bool fitsLeft = leftX >= 6, fitsRight = rightX + size.width() <= m_host->width() - 6;
            const bool wantRight = side == QLatin1String("right");
            x = (wantRight ? (fitsRight || !fitsLeft) : !(fitsLeft || !fitsRight)) ? rightX : leftX;
            x = qBound(6, x, qMax(6, m_host->width() - 6 - size.width()));
            y = pointer.y() - size.height() / 2;
        }
        y = qBound(6, y, qMax(6, m_host->height() - 6 - size.height()));
        setGeometry(x, y, size.width(), size.height());
        show();
        raise();
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(QColor(255, 255, 255, 46), 1));
        painter.setBrush(QColor(22, 23, 26, 242));
        painter.drawRoundedRect(frame, 12, 12);
        const QRectF inner = QRectF(rect()).adjusted(8, 8, -8, -8);
        QPainterPath clip;
        clip.addRoundedRect(inner, 7, 7);
        painter.setClipPath(clip);
        painter.drawPixmap(inner, m_pixmap, QRectF(m_pixmap.rect()));
    }

private:
    QWidget *m_host;
    QUrl m_url;
    QTimer m_watch;
    QRect m_anchor;
    QPixmap m_pixmap;
    QMovie *m_movie = nullptr;
    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    bool m_playing = false;
};

static void ngOpenLightbox(DolphinView *view, const KFileItem &item); // see NgLightbox

class NgSortBar : public QWidget
{
public:
    explicit NgSortBar(QWidget *parent)
        : QWidget(parent)
        , m_view(nullptr)
    {
        setObjectName(QStringLiteral("ngSortBar"));
        applyChipStyle();
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(14, 4, 14, 11); // a little air between the bar and the files
        row->setSpacing(10);
        // the navigation buttons of the window, moved here from the toolbar (see setLeadingActions)
        m_leading = new QWidget(this);
        auto *leading = new QHBoxLayout(m_leading);
        leading->setContentsMargins(0, 0, 4, 0);
        m_leadingLayout = leading;
        leading->setSpacing(2);
        m_leading->hide();
        row->addWidget(m_leading, 0, Qt::AlignTop);
        m_flow = new NgFlowLayout(nullptr, 4, 4);
        m_flow->setContentsMargins(0, 0, 0, 0);
        row->addLayout(m_flow, 1);

        // instant filter of the current folder: what you type narrows the list as you type
        m_filter = new QLineEdit(this);
        m_filter->setPlaceholderText(ngTr("Search in folder", "Cerca nella cartella"));
        m_filter->setClearButtonEnabled(true);
        m_filter->setFixedWidth(210);
        m_filter->setToolTip(ngTr("Show only the items whose name contains the text. Esc clears the field.", "Mostra solo gli elementi il cui nome contiene il testo. Esc svuota il campo."));
        row->addWidget(m_filter, 0, Qt::AlignTop);
        connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (m_view) {
                m_view->setNameFilter(text);
            }
        });
    }

    // The bar sits above the tabs and shows the active view: this moves it to another one.
    void follow(DolphinView *view)
    {
        if (m_view == view) {
            return;
        }
        if (m_view) {
            disconnect(m_view, nullptr, this, nullptr);
        }
        m_view = view;
        m_active = All;
        m_total = 0;
        if (!m_filter->text().isEmpty()) {
            m_filter->clear();
        }
        if (!view) {
            rebuild();
            return;
        }
        connect(view, &DolphinView::urlChanged, this, [this] {
            if (!m_filter->text().isEmpty()) {
                m_filter->clear(); // a new folder starts unfiltered
            }
        });

        connect(view, &DolphinView::sortRoleChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::sortOrderChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::visibleRolesChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::modeChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::urlChanged, this, [this] {
            // a new folder starts unfiltered
            if (m_active != All) {
                m_active = All;
                m_view->setMimeTypeFilters({});
            }
        });
        connect(view, &DolphinView::directoryLoadingCompleted, this, [this] {
            count();
        });
        connect(view, &DolphinView::itemCountChanged, this, [this] {
            count();
        });
        // grouping changed elsewhere (the View menu): the label of the groups button follows
        connect(view, &DolphinView::groupedSortingChanged, this, [this] {
            QTimer::singleShot(0, this, [this] {
                rebuild();
            });
        });
        connect(view, &DolphinView::groupRoleChanged, this, [this] {
            QTimer::singleShot(0, this, [this] {
                rebuild();
            });
        });
        count();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        // not from inside the resize: the changes it makes resize this bar again (narrowing the window to the
        // panel's limit made them call each other until the stack ran out). Several resizes ask once
        if (!m_fitPending) {
            m_fitPending = true;
            QTimer::singleShot(0, this, [this] {
                m_fitPending = false;
                fitSorting();
            });
        }
    }

    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        // A global theme changes the colours, the widget style and the window decoration one after the other (the
        // decoration's name, read below, is written last). Rebuilding the sheet at the first sign left the colours of
        // the old theme in it: a button with its menu open stayed light under a dark theme. So: at every sign, and
        // once more when the change has settled. Setting the sheet is itself a style change: applyChipStyle() skips
        // a sheet that did not change, which ends the round.
        if (!m_sheetApplying && (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)) {
            applyChipStyle();
            if (!m_sheetSettling) {
                m_sheetSettling = true;
                QTimer::singleShot(700, this, [this] {
                    m_sheetSettling = false;
                    applyChipStyle(true);
                });
            }
        }
    }

private:
    bool m_sheetSettling = false;
    bool m_sheetApplying = false;
    // what the filter shows: everything, files only (no folders), or one NgKind::Category
    enum { All = -1, FilesOnly = NgKind::CategoryCount };

    // The chips and the search box draw themselves, the same under every widget style: a translucent
    // pill for the checked one, a fainter one on hover, no outlines that depend on the style's hover tracking.
    void applyChipStyle(bool force = false)
    {
        const QString ink = ngIsDark(palette()) ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
        // the sheet of a menu, as Breeze paints it: the window colour mixed with 30 % of the base colour
        const QColor window = palette().color(QPalette::Window), base = palette().color(QPalette::Base);
        const QColor menuSheet(qRound(window.red() * 0.7 + base.red() * 0.3), qRound(window.green() * 0.7 + base.green() * 0.3), qRound(window.blue() * 0.7 + base.blue() * 0.3));
        // the outline of a menu in Breeze NG: 10 % of ink (Soft), 16 % (Glass); the open button continues it
        const bool glass = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString()).contains(QLatin1String("Glass"));
        const QString menuEdge = glass ? QStringLiteral("0.16") : QStringLiteral("0.10");
        // Breeze NG Aurora: glass without rims
        const bool aurora = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString()).contains(QLatin1String("Aurora"));
        QString sheet = QStringLiteral("QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 3px 7px; background: transparent; color: %4; }"
                                     "QToolButton:hover { background: rgba(%1, 0.08); }"
                                     "QToolButton:pressed { background: rgba(%1, 0.14); }"
                                     "QToolButton:checked { background: rgba(%1, 0.16); border-color: rgba(%1, 0.20); }"
                                     "QToolButton:checked:hover { background: rgba(%1, 0.20); }"
                                     "QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; left: -3px; }"
                                     // the same room around every arrow: 6 px after the text, 7 px before the edge (measured).
                                     // A menu button (sorting, filters, groups) reserves its arrow in the padding...
                                     "QToolButton[popupMode=\"2\"] { padding-right: 11px; }"
                                     "QToolButton[popupMode=\"2\"]::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; left: 0px; right: 4px; }"
                                     // ...and a button with its own arrow part (the cards one) in that part
                                     "QToolButton[popupMode=\"1\"] { padding-right: 12px; }"
                                     "QToolButton::menu-button { border: none; width: 18px; padding-right: 6px; }"
                                     "QToolButton[menuOpen=\"true\"], QToolButton[menuOpen=\"true\"]:hover, QToolButton[menuOpen=\"true\"]:checked, QToolButton[menuOpen=\"true\"]:pressed { background: %2; border-color: rgba(%1, %3); border-bottom-color: %2; border-bottom-left-radius: 0; border-bottom-right-radius: 0; }"
                                     "QLineEdit { border: 1px solid rgba(%1, 0.14); border-radius: 6px; padding: 3px 8px; background: rgba(%1, 0.06); color: %4; }"
                                     "QLineEdit { placeholder-text-color: rgba(%1, 0.5); }"
                                     "QLineEdit:hover { border-color: rgba(%1, 0.22); }"
                                     "QLineEdit:focus { border-color: rgba(%1, 0.34); background: rgba(%1, 0.08); }"
                                     // back and forward in one pill, round ends (its height is fixed: 32)
                                     "QWidget#navPill { background: rgba(%1, 0.05); border: 1px solid rgba(%1, 0.09); border-radius: 16px; }"
                                     "QWidget#navPill QToolButton { padding: 2px 11px; border-radius: 13px; }")
                          .arg(ink, menuSheet.name(), menuEdge, palette().color(QPalette::WindowText).name());
        if (aurora) {
            sheet.replace(QStringLiteral("QToolButton:checked { background: rgba(%1, 0.16); border-color: rgba(%1, 0.20); }").arg(ink), QStringLiteral("QToolButton:checked { background: rgba(%1, 0.16); }").arg(ink));
            sheet.replace(QStringLiteral("QLineEdit { border: 1px solid rgba(%1, 0.14);").arg(ink), QStringLiteral("QLineEdit { border: 1px solid transparent;"));
            sheet.replace(QStringLiteral("QLineEdit:hover { border-color: rgba(%1, 0.22); }").arg(ink), QStringLiteral("QLineEdit:hover { background: rgba(%1, 0.10); }").arg(ink));
            // one intensity for every surface at rest (Breeze NG's setting), the accent for what is on
            const bool dark = ngIsDark(palette());
            const QString veil = QString::number(ngAuroraVeilAlpha(dark, true), 'f', 3);
            const QColor accent = ngAuroraAccent(palette(), 1.0);
            const QString accentRgb = QStringLiteral("%1, %2, %3").arg(accent.red()).arg(accent.green()).arg(accent.blue());
            sheet.replace(QStringLiteral("padding: 3px 8px; background: rgba(%1, 0.06);").arg(ink), QStringLiteral("padding: 3px 8px; background: rgba(%1, %2);").arg(ink, veil));
            sheet.replace(QStringLiteral("QWidget#navPill { background: rgba(%1, 0.05); border: 1px solid rgba(%1, 0.09);").arg(ink), QStringLiteral("QWidget#navPill { background: rgba(%1, %2); border: 0;").arg(ink, veil));
            sheet.replace(QStringLiteral("QToolButton:checked { background: rgba(%1, 0.16); }").arg(ink), QStringLiteral("QToolButton:checked { background: rgba(%1, 0.34); }").arg(accentRgb));
            sheet.replace(QStringLiteral("QToolButton:checked:hover { background: rgba(%1, 0.20); }").arg(ink), QStringLiteral("QToolButton:checked:hover { background: rgba(%1, 0.42); }").arg(accentRgb));
            // the open button wears the menu's sheet: see-through in Aurora, at the menu's own opacity (Breeze NG's
            // setting; solid there means Aurora's 38 %), over the window's glass
            const int menuOpacity = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Style")).readEntry("MenuOpacity", 100);
            // (Aurora's menu: a tone lifted 10 % from the window's colour, at 38 %: as Helper::menuSheet in Breeze NG)
            const QColor inkColor = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
            const QColor lifted(qRound(window.red() * 0.9 + inkColor.red() * 0.1), qRound(window.green() * 0.9 + inkColor.green() * 0.1), qRound(window.blue() * 0.9 + inkColor.blue() * 0.1));
            const QString menuGlass = QStringLiteral("rgba(%1, %2, %3, %4)").arg(lifted.red()).arg(lifted.green()).arg(lifted.blue()).arg(QString::number((menuOpacity >= 100 ? 38 : qMax(menuOpacity, 30)) / 100.0, 'f', 2));
            sheet.replace(menuSheet.name(), menuGlass);
        }
        m_sheetApplying = true; // setting a sheet sends style changes of its own: changeEvent() ignores them
        if (force) {
            setStyleSheet(QString()); // have every child polished again from the palette of now
        }
        if (sheet != styleSheet()) {
            setStyleSheet(sheet);
        }
        m_sheetApplying = false;
    }

    QToolButton *chip(const QString &text, bool checked)
    {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setChecked(checked);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_flow->addWidget(button);
        return button;
    }

    void caption(const QString &text)
    {
        auto *label = new QLabel(text, this);
        label->setEnabled(false); // dimmed, it is only a caption
        label->setContentsMargins(0, 0, 6, 0);
        m_flow->addWidget(label);
    }

public:
    // The filters as the sidebar section shows them: what can be chosen now, with how many items each
    struct FilterEntry {
        QString label;
        int count;
        int id;
        bool active;
        QString icon;
    };
    QList<FilterEntry> filters() const
    {
        QList<FilterEntry> entries;
        if (!m_view || m_total == 0 || m_total > 4000) {
            return entries;
        }
        const QString labels[NgKind::CategoryCount] = {ngTr("Folders", "Cartelle"), ngTr("Images", "Immagini"), ngTr("Documents", "Documenti"), ngTr("Spreadsheets", "Fogli"), ngTr("Archives", "Archivi"), ngTr("Audio and video", "Audio e video"), ngTr("Code and data", "Codice e dati"), ngTr("Packages", "Pacchetti"), ngTr("Other", "Altro")};
        static const char *const icons[NgKind::CategoryCount] = {"folder", "image-x-generic", "x-office-document", "x-office-spreadsheet", "application-zip", "video-x-generic", "application-json", "application-x-rpm", "unknown"};
        entries.append({ngTr("All", "Tutti"), m_total, All, m_active == All, QStringLiteral("view-grid")});
        const int folders = m_counts[NgKind::Folder];
        if (folders > 0 && folders < m_total) {
            entries.append({ngTr("Files", "File"), m_total - folders, FilesOnly, m_active == FilesOnly, QStringLiteral("text-plain")});
        }
        for (int i = 0; i < NgKind::CategoryCount; ++i) {
            if (m_counts[i] > 0) {
                entries.append({labels[i], m_counts[i], i, m_active == i, QString::fromLatin1(icons[i])});
            }
        }
        return entries;
    }
    void setFilter(int id)
    {
        setActive(id);
    }
    std::function<void()> onFiltersChanged;

private:
    // A button that opens a menu: while the menu is open the button loses its own highlight and takes the colour
    // of the menu, so that the two read as one piece (the style joins them, see Breeze NG).
    void menuButton(QToolButton *button)
    {
        QMenu *menu = button->menu();
        if (!menu) {
            return;
        }
        auto repolish = [button](bool open) {
            button->setProperty("menuOpen", open);
            button->style()->unpolish(button);
            button->style()->polish(button);
            button->update();
        };
        connect(menu, &QMenu::aboutToShow, button, [repolish] {
            repolish(true);
        });
        connect(menu, &QMenu::aboutToHide, button, [repolish] {
            repolish(false);
        });
    }

public:
    void setLeadingActions(const QList<QAction *> &actions)
    {
        QLayout *layout = m_leading->layout();
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        // back and forward share one pill
        auto *navPill = new QWidget(m_leading);
        navPill->setObjectName(QStringLiteral("navPill"));
        navPill->setAttribute(Qt::WA_StyledBackground);
        navPill->setFixedHeight(32);
        auto *navLayout = new QHBoxLayout(navPill);
        navLayout->setContentsMargins(3, 3, 3, 3);
        navLayout->setSpacing(0);
        layout->addWidget(navPill);
        layout = navLayout;
        for (QAction *action : actions) {
            if (action->isSeparator()) {
                continue;
            }
            auto *button = new QToolButton(navPill);
            button->setDefaultAction(action);
            button->setAutoRaise(true);
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setIconSize(QSize(20, 20));
            if (action->menu()) {
                button->setPopupMode(QToolButton::DelayedPopup); // back and forward: hold for the history
            }
            layout->addWidget(button);
        }
        m_leading->setVisible(!actions.isEmpty());
        alignLeading();
    }

    // The sorting as a pill when everything fits on one line, else as one button with a menu
    void fitSorting()
    {
        if (!m_sortPill || !m_sortCompact || m_fitting) {
            return;
        }
        // showing or hiding the buttons below resizes this bar, which asked for this again from inside: once at a
        // time (the bar's resizeEvent only schedules it, see there)
        m_fitting = true;
        const auto done = qScopeGuard([this] {
            m_fitting = false;
        });
        const QMargins margins = layout()->contentsMargins();
        int room = width() - margins.left() - margins.right() - 210 - layout()->spacing();
        if (m_leading->isVisible()) {
            room -= m_leading->sizeHint().width() + layout()->spacing();
        }
        // what one line needs: each widget at its width (a fixed-width gap has no size hint: its minimum width counts)
        // and the spacing between them, with a little to spare so that nothing wraps on the edge
        const int spacing = qMax(m_flow->spacing(), 6);
        int needed = 12;
        for (int i = 0; i < m_flow->count(); ++i) {
            QWidget *widget = m_flow->itemAt(i)->widget();
            if (!widget || widget == m_sortCompact) {
                continue;
            }
            needed += qMax(widget->sizeHint().width(), widget->minimumWidth()) + spacing;
        }
        // a margin both ways, so that a width on the edge does not flip between the two forms
        const bool wasCompact = m_sortCompact->isVisible();
        const bool compact = wasCompact ? needed + 30 > room : needed > room;
        // when narrow, the groups also sit closer and the search box is shorter
        for (const QPointer<QWidget> &gap : std::as_const(m_gaps)) {
            if (gap) {
                gap->setFixedWidth(compact ? 6 : 22);
            }
        }
        m_filter->setFixedWidth(compact ? 150 : 210);
        if (m_groups) {
            // narrower still: the groups button keeps only its icon (its label is in the tooltip)
            int compactNeeded = 12;
            for (int i = 0; i < m_flow->count(); ++i) {
                QWidget *widget = m_flow->itemAt(i)->widget();
                if (!widget || widget == m_sortPill || widget == m_sortCaption) {
                    continue;
                }
                const int width = widget == m_groups ? m_groupsTextWidth : m_gaps.contains(widget) ? 6 : qMax(widget->sizeHint().width(), widget->minimumWidth());
                compactNeeded += width + spacing;
            }
            const bool iconOnly = compact && compactNeeded > room + 60;
            m_groups->setToolButtonStyle(iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        }
        m_sortPill->setVisible(!compact);
        if (m_sortCaption) {
            m_sortCaption->setVisible(!compact);
        }
        m_sortCompact->setVisible(compact);
        m_flow->invalidate();
    }

    // the buttons are shorter than the pill of the sorting beside them: centred on the first line of the bar
    void alignLeading()
    {
        int line = 0, button = 0;
        for (int i = 0; i < m_flow->count(); ++i) {
            if (QWidget *widget = m_flow->itemAt(i)->widget()) {
                line = qMax(line, widget->sizeHint().height());
            }
        }
        if (auto *pill = m_leading->findChild<QWidget *>(QStringLiteral("navPill"))) {
            button = pill->height();
        }
        m_leadingLayout->setContentsMargins(0, qMax(0, (line - button) / 2), 4, 0);
    }

private:
    void rebuild()
    {
        while (QLayoutItem *item = m_flow->takeAt(0)) {
            if (item->widget()) {
                item->widget()->hide();
                item->widget()->deleteLater(); // a button may be in the middle of its own click
            }
            delete item;
        }
        if (onFiltersChanged) {
            onFiltersChanged(); // the sidebar section mirrors the filters
        }
        m_sortPill = nullptr;
        m_sortCaption = nullptr;
        m_sortCompact = nullptr;
        m_gaps.clear();
        QTimer::singleShot(0, this, [this] {
            alignLeading();
            fitSorting();
        });
        if (!m_view) {
            m_flow->invalidate();
            return;
        }
        const bool list = m_view->viewMode() == DolphinView::DetailsView;

        // ---- sorting
        caption(i18nc("@action:inmenu View", "Sort By"));
        m_sortCaption = m_flow->itemAt(m_flow->count() - 1)->widget();
        QList<QByteArray> roles = m_view->visibleRoles();
        roles.removeAll("text");
        roles.prepend("text");
        // the usual ones are always there, whatever the view shows (the cards list only the name)
        for (const char *usual : {"modificationtime", "creationtime", "size", "type"}) {
            if (!roles.contains(usual)) {
                roles.append(usual);
            }
        }
        const QList<KFileItemModel::RoleInfo> rolesInfo = KFileItemModel::rolesInformation();
        const QByteArray sortRole = m_view->sortRole();
        // one pill holding every choice, the current one raised inside it (a segmented control)
        auto *pill = new QWidget(this);
        pill->setObjectName(QStringLiteral("sortPill"));
        pill->setAttribute(Qt::WA_StyledBackground);
        auto *segments = new QHBoxLayout(pill);
        segments->setContentsMargins(3, 3, 3, 3);
        segments->setSpacing(1);
        m_flow->addWidget(pill);
        for (const QByteArray &role : std::as_const(roles)) {
            QString text = QString::fromLatin1(role);
            for (const KFileItemModel::RoleInfo &info : rolesInfo) {
                if (info.role == role) {
                    text = info.translation;
                    break;
                }
            }
            QToolButton *button = chip(text, role == sortRole);
            m_flow->removeWidget(button);
            button->setParent(pill);
            segments->addWidget(button);
            if (role == sortRole) {
                button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                const bool ascending = m_view->sortOrder() == Qt::AscendingOrder;
                button->setIcon(QIcon::fromTheme(ascending ? QStringLiteral("view-sort-ascending") : QStringLiteral("view-sort-descending")));
            }
            connect(button, &QToolButton::clicked, this, [this, role] {
                if (m_view->sortRole() == role) {
                    m_view->setSortOrder(m_view->sortOrder() == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder);
                } else {
                    m_view->setSortRole(role);
                }
            });
        }

        m_sortPill = pill;
        {
            // when the window is narrow the pill gives way to one button with the same choices in a menu
            // (see fitSorting); the current one and the direction are in its label
            QString current = QString::fromLatin1(sortRole);
            for (const KFileItemModel::RoleInfo &info : rolesInfo) {
                if (info.role == sortRole) {
                    current = info.translation;
                }
            }
            const bool ascending = m_view->sortOrder() == Qt::AscendingOrder;
            auto *compact = new QToolButton(this);
            compact->setAutoRaise(true);
            compact->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            compact->setIcon(QIcon::fromTheme(ascending ? QStringLiteral("view-sort-ascending") : QStringLiteral("view-sort-descending")));
            compact->setText(ngTr("Sort: %1", "Ordina: %1").arg(current));
            compact->setPopupMode(QToolButton::InstantPopup);
            auto *menu = new QMenu(compact);
            auto *byGroup = new QActionGroup(menu);
            for (const QByteArray &role : std::as_const(roles)) {
                QString text = QString::fromLatin1(role);
                for (const KFileItemModel::RoleInfo &info : rolesInfo) {
                    if (info.role == role) {
                        text = info.translation;
                    }
                }
                QAction *action = menu->addAction(text);
                action->setCheckable(true);
                action->setChecked(role == sortRole);
                byGroup->addAction(action);
                connect(action, &QAction::triggered, this, [this, role] {
                    m_view->setSortRole(role);
                });
            }
            menu->addSeparator();
            auto *orderGroup = new QActionGroup(menu);
            const std::pair<QString, Qt::SortOrder> orders[] = {{ngTr("Ascending", "Crescente"), Qt::AscendingOrder}, {ngTr("Descending", "Decrescente"), Qt::DescendingOrder}};
            for (const auto &[text, order] : orders) {
                QAction *action = menu->addAction(QIcon::fromTheme(order == Qt::AscendingOrder ? QStringLiteral("view-sort-ascending") : QStringLiteral("view-sort-descending")), text);
                action->setCheckable(true);
                action->setChecked(m_view->sortOrder() == order);
                orderGroup->addAction(action);
                const Qt::SortOrder chosen = order;
                connect(action, &QAction::triggered, this, [this, chosen] {
                    m_view->setSortOrder(chosen);
                });
            }
            compact->setMenu(menu);
            menuButton(compact);
            compact->hide(); // until fitSorting() decides
            m_flow->addWidget(compact);
            m_sortCompact = compact;
        }
        {
            // fully rounded ends: the radius is half the height (a larger one would switch the rounding off)
            const QString ink = ngIsDark(palette()) ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
            const int inner = pill->findChild<QToolButton *>() ? pill->findChild<QToolButton *>()->sizeHint().height() : 24;
            pill->setStyleSheet(QStringLiteral("QWidget#sortPill { background: rgba(%1, %4); border: %5; border-radius: %2px; }"
                                               "QToolButton { border-radius: %3px; padding: 3px 11px; }")
                                    .arg(ink)
                                    .arg(inner / 2 + 2) // the pill is the button plus 3 px of margin and the border on each side
                                    .arg(inner / 2 - 1)
                                    .arg(ngAurora() ? QString::number(ngAuroraVeilAlpha(ngIsDark(palette()), true), 'f', 3) : QStringLiteral("0.05"),
                                         ngAurora() ? QStringLiteral("1px solid transparent") : QStringLiteral("1px solid rgba(%1, 0.09)").arg(ink)));
        }

        // ---- the cards view: the button switches it on and off, its menu picks the look
        {
            auto *spacer = new QWidget(this);
            spacer->setFixedWidth(22);
            m_flow->addWidget(spacer);
            m_gaps.append(spacer);
            const bool cardsOn = m_view->viewMode() == DolphinView::CardsView;
            static const char *const looks[] = {"Tessera", "Scheda", "Dossier"};
            auto *cards = new QToolButton(this);
            cards->setAutoRaise(true);
            cards->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            cards->setIcon(QIcon::fromTheme(QStringLiteral("view-grid"), QIcon::fromTheme(QStringLiteral("view-list-icons"))));
            cards->setCheckable(true);
            cards->setChecked(cardsOn);
            cards->setPopupMode(QToolButton::MenuButtonPopup);
            cards->setText(cardsOn ? ngTr("Cards: %1", "Card: %1").arg(QLatin1String(looks[int(ngCardStyle())])) : ngTr("Cards", "Card"));
            cards->setToolTip(cardsOn ? ngTr("A click goes to the next kind of card (Tessera, Scheda, Dossier); the arrow picks one, or another view (list, icons).",
                                             "Un clic passa al tipo di card seguente (Tessera, Scheda, Dossier); la freccia ne sceglie uno, o un'altra vista (lista, icone).")
                                      : ngTr("Cards view: every item is a card with all its details. The arrow picks the kind of card, or another view (list, icons).",
                                             "Vista a card: ogni elemento è una scheda con tutti i dettagli. La freccia sceglie il tipo di card, o un'altra vista (lista, icone)."));
            auto *menu = new QMenu(cards);
            auto *group = new QActionGroup(menu);
            for (int i = 0; i < 3; ++i) {
                QAction *action = menu->addAction(QStringLiteral("%1 · %2").arg(QChar(QLatin1Char('A' + i)), QLatin1String(looks[i])));
                action->setCheckable(true);
                action->setChecked(int(ngCardStyle()) == i);
                group->addAction(action);
                connect(action, &QAction::triggered, this, [this, i] {
                    static const char *const keys[] = {"a", "b", "c"};
                    setNgSetting("CARDS", QLatin1String(keys[i]));
                    ngSettingsInvalidate();
                    if (m_view->viewMode() != DolphinView::CardsView) {
                        m_modeBeforeCards = m_view->viewMode();
                        m_view->setViewMode(DolphinView::CardsView);
                    } else {
                        rebuild();
                    }
                });
            }
            menu->addSeparator();
            auto *fit = new QActionGroup(menu);
            const QString fits[] = {ngTr("Fixed-size cards", "Card di misura fissa"), ngTr("Fill the row", "Riempi la riga")};
            for (int i = 0; i < 2; ++i) {
                QAction *action = menu->addAction(fits[i]);
                action->setCheckable(true);
                action->setChecked(ngCardsStretch() == (i == 1));
                fit->addAction(action);
                connect(action, &QAction::triggered, this, [i] {
                    setNgSetting("CARDS_FIT", i == 1 ? QStringLiteral("stretch") : QString());
                    ngSettingsInvalidate();
                });
            }
            // the other views, one click away from the bar too: the list, the icons as Dolphin NG draws them (tiles)
            // and the classic icons (the icon with its name under it, as in Dolphin)
            menu->addSeparator();
            {
                const struct {
                    QString label;
                    const char *icon;
                    DolphinView::Mode mode;
                    int tiles; // -1: does not matter, 1: with tiles, 0: classic
                } others[] = {{ngTr("List", "Lista"), "view-list-details", DolphinView::DetailsView, -1},
                              {ngTr("Icons", "Icone"), "view-list-icons", DolphinView::IconsView, 1},
                              {ngTr("Classic icons", "Icone classiche"), "view-list-icons", DolphinView::IconsView, 0}};
                auto *views = new QActionGroup(menu);
                for (const auto &other : others) {
                    QAction *action = menu->addAction(QIcon::fromTheme(QLatin1String(other.icon)), other.label);
                    action->setCheckable(true);
                    action->setChecked(m_view->viewMode() == other.mode && (other.tiles < 0 || ngTiles() == (other.tiles == 1)));
                    views->addAction(action);
                    const DolphinView::Mode mode = other.mode;
                    const int tiles = other.tiles;
                    connect(action, &QAction::triggered, this, [this, mode, tiles] {
                        if (tiles >= 0 && ngTiles() != (tiles == 1)) {
                            setNgSetting("TILES", tiles == 1 ? QString() : QStringLiteral("off"));
                            ngSettingsInvalidate();
                        }
                        if (m_view->viewMode() != mode) {
                            m_view->setViewMode(mode);
                        } else {
                            rebuild();
                        }
                    });
                }
            }
            cards->setMenu(menu);
            menuButton(cards);
            // a click: the cards view in the look used last; already there, the next look
            connect(cards, &QToolButton::clicked, this, [this, cards, cardsOn] {
                cards->setChecked(true);
                if (!cardsOn) {
                    m_modeBeforeCards = m_view->viewMode();
                    m_view->setViewMode(DolphinView::CardsView);
                    return;
                }
                static const char *const keys[] = {"a", "b", "c"};
                setNgSetting("CARDS", QLatin1String(keys[(int(ngCardStyle()) + 1) % 3]));
                ngSettingsInvalidate();
                rebuild();
            });
            m_flow->addWidget(cards);
        }

        // ---- filters
        if (m_total == 0 || m_total > 4000) {
            m_flow->invalidate();
            return;
        }
        auto *gap = new QWidget(this);
        gap->setFixedWidth(22);
        m_flow->addWidget(gap);
        m_gaps.append(gap);

        // one button that opens a menu with the filters; its label says which one is active
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(QIcon::fromTheme(QStringLiteral("view-filter")));
        button->setPopupMode(QToolButton::InstantPopup);
        button->setCheckable(true);
        button->setChecked(m_active != All); // highlighted while something is filtered out
        auto *menu = new QMenu(button);
        auto *group = new QActionGroup(menu);
        QString current = ngTr("All", "Tutti");
        auto entry = [&](const QString &label, int number, int what) {
            QAction *action = menu->addAction(QStringLiteral("%1\t%2").arg(label).arg(number));
            action->setCheckable(true);
            action->setChecked(m_active == what);
            group->addAction(action);
            if (m_active == what) {
                current = label;
            }
            connect(action, &QAction::triggered, this, [this, what] {
                setActive(what);
            });
        };
        for (const FilterEntry &filter : filters()) {
            if (filter.id == NgKind::Folder) {
                menu->addSeparator(); // after "Tutti" and "File" come the kinds
            }
            entry(filter.label, filter.count, filter.id);
            menu->actions().constLast()->setIcon(QIcon::fromTheme(filter.icon));
        }
        button->setMenu(menu);
        menuButton(button);
        button->setText(ngTr("Filters: %1", "Filtri: %1").arg(current));
        m_flow->addWidget(button);

        // ---- groups: none, by date (modified or created), by kind, by size (Dolphin's "Group By", at hand)
        {
            auto *groups = new QToolButton(this);
            groups->setAutoRaise(true);
            groups->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            groups->setIcon(QIcon::fromTheme(QStringLiteral("view-group")));
            groups->setPopupMode(QToolButton::InstantPopup);
            const bool grouped = m_view->groupedSorting();
            // an empty raw role: the groups follow whatever the folder is sorted by ("dynamic")
            const QByteArray role = grouped ? m_view->rawGroupRole() : QByteArray();
            const bool dynamicGroups = grouped && role.isEmpty();
            groups->setCheckable(true);
            groups->setChecked(grouped);
            auto *groupMenu = new QMenu(groups);
            auto *groupActions = new QActionGroup(groupMenu);
            QString currentGroup = ngTr("None", "Nessuno");
            const QList<std::tuple<QByteArray, QString, QString>> choices{
                {QByteArray(), ngTr("None", "Nessuno"), QStringLiteral("view-list-details")},
                {QByteArrayLiteral("@sort"), ngTr("Like the sorting (dynamic)", "Come l'ordinamento (dinamici)"), QStringLiteral("view-sort")},
                {QByteArrayLiteral("modificationtime"), ngTr("Date modified", "Data di modifica"), QStringLiteral("view-calendar-day")},
                {QByteArrayLiteral("creationtime"), ngTr("Date created", "Data di creazione"), QStringLiteral("view-calendar")},
                {QByteArrayLiteral("type"), ngTr("Type", "Tipo"), QStringLiteral("view-media-genre")},
                {QByteArrayLiteral("size"), ngTr("Size", "Dimensione"), QStringLiteral("kt-check-data")},
            };
            for (const auto &[choiceRole, label, icon] : choices) {
                QAction *action = groupMenu->addAction(QIcon::fromTheme(icon), label);
                action->setCheckable(true);
                const bool isDynamic = choiceRole == "@sort";
                const bool isCurrent = choiceRole.isEmpty() ? !grouped : isDynamic ? dynamicGroups : (grouped && role == choiceRole);
                action->setChecked(isCurrent);
                groupActions->addAction(action);
                if (isCurrent) {
                    currentGroup = isDynamic ? ngTr("dynamic", "dinamici") : label;
                }
                if (isDynamic) {
                    action->setToolTip(ngTr("The groups follow the sorting: periods by date, letters by name, kinds by type, sizes by size",
                                            "I gruppi seguono l'ordinamento: periodi per data, lettere per nome, tipi per tipo, taglie per dimensione"));
                    groupMenu->addSeparator();
                }
                connect(action, &QAction::triggered, this, [this, choiceRole] {
                    if (choiceRole.isEmpty()) {
                        m_view->setGroupedSorting(false);
                    } else if (choiceRole == "@sort") {
                        // dynamic: no group role of its own, so the groups are made from the sort role, whatever it is
                        m_view->setGroupedSorting(true);
                        m_view->setGroupRole(QByteArray());
                    } else {
                        // the groups come in the order of the sorting: sorted by something else, the same date would
                        // be split all over the folder. Group by a date: newest first; by kind or size: by that
                        m_view->setSortRole(choiceRole);
                        if (choiceRole.endsWith("time")) {
                            m_view->setSortOrder(Qt::DescendingOrder);
                        }
                        m_view->setGroupedSorting(true);
                        m_view->setGroupRole(choiceRole);
                    }
                    QTimer::singleShot(0, this, [this] {
                        rebuild(); // the label says the new choice
                    });
                });
            }
            // folders as items like the files (each in the group of its size, date...), or first as Dolphin keeps them
            groupMenu->addSeparator();
            QAction *foldersWithFiles = groupMenu->addAction(ngTr("Folders with the files", "Cartelle insieme ai file"));
            foldersWithFiles->setCheckable(true);
            foldersWithFiles->setChecked(ngSetting("GROUPS_FOLDERS").toLower() != QLatin1String("first"));
            foldersWithFiles->setEnabled(grouped);
            foldersWithFiles->setToolTip(ngTr("In groups, each folder goes where its own size, date or name puts it, not first",
                                              "Nei gruppi ogni cartella va dove la mettono la sua dimensione, data o nome, non per prima"));
            connect(foldersWithFiles, &QAction::toggled, this, [](bool on) {
                setNgSetting("GROUPS_FOLDERS", on ? QString() : QStringLiteral("first"));
                ngSettingsInvalidate();
            });
            groups->setMenu(groupMenu);
            menuButton(groups);
            groups->setText(ngTr("Groups: %1", "Gruppi: %1").arg(currentGroup));
            groups->setToolTip(groups->text());
            m_flow->addWidget(groups);
            m_groups = groups;
            m_groupsTextWidth = groups->sizeHint().width();
        }

        if (!list) {
            m_flow->invalidate();
            return;
        }
        // row style: cards or open rows; the choice is remembered
        const bool open = ngRowStyle() == NgRowStyle::Open;
        auto *rows = new QToolButton(this);
        rows->setAutoRaise(true);
        rows->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        rows->setIcon(QIcon::fromTheme(open ? QStringLiteral("view-list-text") : QStringLiteral("view-list-details")));
        rows->setText(open ? ngTr("Rows: open", "Righe: aperte") : ngTr("Rows: cards", "Righe: schede"));
        connect(rows, &QToolButton::clicked, this, [this, open] {
            setNgRowStyle(open ? NgRowStyle::Card : NgRowStyle::Open);
            const QList<QGraphicsView *> graphicsViews = m_view->findChildren<QGraphicsView *>();
            for (QGraphicsView *graphicsView : graphicsViews) {
                graphicsView->viewport()->update();
            }
            rebuild();
        });
        m_flow->addWidget(rows);
        m_flow->invalidate();
    }

    // Count the items of each kind. Only while no filter is active: a filtered view no longer lists the rest.
    void count()
    {
        if (m_active != All || !m_view) {
            return;
        }
        const KFileItemList items = m_view->items();
        for (int i = 0; i < NgKind::CategoryCount; ++i) {
            m_counts[i] = 0;
            m_mimeTypes[i].clear();
        }
        m_total = items.count();
        if (m_total <= 4000) { // asking thousands of files for their type would stall the window
            for (const KFileItem &item : items) {
                const NgKind::Category category = ngKindFor(item.text(), item.isDir()).category;
                ++m_counts[category];
                const QString mimeType = item.mimetype();
                if (!m_mimeTypes[category].contains(mimeType)) {
                    m_mimeTypes[category].append(mimeType);
                }
            }
        }
        rebuild();
    }

    void setActive(int what)
    {
        m_active = what;
        QStringList mimeTypes;
        if (what == FilesOnly) {
            for (int i = 0; i < NgKind::CategoryCount; ++i) {
                if (i != NgKind::Folder) {
                    mimeTypes += m_mimeTypes[i];
                }
            }
            mimeTypes.removeDuplicates();
        } else if (what != All) {
            mimeTypes = m_mimeTypes[what];
        }
        m_view->setMimeTypeFilters(mimeTypes);
        rebuild();
    }

    DolphinView *m_view;
    QWidget *m_leading;
    QHBoxLayout *m_leadingLayout = nullptr;
    NgFlowLayout *m_flow;
    QPointer<QWidget> m_sortPill, m_sortCaption, m_sortCompact; // the sorting as a pill, or as one button when narrow
    bool m_fitting = false; // fitSorting() is running
    bool m_fitPending = false; // fitSorting() is scheduled
    QPointer<QToolButton> m_groups; // the groups button (icon only when even the compact bar is too wide)
    int m_groupsTextWidth = 0;
    QList<QPointer<QWidget>> m_gaps; // the spaces between the groups of the bar (narrower when the bar is)
    DolphinView::Mode m_modeBeforeCards = DolphinView::DetailsView; // the cards button goes back to this
    QLineEdit *m_filter;
    int m_active = All;
    int m_total = 0;
    int m_counts[NgKind::CategoryCount] = {};
    QStringList m_mimeTypes[NgKind::CategoryCount];
};

// dolphin-ng: a picture that fits its area, zooms with the mouse wheel around the pointer and pans by dragging
class NgZoomImage : public QWidget
{
public:
    explicit NgZoomImage(QWidget *parent)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        setMinimumSize(200, 120);
    }

    void setPixmap(const QPixmap &pixmap, bool keepView = false)
    {
        stopMovie();
        m_pixmap = pixmap;
        if (!keepView) {
            applyWantedZoom();
        }
        update();
    }

    // a picture that is not the content (the type icon of a file without a preview): never zoomed
    void setPlainPixmap(const QPixmap &pixmap)
    {
        stopMovie();
        m_pixmap = pixmap;
        m_zoom = 1.0;
        m_offset = QPointF();
        update();
    }

    // The zoom chosen on the last item is kept for the next ones (in this session, in every window): who enlarges
    // one PDF to read it wants the next one enlarged too. A page taller than wide starts from its top.
    void applyWantedZoom()
    {
        m_zoom = s_wantedZoom;
        m_offset = QPointF();
        if (m_zoom > 1.0 && !m_pixmap.isNull() && m_pixmap.height() > m_pixmap.width() * 1.15) {
            m_offset = QPointF(0, 1e6);
        }
        clampOffset();
    }

    // an animated picture (GIF, APNG, WebP): plays in a loop, zoom and pan work on the running frames
    void setMovie(const QString &path)
    {
        stopMovie();
        m_movie = new QMovie(path, QByteArray(), this);
        m_movie->setCacheMode(QMovie::CacheAll);
        connect(m_movie, &QMovie::frameChanged, this, [this] {
            m_pixmap = m_movie->currentPixmap();
            update();
        });
        m_movie->start();
        m_pixmap = m_movie->currentPixmap();
        applyWantedZoom();
    }

    void stopMovie()
    {
        if (m_movie) {
            m_movie->stop();
            delete m_movie;
            m_movie = nullptr;
        }
    }

    void zoomBy(qreal factor)
    {
        m_zoom = qBound(1.0, m_zoom * factor, 12.0);
        s_wantedZoom = m_zoom;
        clampOffset();
        update();
    }

    void resetZoom()
    {
        m_zoom = 1.0;
        s_wantedZoom = 1.0;
        m_offset = QPointF();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        if (m_pixmap.isNull()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(target(), m_pixmap, QRectF(m_pixmap.rect()));
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_pixmap.isNull()) {
            return;
        }
        const qreal oldZoom = m_zoom;
        m_zoom = qBound(1.0, m_zoom * std::pow(1.2, event->angleDelta().y() / 120.0), 12.0);
        s_wantedZoom = m_zoom;
        // keep the point under the pointer where it is
        const QPointF fromCentre = event->position() - QPointF(width(), height()) / 2.0;
        m_offset = fromCentre - (fromCentre - m_offset) * (m_zoom / oldZoom);
        clampOffset();
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_dragFrom = event->position();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            m_offset += event->position() - m_dragFrom;
            m_dragFrom = event->position();
            clampOffset();
            update();
        }
    }

private:
    QSizeF fitted() const
    {
        return QSizeF(m_pixmap.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio) * m_zoom;
    }
    QRectF target() const
    {
        const QSizeF s = fitted();
        return QRectF(QPointF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0) + m_offset, s);
    }
    void clampOffset()
    {
        const QSizeF s = fitted();
        const qreal maxX = qMax(0.0, (s.width() - width()) / 2.0);
        const qreal maxY = qMax(0.0, (s.height() - height()) / 2.0);
        m_offset = QPointF(qBound(-maxX, m_offset.x(), maxX), qBound(-maxY, m_offset.y(), maxY));
    }

    QPixmap m_pixmap;
    QMovie *m_movie = nullptr;
    qreal m_zoom = 1.0;
    static inline qreal s_wantedZoom = 1.0; // see applyWantedZoom()
    QPointF m_offset;
    QPointF m_dragFrom;
};

// dolphin-ng: a card with a preview of one item and its main actions, shown over the list right under the item's row.
// Text files show their first lines, folders their first entries, everything else the thumbnail KIO can make.
class NgPreviewCard : public QWidget
{
public:
    NgPreviewCard(DolphinView *view, QWidget *parent)
        : QWidget(parent)
        , m_view(view)
    {
        setMouseTracking(true); // edge cursors
        applyStyle();
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 10);
        layout->setSpacing(8);

        // ---- the picture area, with its overlays (type chip top-left, zoom top-right, video bar at the bottom)
        m_art = new QWidget(this);
        m_art->setObjectName(QStringLiteral("art"));
        m_art->setAutoFillBackground(true);
        m_art->setMinimumHeight(90);
        m_art->setMouseTracking(true);
        m_art->installEventFilter(this);
        auto *artLayout = new QVBoxLayout(m_art);
        artLayout->setContentsMargins(0, 0, 0, 0);
        m_stack = new QStackedLayout();
        artLayout->addLayout(m_stack);
        m_preview = new QLabel(m_art);
        m_preview->setObjectName(QStringLiteral("textPreview"));
        m_preview->setTextFormat(Qt::PlainText);
        m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_preview->setMargin(10);
        m_image = new NgZoomImage(m_art);
        m_stack->addWidget(m_preview);
        m_stack->addWidget(m_image);
        layout->addWidget(m_art, 1);

        m_chip = new QLabel(m_art);
        m_chip->setObjectName(QStringLiteral("chip"));
        m_zoomBar = new QWidget(m_art);
        auto *zoomLayout = new QHBoxLayout(m_zoomBar);
        zoomLayout->setContentsMargins(0, 0, 0, 0);
        zoomLayout->setSpacing(4);
        auto overlayButton = [this, zoomLayout](const QString &text, const QString &tip, std::function<void()> action) {
            auto *button = new QToolButton(m_zoomBar);
            button->setObjectName(QStringLiteral("overlay"));
            button->setText(text);
            button->setToolTip(tip);
            button->setAutoRaise(true);
            connect(button, &QToolButton::clicked, this, action);
            zoomLayout->addWidget(button);
            return button;
        };
        overlayButton(QStringLiteral("−"), ngTr("Zoom out", "Riduci"), [this] {
            m_image->zoomBy(1 / 1.25);
        });
        overlayButton(QStringLiteral("+"), ngTr("Zoom in (also with the mouse wheel)", "Ingrandisci (anche con la rotellina)"), [this] {
            m_image->zoomBy(1.25);
        });
        overlayButton(QStringLiteral("⤢"), ngTr("Fit", "Adatta"), [this] {
            m_image->resetZoom();
        });

        // video: muted while the card is open by hovering, a thin bar on the picture while the mouse is on it
        m_player = new QMediaPlayer(this);
        m_audio = new QAudioOutput(this);
        m_audio->setVolume(0.6);
        m_player->setAudioOutput(m_audio);
        // frames go through a sink into the picture widget (a QVideoWidget is a native window: nothing can be drawn over it)
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (m_kind != Video || !frame.isValid()) {
                return;
            }
            m_image->setPixmap(QPixmap::fromImage(frame.toImage()), true);
            if (!m_mediaSize.isValid() && frame.size().isValid()) {
                m_mediaSize = frame.size();
                updateChip();
                place();
            }
        });
        m_player->setLoops(QMediaPlayer::Infinite);
        m_videoBar = new QWidget(m_art);
        m_videoBar->setObjectName(QStringLiteral("videoBar"));
        auto *barLayout = new QHBoxLayout(m_videoBar);
        barLayout->setContentsMargins(6, 3, 6, 3);
        barLayout->setSpacing(6);
        m_playButton = new QToolButton(m_videoBar);
        m_playButton->setObjectName(QStringLiteral("barButton"));
        m_playButton->setAutoRaise(true);
        m_playButton->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-pause")));
        connect(m_playButton, &QToolButton::clicked, this, [this] {
            if (m_player->playbackState() == QMediaPlayer::PlayingState) {
                m_player->pause();
            } else {
                m_player->play();
            }
        });
        m_position = new QSlider(Qt::Horizontal, m_videoBar);
        m_position->setRange(0, 1000);
        connect(m_position, &QSlider::sliderMoved, this, [this](int value) {
            m_player->setPosition(m_player->duration() * value / 1000);
        });
        m_time = new QLabel(m_videoBar);
        m_muteButton = new QToolButton(m_videoBar);
        m_muteButton->setObjectName(QStringLiteral("barButton"));
        m_muteButton->setAutoRaise(true);
        connect(m_muteButton, &QToolButton::clicked, this, [this] {
            m_audio->setMuted(!m_audio->isMuted());
        });
        barLayout->addWidget(m_playButton);
        barLayout->addWidget(m_position, 1);
        barLayout->addWidget(m_time);
        barLayout->addWidget(m_muteButton);
        connect(m_player, &QMediaPlayer::positionChanged, this, [this] {
            updateVideoBar();
        });
        connect(m_player, &QMediaPlayer::durationChanged, this, [this] {
            updateVideoBar();
            updateChip();
        });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] {
            updateVideoBar();
        });
        connect(m_audio, &QAudioOutput::mutedChanged, this, [this] {
            updateVideoBar();
        });
        connect(m_player, &QMediaPlayer::metaDataChanged, this, [this] {
            updateChip();
        });
        m_videoBar->hide();

        // ---- name, chips, actions
        m_title = new QLabel(this);
        m_title->setObjectName(QStringLiteral("title"));
        layout->addWidget(m_title);
        m_metaRow = new QWidget(this);
        m_metaFlow = new NgFlowLayout(m_metaRow, 6, 6);
        m_metaFlow->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_metaRow);

        // the four actions share one pill, like the sort choices and the actions of a card; "Open" is raised in it
        auto *actionPill = new QWidget(this);
        actionPill->setObjectName(QStringLiteral("actionPill"));
        actionPill->setAttribute(Qt::WA_StyledBackground);
        auto *actions = new QHBoxLayout(actionPill);
        actions->setContentsMargins(3, 3, 3, 3);
        actions->setSpacing(2);
        layout->addWidget(actionPill);
        auto action = [actionPill, this, actions](const QString &text, const QString &icon, bool primary, std::function<void()> run) {
            auto *button = new QPushButton(QIcon::fromTheme(icon), QLatin1Char(' ') + text, actionPill); // the space keeps the icon off the text
            button->setFixedHeight(fontMetrics().height() + 14); // a known height: the rounded ends are half of it
            button->setProperty("kind", primary ? QStringLiteral("primary") : QStringLiteral("plain"));
            button->setCursor(Qt::PointingHandCursor);
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QPushButton::clicked, this, [this, run] {
                hide();
                run();
            });
            actions->addWidget(button, 1);
            return button;
        };
        action(ngTr("Open", "Apri"), QStringLiteral("document-open"), true, [this] {
            Q_EMIT m_view->itemActivated(m_item);
        });
        action(ngTr("Open with", "Apri con"), QStringLiteral("system-run"), false, [this] {
            auto *job = new KIO::ApplicationLauncherJob(); // no application given: asks which one
            job->setUrls({m_item.url()});
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
            job->start();
        });
        action(ngTr("Rename", "Rinomina"), QStringLiteral("edit-rename"), false, [this] {
            m_view->renameSelectedItems(); // the controller selected the item before asking for the preview
        });
        action(ngTr("Path", "Percorso"), QStringLiteral("edit-copy"), false, [this] {
            QGuiApplication::clipboard()->setText(m_item.isLocalFile() ? m_item.localPath() : m_item.url().toDisplayString());
        });

        {
            const QString ink = ngIsDark(palette()) ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
            const int inner = fontMetrics().height() + 14;
            actionPill->setStyleSheet(QStringLiteral("QWidget#actionPill { background: rgba(%1, 0.05); border: 1px solid rgba(%1, 0.09); border-radius: %2px; }"
                                                     "QPushButton { border: 1px solid transparent; border-radius: %3px; padding: 0 10px; background: transparent; font-weight: 500; }"
                                                     "QPushButton:hover { background: rgba(%1, 0.10); }"
                                                     "QPushButton:pressed { background: rgba(%1, 0.16); }"
                                                     "QPushButton[kind=\"primary\"] { background: rgba(%1, 0.15); border-color: rgba(%1, 0.20); }"
                                                     "QPushButton[kind=\"primary\"]:hover { background: rgba(%1, 0.21); }")
                                          .arg(ink)
                                          .arg((inner + 6) / 2 - 1)
                                          .arg(inner / 2 - 1));
        }

        m_divider = new QWidget(this);
        m_divider->setObjectName(QStringLiteral("divider"));
        m_divider->setFixedHeight(1);
        layout->addWidget(m_divider);
        m_typeRow = new QWidget(this);
        m_typeFlow = new NgFlowLayout(m_typeRow, 6, 6);
        m_typeFlow->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_typeRow);

        qApp->installEventFilter(this);
        applyWindowMode();
        connect(NgSettings::instance(), &NgSettings::changed, this, [this] {
            applyWindowMode();
        });
        hide();
    }

    // PREVIEW_FLOAT=1: the card is a frameless window of its own above Dolphin's window, free to open over the
    // side panel or past the window's edge; otherwise it is a child of the view and stays inside it.
    void applyWindowMode()
    {
        const bool floating = ngSetting("PREVIEW_FLOAT") == QLatin1String("1");
        if (floating == isWindow()) {
            return;
        }
        hide();
        if (floating) {
            // a window that never takes the focus: the keyboard stays on the list, the window manager leaves Dolphin active
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus);
            setAttribute(Qt::WA_TranslucentBackground, true);
            setAttribute(Qt::WA_ShowWithoutActivating, true);
        } else {
            setWindowFlags(Qt::Widget);
            setAttribute(Qt::WA_TranslucentBackground, false);
            setAttribute(Qt::WA_ShowWithoutActivating, false);
        }
    }

    // where the card may go: the screen (floating) or the view it belongs to, in the card's own coordinates
    QRect hostArea() const
    {
        if (isWindow()) {
            const QScreen *screen = QGuiApplication::screenAt(m_rowRect.center());
            return (screen ? screen : parentWidget()->screen())->availableGeometry();
        }
        return QRect(QPoint(0, 0), parentWidget()->size());
    }

    QPoint toHost(const QPoint &globalPos) const
    {
        return isWindow() ? globalPos : parentWidget()->mapFromGlobal(globalPos);
    }

    // is the pointer on the card? (a window of its own may not have had its enter event yet: ask the pointer)
    bool pointerOnCard() const
    {
        return isWindow() ? QRect(pos(), size()).contains(QCursor::pos()) : underMouse();
    }

    // Opened by hovering a row (PREVIEW_HOVER): the card goes away when the mouse leaves both the row and the card.
    void showForHover(const KFileItem &item, const QRect &rowRect)
    {
        if (isVisible() && item.url() == m_item.url()) {
            return;
        }
        m_hiddenAt.invalidate();
        showFor(item, rowRect);
        m_hover = true;
        m_audio->setMuted(m_item.mimetype().startsWith(QLatin1String("video/"))); // a video opened by passing by stays quiet
        updateVideoBar();
    }

    void hoverEnded()
    {
        if (!m_hover || !isVisible()) {
            return;
        }
        // give the mouse a moment to reach the card
        QTimer::singleShot(260, this, [this] {
            if (m_hover && isVisible() && !pointerOnCard() && !QApplication::activePopupWidget()) {
                hide();
            }
        });
    }

    void showFor(const KFileItem &item, const QRect &rowRect)
    {
        m_hover = false;
        // a second click on the same preview button closes the card (the press already hid it)
        if (m_hiddenAt.isValid() && m_hiddenAt.elapsed() < 400 && item.url() == m_item.url()) {
            m_hiddenAt.invalidate();
            return;
        }
        m_item = item;
        m_rowRect = rowRect;
        m_mediaSize = QSize();
        m_title->setText(m_title->fontMetrics().elidedText(item.text(), Qt::ElideMiddle, 600));
        m_title->setToolTip(item.text());
        rebuildMeta();
        rebuildTypeButtons();
        fillPreview();
        updateChip();
        m_audio->setMuted(false);
        place();
        placeOverlays(); // the stacked layout raised its current widget over the chip and the zoom buttons
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = ngIsDark(palette());
        const QColor window = palette().color(QPalette::Window);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        // the glass sheet: a little lighter than the window, brighter towards the top, a bright hairline on top
        QLinearGradient sheet(frame.topLeft(), frame.bottomLeft());
        sheet.setColorAt(0.0, dark ? window.lighter(132) : window.darker(102));
        sheet.setColorAt(0.35, dark ? window.lighter(120) : window.darker(104));
        sheet.setColorAt(1.0, dark ? window.lighter(116) : window.darker(105));
        painter.setPen(QPen(dark ? QColor(255, 255, 255, 34) : QColor(0, 0, 0, 40), 1));
        painter.setBrush(sheet);
        painter.drawRoundedRect(frame, 14, 14);
        painter.setPen(QPen(QColor(255, 255, 255, dark ? 70 : 200), 1));
        painter.drawLine(QPointF(frame.left() + 14, frame.top() + 1), QPointF(frame.right() - 14, frame.top() + 1));
        // resize grip: three short diagonal strokes in the bottom-right corner
        QColor grip = palette().color(QPalette::WindowText);
        grip.setAlphaF(0.35);
        painter.setPen(QPen(grip, 1.2));
        const QPointF corner(width() - 5, height() - 5);
        for (int i = 0; i < 3; ++i) {
            const qreal d = 4 + i * 4;
            painter.drawLine(QPointF(corner.x() - d, corner.y()), QPointF(corner.x(), corner.y() - d));
        }
    }

    // ---- resizing by the right / bottom edge or the corner
    Qt::Edges edgesAt(const QPoint &pos) const
    {
        Qt::Edges edges;
        if (pos.x() >= width() - EdgeWidth) {
            edges |= Qt::RightEdge;
        }
        if (pos.y() >= height() - EdgeWidth) {
            edges |= Qt::BottomEdge;
        }
        if (QRect(width() - 18, height() - 18, 18, 18).contains(pos)) {
            edges = Qt::RightEdge | Qt::BottomEdge;
        }
        return edges;
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_resizing = event->button() == Qt::LeftButton ? edgesAt(event->pos()) : Qt::Edges();
        if (m_resizing) {
            m_resizeStart = event->globalPosition().toPoint();
            m_resizeSize = size();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_resizing) {
            const QPoint delta = event->globalPosition().toPoint() - m_resizeStart;
            QSize next = m_resizeSize;
            if (m_resizing & Qt::RightEdge) {
                next.setWidth(m_resizeSize.width() + delta.x());
            }
            if (m_resizing & Qt::BottomEdge) {
                next.setHeight(m_resizeSize.height() + delta.y());
            }
            const QRect area = hostArea();
            next.setWidth(qBound(MinimumWidth, next.width(), qMax(MinimumWidth, area.right() - x() - 4)));
            next.setHeight(qBound(MinimumHeight, next.height(), qMax(MinimumHeight, area.bottom() - y() - 4)));
            resize(next);
            return;
        }
        const Qt::Edges edges = edgesAt(event->pos());
        setCursor(edges == (Qt::RightEdge | Qt::BottomEdge) ? Qt::SizeFDiagCursor
                      : edges & Qt::RightEdge               ? Qt::SizeHorCursor
                      : edges & Qt::BottomEdge              ? Qt::SizeVerCursor
                                                            : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_resizing) {
            m_resizing = Qt::Edges();
            // remembered for the next time: the width always, the height only for cards without a picture to fit
            setNgSetting("PREVIEW_SIZE", QStringLiteral("%1x%2").arg(width()).arg(height()));
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        if (m_hover && isVisible()) {
            // left the card: if the mouse is not back on a row, close; a row re-opens it after its delay
            QTimer::singleShot(120, this, [this] {
                if (m_hover && isVisible() && !pointerOnCard() && !QApplication::activePopupWidget()) {
                    hide(); // (a menu of the card holds the mouse: not a leave)
                }
            });
        }
    }

    void hideEvent(QHideEvent *event) override
    {
        m_hiddenAt.start();
        m_player->stop();
        m_player->setSource(QUrl());
        m_image->stopMovie();
        QWidget::hideEvent(event);
    }

    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::PaletteChange) {
            applyStyle();
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_art) {
            if (event->type() == QEvent::Resize) {
                placeOverlays();
            } else if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
                updateVideoBar();
            }
            return false;
        }
        if (isVisible()) {
            if (event->type() == QEvent::MouseButtonPress) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (QApplication::activePopupWidget()) {
                    // a click in a menu of the card (or any popup) is not a click elsewhere
                } else if (!QRect(mapToGlobal(QPoint(0, 0)), size()).contains(mouse->globalPosition().toPoint())) {
                    close(); // a click anywhere else closes the card; the click still does its job
                }
            } else if (event->type() == QEvent::Wheel) {
                const auto *wheel = static_cast<QWheelEvent *>(event);
                if (!QRect(mapToGlobal(QPoint(0, 0)), size()).contains(wheel->globalPosition().toPoint())) {
                    close(); // scrolling the list closes the card; over the card the wheel zooms the picture
                }
            } else if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                close();
                return true;
            } else if (isWindow() && watched == m_view->window() && (event->type() == QEvent::Move || event->type() == QEvent::Resize || event->type() == QEvent::Hide)) {
                hide(); // the floating card is tied to the row's place on screen
            } else if (isWindow() && event->type() == QEvent::ApplicationDeactivate) {
                hide(); // another application took over
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    enum Kind { Picture, Animation, Video, Audio, Text, Folder, Other };

    Kind kindOf(const KFileItem &item) const
    {
        const QString mime = item.mimetype();
        if (item.isDir()) {
            return Folder;
        }
        if (mime == QLatin1String("image/gif") || mime == QLatin1String("image/apng") || mime == QLatin1String("image/webp")) {
            return Animation;
        }
        if (mime.startsWith(QLatin1String("image/"))) {
            return Picture;
        }
        if (mime.startsWith(QLatin1String("video/"))) {
            return Video;
        }
        if (mime.startsWith(QLatin1String("audio/"))) {
            return Audio;
        }
        const QMimeType mimeType = QMimeDatabase().mimeTypeForName(mime);
        if (mimeType.isValid() && mimeType.inherits(QStringLiteral("text/plain"))) {
            return Text;
        }
        return Other;
    }

    void applyStyle()
    {
        const bool dark = ngIsDark(palette());
        const QString ink = dark ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
        const QString amber = dark ? QStringLiteral("#f1cf9a") : QStringLiteral("#7d4f12");
        const QString amberRgb = dark ? QStringLiteral("217, 163, 90") : QStringLiteral("168, 107, 35");
        setStyleSheet(QStringLiteral(
            "QWidget#art { background: %3; }"
            "QLabel#textPreview { color: %4; background: transparent; }"
            "QLabel#title { font-weight: 700; font-size: 15px; background: transparent; }"
            "QLabel#metaChip { color: rgba(%1, 0.80); padding: 3px 8px; border-radius: 10px; background: rgba(%1, 0.07); border: 1px solid rgba(%1, 0.09); font-size: 12px; }"
            "QLabel#chip { color: white; background: rgba(0, 0, 0, 0.50); border: 1px solid rgba(255, 255, 255, 0.22); border-radius: 10px; padding: 2px 8px; font-size: 11px; font-weight: 600; letter-spacing: 1px; }"
            "QToolButton#overlay { color: white; background: rgba(0, 0, 0, 0.50); border: 1px solid rgba(255, 255, 255, 0.22); border-radius: 8px; min-width: 24px; min-height: 24px; padding: 0; font-size: 14px; font-weight: 600; }"
            "QToolButton#overlay:hover { background: rgba(0, 0, 0, 0.72); }"
            "QWidget#videoBar { background: rgba(0, 0, 0, 0.55); border: 1px solid rgba(255, 255, 255, 0.20); border-radius: 10px; }"
            "QWidget#videoBar QLabel { color: white; font-size: 11px; background: transparent; border: 0; }"
            "QToolButton#barButton { border: 0; background: transparent; padding: 2px; }"
            "QToolButton#barButton:hover { background: rgba(255, 255, 255, 0.18); border-radius: 6px; }"
            "QWidget#videoBar QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: rgba(255, 255, 255, 0.25); }"
            "QWidget#videoBar QSlider::sub-page:horizontal { border-radius: 2px; background: %5; }"
            "QWidget#videoBar QSlider::handle:horizontal { width: 10px; margin: -3px 0; border-radius: 5px; background: white; }"
            "QWidget#divider { background: rgba(%1, 0.10); }"
            "QPushButton { border: 1px solid rgba(%1, 0.12); border-radius: 10px; padding: 7px 8px; background: rgba(%1, 0.07); color: palette(window-text); font-weight: 500; }"
            "QPushButton:hover { background: rgba(%1, 0.13); border-color: rgba(%1, 0.22); }"
            "QPushButton:pressed { background: rgba(%1, 0.18); }"
            "QPushButton[kind=\"primary\"] { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 rgba(%1, 0.20), stop: 1 rgba(%1, 0.09)); border-color: rgba(%1, 0.28); }"
            "QPushButton[kind=\"primary\"]:hover { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 rgba(%1, 0.27), stop: 1 rgba(%1, 0.14)); }"
            "QPushButton[kind=\"type\"] { border-radius: 14px; padding: 5px 12px; color: %2; border: 1px solid rgba(%6, 0.45); background: rgba(%6, 0.09); font-weight: 500; }"
            "QPushButton[kind=\"type\"]:hover { background: rgba(%6, 0.20); border-color: rgba(%6, 0.70); }"
            "QPushButton::menu-indicator { image: none; width: 0; }")
                          .arg(ink, amber, dark ? QStringLiteral("#0f1115") : QStringLiteral("#e9e8e3"), dark ? QStringLiteral("rgba(255, 255, 255, 0.86)") : QStringLiteral("rgba(0, 0, 0, 0.82)"), amber, amberRgb));
    }

    void placeOverlays()
    {
        const int w = m_art->width();
        const int h = m_art->height();
        QPainterPath mask;
        mask.addRoundedRect(QRectF(0, 0, w, h), 10, 10);
        m_art->setMask(QRegion(mask.toFillPolygon().toPolygon()));
        m_chip->adjustSize();
        m_chip->move(10, 10);
        m_zoomBar->adjustSize();
        m_zoomBar->move(w - m_zoomBar->width() - 10, 10);
        m_videoBar->setGeometry(10, h - 10 - 32, w - 20, 32);
        m_chip->raise();
        m_zoomBar->raise();
        m_videoBar->raise();
    }

    void updateVideoBar()
    {
        const bool media = m_kind == Video || m_kind == Audio;
        m_videoBar->setVisible(media && (m_art->underMouse() || m_kind == Audio || m_player->playbackState() != QMediaPlayer::PlayingState));
        if (!media) {
            return;
        }
        m_playButton->setIcon(QIcon::fromTheme(m_player->playbackState() == QMediaPlayer::PlayingState ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start")));
        m_muteButton->setIcon(QIcon::fromTheme(m_audio->isMuted() ? QStringLiteral("audio-volume-muted") : QStringLiteral("audio-volume-high")));
        const qint64 duration = m_player->duration();
        if (duration > 0 && !m_position->isSliderDown()) {
            m_position->setValue(int(m_player->position() * 1000 / duration));
        }
        m_time->setText(QStringLiteral("%1 / %2").arg(clock(m_player->position()), clock(duration)));
    }

    static QString clock(qint64 ms)
    {
        const qint64 seconds = ms / 1000;
        if (seconds >= 3600) {
            return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
        }
        return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }

    // the small label on the picture: type, and the size of a picture or the length of a video
    void updateChip()
    {
        QStringList parts;
        const QString suffix = QFileInfo(m_item.name()).suffix().toUpper();
        parts << (m_item.isDir() ? QStringLiteral("DIR") : suffix.isEmpty() ? QStringLiteral("FILE") : suffix);
        if (m_mediaSize.isValid()) {
            parts << QStringLiteral("%1×%2").arg(m_mediaSize.width()).arg(m_mediaSize.height());
        }
        if ((m_kind == Video || m_kind == Audio) && m_player->duration() > 0) {
            const QSize resolution = m_player->metaData().value(QMediaMetaData::Resolution).toSize();
            if (resolution.isValid() && !m_mediaSize.isValid()) {
                parts << QStringLiteral("%1p").arg(resolution.height());
            }
            parts << clock(m_player->duration());
        }
        m_chip->setText(parts.join(QStringLiteral("  ·  ")));
        m_chip->adjustSize();
    }

    void rebuildMeta()
    {
        while (QLayoutItem *item = m_metaFlow->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        auto chip = [this](const QString &text) {
            auto *label = new QLabel(text, m_metaRow);
            label->setObjectName(QStringLiteral("metaChip"));
            m_metaFlow->addWidget(label);
        };
        chip(m_item.mimeComment());
        if (!m_item.isDir()) {
            chip(KIO::convertSize(m_item.size()));
        }
        const QDateTime modified = m_item.time(KFileItem::ModificationTime);
        if (modified.isValid()) {
            chip(ngTr("Modified %1 · %2", "Modificato %1 · %2").arg(QLocale().toString(modified.date(), QLocale::ShortFormat), ngAgoText(modified)));
        }
    }

    // Runs a command line tool in the folder of the item; the folder view picks the result up by itself.
    void run(const QString &program, const QStringList &arguments)
    {
        if (QStandardPaths::findExecutable(program).isEmpty()) {
            KMessageBox::error(window(), ngTr("This needs the program “%1”, which is not installed.", "Serve il programma «%1», che non è installato.").arg(program));
            return;
        }
        auto *job = new KIO::CommandLauncherJob(program, arguments);
        job->setWorkingDirectory(m_item.url().adjusted(QUrl::RemoveFilename).toLocalFile());
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
        job->start();
    }

    QPushButton *addTypeButton(const QString &text, std::function<void()> action, bool keepOpen = false)
    {
        auto *button = new QPushButton(text, m_typeRow);
        button->setProperty("kind", QStringLiteral("type"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, action, keepOpen] {
            if (!keepOpen) {
                hide();
            }
            action();
        });
        m_typeFlow->addWidget(button);
        return button;
    }

    // a pill that opens a menu of choices; `choices` are (label, arguments for `program`) pairs
    void addTypeMenu(const QString &text, const QString &program, const QList<std::pair<QString, QStringList>> &choices)
    {
        QPushButton *button = addTypeButton(text, [] {}, true);
        auto *menu = new QMenu(button);
        for (const auto &[label, arguments] : choices) {
            menu->addAction(label, this, [this, program, arguments] {
                hide();
                run(program, arguments);
            });
        }
        button->setMenu(menu);
        connect(menu, &QMenu::aboutToHide, this, [this] {
            // menu closed without a choice while the card was opened by hovering: close it unless the mouse is back on it
            QTimer::singleShot(300, this, [this] {
                if (m_hover && isVisible() && !pointerOnCard() && !QApplication::activePopupWidget()) {
                    hide();
                }
            });
        });
    }

    // The pills that make sense for this kind of file (images, archives, PDF, audio, video).
    void rebuildTypeButtons()
    {
        while (QLayoutItem *item = m_typeFlow->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const bool any = m_item.isLocalFile() && !m_item.isDir();
        if (!any) {
            m_divider->hide();
            m_typeRow->hide();
            return;
        }
        const QString path = m_item.localPath();
        const QFileInfo info(path);
        const QString base = info.completeBaseName();
        const QString suffix = info.suffix();
        const QString mime = m_item.mimetype();
        const bool isImage = mime.startsWith(QLatin1String("image/")) && !mime.contains(QLatin1String("svg"));
        static const QSet<QString> archives{QStringLiteral("application/zip"),
                                            QStringLiteral("application/x-tar"),
                                            QStringLiteral("application/x-compressed-tar"),
                                            QStringLiteral("application/x-bzip-compressed-tar"),
                                            QStringLiteral("application/x-xz-compressed-tar"),
                                            QStringLiteral("application/x-zstd-compressed-tar"),
                                            QStringLiteral("application/x-7z-compressed"),
                                            QStringLiteral("application/vnd.rar"),
                                            QStringLiteral("application/x-rar"),
                                            QStringLiteral("application/gzip"),
                                            QStringLiteral("application/x-bzip"),
                                            QStringLiteral("application/x-xz"),
                                            QStringLiteral("application/zstd")};
        if (isImage) {
            addTypeMenu(ngTr("Resize…", "Ridimensiona…"),
                        QStringLiteral("magick"),
                        {{QStringLiteral("75 %"), {path, QStringLiteral("-resize"), QStringLiteral("75%"), QStringLiteral("%1-75.%2").arg(base, suffix)}},
                         {QStringLiteral("50 %"), {path, QStringLiteral("-resize"), QStringLiteral("50%"), QStringLiteral("%1-50.%2").arg(base, suffix)}},
                         {QStringLiteral("25 %"), {path, QStringLiteral("-resize"), QStringLiteral("25%"), QStringLiteral("%1-25.%2").arg(base, suffix)}},
                         {ngTr("Width 1920", "Larghezza 1920"), {path, QStringLiteral("-resize"), QStringLiteral("1920x>"), QStringLiteral("%1-1920.%2").arg(base, suffix)}},
                         {ngTr("Width 1280", "Larghezza 1280"), {path, QStringLiteral("-resize"), QStringLiteral("1280x>"), QStringLiteral("%1-1280.%2").arg(base, suffix)}},
                         {ngTr("Width 800", "Larghezza 800"), {path, QStringLiteral("-resize"), QStringLiteral("800x>"), QStringLiteral("%1-800.%2").arg(base, suffix)}}});
            QList<std::pair<QString, QStringList>> formats;
            for (const char *format : {"png", "jpg", "webp", "avif", "tiff", "bmp"}) {
                if (QString::fromLatin1(format) != suffix.toLower() && !(suffix.toLower() == QLatin1String("jpeg") && QLatin1String(format) == QLatin1String("jpg"))) {
                    formats.append({QString::fromLatin1(format).toUpper(), {path, QStringLiteral("%1.%2").arg(base, QString::fromLatin1(format))}});
                }
            }
            addTypeMenu(ngTr("Convert to…", "Converti in…"), QStringLiteral("magick"), formats);
            if (mime == QLatin1String("image/gif")) {
                addTypeButton(ngTr("To MP4", "In MP4"), [this, path, base] {
                    run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("-movflags"), QStringLiteral("faststart"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-vf"), QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2"), QStringLiteral("%1.mp4").arg(base)});
                });
            }
        } else if (archives.contains(mime)) {
            addTypeButton(ngTr("Extract here into a subfolder", "Estrai qui in sottocartella"), [this, path] {
                if (!QStandardPaths::findExecutable(QStringLiteral("ark")).isEmpty()) {
                    run(QStringLiteral("ark"), {QStringLiteral("--batch"), QStringLiteral("--autosubfolder"), path});
                } else {
                    const QString folder = QFileInfo(path).completeBaseName();
                    QDir(QFileInfo(path).absolutePath()).mkdir(folder);
                    run(QStringLiteral("bsdtar"), {QStringLiteral("-xf"), path, QStringLiteral("-C"), folder});
                }
            });
        } else if (mime == QLatin1String("application/pdf")) {
            addTypeButton(ngTr("Extract the text", "Estrai il testo"), [this, path, base] {
                run(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), path, QStringLiteral("%1.txt").arg(base)});
            });
            addTypeButton(ngTr("Pages to PNG", "Pagine in PNG"), [this, path, base] {
                run(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("150"), path, base});
            });
        } else if (mime.startsWith(QLatin1String("audio/"))) {
            if (suffix.toLower() != QLatin1String("mp3")) {
                addTypeButton(ngTr("To MP3", "In MP3"), [this, path, base] {
                    run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
                });
            }
            addTypeButton(ngTr("To FLAC", "In FLAC"), [this, path, base] {
                run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("%1.flac").arg(base)});
            });
        } else if (mime.startsWith(QLatin1String("video/"))) {
            addTypeButton(ngTr("To MP4 (H.264)", "In MP4 (H.264)"), [this, path, base] {
                run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-crf"), QStringLiteral("23"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("%1.mp4").arg(base)});
            });
            addTypeButton(ngTr("Extract the audio (MP3)", "Estrai l'audio (MP3)"), [this, path, base] {
                run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("-vn"), QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
            });
            addTypeButton(ngTr("Frame to PNG", "Fotogramma in PNG"), [this, path, base] {
                const qint64 at = m_player->position() / 1000;
                run(QStringLiteral("ffmpeg"), {QStringLiteral("-ss"), QString::number(at), QStringLiteral("-i"), path, QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("%1-%2s.png").arg(base).arg(at)});
            }, true);
        }
        const bool some = m_typeFlow->count() > 0;
        m_divider->hide(); // no rule between the two rows of actions
        m_typeRow->setVisible(some);
    }

    // The card takes the shape of what it shows: a picture or video sets the height of its area from its
    // proportions (a landscape picture gets a wider card, a portrait one a taller card); text and folders get a
    // short block of lines; audio only the bar. A width the user dragged it to is kept (PREVIEW_SIZE).
    void place()
    {
        const QRect area = hostArea();
        int width = 440;
        int height = 0; // 0: from the content
        const QStringList saved = ngSetting("PREVIEW_SIZE").split(QLatin1Char('x'));
        const bool userSize = saved.size() == 2 && saved.at(0).toInt() >= MinimumWidth && saved.at(1).toInt() >= MinimumHeight;
        if (userSize) {
            width = saved.at(0).toInt();
        }
        qreal aspect = 0; // width / height of the picture, when known
        if (m_mediaSize.isValid() && m_mediaSize.height() > 0) {
            aspect = qreal(m_mediaSize.width()) / m_mediaSize.height();
            if (!userSize) {
                width = aspect > 1.3 ? 520 : aspect < 0.8 ? 380 : 440;
            }
        } else if (m_kind == Video) {
            aspect = 16.0 / 9.0;
            if (!userSize) {
                width = 520;
            }
        }
        width = qMin(width, qMax(MinimumWidth, area.width() - 8));
        const int artWidth = width - 16;
        int artHeight;
        switch (m_kind) {
        case Picture:
        case Animation:
        case Video:
            artHeight = aspect > 0 ? qRound(artWidth / aspect) : qRound(artWidth * 0.66);
            artHeight = qBound(140, artHeight, qRound(artWidth * 1.4));
            break;
        case Audio:
            artHeight = 52;
            break;
        case Text:
        case Folder:
            artHeight = 170;
            break;
        default:
            artHeight = 150;
            break;
        }
        if (userSize && m_kind != Picture && m_kind != Animation && m_kind != Video) {
            height = saved.at(1).toInt();
        } else {
            // the rest of the card below the picture
            m_art->setFixedHeight(artHeight);
            height = sizeHint().height();
            m_art->setMinimumHeight(90);
            m_art->setMaximumHeight(QWIDGETSIZE_MAX);
        }
        height = qMin(qMax(height, MinimumHeight), qMax(MinimumHeight, area.height() - 8));

        const QPoint below = toHost(m_rowRect.bottomLeft());
        int y = below.y() + 2;
        if (y + height > area.bottom() - 6) {
            y = toHost(m_rowRect.topLeft()).y() - height - 2; // no room below: open above the row
        }
        // rows: the eye is at the right end of the row, so the card hangs from that end, under the eye
        int x = toHost(m_rowRect.bottomRight()).x() - width - 3;
        if (m_view->viewMode() == DolphinView::CardsView) {
            // cards: the popup opens at the pointer (the eye is in a corner of the card, far from its bottom left),
            // below and to the right of it, or on the other side where there is no room
            const QPoint pointer = toHost(QCursor::pos());
            x = pointer.x() + 14;
            if (x + width > area.right() - 4) {
                x = pointer.x() - 14 - width;
            }
            y = pointer.y() + 14;
            if (y + height > area.bottom() - 6) {
                y = pointer.y() - 14 - height;
            }
        }
        x = qBound(area.left() + 4, x, qMax(area.left() + 4, area.right() - width - 4));
        setGeometry(x, qMax(area.top() + 4, y), width, height);
    }

    void fillPreview()
    {
        m_kind = kindOf(m_item);
        m_stack->setCurrentWidget(m_preview);
        m_preview->setPixmap(QPixmap());
        m_preview->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_preview->setFont(font());
        m_zoomBar->setVisible(m_kind == Picture || m_kind == Animation);
        m_player->stop();
        m_player->setSource(QUrl());
        const QString path = m_item.localPath();

        if (m_kind == Folder && !path.isEmpty()) {
            const QStringList entries = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
            QStringList lines = entries.mid(0, 9);
            if (entries.size() > lines.size()) {
                lines.append(ngTr("… %1 more", "… altri %1").arg(entries.size() - lines.size()));
            }
            m_preview->setText(lines.isEmpty() ? ngTr("(empty folder)", "(cartella vuota)") : lines.join(QLatin1Char('\n')));
            updateVideoBar();
            return;
        }

        if (m_kind == Text && !path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QStringList lines = QString::fromUtf8(file.read(8000)).split(QLatin1Char('\n')).mid(0, 9);
                for (QString &line : lines) {
                    line = line.left(140);
                }
                m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
                m_preview->setText(lines.join(QLatin1Char('\n')));
                updateVideoBar();
                return;
            }
        }

        if ((m_kind == Video || m_kind == Audio) && !path.isEmpty()) {
            m_stack->setCurrentWidget(m_image);
            m_image->setPlainPixmap(m_kind == Audio ? QIcon::fromTheme(m_item.iconName()).pixmap(40, 40) : QPixmap());
            m_player->setSource(QUrl::fromLocalFile(path));
            m_player->play();
            updateVideoBar();
            return;
        }

        // everything else: the type icon at once, then a thumbnail from KIO, then (pictures only) the picture itself,
        // so that zooming in shows real detail
        m_stack->setCurrentWidget(m_image);
        {
            // the icon keeps its size in the middle of the frame: the view fits what it is given to the frame, and
            // a bare 96 px icon came out several times larger and blurred
            QPixmap canvas(640, 420);
            canvas.fill(Qt::transparent);
            QPainter iconPainter(&canvas);
            QIcon::fromTheme(m_item.iconName()).paint(&iconPainter, QRect((canvas.width() - 144) / 2, (canvas.height() - 144) / 2, 144, 144));
            iconPainter.end();
            m_image->setPlainPixmap(canvas);
        }
        updateVideoBar();
        const QUrl url = m_item.url();
        if (m_kind == Animation && !path.isEmpty()) {
            QImageReader reader(path);
            m_mediaSize = reader.size();
            m_image->setMovie(path);
            return;
        }
        if (m_kind == Picture && !path.isEmpty()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            m_mediaSize = reader.size(); // only the header is read
            if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
                m_mediaSize.transpose();
            }
        }
        KIO::PreviewJob *job = KIO::filePreview(KFileItemList{m_item}, QSize(1024, 640));
        job->setIgnoreMaximumSize(m_item.isLocalFile());
        connect(job, &KIO::PreviewJob::gotPreview, this, [this, url](const KFileItem &, const QPixmap &pixmap) {
            if (isVisible() && m_item.url() == url && !pixmap.isNull() && !m_fullPicture) {
                m_image->setPixmap(pixmap);
            }
        });
        m_fullPicture = false;
        if (!path.isEmpty() && m_item.mimetype() == QLatin1String("application/pdf")) {
            // a PDF thumbnail is too coarse to zoom into: render the first page properly with pdftoppm (poppler-utils)
            static const QString pdftoppm = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
            if (!pdftoppm.isEmpty()) {
                auto *dir = new QTemporaryDir();
                auto *process = new QProcess(this);
                const QString base = dir->filePath(QStringLiteral("page"));
                connect(process, &QProcess::finished, this, [this, url, process, dir, base](int exitCode) {
                    if (exitCode == 0 && isVisible() && m_item.url() == url) {
                        const QPixmap page(base + QStringLiteral(".png"));
                        if (!page.isNull()) {
                            m_fullPicture = true;
                            m_image->setPixmap(page, true);
                        }
                    }
                    process->deleteLater();
                    delete dir;
                });
                process->start(pdftoppm, {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("200"), QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"), QStringLiteral("1"), QStringLiteral("-singlefile"), path, base});
            }
        }
        if (m_kind == Picture && !path.isEmpty() && m_item.size() < 80 * 1024 * 1024) {
            QTimer::singleShot(150, this, [this, url, path] {
                if (!isVisible() || m_item.url() != url) {
                    return;
                }
                QImageReader reader(path);
                reader.setAutoTransform(true);
                const QSize original = reader.size();
                if (original.isValid() && qMax(original.width(), original.height()) > 3200) {
                    reader.setScaledSize(original.scaled(3200, 3200, Qt::KeepAspectRatio)); // enough to zoom, without eating memory
                }
                const QImage image = reader.read();
                if (!image.isNull()) {
                    m_fullPicture = true;
                    m_image->setPixmap(QPixmap::fromImage(image), true);
                }
            });
        }
    }

    DolphinView *m_view;
    KFileItem m_item;
    QRect m_rowRect;
    Kind m_kind = Other;
    QSize m_mediaSize;
    QWidget *m_art;
    QLabel *m_preview;
    NgZoomImage *m_image;
    QVideoSink *m_sink;
    QStackedLayout *m_stack;
    QLabel *m_chip;
    QWidget *m_zoomBar;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QWidget *m_videoBar;
    QToolButton *m_playButton;
    QSlider *m_position;
    QLabel *m_time;
    QToolButton *m_muteButton;
    bool m_fullPicture = false;
    QLabel *m_title;
    QWidget *m_metaRow;
    NgFlowLayout *m_metaFlow;
    QWidget *m_divider;
    QWidget *m_typeRow;
    NgFlowLayout *m_typeFlow;
    static constexpr int MinimumWidth = 320;
    static constexpr int MinimumHeight = 260;
    static constexpr int EdgeWidth = 8;
    Qt::Edges m_resizing;
    QPoint m_resizeStart;
    QSize m_resizeSize;
    QElapsedTimer m_hiddenAt;
    bool m_hover = false; // opened by resting the mouse on the row
};

DolphinViewContainer::DolphinViewContainer(const QUrl &url, QWidget *parent)
    : QWidget(parent)
    , m_topLayout(nullptr)
    , m_urlNavigator{new DolphinUrlNavigator(url)}
    , m_urlNavigatorConnected{nullptr}
    , m_searchBar(nullptr)
    , m_searchModeEnabled(false)
    , m_adminBar{nullptr}
    , m_authorizeToEnterFolderAction{nullptr}
    , m_createFolderAction(nullptr)
    , m_messageWidget(nullptr)
    , m_selectionModeTopBar{nullptr}
    , m_view(nullptr)
    , m_filterBar(nullptr)
    , m_selectionModeBottomBar{nullptr}
    , m_statusBar(nullptr)
    , m_statusBarTimer(nullptr)
    , m_statusBarTimestamp()
    , m_grabFocusOnUrlChange{true}
{
    hide();

    m_topLayout = new QGridLayout(this);
    m_topLayout->setSpacing(0);
    m_topLayout->setContentsMargins(0, 0, 0, 0);

    m_messageWidget = new KMessageWidget(this);
    m_messageWidget->setCloseButtonVisible(true);
    m_messageWidget->setPosition(KMessageWidget::Header);
    m_messageWidget->hide();

#if !defined(Q_OS_WIN) && !defined(Q_OS_HAIKU)
    if (getuid() == 0) {
        // We must be logged in as the root user; show a big scary warning
        showMessage(i18n("Running Dolphin as root can be dangerous. Please be careful."), KMessageWidget::Warning);
    }
#endif

    // Initialize filter bar
    m_filterBar = new FilterBar(this);
    m_filterBar->setVisible(GeneralSettings::filterBar(), WithoutAnimation);

    connect(m_filterBar, &FilterBar::filterChanged, this, &DolphinViewContainer::setNameFilter);
    connect(m_filterBar, &FilterBar::filterModeChanged, this, &DolphinViewContainer::setFilterMode);
    connect(m_filterBar, &FilterBar::caseSensitiveChanged, this, &DolphinViewContainer::setFilterCaseSensitive);
    connect(m_filterBar, &FilterBar::closeRequest, this, &DolphinViewContainer::closeFilterBar);
    connect(m_filterBar, &FilterBar::focusViewRequest, this, &DolphinViewContainer::requestFocus);

    // Initialize the main view
    m_view = new DolphinView(url, this);
    connect(m_view, &DolphinView::urlChanged, m_filterBar, &FilterBar::clearIfUnlocked);
    connect(m_view, &DolphinView::urlChanged, m_messageWidget, &KMessageWidget::hide);
    // m_urlNavigator stays in sync with m_view's location changes and
    // keeps track of them so going back and forth in the history works.
    connect(m_view, &DolphinView::urlChanged, m_urlNavigator.get(), &DolphinUrlNavigator::setLocationUrl);
    connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlChanged, this, &DolphinViewContainer::slotUrlNavigatorLocationChanged);
    connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlAboutToBeChanged, this, &DolphinViewContainer::slotUrlNavigatorLocationAboutToBeChanged);
    connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlSelectionRequested, this, &DolphinViewContainer::slotUrlSelectionRequested);
    connect(m_view, &DolphinView::writeStateChanged, this, &DolphinViewContainer::writeStateChanged);
    connect(m_view, &DolphinView::requestItemInfo, this, &DolphinViewContainer::showItemInfo);
    connect(m_view, &DolphinView::itemActivated, this, &DolphinViewContainer::slotItemActivated);
    connect(m_view, &DolphinView::fileMiddleClickActivated, this, &DolphinViewContainer::slotfileMiddleClickActivated);
    connect(m_view, &DolphinView::itemsActivated, this, &DolphinViewContainer::slotItemsActivated);
    connect(m_view, &DolphinView::redirection, this, &DolphinViewContainer::redirect);
    connect(m_view, &DolphinView::directoryLoadingStarted, this, &DolphinViewContainer::slotDirectoryLoadingStarted);
    connect(m_view, &DolphinView::directoryLoadingCompleted, this, &DolphinViewContainer::slotDirectoryLoadingCompleted);
    connect(m_view, &DolphinView::directoryLoadingCanceled, this, &DolphinViewContainer::slotDirectoryLoadingCanceled);
    connect(m_view, &DolphinView::itemCountChanged, this, &DolphinViewContainer::delayedStatusBarUpdate);
    connect(m_view, &DolphinView::selectionChanged, this, &DolphinViewContainer::delayedStatusBarUpdate);
    connect(m_view, &DolphinView::errorMessage, this, &DolphinViewContainer::slotErrorMessageFromView);
    connect(m_view, &DolphinView::urlIsFileError, this, &DolphinViewContainer::slotUrlIsFileError);
    connect(m_view, &DolphinView::activated, this, &DolphinViewContainer::activate);
    connect(m_view, &DolphinView::hiddenFilesShownChanged, this, &DolphinViewContainer::slotHiddenFilesShownChanged);
    connect(m_view, &DolphinView::sortHiddenLastChanged, this, &DolphinViewContainer::slotSortHiddenLastChanged);
    connect(m_view, &DolphinView::currentDirectoryRemoved, this, &DolphinViewContainer::slotCurrentDirectoryRemoved);

    // Initialize status bar
    m_statusBar = new DolphinStatusBar(this);
    m_statusBar->setUrl(m_view->url());
    m_statusBar->setZoomLevel(m_view->zoomLevel());
    connect(m_view, &DolphinView::urlChanged, m_statusBar, &DolphinStatusBar::setUrl);
    connect(m_view, &DolphinView::zoomLevelChanged, m_statusBar, &DolphinStatusBar::setZoomLevel);
    connect(m_view, &DolphinView::infoMessage, m_statusBar, &DolphinStatusBar::setTemporaryRichText);
    connect(m_view, &DolphinView::operationCompletedMessage, m_statusBar, &DolphinStatusBar::setTemporaryRichText);
    connect(m_view, &DolphinView::showTypeAheadFeedback, m_statusBar, &DolphinStatusBar::setTemporaryRichText);
    connect(m_view, &DolphinView::statusBarTextChanged, m_statusBar, &DolphinStatusBar::setDefaultText);
    connect(m_view, &DolphinView::directoryLoadingProgress, m_statusBar, [this](int percent) {
        m_statusBar->showProgress(i18nc("@info:progress", "Loading folder…"), percent);
    });
    connect(m_view, &DolphinView::directorySortingProgress, m_statusBar, [this](int percent) {
        m_statusBar->showProgress(i18nc("@info:progress", "Sorting…"), percent);
    });
    connect(m_statusBar, &DolphinStatusBar::stopPressed, this, &DolphinViewContainer::stopDirectoryLoading);
    connect(m_statusBar, &DolphinStatusBar::zoomLevelChanged, this, &DolphinViewContainer::slotStatusBarZoomLevelChanged);
    connect(m_statusBar, &DolphinStatusBar::showMessage, this, [this](const QString &message, KMessageWidget::MessageType messageType) {
        showMessage(message, messageType);
    });
    connect(m_statusBar, &DolphinStatusBar::widthUpdated, this, &DolphinViewContainer::updateStatusBarGeometry);
    connect(m_statusBar, &DolphinStatusBar::urlChanged, this, &DolphinViewContainer::updateStatusBar);
    connect(this, &DolphinViewContainer::showFilterBarChanged, this, &DolphinViewContainer::updateStatusBar);

    m_statusBarTimer = new QTimer(this);
    m_statusBarTimer->setSingleShot(true);
    m_statusBarTimer->setInterval(300);
    connect(m_statusBarTimer, &QTimer::timeout, this, &DolphinViewContainer::updateStatusBar);

    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    connect(undoManager, &KIO::FileUndoManager::jobRecordingFinished, this, &DolphinViewContainer::delayedStatusBarUpdate);

    m_topLayout->addWidget(m_messageWidget, positionFor.messageWidget, 0);
    {
        // dolphin-ng: the preview card (the sort/filter bar is the window's, above the tabs: ngCreateSortBar)
        // It is made the first time it is asked for, not with the view: its media player sets up the video
        // decoders of the system when it is created, and that was about a third of the time a window took to open.
        auto holder = std::make_shared<QPointer<NgPreviewCard>>();
        auto card = [this, holder]() -> NgPreviewCard * {
            if (!*holder) {
                *holder = new NgPreviewCard(m_view, this);
            }
            return holder->data();
        };
        connect(m_view, &DolphinView::ngPreviewRequested, this, [this, holder, card](const KFileItem &item, const QRect &rowRect) {
            // in the cards view the eye of a picture opens the lightbox: the picture alone, large
            if (m_view->viewMode() == DolphinView::CardsView && !item.isDir() && item.isLocalFile() && item.mimetype().startsWith(QLatin1String("image/"))
                && ngSetting("CARDS_LIGHTBOX").toLower() != QLatin1String("off")) {
                if (*holder) {
                    (*holder)->hide();
                }
                ngOpenLightbox(m_view, item);
                return;
            }
            card()->showFor(item, rowRect);
        });
        connect(m_view, &DolphinView::ngHoverPreviewRequested, this, [card](const KFileItem &item, const QRect &rowRect) {
            card()->showForHover(item, rowRect);
        });
        connect(m_view, &DolphinView::ngHoverPreviewEnded, this, [holder] {
            if (*holder) {
                (*holder)->hoverEnded();
            }
        });
        connect(m_view, &DolphinView::urlChanged, this, [holder] {
            if (*holder) {
                (*holder)->hide();
            }
        });
        auto *quickView = new NgQuickView(this);
        connect(m_view, &DolphinView::ngQuickViewRequested, quickView, [quickView, holder](const KFileItem &item, const QRect &itemRect) {
            if (!*holder || !(*holder)->isVisible()) {
                quickView->showFor(item, itemRect);
            }
        });
        connect(m_view, &DolphinView::ngQuickViewEnded, quickView, [quickView] {
            quickView->end();
        });
        connect(m_view, &DolphinView::urlChanged, quickView, [quickView] {
            quickView->end();
        });
    }
    m_topLayout->addWidget(m_view, positionFor.view, 0);
    m_topLayout->addWidget(m_filterBar, positionFor.filterBar, 0);
    if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::FullWidth) {
        m_topLayout->addWidget(m_statusBar, positionFor.statusBar, 0);
    }
    connect(m_statusBar, &DolphinStatusBar::modeUpdated, this, [this]() {
        const bool statusBarInLayout = m_topLayout->itemAtPosition(positionFor.statusBar, 0);
        if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::FullWidth) {
            if (!statusBarInLayout) {
                m_topLayout->addWidget(m_statusBar, positionFor.statusBar, 0);
                m_statusBar->setUrl(m_view->url());
            }
        } else {
            if (statusBarInLayout) {
                m_topLayout->removeWidget(m_statusBar);
            }
        }
        updateStatusBarGeometry();
    });
    m_statusBar->setHidden(false);

    setSearchBarVisible(isSearchUrl(url));

    // Update view as the ContentDisplaySettings change
    // this happens here and not in DolphinView as DolphinviewContainer and DolphinView are not in the same build target ATM
    connect(ContentDisplaySettings::self(), &KCoreConfigSkeleton::configChanged, m_view, &DolphinView::reload);

    KFilePlacesModel *placesModel = DolphinPlacesModelSingleton::instance().placesModel();
    connect(placesModel, &KFilePlacesModel::dataChanged, this, &DolphinViewContainer::slotPlacesModelChanged);
    connect(placesModel, &KFilePlacesModel::rowsInserted, this, &DolphinViewContainer::slotPlacesModelChanged);
    connect(placesModel, &KFilePlacesModel::rowsRemoved, this, &DolphinViewContainer::slotPlacesModelChanged);

    QApplication::instance()->installEventFilter(this);

    // Update the view with the current state of the filter bar (from the state config)
    m_view->setFilterMode(m_filterBar->filterMode());
    m_view->setFilterCaseSensitive(m_filterBar->isCaseSensitive());
}

DolphinViewContainer::~DolphinViewContainer() = default;

QUrl DolphinViewContainer::url() const
{
    return m_view->url();
}

KFileItem DolphinViewContainer::rootItem() const
{
    return m_view->rootItem();
}

void DolphinViewContainer::setActive(bool active)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setActive(active);
    }
    m_view->setActive(active);
}

bool DolphinViewContainer::isActive() const
{
    return m_view->isActive();
}

void DolphinViewContainer::setGrabFocusOnUrlChange(bool grabFocus)
{
    m_grabFocusOnUrlChange = grabFocus;
}

const DolphinUrlNavigator *DolphinViewContainer::urlNavigator() const
{
    return m_urlNavigatorConnected;
}

DolphinUrlNavigator *DolphinViewContainer::urlNavigator()
{
    return m_urlNavigatorConnected;
}

const DolphinUrlNavigator *DolphinViewContainer::urlNavigatorInternalWithHistory() const
{
    return m_urlNavigator.get();
}

DolphinUrlNavigator *DolphinViewContainer::urlNavigatorInternalWithHistory()
{
    return m_urlNavigator.get();
}

const DolphinView *DolphinViewContainer::view() const
{
    return m_view;
}

DolphinView *DolphinViewContainer::view()
{
    return m_view;
}

void DolphinViewContainer::connectUrlNavigator(DolphinUrlNavigator *urlNavigator)
{
    Q_ASSERT(urlNavigator);
    Q_ASSERT(!m_urlNavigatorConnected);
    Q_ASSERT(m_urlNavigator.get() != urlNavigator);
    Q_ASSERT(m_view);

    urlNavigator->setLocationUrl(m_view->url());
    urlNavigator->setShowHiddenFolders(m_view->hiddenFilesShown());
    urlNavigator->setSortHiddenFoldersLast(m_view->sortHiddenLast());
    if (m_urlNavigatorVisualState) {
        urlNavigator->setVisualState(*m_urlNavigatorVisualState.get());
        m_urlNavigatorVisualState.reset();
    }
    urlNavigator->setActive(isActive());

    // Url changes are still done via m_urlNavigator.
    connect(urlNavigator, &DolphinUrlNavigator::urlChanged, m_urlNavigator.get(), &DolphinUrlNavigator::setLocationUrl);
    connect(urlNavigator, &DolphinUrlNavigator::urlsDropped, this, [=, this](const QUrl &destination, QDropEvent *event) {
        m_view->dropUrls(destination, event, urlNavigator->dropWidget());
    });
    // Aside from these, only visual things need to be connected.
    connect(m_view, &DolphinView::urlChanged, urlNavigator, &DolphinUrlNavigator::setLocationUrl);
    connect(urlNavigator, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);
    connect(urlNavigator, &DolphinUrlNavigator::requestToLoseFocus, m_view, [this]() {
        m_view->setFocus();
    });

    urlNavigator->setReadOnlyBadgeVisible(rootItem().isLocalFile() && !rootItem().isWritable());

    m_urlNavigatorConnected = urlNavigator;
}

void DolphinViewContainer::disconnectUrlNavigator()
{
    if (!m_urlNavigatorConnected) {
        return;
    }

    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::urlChanged, m_urlNavigator.get(), &DolphinUrlNavigator::setLocationUrl);
    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::urlsDropped, this, nullptr);
    disconnect(m_view, &DolphinView::urlChanged, m_urlNavigatorConnected, &DolphinUrlNavigator::setLocationUrl);
    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);
    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::requestToLoseFocus, m_view, nullptr);

    m_urlNavigatorVisualState = m_urlNavigatorConnected->visualState();
    m_urlNavigatorConnected = nullptr;
}

void DolphinViewContainer::setSearchBarVisible(bool visible)
{
    if (!visible) {
        if (isSearchBarVisible()) {
            m_searchBar->setVisible(false, WithAnimation, Search::Bar::HideBehavior::KeepCurrentUrl);
        }
        return;
    }

    if (!m_searchBar) {
        m_searchBar = new Search::Bar(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), QUrl{} /** will be set below. */), this);
        connect(m_searchBar, &Search::Bar::urlChangeRequested, this, [this](const QUrl &url) {
            m_view->setViewPropertiesContext(isSearchUrl(url) ? QStringLiteral("search") : QString());
            setGrabFocusOnUrlChange(false); // Prevent loss of focus while typing or refining a search.
            setUrl(url);
            setGrabFocusOnUrlChange(true);
        });
        connect(m_searchBar, &Search::Bar::focusViewRequest, this, &DolphinViewContainer::requestFocus);
        connect(m_searchBar, &Search::Bar::showMessage, this, [this](const QString &message, KMessageWidget::MessageType messageType) {
            showMessage(message, messageType);
        });
        connect(m_searchBar,
                &Search::Bar::showInstallationProgress,
                m_statusBar,
                [this](const QString &currentlyRunningTaskTitle, int installationProgressPercent) {
                    m_statusBar->showProgress(currentlyRunningTaskTitle, installationProgressPercent, DolphinStatusBar::CancelLoading::Disallowed);
                });
        connect(m_searchBar, &Search::Bar::visibilityChanged, this, &DolphinViewContainer::searchBarVisibilityChanged);
        m_topLayout->addWidget(m_searchBar, positionFor.searchBar, 0);
    }

    m_searchBar->setVisible(true, WithAnimation);

    // The Search::Bar has been set visible but its state does not yet match with this view container or view.
    // The view might for example already be searching because it was opened with a search URL. The Search::Bar needs to be updated to show the parameters of
    // that search. And even if there is no search URL loaded in the view currently, we still need to figure out where the Search::Bar should be searching if
    // the user starts a search from there. Let's figure out the search location in this method and let the DolphinQuery constructor figure out the rest from
    // the current m_urlNavigator->locationUrl().
    for (int i = m_urlNavigator->historyIndex(); i < m_urlNavigator->historySize(); i++) {
        QUrl url = m_urlNavigator->locationUrl(i);
        if (isSearchUrl(url)) {
            // The previous location was a search URL. Try to see if that search URL has a valid search path so we keep searching in the same location.
            const auto searchPath = Search::DolphinQuery(url, QUrl{}).searchPath(); // DolphinQuery is great at extracting the search path from a search URL.
            if (searchPath.isValid()) {
                m_searchBar->updateStateToMatch(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), searchPath));
                return;
            }
        } else if (url.scheme() == QLatin1String("tags")) {
            continue; // We avoid setting a tags url as the backup search path because a DolphinQuery constructed from a tags url will already search tagged
                      // items everywhere.
        } else {
            m_searchBar->updateStateToMatch(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), url));
            return;
        }
    }
    // We could not find any URL fit for searching in the history. This might happen because this view only ever loaded a search which searches "Everywhere"
    // and therefore there is no specific search path to choose from. But the Search::Bar *needs* to know a search path because the user might switch from
    // searching "Everywhere" to "Here" and it is everybody's guess what "Here" is supposed to mean in that context… We'll simply fall back to the user's home
    // path for lack of a better option.
    m_searchBar->updateStateToMatch(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), QUrl::fromUserInput(QDir::homePath())));
}

bool DolphinViewContainer::isSearchBarVisible() const
{
    return m_searchBar && m_searchBar->isVisible() && m_searchBar->isEnabled();
}

void DolphinViewContainer::setFocusToSearchBar()
{
    Q_ASSERT(isSearchBarVisible());
    m_searchBar->selectAll();
}

void DolphinViewContainer::setSelectionModeEnabled(bool enabled, KActionCollection *actionCollection, SelectionMode::BottomBar::Contents bottomBarContents)
{
    const bool wasEnabled = m_view->selectionMode();
    m_view->setSelectionModeEnabled(enabled);

    if (!enabled) {
        if (!wasEnabled) {
            return; // nothing to do here
        }
        Q_ASSERT(m_selectionModeTopBar); // there is no point in disabling selectionMode when it wasn't even enabled once.
        Q_ASSERT(m_selectionModeBottomBar);
        m_selectionModeTopBar->setVisible(false, WithAnimation);
        m_selectionModeBottomBar->setVisible(false, WithAnimation);
        Q_EMIT selectionModeChanged(false);

        if (!QApplication::focusWidget() || m_selectionModeTopBar->isAncestorOf(QApplication::focusWidget())
            || m_selectionModeBottomBar->isAncestorOf(QApplication::focusWidget())) {
            m_view->setFocus();
        }
        return;
    }

    if (!m_selectionModeTopBar) {
        // Changing the location will disable selection mode.
        connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlChanged, this, [this]() {
            setSelectionModeEnabled(false);
        });

        m_selectionModeTopBar = new SelectionMode::TopBar(this); // will be created hidden
        connect(m_selectionModeTopBar, &SelectionMode::TopBar::selectionModeLeavingRequested, this, [this]() {
            setSelectionModeEnabled(false);
        });
        m_topLayout->addWidget(m_selectionModeTopBar, positionFor.selectionModeTopBar, 0);
    }

    if (!m_selectionModeBottomBar) {
        m_selectionModeBottomBar = new SelectionMode::BottomBar(actionCollection, this);
        connect(m_view, &DolphinView::selectionChanged, this, [this](const KFileItemList &selection) {
            m_selectionModeBottomBar->slotSelectionChanged(selection, m_view->url());
        });
        connect(m_selectionModeBottomBar, &SelectionMode::BottomBar::error, this, &DolphinViewContainer::showErrorMessage);
        connect(m_selectionModeBottomBar, &SelectionMode::BottomBar::selectionModeLeavingRequested, this, [this]() {
            setSelectionModeEnabled(false);
        });
        m_topLayout->addWidget(m_selectionModeBottomBar, positionFor.selectionModeBottomBar, 0);
    }
    m_selectionModeBottomBar->resetContents(bottomBarContents);
    if (bottomBarContents == SelectionMode::BottomBar::GeneralContents) {
        m_selectionModeBottomBar->slotSelectionChanged(m_view->selectedItems(), m_view->url());
    }

    if (!wasEnabled) {
        m_selectionModeTopBar->setVisible(true, WithAnimation);
        m_selectionModeBottomBar->setVisible(true, WithAnimation);
        Q_EMIT selectionModeChanged(true);
    }
}

bool DolphinViewContainer::isSelectionModeEnabled() const
{
    const bool isEnabled = m_view->selectionMode();
    Q_ASSERT((!isEnabled
              // We can't assert that the bars are invisible only because the selection mode is disabled because the hide animation might still be playing.
              && (!m_selectionModeBottomBar || !m_selectionModeBottomBar->isEnabled() || !m_selectionModeBottomBar->isVisible()
                  || m_selectionModeBottomBar->contents() == SelectionMode::BottomBar::PasteContents))
             || (isEnabled && m_selectionModeTopBar
                 && m_selectionModeTopBar->isVisible()
                 // The bottom bar is either visible or was hidden because it has nothing to show in GeneralContents mode e.g. because no items are selected.
                 && m_selectionModeBottomBar
                 && (m_selectionModeBottomBar->isVisible() || m_selectionModeBottomBar->contents() == SelectionMode::BottomBar::GeneralContents)));
    return isEnabled;
}

void DolphinViewContainer::slotSplitTabDisabled()
{
    if (m_selectionModeBottomBar) {
        m_selectionModeBottomBar->slotSplitTabDisabled();
    }
}

void DolphinViewContainer::showMessage(const QString &message, KMessageWidget::MessageType messageType, std::initializer_list<QAction *> buttonActions)
{
    if (message.isEmpty()) {
        return;
    }

    m_messageWidget->setText(message);

    // TODO: wrap at arbitrary character positions once QLabel can do this
    // https://bugreports.qt.io/browse/QTBUG-1276
    m_messageWidget->setWordWrap(true);
    m_messageWidget->setMessageType(messageType);

    const QList<QAction *> previousMessageWidgetActions = m_messageWidget->actions();
    for (auto action : previousMessageWidgetActions) {
        m_messageWidget->removeAction(action);
    }
    for (QAction *action : buttonActions) {
        m_messageWidget->addAction(action);
    }

    m_messageWidget->setWordWrap(false);
    const int unwrappedWidth = m_messageWidget->sizeHint().width();
    m_messageWidget->setWordWrap(unwrappedWidth > size().width());

    if (m_messageWidget->isVisible()) {
        m_messageWidget->hide();
    }
    m_messageWidget->animatedShow();

#ifndef QT_NO_ACCESSIBILITY
    if (QAccessible::isActive() && isActive()) {
        // To announce the new message keyboard focus must be moved to the message label. However, we do not have direct access to the label that is internal
        // to the KMessageWidget. Instead we setFocus() on the KMessageWidget and trust that it has set correct focus handling.
        m_messageWidget->setFocus();
    }
#endif
}

void DolphinViewContainer::showProgress(const QString &currentlyRunningTaskTitle, int progressPercent)
{
    m_statusBar->showProgress(currentlyRunningTaskTitle, progressPercent, DolphinStatusBar::CancelLoading::Disallowed);
}

void DolphinViewContainer::readSettings()
{
    // The startup settings should (only) get applied if they have been
    // modified by the user. Otherwise keep the (possibly) different current
    // setting of the filterbar.
    if (GeneralSettings::modifiedStartupSettings()) {
        setFilterBarVisible(GeneralSettings::filterBar());
    }

    m_view->readSettings();
    m_statusBar->readSettings();
}

bool DolphinViewContainer::isFilterBarVisible() const
{
    return m_filterBar->isEnabled(); // Gets disabled in AnimatedHeightWidget while animating towards a hidden state.
}

QString DolphinViewContainer::placesText() const
{
    QString text;

    if (isSearchBarVisible() && m_searchBar->isSearchConfigured()) {
        text = m_searchBar->queryTitle();
    } else {
        text = url().adjusted(QUrl::StripTrailingSlash).fileName();
        if (text.isEmpty()) {
            text = url().host();
        }
        if (text.isEmpty()) {
            text = url().scheme();
        }
    }

    return text;
}

void DolphinViewContainer::reload()
{
    view()->reload();
    m_messageWidget->hide();
    m_statusBar->updateSpaceInfo();
}

QString DolphinViewContainer::captionWindowTitle() const
{
    if (GeneralSettings::showFullPathInTitlebar() && (!isSearchBarVisible() || !m_searchBar->isSearchConfigured())) {
        if (!url().isLocalFile()) {
            return url().adjusted(QUrl::StripTrailingSlash).toString();
        }
        return url().adjusted(QUrl::StripTrailingSlash).path();
    } else {
        return DolphinViewContainer::caption();
    }
}

QString DolphinViewContainer::caption() const
{
    // see KUrlNavigatorPrivate::firstButtonText().
    if (url().path().isEmpty() || url().path() == QLatin1Char('/')) {
        QUrlQuery query(url());
        const QString title = query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded);
        if (!title.isEmpty()) {
            return title;
        }
    }

    if (isSearchBarVisible() && m_searchBar->isSearchConfigured()) {
        return m_searchBar->queryTitle();
    }

    KFilePlacesModel *placesModel = DolphinPlacesModelSingleton::instance().placesModel();

    QModelIndex url_index = placesModel->closestItem(url());

    if (url_index.isValid() && placesModel->url(url_index).matches(url(), QUrl::StripTrailingSlash)) {
        return placesModel->text(url_index);
    }

    if (!url().isLocalFile()) {
        QUrl adjustedUrl = url().adjusted(QUrl::StripTrailingSlash);
        QString caption;
        if (!adjustedUrl.fileName().isEmpty()) {
            caption = adjustedUrl.fileName();
        } else if (!adjustedUrl.path().isEmpty() && adjustedUrl.path() != "/") {
            caption = adjustedUrl.path();
        } else if (!adjustedUrl.host().isEmpty()) {
            caption = adjustedUrl.host();
        } else {
            caption = adjustedUrl.toString();
        }
        return caption;
    }

    QString fileName = url().adjusted(QUrl::StripTrailingSlash).fileName();
    if (fileName.isEmpty()) {
        fileName = '/';
    }

    return fileName;
}

void DolphinViewContainer::setUrl(const QUrl &newUrl)
{
    if (newUrl != m_urlNavigator->locationUrl()) {
        m_urlNavigator->setLocationUrl(newUrl);
        if (m_searchBar && !Search::isSupportedSearchScheme(newUrl.scheme())) {
            m_searchBar->setSearchPath(newUrl);
        }
    }
}

void DolphinViewContainer::setFilterBarVisible(bool visible)
{
    Q_ASSERT(m_filterBar);
    if (visible) {
        m_view->hideToolTip(ToolTipManager::HideBehavior::Instantly);
        m_filterBar->setVisible(true, WithAnimation);
        m_filterBar->setFocus();
        m_filterBar->selectAll();
        Q_EMIT showFilterBarChanged(true);
    } else {
        closeFilterBar();
    }
}

void DolphinViewContainer::delayedStatusBarUpdate()
{
    if (m_statusBarTimer->isActive() && (m_statusBarTimestamp.elapsed() > 2000)) {
        // No update of the statusbar has been done during the last 2 seconds,
        // although an update has been requested. Trigger an immediate update.
        m_statusBarTimer->stop();
        updateStatusBar();
    } else {
        // Invoke updateStatusBar() with a small delay. This assures that
        // when a lot of delayedStatusBarUpdates() are done in a short time,
        // no bottleneck is given.
        m_statusBarTimer->start();
    }
}

void DolphinViewContainer::updateStatusBar()
{
    m_statusBarTimestamp.start();
    m_view->requestStatusBarText();
    updateStatusBarGeometry();
}

void DolphinViewContainer::slotDirectoryLoadingStarted()
{
    if (isSearchUrl(url())) {
        // Search KIO-slaves usually don't provide any progress information. Give
        // a hint to the user that a searching is done:
        updateStatusBar();
        m_statusBar->showProgress(i18nc("@info", "Searching…"), -1);
    } else {
        // Trigger an undetermined progress indication. The progress
        // information in percent will be triggered by the percent() signal
        // of the directory lister later.
        m_statusBar->showProgress(QString(), -1);
    }

    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setReadOnlyBadgeVisible(false);
    }
}

void DolphinViewContainer::slotDirectoryLoadingCompleted()
{
    m_statusBar->showProgress(QString(), 100);

    if (isSearchUrl(url()) && m_view->itemsCount() == 0) {
        // The dir lister has been completed on a Baloo-URI and no items have been found. Instead
        // of showing the default status bar information ("0 items") a more helpful information is given:
        m_statusBar->setDefaultText(i18nc("@info:status", "No items found."));
    } else {
        updateStatusBar();
    }

    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setReadOnlyBadgeVisible(rootItem().isLocalFile() && !rootItem().isWritable());
    }

    // Update admin bar visibility
    if (m_view->url().scheme() == QStringLiteral("admin")) {
        if (!m_adminBar) {
            m_adminBar = new Admin::Bar(this);
            m_topLayout->addWidget(m_adminBar, positionFor.adminBar, 0);
        }
        m_adminBar->setVisible(true, WithAnimation);
    } else if (m_adminBar) {
        m_adminBar->setVisible(false, WithAnimation);
    }
}

void DolphinViewContainer::slotDirectoryLoadingCanceled()
{
    m_statusBar->showProgress(QString(), 100);
}

void DolphinViewContainer::slotUrlIsFileError(const QUrl &url)
{
    const KFileItem item(url);

    setUrl(KIO::upUrl(item.url()));

    if (!m_openAsFile) {
        m_openAsFile = [this, item] {
            auto action = new QAction(i18nc("@action", "Open as File"), this);
            connect(action, &QAction::triggered, this, [this, item]() {
                m_messageWidget->animatedHide();

                // Find out if the file can be opened in the view (for example, this is the
                // case if the file is an archive). The mime type must be known for that.
                item.determineMimeType();
                const QUrl &folderUrl = DolphinView::openItemAsFolderUrl(item, true);
                if (!folderUrl.isEmpty()) {
                    setUrl(folderUrl);
                } else {
                    slotItemActivated(item);
                }
            });
            return action;
        }();
    }
    showMessage(xi18nc("@info", "Trying to open a file as a folder: <filename>%1</filename>", item.url().toString()), KMessageWidget::Error, {m_openAsFile});
}

void DolphinViewContainer::slotItemActivated(const KFileItem &item)
{
    // It is possible to activate items on inactive views by
    // drag & drop operations. Assure that activating an item always
    // results in an active view.
    m_view->setActive(true);

    const QUrl &url = DolphinView::openItemAsFolderUrl(item, GeneralSettings::browseThroughArchives());
    if (!url.isEmpty()) {
        const auto modifiers = QGuiApplication::keyboardModifiers();
        // keep in sync with KUrlNavigator::slotNavigatorButtonClicked
        if (modifiers & Qt::ControlModifier && modifiers & Qt::ShiftModifier) {
            Q_EMIT activeTabRequested(url);
        } else if (modifiers & Qt::ControlModifier) {
            Q_EMIT tabRequested(url);
        } else if (modifiers & Qt::ShiftModifier) {
            Dolphin::openNewWindow({KFilePlacesModel::convertedUrl(url)}, this);
        } else {
            setUrl(url);
        }
        return;
    }

    KIO::OpenUrlJob *job = new KIO::OpenUrlJob(item.targetUrl(), item.mimetype());
    // Auto*Warning*Handling, errors are put in a KMessageWidget by us in slotOpenUrlFinished.
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoWarningHandlingEnabled, this));
    job->setShowOpenOrExecuteDialog(true);
    connect(job, &KIO::OpenUrlJob::finished, this, &DolphinViewContainer::slotOpenUrlFinished);
    job->start();
}

void DolphinViewContainer::slotfileMiddleClickActivated(const KFileItem &item)
{
    KService::List services = KApplicationTrader::queryByMimeType(item.mimetype());
    const auto modifiers = QGuiApplication::keyboardModifiers();

    int indexOfAppToOpenFileWith = 1;
    if (modifiers & Qt::ShiftModifier) {
        indexOfAppToOpenFileWith = 2;
    }

    // executable scripts
    auto mimeType = item.currentMimeType();
    if (item.isLocalFile() && mimeType.inherits(QStringLiteral("application/x-executable")) && mimeType.inherits(QStringLiteral("text/plain"))
        && item.isExecutable()) {
        KConfigGroup cfgGroup(KSharedConfig::openConfig(QStringLiteral("kiorc")), QStringLiteral("Executable scripts"));
        const QString value = cfgGroup.readEntry("behaviourOnLaunch", "alwaysAsk");

        // in case KIO::WidgetsOpenOrExecuteFileHandler::promptUserOpenOrExecute would not open the file
        if (value != QLatin1String("open")) {
            --indexOfAppToOpenFileWith;
        }
    }

    if (services.length() >= indexOfAppToOpenFileWith + 1) {
        const auto &service = services.at(indexOfAppToOpenFileWith);

        KIO::ApplicationLauncherJob *job = new KIO::ApplicationLauncherJob(service, this);
        job->setUrls({item.targetUrl()});

        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
        connect(job, &KIO::OpenUrlJob::finished, this, &DolphinViewContainer::slotOpenUrlFinished);
        job->start();
    } else {
        // If no 2nd or 3rd service available, try to open archives in new tabs, regardless of the "Browse compressed files as folders" setting.
        const QUrl &url = DolphinView::openItemAsFolderUrl(item);
        if (!url.isEmpty()) {
            // keep in sync with KUrlNavigator::slotNavigatorButtonClicked
            if (modifiers & Qt::ShiftModifier) {
                Q_EMIT activeTabRequested(url);
            } else {
                Q_EMIT tabRequested(url);
            }
        }
    }
}

void DolphinViewContainer::slotItemsActivated(const KFileItemList &items)
{
    Q_ASSERT(items.count() >= 2);

    KFileItemActions fileItemActions(this);
    fileItemActions.runPreferredApplications(items);
}

void DolphinViewContainer::showItemInfo(const KFileItem &item)
{
    if (item.isNull()) {
        m_statusBar->setHoveredItemText(QString());
    } else {
        m_statusBar->setHoveredItemText(item.getStatusBarInfo());
    }
}

void DolphinViewContainer::closeFilterBar()
{
    m_filterBar->closeFilterBar();
    m_view->setFocus();
    Q_EMIT showFilterBarChanged(false);
}

void DolphinViewContainer::clearFilterBar()
{
    m_filterBar->clearIfUnlocked();
}

void DolphinViewContainer::setNameFilter(const QString &nameFilter)
{
    m_view->hideToolTip(ToolTipManager::HideBehavior::Instantly);
    m_view->setNameFilter(nameFilter);
    delayedStatusBarUpdate();
}

void DolphinViewContainer::setFilterMode(const KFileItemModelFilter::FilterMode mode)
{
    m_view->setFilterMode(mode);
    delayedStatusBarUpdate();
}

void DolphinViewContainer::setFilterCaseSensitive(const bool caseSensitive)
{
    m_view->setFilterCaseSensitive(caseSensitive);
    delayedStatusBarUpdate();
}

void DolphinViewContainer::activate()
{
    setActive(true);
}

void DolphinViewContainer::slotUrlNavigatorLocationAboutToBeChanged(const QUrl &)
{
    saveViewState();
}

void DolphinViewContainer::slotUrlNavigatorLocationChanged(const QUrl &url)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->slotReturnPressed();
    }

    if (KProtocolManager::supportsListing(url)) {
        if (isSearchUrl(url)) {
            setSearchBarVisible(true);
        } else if (m_searchBar && m_searchBar->isSearchConfigured()) {
            // Hide the search bar because it shows an outdated search which the user does not care about anymore.
            setSearchBarVisible(false);
        }

        m_view->setUrl(url);
        tryRestoreViewState();

        if (m_grabFocusOnUrlChange && isActive()) {
            // When an URL has been entered, the view should get the focus.
            // The focus must be requested asynchronously, as changing the URL might create
            // a new view widget.
            QTimer::singleShot(0, this, &DolphinViewContainer::requestFocus);
        }
    } else if (KProtocolManager::isSourceProtocol(url)) {
        if (url.scheme().startsWith(QLatin1String("http"))) {
            showMessage(i18nc("@info:status", // krazy:exclude=qmethods
                              "Dolphin does not support web pages, the web browser has been launched"),
                        KMessageWidget::Information);
        } else {
            showMessage(i18nc("@info:status", "Protocol not supported by Dolphin, default application has been launched"), KMessageWidget::Information);
        }

        QDesktopServices::openUrl(url);
        redirect(QUrl(), m_urlNavigator->locationUrl(1));
    } else {
        if (!url.scheme().isEmpty()) {
            showMessage(i18nc("@info:status", "Invalid protocol '%1'", url.scheme()), KMessageWidget::Error);
        } else {
            showMessage(i18nc("@info:status", "Invalid protocol"), KMessageWidget::Error);
        }
        m_urlNavigator->goBack();
    }
}

void DolphinViewContainer::slotUrlSelectionRequested(const QUrl &url)
{
    m_view->markUrlsAsSelected({url});
    m_view->markUrlAsCurrent(url); // makes the item scroll into view
}

void DolphinViewContainer::disableUrlNavigatorSelectionRequests()
{
    disconnect(m_urlNavigator.get(), &KUrlNavigator::urlSelectionRequested, this, &DolphinViewContainer::slotUrlSelectionRequested);
}

void DolphinViewContainer::enableUrlNavigatorSelectionRequests()
{
    connect(m_urlNavigator.get(), &KUrlNavigator::urlSelectionRequested, this, &DolphinViewContainer::slotUrlSelectionRequested);
}

void DolphinViewContainer::redirect(const QUrl &oldUrl, const QUrl &newUrl)
{
    Q_UNUSED(oldUrl)
    const bool block = m_urlNavigator->signalsBlocked();
    m_urlNavigator->blockSignals(true);

    // Assure that the location state is reset for redirection URLs. This
    // allows to skip redirection URLs when going back or forward in the
    // URL history.
    m_urlNavigator->saveLocationState(QByteArray());
    m_urlNavigator->setLocationUrl(newUrl);
    setSearchBarVisible(isSearchUrl(newUrl));

    m_urlNavigator->blockSignals(block);

    // Before emitting `urlChanged`, temporarily disconnect the `activated` signal to avoid activation of `DolphinViewContainer`.
    bool blockActivation = m_urlNavigatorConnected && !isActive();
    if (blockActivation) {
        disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);
    }

    Q_EMIT m_view->urlChanged(newUrl);

    if (blockActivation) {
        connect(m_urlNavigatorConnected, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);

        // Force inactivate `DolphinUrlNavigator`.
        m_urlNavigatorConnected->setActive(false);
    }
}

void DolphinViewContainer::requestFocus()
{
    m_view->setFocus();
}

void DolphinViewContainer::stopDirectoryLoading()
{
    m_view->stopLoading();
    m_statusBar->showProgress(QString(), 100);
}

void DolphinViewContainer::slotStatusBarZoomLevelChanged(int zoomLevel)
{
    m_view->setZoomLevel(zoomLevel);
}

bool DolphinViewContainer::isTopMostExistingParentFolderWritable(QUrl url)
{
    Q_ASSERT(url.isLocalFile());

    while (!url.toLocalFile().isEmpty()) {
        url = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);

        QFileInfo info(url.toLocalFile());

        if (info.exists()) {
            return info.isWritable();
        } else if (info.isSymLink()) {
            return false;
        }
    }
    return false;
}

void DolphinViewContainer::slotErrorMessageFromView(const QString &message, const int kioErrorCode)
{
    if (kioErrorCode == KIO::ERR_CANNOT_ENTER_DIRECTORY && m_view->url().scheme() == QStringLiteral("file")
        && KProtocolInfo::isKnownProtocol(QStringLiteral("admin")) && !rootItem().isReadable()) {
        // Explain to users that they need authentication to see the folder contents.
        if (!m_authorizeToEnterFolderAction) { // This code is similar to parts of Admin::Bar::hideTheNextTimeAuthorizationExpires().
            // We should not simply use the actAsAdminAction() itself here because that one always refers to the active view instead of this->m_view.
            auto actAsAdminAction = Admin::WorkerIntegration::FriendAccess::actAsAdminAction();
            m_authorizeToEnterFolderAction = new QAction{actAsAdminAction->icon(), actAsAdminAction->text(), this};
            m_authorizeToEnterFolderAction->setToolTip(actAsAdminAction->toolTip());
            m_authorizeToEnterFolderAction->setWhatsThis(actAsAdminAction->whatsThis());
            connect(m_authorizeToEnterFolderAction, &QAction::triggered, this, [this, actAsAdminAction]() {
                setActive(true);
                actAsAdminAction->trigger();
            });
        }
        showMessage(i18nc("@info", "Authorization required to enter this folder."), KMessageWidget::Error, {m_authorizeToEnterFolderAction});
        return;
    } else if (kioErrorCode == KIO::ERR_DOES_NOT_EXIST && m_view->url().isLocalFile()) {
        if (!m_createFolderAction) {
            m_createFolderAction = new QAction(this);
            m_createFolderAction->setText(i18nc("@action", "Create missing folder"));
            m_createFolderAction->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
            connect(m_createFolderAction, &QAction::triggered, this, [this](bool) {
                KIO::MkpathJob *job = KIO::mkpath(m_view->url());
                KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkpath, {}, m_view->url(), job);
                connect(job, &KJob::result, this, [this](KJob *job) {
                    if (job->error()) {
                        showErrorMessage(job->errorString());
                    } else {
                        m_view->reload();
                        m_messageWidget->animatedHide();
                    }
                });
            });
        }
        if (isTopMostExistingParentFolderWritable(m_view->url())) {
            m_createFolderAction->setEnabled(true);
            m_createFolderAction->setToolTip(i18nc("@info:tooltip", "Create the folder at this path and open it"));
        } else {
            m_createFolderAction->setEnabled(false);
            m_createFolderAction->setToolTip(i18nc("@info:tooltip", "You do not have permission to create the folder"));
        }
        showMessage(message, KMessageWidget::Error, {m_createFolderAction});
        return;
    }
    Q_EMIT showErrorMessage(message);
}

void DolphinViewContainer::showErrorMessage(const QString &message)
{
    showMessage(message, KMessageWidget::Error);
}

void DolphinViewContainer::slotPlacesModelChanged()
{
    if (!GeneralSettings::showFullPathInTitlebar()) {
        Q_EMIT captionChanged();
    }
}

void DolphinViewContainer::slotHiddenFilesShownChanged(bool showHiddenFiles)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setShowHiddenFolders(showHiddenFiles);
    }
}

void DolphinViewContainer::slotSortHiddenLastChanged(bool hiddenLast)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setSortHiddenFoldersLast(hiddenLast);
    }
}

void DolphinViewContainer::slotCurrentDirectoryRemoved()
{
    const QString location(url().toDisplayString(QUrl::PreferLocalFile));
    if (url().isLocalFile()) {
        const QString dirPath = url().toLocalFile();
        const QString newPath = getNearestExistingAncestorOfPath(dirPath);
        const QUrl newUrl = QUrl::fromLocalFile(newPath);
        // #473377: Delay changing the url to avoid modifying KCoreDirLister before KCoreDirListerCache::deleteDir() returns.
        QTimer::singleShot(0, this, [&, newUrl, location] {
            setUrl(newUrl);
            showMessage(xi18n("Current location changed, <filename>%1</filename> is no longer accessible.", location), KMessageWidget::Warning);
        });
    } else
        showMessage(xi18n("Current location changed, <filename>%1</filename> is no longer accessible.", location), KMessageWidget::Warning);
}

void DolphinViewContainer::slotOpenUrlFinished(KJob *job)
{
    if (job->error() && job->error() != KIO::ERR_USER_CANCELED) {
        showErrorMessage(job->errorString());
    }
}

void DolphinViewContainer::saveViewState()
{
    QByteArray locationState;
    QDataStream stream(&locationState, QIODevice::WriteOnly);
    m_view->saveState(stream);
    m_urlNavigator->saveLocationState(locationState);
}

void DolphinViewContainer::tryRestoreViewState()
{
    QByteArray locationState = m_urlNavigator->locationState();
    if (!locationState.isEmpty()) {
        QDataStream stream(&locationState, QIODevice::ReadOnly);
        m_view->restoreState(stream);
    }
}

QString DolphinViewContainer::getNearestExistingAncestorOfPath(const QString &path) const
{
    QDir dir(path);
    do {
        dir.setPath(QDir::cleanPath(dir.filePath(QStringLiteral(".."))));
    } while (!dir.exists() && !dir.isRoot());

    return dir.exists() ? dir.path() : QString{};
}

void DolphinViewContainer::updateStatusBarGeometry()
{
    if (!m_statusBar) {
        return;
    }
    if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::Small) {
        QRect statusBarRect(preferredSmallStatusBarGeometry());
        if (view()->layoutDirection() == Qt::RightToLeft) {
            const int splitterWidth = m_statusBar->clippingAmount();
            statusBarRect.setLeft(rect().width() - m_statusBar->width() + splitterWidth); // Add clipping amount.
        }
        // Move statusbar to bottomLeft, or bottomRight with right-to-left-layout.
        m_statusBar->setGeometry(statusBarRect);
        // Add 1 due to how qrect coordinates work.
        m_view->setStatusBarOffset(m_statusBar->geometry().height() - m_statusBar->clippingAmount() + 1);
    } else {
        m_view->setStatusBarOffset(0);
    }
}

bool DolphinViewContainer::eventFilter(QObject *object, QEvent *event)
{
    if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::Small && object == m_view) {
        switch (event->type()) {
        case QEvent::Resize: {
            m_statusBar->updateWidthToContent();
            break;
        }
        case QEvent::LayoutRequest: {
            m_statusBar->updateWidthToContent();
            break;
        }
        default:
            break;
        }
    }
    return false;
}

QRect DolphinViewContainer::preferredSmallStatusBarGeometry()
{
    // Add offset depending if horizontal scrollbar or filterbar is visible, we need to add 1 due to how QRect coordinates work.
    const int yPos = m_view->geometry().bottom() - m_view->horizontalScrollBarHeight() - m_statusBar->minimumHeight() + 1;
    QRect statusBarRect = rect().adjusted(0, yPos, 0, 0);
    return statusBarRect;
}

#include "moc_dolphinviewcontainer.cpp"

// dolphin-ng: a lightbox for pictures. It covers the Dolphin window with a dark veil and shows only the picture,
// as large as it fits; the wheel zooms, dragging pans, the arrow keys go to the previous and next picture of the
// folder, Esc or a click on the veil closes it. Animated pictures play.
class NgLightbox : public QWidget
{
public:
    explicit NgLightbox(DolphinView *view)
        : QWidget(view->window())
        , m_view(view)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::StrongFocus);
        m_image = new NgZoomImage(this);
        m_caption = new QLabel(this);
        m_caption->setAlignment(Qt::AlignCenter);
        m_caption->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.85); background: transparent;"));
        m_close = new QToolButton(this);
        m_close->setText(QStringLiteral("✕"));
        m_close->setCursor(Qt::PointingHandCursor);
        m_close->setStyleSheet(QStringLiteral("QToolButton { color: white; background: rgba(255, 255, 255, 0.12); border: 0; border-radius: 16px; font-size: 15px; } QToolButton:hover { background: rgba(255, 255, 255, 0.24); }"));
        connect(m_close, &QToolButton::clicked, this, &QWidget::close);
        window()->installEventFilter(this);
    }

    void open(const KFileItem &item)
    {
        m_pictures.clear();
        const KFileItemList items = m_view->items();
        for (const KFileItem &candidate : items) {
            if (!candidate.isDir() && candidate.isLocalFile() && candidate.mimetype().startsWith(QLatin1String("image/"))) {
                m_pictures.append(candidate);
            }
        }
        m_current = qMax(0, m_pictures.indexOf(item));
        if (m_pictures.isEmpty()) {
            m_pictures.append(item);
        }
        place();
        show();
        raise();
        setFocus();
        load();
    }

protected:
    void place()
    {
        setGeometry(parentWidget()->rect());
        const int margin = 48;
        m_image->setGeometry(margin, margin, width() - 2 * margin, height() - 2 * margin - 28);
        m_caption->setGeometry(margin, height() - margin - 22, width() - 2 * margin, 24);
        m_close->setGeometry(width() - 44, 12, 32, 32);
    }
    void load()
    {
        const KFileItem item = m_pictures.at(m_current);
        const QString path = item.localPath();
        const QString mime = item.mimetype();
        m_image->stopMovie();
        if (mime == QLatin1String("image/gif") || mime == QLatin1String("image/apng") || mime == QLatin1String("image/webp")) {
            m_image->setMovie(path);
        } else {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            m_image->setPixmap(QPixmap::fromImage(image));
            m_caption->setText(image.isNull() ? item.text() : QStringLiteral("%1  ·  %2 × %3").arg(item.text()).arg(image.width()).arg(image.height()));
            if (m_pictures.count() > 1) {
                m_caption->setText(m_caption->text() + QStringLiteral("  ·  %1 / %2").arg(m_current + 1).arg(m_pictures.count()));
            }
            return;
        }
        m_caption->setText(item.text());
    }
    void step(int by)
    {
        if (m_pictures.count() > 1) {
            m_current = (m_current + by + m_pictures.count()) % m_pictures.count();
            load();
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(8, 9, 11, 225));
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_image->geometry().contains(event->pos())) {
            close(); // a click on the veil
        }
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        switch (event->key()) {
        case Qt::Key_Escape:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            close();
            break;
        case Qt::Key_Left:
        case Qt::Key_Up:
            step(-1);
            break;
        case Qt::Key_Right:
        case Qt::Key_Down:
            step(1);
            break;
        default:
            QWidget::keyPressEvent(event);
        }
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == window() && event->type() == QEvent::Resize) {
            place();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    DolphinView *m_view;
    NgZoomImage *m_image;
    QLabel *m_caption;
    QToolButton *m_close;
    KFileItemList m_pictures;
    int m_current = 0;
};

static void ngOpenLightbox(DolphinView *view, const KFileItem &item)
{
    (new NgLightbox(view))->open(item);
}

// dolphin-ng: the filters of the sort bar again, as a section at the bottom of the sidebar: a title pill and one
// row per kind of file with its count; the active one sits on a pill, a click chooses it.
class NgFilterPanel : public QWidget
{
    // dolphin-ng: the sections at the bottom of the sidebar: what is pinned to the top of its folder
    // (SIDEBAR_PINNED=hide switches it off), then the filters of the view (SIDEBAR_FILTERS=hide)
public:
    // part: 0 both sections, 1 the pinned items only, 2 the filters only (the sidebar orders them one by one)
    NgFilterPanel(NgSortBar *bar, QWidget *parent, std::function<void(const QUrl &)> open, int part = 0)
        : QWidget(parent)
        , m_bar(bar)
        , m_open(std::move(open))
        , m_part(part)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        // several panels listen to the same bar: each one adds itself to the chain
        bar->onFiltersChanged = [this, previous = bar->onFiltersChanged] {
            if (previous) {
                previous();
            }
            refresh();
        };
        connect(NgSettings::instance(), &NgSettings::changed, this, [this] {
            refresh(); // a section switched on or off
        });
        connect(NgPins::self(), &NgPins::changed, this, [this] {
            refresh();
        });
        NgPins::self()->prune(); // what was deleted or unpinned while Dolphin was closed
        refresh();
    }

    void refresh()
    {
        m_entries = m_bar ? m_bar->filters() : QList<NgSortBar::FilterEntry>();
        const bool filtersOff = ngSetting("SIDEBAR_FILTERS").toLower() == QLatin1String("hide");
        if (filtersOff || m_entries.count() < 2 || m_part == 1) {
            m_entries.clear();
        }
        // the pinned items: folders first, then files, each by name; no more than fit comfortably
        m_pins.clear();
        if (ngSetting("SIDEBAR_PINNED").toLower() != QLatin1String("hide") && m_part != 2) {
            const QStringList paths = NgPins::self()->paths();
            QMimeDatabase mimes;
            for (const QString &path : paths) {
                const QFileInfo info(path);
                if (!info.exists()) {
                    continue;
                }
                Pin pin;
                pin.path = path;
                pin.name = info.fileName();
                pin.isDir = info.isDir();
                pin.icon = pin.isDir ? QStringLiteral("folder") : mimes.mimeTypeForFile(info).iconName();
                m_pins.append(pin);
            }
            std::stable_sort(m_pins.begin(), m_pins.end(), [](const Pin &a, const Pin &b) {
                return a.isDir != b.isDir ? a.isDir : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
            if (m_pins.count() > 10) {
                m_pins.resize(10);
            }
            // two pinned items with the same name: the folder each is in, in brackets (and the folders above it
            // while those are the same too)
            for (int i = 0; i < m_pins.count(); ++i) {
                QList<int> same;
                for (int j = 0; j < m_pins.count(); ++j) {
                    if (QFileInfo(m_pins.at(j).path).fileName().compare(QFileInfo(m_pins.at(i).path).fileName(), Qt::CaseInsensitive) == 0) {
                        same.append(j);
                    }
                }
                if (same.count() < 2 || same.first() != i) {
                    continue;
                }
                for (int depth = 1; depth <= 4; ++depth) {
                    QStringList where;
                    for (const int j : std::as_const(same)) {
                        const QStringList parts = QFileInfo(m_pins.at(j).path).absolutePath().split(QLatin1Char('/'), Qt::SkipEmptyParts);
                        where.append(parts.mid(qMax(0, int(parts.count()) - depth)).join(QLatin1Char('/')));
                    }
                    if (QSet<QString>(where.begin(), where.end()).count() == where.count() || depth == 4) {
                        for (int k = 0; k < same.count(); ++k) {
                            m_pins[same.at(k)].name = QFileInfo(m_pins.at(same.at(k)).path).fileName() + QStringLiteral(" (") + where.at(k) + QLatin1Char(')');
                        }
                        break;
                    }
                }
            }
        }
        const int wanted = pinsHeight() + filtersHeight();
        if (wanted == 0) {
            hide();
        } else {
            setFixedHeight(wanted);
            show();
        }
        update();
    }

protected:
    struct Pin {
        QString path;
        QString name;
        QString icon;
        bool isDir = false;
    };
    int rowHeight() const
    {
        return fontMetrics().height() + 8;
    }
    int titleHeight() const
    {
        return fontMetrics().height() + 4;
    }
    // a folded section keeps its title only (a click on the title folds and unfolds it)
    bool pinsFolded() const
    {
        return ngSectionCollapsed(QStringLiteral("pinned"));
    }
    bool filtersFolded() const
    {
        return ngSectionCollapsed(QStringLiteral("filters"));
    }
    // how much of a section's rows shows: 1 open, 0 folded, in between while it slides
    qreal pinsOpen() const
    {
        return m_slideKey == QLatin1String("pinned") ? m_slide : pinsFolded() ? 0.0 : 1.0;
    }
    qreal filtersOpen() const
    {
        return m_slideKey == QLatin1String("filters") ? m_slide : filtersFolded() ? 0.0 : 1.0;
    }
    int pinsHeight() const
    {
        if (m_pins.isEmpty()) {
            return 0;
        }
        return titleHeight() + qRound((6 + m_pins.count() * rowHeight()) * pinsOpen()) + (m_entries.isEmpty() ? 6 : 14);
    }
    int filtersHeight() const
    {
        return m_entries.isEmpty() ? 0 : titleHeight() + qRound((6 + m_entries.count() * rowHeight()) * filtersOpen()) + 6;
    }
    // fold or open a section, sliding: folding slides first and folds at the end; opening opens first and slides
    void slide(const QString &key)
    {
        if (m_slideAnim && m_slideAnim->state() == QAbstractAnimation::Running) {
            m_slideAnim->setCurrentTime(m_slideAnim->duration());
        }
        const bool folding = !ngSectionCollapsed(key);
        m_slideKey = key;
        m_slide = folding ? 1.0 : 0.0;
        if (!folding) {
            ngToggleSection(key);
        }
        if (!m_slideAnim) {
            m_slideAnim = new QVariantAnimation(this);
            m_slideAnim->setDuration(180);
            m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);
        }
        m_slideAnim->disconnect(this, nullptr);
        connect(m_slideAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_slide = value.toReal();
            setFixedHeight(qMax(1, pinsHeight() + filtersHeight()));
            update();
        });
        connect(m_slideAnim, &QAbstractAnimation::finished, this, [this, key, folding] {
            m_slideKey.clear();
            m_slide = 1.0;
            if (folding) {
                ngToggleSection(key); // refresh() follows
            } else {
                setFixedHeight(qMax(1, pinsHeight() + filtersHeight()));
                update();
            }
        });
        m_slideAnim->setStartValue(folding ? 1.0 : 0.0);
        m_slideAnim->setEndValue(folding ? 0.0 : 1.0);
        m_slideAnim->start();
    }
    // the title pills: the label, the arrow, and when folded how many entries are inside
    QRectF titleRect(qreal y, const QString &label, int count, bool folded) const
    {
        QFont titleFont = font();
        titleFont.setPointSizeF(titleFont.pointSizeF() * 0.86);
        titleFont.setBold(true);
        const QFontMetrics titleMetrics(titleFont);
        const int extra = folded ? titleMetrics.horizontalAdvance(QStringLiteral(" %1").arg(count)) + 4 : 0;
        return QRectF(0, y, titleMetrics.horizontalAdvance(label) + 20 + 14 + extra, titleHeight());
    }
    QRectF pinsTitle() const
    {
        return m_pins.isEmpty() ? QRectF() : titleRect(0, ngTr("Pinned", "Fissati"), m_pins.count(), pinsFolded());
    }
    QRectF filtersTitle() const
    {
        return m_entries.isEmpty() ? QRectF() : titleRect(pinsHeight(), ngTr("Filters", "Filtri"), m_entries.count(), filtersFolded());
    }
    // hit test: a pinned row (0...), a filter row (1000...), or -1
    int rowAt(const QPoint &pos) const
    {
        if (pinsTitle().contains(pos)) {
            return -2;
        }
        if (filtersTitle().contains(pos)) {
            return -3;
        }
        if (!m_pins.isEmpty() && pinsOpen() >= 1.0) {
            const int top = titleHeight() + 6;
            if (pos.y() >= top && pos.y() < top + m_pins.count() * rowHeight()) {
                return (pos.y() - top) / rowHeight();
            }
        }
        if (!m_entries.isEmpty() && filtersOpen() >= 1.0) {
            const int top = pinsHeight() + titleHeight() + 6;
            if (pos.y() >= top) {
                const int row = (pos.y() - top) / rowHeight();
                return row < m_entries.count() ? 1000 + row : -1;
            }
        }
        return -1;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = ngIsDark(palette());
        const QColor text = palette().color(QPalette::WindowText);
        // (Aurora: the veil of every other surface, no edges, the accent for the filter that is on)
        const QColor veil = ngAurora() ? ngAuroraVeil(dark, true) : dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
        const QColor edge = ngAurora() ? QColor(0, 0, 0, 0) : dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 30);
        QColor dim = text;
        dim.setAlphaF(0.6);

        // a section title: a small pill like the section titles of the places
        auto title = [&](qreal y, const QString &label, int count, bool folded, bool hovered) {
            QFont titleFont = font();
            titleFont.setPointSizeF(titleFont.pointSizeF() * 0.86);
            titleFont.setBold(true);
            const QRectF pill = titleRect(y, label, count, folded);
            painter.setPen(QPen(hovered ? QColor(95, 168, 216, 150) : edge, 1));
            QColor ground = veil;
            if (hovered) {
                ground.setAlpha(qMin(255, veil.alpha() * 2));
            }
            painter.setBrush(ground);
            painter.drawRoundedRect(pill.adjusted(0.5, 0.5, -0.5, -0.5), pill.height() / 2.0, pill.height() / 2.0);
            ngDrawChevron(&painter, QRectF(pill.left() + 6, pill.top(), 12, pill.height()), folded, dim);
            painter.setFont(titleFont);
            painter.setPen(text);
            const QRectF textRect = pill.adjusted(18, 0, -8, 0);
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
            if (folded) {
                painter.setPen(dim);
                painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(count));
            }
            painter.setFont(font());
        };
        auto rowGround = [&](const QRectF &row, bool active, bool hovered) {
            if (active || hovered) {
                QColor fill = veil;
                fill.setAlpha(active ? veil.alpha() : veil.alpha() / 2);
                if (ngAurora()) {
                    fill = active ? ngAuroraAccent(palette(), hovered ? 0.42 : 0.34) : veil;
                }
                painter.setPen(active ? QPen(edge, 1) : QPen(Qt::NoPen));
                painter.setBrush(fill);
                painter.drawRoundedRect(row.adjusted(0.5, 1.5, -0.5, -1.5), (row.height() - 3) / 2.0, (row.height() - 3) / 2.0);
            }
        };

        if (!m_pins.isEmpty()) {
            title(0, ngTr("Pinned", "Fissati"), m_pins.count(), pinsFolded() && m_slideKey != QLatin1String("pinned"), m_hover == -2);
            const qreal open = pinsOpen();
            const int body = 6 + m_pins.count() * rowHeight();
            const int top = titleHeight() + 6 - qRound(body * (1.0 - open)); // the rows slide up under the title
            painter.save();
            painter.setClipRect(QRectF(0, titleHeight(), width(), qRound(body * open)));
            painter.setOpacity(open);
            for (int i = 0; i < (open > 0.0 ? m_pins.count() : 0); ++i) {
                const QRectF row(0, top + i * rowHeight(), width(), rowHeight());
                const Pin &pin = m_pins.at(i);
                rowGround(row, false, i == m_hover);
                const int side = qMin(16, int(row.height()) - 6);
                QIcon::fromTheme(pin.icon, QIcon::fromTheme(QStringLiteral("unknown"))).paint(&painter, QRect(int(row.left()) + 12, int(row.center().y() - side / 2.0) + 1, side, side));
                painter.setPen(text);
                const QRectF labelRect = row.adjusted(12 + side + 8, 0, -12, 0);
                painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, painter.fontMetrics().elidedText(pin.name, Qt::ElideMiddle, qMax(0, int(labelRect.width()))));
            }
            painter.restore();
        }

        if (!m_entries.isEmpty()) {
            const int base = pinsHeight();
            title(base, ngTr("Filters", "Filtri"), m_entries.count(), filtersFolded() && m_slideKey != QLatin1String("filters"), m_hover == -3);
            const qreal open = filtersOpen();
            const int body = 6 + m_entries.count() * rowHeight();
            const int top = base + titleHeight() + 6 - qRound(body * (1.0 - open));
            painter.save();
            painter.setClipRect(QRectF(0, base + titleHeight(), width(), qRound(body * open)));
            painter.setOpacity(open);
            for (int i = 0; i < (open > 0.0 ? m_entries.count() : 0); ++i) {
                const QRectF row(0, top + i * rowHeight(), width(), rowHeight());
                const auto &entry = m_entries.at(i);
                rowGround(row, entry.active, 1000 + i == m_hover);
                const int side = qMin(16, int(row.height()) - 6);
                QIcon::fromTheme(entry.icon).paint(&painter, QRect(int(row.left()) + 12, int(row.center().y() - side / 2.0) + 1, side, side));
                painter.setPen(text);
                // the count keeps its place on the right; the label gets what is left and is cut with an ellipsis
                const QString count = QString::number(entry.count);
                const int countWidth = painter.fontMetrics().horizontalAdvance(count);
                const QRectF labelRect = row.adjusted(12 + side + 8, 0, -14 - countWidth - 8, 0);
                painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, painter.fontMetrics().elidedText(entry.label, Qt::ElideRight, qMax(0, int(labelRect.width()))));
                painter.setPen(dim);
                painter.drawText(row.adjusted(14, 0, -14, 0), Qt::AlignRight | Qt::AlignVCenter, count);
            }
            painter.restore();
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int row = rowAt(event->pos());
        if (row != m_hover) {
            m_hover = row;
            update();
            if (row >= 0 && row < 1000) {
                setToolTip(m_pins.at(row).path);
            } else if (row == -2 || row == -3) {
                const bool folded = row == -2 ? pinsFolded() : filtersFolded();
                setToolTip(folded ? ngTr("Click to show this section", "Clic per mostrare la sezione") : ngTr("Click to fold this section", "Clic per richiudere la sezione"));
            } else {
                setToolTip(QString());
            }
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const int row = rowAt(event->pos());
        if ((row == -2 || row == -3) && event->button() == Qt::LeftButton) {
            slide(row == -2 ? QStringLiteral("pinned") : QStringLiteral("filters"));
            return;
        }
        if (row < 0) {
            return;
        }
        if (row >= 1000) {
            if (event->button() == Qt::LeftButton && m_bar) {
                m_bar->setFilter(m_entries.at(row - 1000).id);
            }
            return;
        }
        const Pin pin = m_pins.at(row);
        const QUrl url = QUrl::fromLocalFile(pin.path);
        if (event->button() == Qt::LeftButton) {
            if (pin.isDir) {
                if (m_open) {
                    m_open(url); // a folder opens in the view
                }
            } else {
                auto *job = new KIO::OpenUrlJob(url); // a file opens in its application
                job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
                job->start();
            }
        } else if (event->button() == Qt::RightButton) {
            QMenu menu(this);
            if (!pin.isDir && m_open) {
                menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), ngTr("Show in its folder", "Mostra nella sua cartella"), this, [this, url] {
                    m_open(url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash));
                });
            }
            menu.addAction(QIcon::fromTheme(QStringLiteral("window-unpin")), ngTr("Unpin from the Top", "Togli dall'alto"), this, [path = pin.path] {
                NgPins::self()->unpin(path);
            });
            menu.exec(event->globalPosition().toPoint());
        }
    }

private:
    QPointer<NgSortBar> m_bar;
    std::function<void(const QUrl &)> m_open;
    int m_part = 0;
    QList<NgSortBar::FilterEntry> m_entries;
    QList<Pin> m_pins;
    int m_hover = -1;
    QString m_slideKey; // the section sliding now
    qreal m_slide = 1.0;
    QVariantAnimation *m_slideAnim = nullptr;
};

QWidget *ngCreateFilterPanel(QWidget *sortBar, QWidget *parent, std::function<void(const QUrl &)> open, int part)
{
    auto *bar = dynamic_cast<NgSortBar *>(sortBar);
    return bar ? new NgFilterPanel(bar, parent, std::move(open), part) : nullptr;
}

void ngSortBarSetLeadingActions(QWidget *bar, const QList<QAction *> &actions)
{
    if (auto *sortBar = dynamic_cast<NgSortBar *>(bar)) {
        sortBar->setLeadingActions(actions);
    }
}

// dolphin-ng: the sort/filter bar of the window (see NgSortBar); it follows the active view
QWidget *ngCreateSortBar(QWidget *parent)
{
    return new NgSortBar(parent);
}

void ngSortBarFollow(QWidget *bar, DolphinView *view)
{
    if (auto *sortBar = dynamic_cast<NgSortBar *>(bar)) {
        sortBar->follow(view);
    }
}
