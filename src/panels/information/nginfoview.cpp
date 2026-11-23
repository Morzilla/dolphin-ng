/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "nginfoview.h"

#include "kitemviews/kitemlistwidget.h" // ngTr, ngKindFor, ngIsDark, NgActivity, quick actions, text halo
#include "kitemviews/private/ngfacts.h"
#include "mediawidget.h"

#include <KFileMetaData/UserMetaData>
#include <KIO/Global>
#include <KIO/DirectorySizeJob>
#include <KIO/PreviewJob>

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QFileInfo>
#include <QtMath>
#include <QScrollBar>
#include <QDirIterator>
#include <QDir>
#include <QElapsedTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QMimeDatabase>
#include <QCursor>
#include <QToolTip>
#include <QImageReader>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrentRun>
#include <cmath>
#include <functional>
#include <optional>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QResizeEvent>
#include <QStringDecoder>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
// the veil every surface of the panel stands on (Aurora: Breeze NG's own)
QColor ngVeil(const QPalette &palette)
{
    const bool dark = ngIsDark(palette);
    if (ngAurora()) {
        return ngAuroraVeil(dark, true);
    }
    return dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 13);
}

// a name that can break anywhere: a label cannot wrap inside a word, and a long file name without spaces
// ("186851_2026-AE_-_09_Studio_Legale…") made the panel as wide as the name. A zero width space after every letter
// lets it wrap at the panel's edge, wherever that falls
QString ngBreakable(const QString &text)
{
    QString out;
    out.reserve(text.size() * 2);
    for (const QChar c : text) {
        out.append(c);
        if (!c.isSpace() && !c.isHighSurrogate()) {
            out.append(QChar(0x200B));
        }
    }
    return out;
}

// text painted by the panel itself: with the shadow of the sidebar's text on glass (see ngSidebarHalo)
void ngInfoText(QPainter &painter, const QRectF &rect, int flags, const QString &text, qreal strength = 1.0)
{
    if (ngSidebarHalo() || ngTextHalo()) {
        // (a dimmed label solid and well shaded: see-through, it vanished over a white window behind the glass)
        painter.setPen(ngHaloInk(painter.pen().color(), QApplication::palette()));
        strength = qMax(strength, 0.85);
        ngPaintTextHalo(
            &painter,
            painter.pen().color(),
            [&](const QPointF &offset) {
                painter.drawText(rect.translated(offset), flags, text);
            },
            strength);
    }
    painter.drawText(rect, flags, text);
}
}

// ---- the preview at the top: a picture, the first lines of a text, an icon, or a stack of several pictures
// the tags: chips side by side, on as many lines as they need (in one row a few tags squeezed each other to "V…e")
namespace
{
// (in a namespace of this file: the view container has a class of the same name, and the linker kept one of the two)
class NgFlowLayout : public QLayout
{
public:
    explicit NgFlowLayout(QWidget *parent)
        : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
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
        return m_items.count();
    }
    QLayoutItem *itemAt(int index) const override
    {
        return m_items.value(index);
    }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.count() ? m_items.takeAt(index) : nullptr;
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
        return arrange(QRect(0, 0, width, 0), false);
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }
    QSize sizeHint() const override
    {
        return minimumSize();
    }
    QSize minimumSize() const override
    {
        QSize size;
        for (QLayoutItem *item : m_items) {
            size = size.expandedTo(item->minimumSize());
        }
        return size;
    }

private:
    int arrange(const QRect &rect, bool place) const
    {
        const int gap = 4;
        int x = rect.x();
        int y = rect.y();
        int lineHeight = 0;
        for (QLayoutItem *item : m_items) {
            if (item->isEmpty()) {
                continue;
            }
            const QSize hint = item->sizeHint().boundedTo(QSize(rect.width(), QWIDGETSIZE_MAX));
            if (x > rect.x() && x + hint.width() > rect.right() + 1) {
                x = rect.x();
                y += lineHeight + gap;
                lineHeight = 0;
            }
            if (place) {
                item->setGeometry(QRect(QPoint(x, y), hint));
            }
            x += hint.width() + gap;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y();
    }
    QList<QLayoutItem *> m_items;
};
}

class NgInfoHero : public QWidget
{
public:
    explicit NgInfoHero(QWidget *parent)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_eye = new QToolButton(this);
        m_eye->setIcon(QIcon::fromTheme(QStringLiteral("view-preview")));
        m_eye->setIconSize(QSize(18, 18));
        m_eye->setAutoRaise(true);
        m_eye->setToolTip(ngTr("Open large", "Apri in grande"));
        m_eye->setCursor(Qt::PointingHandCursor);
        m_eye->setStyleSheet(QStringLiteral("QToolButton { background: rgba(10, 12, 20, 150); border-radius: 16px; padding: 7px; }"
                                            "QToolButton:hover { background: rgba(10, 12, 20, 200); }"));
        m_eye->hide();
        // zoom out and in, beside it (the wheel does the same around the pointer)
        auto zoomButton = [this](const QString &icon, const QString &tip, qreal factor) {
            auto *button = new QToolButton(this);
            button->setIcon(QIcon::fromTheme(icon));
            button->setIconSize(QSize(18, 18));
            button->setAutoRaise(true);
            button->setToolTip(tip);
            button->setCursor(Qt::PointingHandCursor);
            button->setAutoRepeat(true);
            button->setStyleSheet(m_eye->styleSheet());
            button->hide();
            connect(button, &QToolButton::clicked, this, [this, factor] {
                zoomBy(factor, rect().center());
            });
            return button;
        };
        m_zoomOut = zoomButton(QStringLiteral("zoom-out"), ngTr("Zoom out", "Riduci"), 1 / 1.5);
        m_zoomIn = zoomButton(QStringLiteral("zoom-in"), ngTr("Zoom in (or the wheel)", "Ingrandisci (o la rotellina)"), 1.5);
        setMouseTracking(true);
        // the height the user gave it by dragging its bottom edge, as a share of the width (0.75 by default)
        m_ratio = qBound(MinRatio, ngSetting("INFO_PREVIEW_RATIO", QStringLiteral("0.75")).toDouble(), MaxRatio);
    }
    QToolButton *eye() const
    {
        return m_eye;
    }
    void clear()
    {
        m_pixmap = QPixmap();
        m_stack.clear();
        m_lines.clear();
        m_icon.clear();
        m_canOpen = false;
        m_zoom = 1.0;
        m_bar = NgFileFacts::NoBar;
        m_detail.clear();
        m_entries.clear();
        m_camera.clear();
        m_sizeText.clear();
        m_pageable = false;
        m_page = 1;
        m_listing = false;
        stopMedia();
        showButtons(false);
        update();
    }
    // asked, a moment after the zoom settles, when the picture shown has fewer pixels than the zoom needs: the longest
    // side wanted, in device pixels (the owner renders the file again at that size)
    void setSharperWanted(std::function<void(int)> wanted)
    {
        m_sharperWanted = std::move(wanted);
        m_sharpTimer = new QTimer(this);
        m_sharpTimer->setSingleShot(true);
        m_sharpTimer->setInterval(180);
        connect(m_sharpTimer, &QTimer::timeout, this, [this] {
            if (!zoomed()) {
                return;
            }
            const QSizeF shown = fitRect().size() * m_zoom * devicePixelRatioF();
            const int needed = qRound(qMax(shown.width(), shown.height()));
            if (needed > qMax(m_pixmap.width(), m_pixmap.height()) * 1.1) {
                m_sharperWanted(needed);
            }
        });
    }
    void setIcon(const QString &icon, const QColor &tint)
    {
        m_icon = icon;
        m_tint = tint;
        update();
    }
    void setPixmap(const QPixmap &pixmap)
    {
        m_pixmap = pixmap;
        showButtons(underMouse());
        update();
        // for tests: DOLPHIN_NG_TEST_ZOOM=factor zooms the first picture in, its buttons shown (no input sent)
        if (const qreal factor = qEnvironmentVariable("DOLPHIN_NG_TEST_ZOOM").toDouble(); factor > 1 && !zoomed()) {
            QTimer::singleShot(800, this, [this, factor] {
                if (!zoomed()) {
                    zoomBy(factor, rect().center());
                }
                showButtons(true);
            });
        }
    }
    void addToStack(const QPixmap &pixmap)
    {
        if (m_stack.count() < 3) {
            m_stack.append(pixmap);
            update();
        }
    }
    void setLines(const QStringList &lines)
    {
        m_lines = lines;
        update();
    }
    // what is told over the preview: the kind as a badge at the top left, the size and the facts as pills at the bottom
    // on a dark veil (as a photo viewer does), so that the panel under it is shorter
    void setOverlay(const QString &badge, const QColor &tint, const QList<std::pair<QString, QString>> &chips)
    {
        m_badge = badge;
        m_badgeTint = tint;
        m_chips = chips;
        resizeEvent(nullptr);
        update();
    }
    // the band at the foot of the preview, in the manner of the file's kind (see paintOverlay)
    void setBar(NgFileFacts::Bar bar, const QVariantMap &detail, const QList<NgFileFacts::Entry> &entries, const QString &camera, const QString &sizeText)
    {
        m_bar = bar;
        m_detail = detail;
        if (!m_listing) {
            m_entries = entries;
        }
        m_camera = camera;
        m_sizeText = sizeText;
        update();
    }
    // a folder: its first items in place of its picture (folders first, with how many items each holds, then files)
    void setListing(const QList<NgFileFacts::Entry> &entries)
    {
        m_entries = entries;
        m_listing = !entries.isEmpty();
        showButtons(underMouse());
        update();
    }
    void setSizeText(const QString &sizeText)
    {
        m_sizeText = sizeText;
        update();
    }
    // a PDF: the bar turns its pages; @p wanted is asked for the page to show
    void setPageable(bool pageable, std::function<void(int)> wanted = {})
    {
        m_pageable = pageable;
        m_page = 1;
        m_pageWanted = std::move(wanted);
        update();
        // for tests: DOLPHIN_NG_TEST_PAGE=n turns a PDF to page n a moment after it shows (no input sent)
        if (const int page = qEnvironmentVariableIntValue("DOLPHIN_NG_TEST_PAGE"); pageable && page > 1) {
            QTimer::singleShot(1500, this, [this, page] {
                if (m_pageable && m_pageWanted) {
                    m_page = page;
                    m_pageWanted(page);
                    update();
                }
            });
        }
    }
    int page() const
    {
        return m_page;
    }
    // a video or a song: ▶ in the bar plays it here, its frames drawn in the preview (through a video sink: no native
    // video surface, the glass stays whole); the line of time seeks
    void setMedia(const QUrl &url, bool video)
    {
        stopMedia();
        m_mediaUrl = url;
        m_mediaVideo = video;
        update();
        // for tests: DOLPHIN_NG_TEST_PLAY plays it a moment after it shows (no input sent)
        if (qEnvironmentVariableIsSet("DOLPHIN_NG_TEST_PLAY")) {
            QTimer::singleShot(1500, this, [this] {
                if (!playing()) {
                    togglePlay();
                }
            });
        }
    }
    void stopMedia()
    {
        if (m_player) {
            m_player->stop();
            m_player->deleteLater();
            m_player = nullptr;
        }
        m_frame = QImage();
        m_mediaUrl = QUrl();
        update();
    }
    bool playing() const
    {
        return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    }
    void togglePlay()
    {
        if (m_mediaUrl.isEmpty()) {
            return;
        }
        if (!m_player) {
            m_player = new QMediaPlayer(this);
            auto *output = new QAudioOutput(m_player);
            m_player->setAudioOutput(output);
            if (m_mediaVideo) {
                auto *sink = new QVideoSink(m_player);
                m_player->setVideoSink(sink);
                connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &videoFrame) {
                    // (no more pictures than the panel can show: a 4K frame turned into an image every time was wasted)
                    if (m_frameClock.isValid() && m_frameClock.elapsed() < 30) {
                        return;
                    }
                    m_frameClock.start();
                    const QImage image = videoFrame.toImage();
                    if (!image.isNull()) {
                        const qreal dpr = devicePixelRatioF();
                        const bool first = m_frame.isNull();
                        // (zoomed in, kept sharper: up to three times the card's width)
                        const qreal room = width() * dpr * qMax<qreal>(1.5, qMin<qreal>(m_zoom * 1.2, 3.0));
                        m_frame = image.width() > room ? image.scaledToWidth(qRound(room), Qt::SmoothTransformation) : image;
                        if (first && zoomed()) {
                            keepInside(); // (the zoom was taken on the still picture: laid again on the frames)
                        }
                        showButtons(underMouse());
                        update();
                    }
                });
            }
            connect(m_player, &QMediaPlayer::positionChanged, this, [this] {
                update();
            });
            connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] {
                update();
            });
            m_player->setSource(m_mediaUrl);
        }
        if (playing()) {
            m_player->pause();
        } else {
            m_player->play();
        }
        update();
    }
    void seekTo(qreal x)
    {
        if (m_trackRect.width() <= 0) {
            return;
        }
        if (!m_player) {
            togglePlay();
        }
        const qint64 length = m_player->duration() > 0 ? m_player->duration() : qint64(m_detail.value(QStringLiteral("duration")).toDouble() * 1000);
        m_player->setPosition(qint64(qBound(0.0, (x - m_trackRect.left()) / m_trackRect.width(), 1.0) * length));
        update();
    }
    void setCanOpen(bool canOpen)
    {
        m_canOpen = canOpen;
        showButtons(underMouse());
    }
    bool hasPicture() const
    {
        return !m_pixmap.isNull() || !m_lines.isEmpty();
    }
    QSize sizeHint() const override
    {
        return QSize(320, 240);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        if (event) {
            QWidget::resizeEvent(event);
        }
        // (an icon alone, with nothing to show, a little lower)
        const bool bare = m_stack.isEmpty() && m_pixmap.isNull() && m_lines.isEmpty();
        const int height = qRound(width() * (bare ? m_ratio * 0.75 : m_ratio));
        if (height != this->height()) {
            setFixedHeight(qMax(m_chips.isEmpty() ? 110 : 160, height));
        }
        placeButtons();
    }
    void placeButtons()
    {
        int x = width() - 10;
        for (QToolButton *button : {m_eye, m_zoomIn, m_zoomOut}) {
            if (button->isVisible()) {
                x -= button->sizeHint().width();
                button->move(x, 10);
                x -= 6;
            }
        }
    }
    bool canZoom() const
    {
        return (!m_pixmap.isNull() || !m_frame.isNull()) && m_stack.isEmpty() && !m_listing; // (a video playing: its frames)
    }
    void showButtons(bool hovered)
    {
        m_eye->setVisible(hovered && m_canOpen);
        m_zoomIn->setVisible((hovered || zoomed()) && canZoom());
        m_zoomOut->setVisible((hovered || zoomed()) && canZoom());
        if (m_zoomIn->isVisible()) {
            m_zoomIn->setEnabled(m_zoom < MaxZoom);
            m_zoomOut->setEnabled(zoomed());
        }
        placeButtons();
    }
    void enterEvent(QEnterEvent *event) override
    {
        showButtons(true);
        QWidget::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent *event) override
    {
        showButtons(false);
        QWidget::leaveEvent(event);
        update();
    }
    // the bottom edge drags the card taller or shorter; a double click there brings back the first height
    bool onGrip(const QPointF &pos) const
    {
        return pos.y() >= height() - 10;
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onGrip(event->position())) {
            m_dragFrom = event->globalPosition().y();
            m_dragHeight = height();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_playRect.contains(event->position())) {
            togglePlay();
            return;
        }
        if (event->button() == Qt::LeftButton && m_trackRect.adjusted(0, -8, 0, 8).contains(event->position())) {
            m_seeking = true;
            seekTo(event->position().x());
            return;
        }
        if (event->button() == Qt::LeftButton && (m_pagerPrev.contains(event->position()) || m_pagerNext.contains(event->position()))) {
            const int pages = m_detail.value(QStringLiteral("pages")).toInt();
            const int page = qBound(1, m_page + (m_pagerNext.contains(event->position()) ? 1 : -1), qMax(1, pages));
            if (page != m_page) {
                m_page = page;
                m_zoom = 1.0;
                if (m_pageWanted) {
                    m_pageWanted(page);
                }
                update();
            }
            return;
        }
        if (event->button() == Qt::LeftButton && zoomed()) {
            m_panFrom = event->position();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragFrom >= 0) {
            const bool bare = m_stack.isEmpty() && m_pixmap.isNull() && m_lines.isEmpty();
            const qreal wanted = (m_dragHeight + event->globalPosition().y() - m_dragFrom) / qMax(1.0, qreal(width()));
            m_ratio = qBound(MinRatio, bare ? wanted / 0.75 : wanted, MaxRatio);
            resizeEvent(nullptr);
            return;
        }
        if (m_seeking) {
            seekTo(event->position().x());
            return;
        }
        if (m_panFrom) {
            m_zoomTopLeft += event->position() - *m_panFrom;
            m_panFrom = event->position();
            keepInside();
            update();
            return;
        }
        setCursor(onGrip(event->position()) ? Qt::SizeVerCursor
                      : (m_pagerPrev.contains(event->position()) || m_pagerNext.contains(event->position()) || m_playRect.contains(event->position())
                         || m_trackRect.adjusted(0, -8, 0, 8).contains(event->position()))
                      ? Qt::PointingHandCursor
                      : zoomed() ? Qt::OpenHandCursor
                                 : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_dragFrom >= 0) {
            m_dragFrom = -1;
            setNgSetting("INFO_PREVIEW_RATIO", QString::number(m_ratio, 'f', 3));
            return;
        }
        if (m_seeking) {
            m_seeking = false;
            return;
        }
        if (m_panFrom) {
            m_panFrom.reset();
            setCursor(Qt::OpenHandCursor);
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (onGrip(event->position())) {
            m_ratio = 0.75;
            setNgSetting("INFO_PREVIEW_RATIO", QString());
            resizeEvent(nullptr);
            return;
        }
        if (zoomed()) {
            m_zoom = 1.0;
            setCursor(Qt::ArrowCursor);
            showButtons(true);
            update();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }
    // the wheel zooms the picture in and out around the pointer; back at its whole size, the wheel scrolls the panel
    void wheelEvent(QWheelEvent *event) override
    {
        const int delta = event->angleDelta().y();
        if (!canZoom() || delta == 0 || (!zoomed() && delta < 0)) {
            event->ignore();
            return;
        }
        zoomBy(std::pow(1.25, delta / 120.0), event->position());
        event->accept();
    }
    // zoom by a factor, the point under the pointer (or the centre) staying where it is
    void zoomBy(qreal factor, const QPointF &at)
    {
        if (!canZoom()) {
            return;
        }
        const QRectF before = zoomed() ? zoomedRect() : fitRect();
        const qreal zoom = qBound(1.0, m_zoom * factor, MaxZoom);
        m_zoomTopLeft = at - (at - before.topLeft()) * (zoom / m_zoom);
        m_zoom = zoom < 1.02 ? 1.0 : zoom;
        keepInside();
        if (!m_zoomIn->underMouse() && !m_zoomOut->underMouse()) {
            setCursor(zoomed() ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
        if (zoomed() && m_sharpTimer) {
            m_sharpTimer->start();
        }
        showButtons(underMouse());
        update();
    }
    bool zoomed() const
    {
        return m_zoom > 1.0 && canZoom();
    }
    QRectF fitRect() const
    {
        // the frame of a video playing fills the card; a picture keeps a margin and is not blown up past three times
        const bool frame = !m_frame.isNull();
        const QSizeF size = frame ? QSizeF(m_frame.size()) / devicePixelRatioF() : QSizeF(m_pixmap.size()) / m_pixmap.devicePixelRatio();
        const qreal margin = frame ? 0.0 : 16.0;
        const qreal scale = qMin((width() - margin) / size.width(), (height() - margin) / size.height());
        const QSizeF shown = size * (frame ? scale : qMin(scale, 3.0));
        return QRectF(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown);
    }
    QRectF zoomedRect() const
    {
        return QRectF(m_zoomTopLeft, fitRect().size() * m_zoom);
    }
    // the zoomed picture covers the card where it is larger than it, and stays centred where it is not
    void keepInside()
    {
        const QSizeF size = fitRect().size() * m_zoom;
        auto clamp = [](qreal pos, qreal length, qreal room) {
            return length <= room ? (room - length) / 2 : qBound(room - length, pos, 0.0);
        };
        m_zoomTopLeft = QPointF(clamp(m_zoomTopLeft.x(), size.width(), width()), clamp(m_zoomTopLeft.y(), size.height(), height()));
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape;
        shape.addRoundedRect(frame, 14, 14);
        const bool dark = ngIsDark(palette());
        painter.fillPath(shape, dark ? QColor(0, 0, 0, 70) : QColor(0, 0, 0, 16));
        painter.save();
        painter.setClipPath(shape);
        if (!m_stack.isEmpty()) {
            // several items: their pictures fanned out, the first one in front
            const QSizeF card(width() * 0.42, width() * 0.32);
            const QPointF centre = rect().center();
            for (int i = m_stack.count() - 1; i >= 0; --i) {
                painter.save();
                painter.translate(centre + QPointF((i - (m_stack.count() - 1) / 2.0) * card.width() * 0.45, 0));
                painter.rotate((i - (m_stack.count() - 1) / 2.0) * 6.0);
                const QRectF box(-card.width() / 2, -card.height() / 2, card.width(), card.height());
                QPainterPath sheet;
                sheet.addRoundedRect(box, 8, 8);
                painter.fillPath(sheet, QColor(0, 0, 0, 60));
                painter.setClipPath(sheet);
                const QPixmap &picture = m_stack.at(i);
                const qreal scale = qMax(box.width() / picture.width(), box.height() / picture.height());
                const QSizeF part(box.width() / scale, box.height() / scale);
                painter.drawPixmap(box, picture, QRectF((picture.width() - part.width()) / 2, (picture.height() - part.height()) / 2, part.width(), part.height()));
                painter.restore();
            }
        } else if (!m_lines.isEmpty()) {
            // a text: its first lines, as an editor shows them: numbered in a gutter, under the badge
            painter.fillRect(rect(), dark ? QColor(20, 24, 33, 240) : QColor(250, 249, 246, 240));
            QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            mono.setPointSizeF(mono.pointSizeF() * 0.82);
            painter.setFont(mono);
            const QFontMetricsF metrics(mono);
            const qreal gutter = metrics.horizontalAdvance(QStringLiteral("000")) + 12;
            qreal y = topRoom() + metrics.ascent(); // (under the size's pill, if any)
            int number = 1;
            for (const QString &line : std::as_const(m_lines)) {
                if (y > height() - 8) {
                    break;
                }
                painter.setPen(dark ? QColor(255, 255, 255, 64) : QColor(0, 0, 0, 70));
                painter.drawText(QRectF(0, y - metrics.ascent(), gutter - 8, metrics.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(number++));
                painter.setPen(dark ? QColor(205, 212, 228) : QColor(40, 44, 52));
                painter.drawText(QPointF(gutter, y), metrics.elidedText(line, Qt::ElideRight, width() - gutter - 12));
                y += metrics.height() + 1;
            }
        } else if ((m_listing || (m_bar == NgFileFacts::Archive && m_pixmap.isNull())) && !m_entries.isEmpty()) {
            // an archive: what is inside, its top level (folders with how many files, then the files), each with its size
            painter.fillRect(rect(), dark ? QColor(22, 25, 34, 235) : QColor(246, 247, 250, 240));
            QFont row = font();
            row.setPointSizeF(row.pointSizeF() * 0.86);
            painter.setFont(row);
            const QFontMetricsF fm(row);
            const qreal rowHeight = fm.height() + 8;
            qreal y = topRoom();
            const qreal bottom = height() - (m_listing ? QFontMetricsF(font()).height() * 0.9 + 26 : fm.height() + 42); // (down to the band)
            for (const NgFileFacts::Entry &entry : std::as_const(m_entries)) {
                if (y + rowHeight > bottom) {
                    break;
                }
                const QRectF box(12, y, width() - 24, rowHeight - 3);
                painter.setPen(Qt::NoPen);
                painter.setBrush(dark ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 8));
                painter.drawRoundedRect(box, 6, 6);
                const bool folder = entry.name.endsWith(QLatin1Char('/'));
                const NgKind kind = ngKindFor(folder ? entry.name.chopped(1) : entry.name, folder);
                const int side = qRound(fm.height() * 0.85);
                QIcon::fromTheme(folder ? QStringLiteral("folder") : QMimeDatabase().mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension).iconName(),
                                 QIcon::fromTheme(QStringLiteral("unknown")))
                    .paint(&painter, QRect(qRound(box.left() + 8), qRound(box.center().y() - side / 2.0), side, side));
                Q_UNUSED(kind)
                // (a folder of a folder: how many items it holds; of an archive: how many files)
                const QString right = !folder ? KIO::convertSize(KIO::filesize_t(entry.size))
                    : m_listing ? (entry.count == 0 ? ngTr("empty", "vuota") : entry.count == 1 ? ngTr("1 item", "1 elemento") : ngTr("%1 items", "%1 elementi").arg(entry.count))
                                : (entry.count == 1 ? ngTr("1 file", "1 file") : ngTr("%1 files", "%1 file").arg(entry.count));
                const qreal rightWidth = fm.horizontalAdvance(right);
                painter.setPen(dark ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 120));
                painter.drawText(box.adjusted(0, 0, -10, 0), Qt::AlignRight | Qt::AlignVCenter, right);
                painter.setPen(dark ? QColor(230, 234, 242) : QColor(30, 32, 38));
                painter.drawText(box.adjusted(side + 16, 0, -rightWidth - 18, 0), Qt::AlignLeft | Qt::AlignVCenter,
                                 fm.elidedText(folder ? entry.name.chopped(1) : entry.name, Qt::ElideMiddle, box.width() - side - rightWidth - 34)); // (the icon says folder)
                y += rowHeight;
            }
        } else if (!m_frame.isNull()) {
            // a video playing: its frame, as large as it fits
            painter.fillRect(rect(), Qt::black);
            painter.drawImage(zoomed() ? zoomedRect() : fitRect(), m_frame); // (zoomed with the wheel as a picture is)
        } else if (!m_pixmap.isNull()) {
            // the picture whole, as large as it fits (or zoomed in with the wheel)
            const QRectF target = zoomed() ? zoomedRect() : fitRect();
            QPainterPath picture;
            picture.addRoundedRect(target, zoomed() ? 0 : 6, zoomed() ? 0 : 6);
            painter.setClipPath(picture, Qt::IntersectClip);
            painter.drawPixmap(target, m_pixmap, QRectF(QPointF(0, 0), QSizeF(m_pixmap.size())));
        } else if (!m_icon.isEmpty()) {
            const int side = qMin(96, qMin(width(), height()) - 24);
            QIcon::fromTheme(m_icon, QIcon::fromTheme(QStringLiteral("unknown"))).paint(&painter, QRect((width() - side) / 2, (height() - side) / 2, side, side));
        }
        paintOverlay(painter, shape);
        painter.restore();
        // the grip of the bottom edge, while the pointer is over the card
        if (underMouse() || m_dragFrom >= 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, m_dragFrom >= 0 ? 230 : 170));
            painter.drawRoundedRect(QRectF(width() / 2.0 - 18, height() - 7, 36, 4), 2, 2);
        }
    }

    void paintOverlay(QPainter &painter, const QPainterPath &shape)
    {
        if (zoomed()) {
            // zoomed in: the picture alone, and how much it is enlarged
            painter.setClipPath(shape);
            const QString text = QStringLiteral("%1%").arg(qRound(m_zoom * 100));
            QFont font = this->font();
            font.setPointSizeF(font.pointSizeF() * 0.85);
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            const QFontMetricsF metrics(font);
            // (a video or a song keeps its player: the percentage goes to the top left, out of the band's way)
            const bool media = m_bar == NgFileFacts::Video || m_bar == NgFileFacts::Audio;
            const QSizeF pillSize(metrics.horizontalAdvance(text) + 18, metrics.height() + 8);
            const QRectF pill = media ? QRectF(QPointF(10, 10), pillSize) : QRectF(QPointF(width() - pillSize.width() - 12, height() - pillSize.height() - 12), pillSize);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(10, 12, 20, 170));
            painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            painter.setPen(Qt::white);
            painter.drawText(pill, Qt::AlignCenter, text);
            if (media) {
                m_pagerPrev = m_pagerNext = m_playRect = m_trackRect = QRectF();
                paintPlayerBar(painter);
            }
            return;
        }
        painter.setClipPath(shape);
        QFont bold = font();
        bold.setPointSizeF(bold.pointSizeF() * 0.9);
        bold.setWeight(QFont::Bold);
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.78);
        const QFontMetricsF boldFm(bold), smallFm(small);
        // the kind and the size, top left: one badge, a dot of the kind's colour
        if (m_badge.isEmpty() && !m_sizeText.isEmpty()) {
            // (the kind is in the panel's title: the size alone, top left)
            const QRectF pill(10, 10, smallFm.horizontalAdvance(m_sizeText) + 18, boldFm.height() + 6);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(10, 12, 18, 150));
            painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            painter.setFont(small);
            painter.setPen(QColor(255, 255, 255, 230));
            painter.drawText(pill, Qt::AlignCenter, m_sizeText);
        }
        if (!m_badge.isEmpty()) {
            const QString size = m_sizeText.isEmpty() ? QString() : QStringLiteral(" · ") + m_sizeText;
            const qreal w = 26 + boldFm.horizontalAdvance(m_badge) + smallFm.horizontalAdvance(size) + 10;
            const QRectF badge(10, 10, w, boldFm.height() + 8);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(10, 12, 18, 168));
            painter.drawRoundedRect(badge, badge.height() / 2, badge.height() / 2);
            painter.setBrush(m_badgeTint.isValid() ? m_badgeTint : QColor(140, 150, 170));
            painter.drawEllipse(QPointF(badge.left() + 13, badge.center().y()), 4, 4);
            painter.setFont(bold);
            painter.setPen(Qt::white);
            const QRectF text = badge.adjusted(22, 0, -8, 0);
            painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, m_badge);
            painter.setFont(small);
            painter.setPen(QColor(255, 255, 255, 185));
            painter.drawText(text.adjusted(boldFm.horizontalAdvance(m_badge), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, size);
        }
        m_pagerPrev = m_pagerNext = m_playRect = m_trackRect = QRectF();
        switch (m_bar) {
        case NgFileFacts::Photo:
            paintPhotoBar(painter);
            break;
        case NgFileFacts::Pages:
            paintPagesBar(painter);
            break;
        case NgFileFacts::Video:
        case NgFileFacts::Audio:
            paintPlayerBar(painter);
            break;
        case NgFileFacts::Code:
            paintCodeBar(painter);
            break;
        case NgFileFacts::Archive:
            paintArchiveBar(painter);
            break;
        default:
            paintGenericBar(painter);
            break;
        }
    }

    // ---- the bars: one band fused to the foot of the preview, in segments divided by hairlines, each in the manner of
    // its kind of file (a camera's display, the foot of a page, a player, an editor's status line, a gauge)
    struct Segment {
        QString value;
        QString word; // under the value (empty: the value alone)
        int keep = 0; // the lowest goes first when the band is too narrow
        bool grow = false; // takes the room left
        int icon = 0; // 1 aperture, 2 shutter
    };
    QString detailText(const char *key) const
    {
        return m_detail.value(QLatin1String(key)).toString();
    }
    static QString clock(double seconds)
    {
        const int total = qRound(seconds), h = total / 3600, m = total / 60 % 60, sec = total % 60;
        return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(m).arg(sec, 2, 10, QLatin1Char('0'));
    }
    // a band over a picture (a photo, a video's frames) stays dark, to read over any picture; over anything else it takes
    // the theme: light with dark words on a light scheme
    // where what the preview lists or shows of a text starts: under the size's pill at the top left when there is one,
    // else at the top (the kind's badge moved to the panel's title: no more room kept for it)
    qreal topRoom() const
    {
        return (m_badge.isEmpty() && m_sizeText.isEmpty()) ? 10 : QFontMetricsF(font()).height() + 18;
    }
    bool lightBand() const
    {
        // (a folder's or an archive's list is not a picture, even with the folder's thumbnail behind it)
        return !ngIsDark(QApplication::palette()) && ((m_pixmap.isNull() && m_frame.isNull()) || m_listing);
    }
    QColor bandInk(int alpha = 255) const
    {
        return lightBand() ? QColor(30, 33, 40, alpha) : QColor(255, 255, 255, alpha);
    }
    void drawBand(QPainter &painter, const QRectF &band, bool paper) const
    {
        QLinearGradient ground(0, band.top(), 0, band.bottom());
        if (!paper && lightBand()) {
            ground.setColorAt(0, QColor(252, 252, 253, 225));
            ground.setColorAt(1, QColor(246, 247, 249, 245));
            painter.fillRect(band, ground);
            painter.fillRect(QRectF(band.left(), band.top(), band.width(), 1), QColor(0, 0, 0, 22));
            return;
        }
        if (paper) {
            ground.setColorAt(0, QColor(246, 244, 238, 230));
            ground.setColorAt(1, QColor(246, 244, 238, 245));
        } else {
            ground.setColorAt(0, QColor(8, 10, 16, 178));
            ground.setColorAt(1, QColor(8, 10, 16, 220));
        }
        painter.fillRect(band, ground);
        painter.fillRect(QRectF(band.left(), band.top(), band.width(), 1), paper ? QColor(0, 0, 0, 22) : QColor(255, 255, 255, 26));
    }
    // the segments laid left to right in @p band; the ones that do not fit go, lowest keep first
    void drawSegments(QPainter &painter, const QRectF &band, QList<Segment> segments, const QFont &valueFont, const QFont &wordFont, const QColor &ink,
                      const QColor &dim, const QColor &hair) const
    {
        const QFontMetricsF vfm(valueFont), wfm(wordFont);
        const qreal pad = 10, iconRoom = 17;
        auto widthOf = [&](const Segment &segment) {
            return 2 * pad + qMax(vfm.horizontalAdvance(segment.value) + (segment.icon ? iconRoom : 0), segment.word.isEmpty() ? 0.0 : wfm.horizontalAdvance(segment.word));
        };
        auto total = [&] {
            qreal sum = 0;
            for (const Segment &segment : std::as_const(segments)) {
                sum += widthOf(segment);
            }
            return sum;
        };
        while (segments.size() > 1 && total() > band.width()) {
            auto lowest = std::min_element(segments.begin(), segments.end(), [](const Segment &a, const Segment &b) {
                return a.keep < b.keep;
            });
            segments.erase(lowest);
        }
        qreal spare = qMax(0.0, band.width() - total());
        int growers = int(std::count_if(segments.cbegin(), segments.cend(), [](const Segment &segment) {
            return segment.grow;
        }));
        qreal x = band.left();
        for (int i = 0; i < segments.size(); ++i) {
            const Segment &segment = segments.at(i);
            qreal w = widthOf(segment);
            if (segment.grow && growers > 0) {
                w += spare / growers;
            } else if (growers == 0 && i == segments.size() - 1) {
                w += spare;
            }
            const QRectF cell(x, band.top(), qMin(w, band.right() - x), band.height());
            if (i > 0) {
                painter.fillRect(QRectF(x, band.top() + 7, 1, band.height() - 14), hair);
            }
            QRectF text = cell.adjusted(pad, 0, -pad, 0);
            if (segment.icon) {
                drawIcon(painter, segment.icon, QPointF(text.left() + 6, segment.word.isEmpty() ? cell.center().y() : cell.top() + cell.height() * 0.36), ink);
                text.adjust(iconRoom, 0, 0, 0);
            }
            painter.setFont(valueFont);
            painter.setPen(ink);
            if (segment.word.isEmpty()) {
                painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, vfm.elidedText(segment.value, Qt::ElideRight, text.width()));
            } else {
                const qreal block = vfm.height() + wfm.height() - 2;
                const qreal top = cell.center().y() - block / 2;
                painter.drawText(QRectF(text.left(), top, text.width(), vfm.height()), Qt::AlignLeft | Qt::AlignVCenter, vfm.elidedText(segment.value, Qt::ElideRight, text.width()));
                painter.setFont(wordFont);
                painter.setPen(dim);
                painter.drawText(QRectF(cell.left() + pad, top + vfm.height() - 2, cell.width() - 2 * pad, wfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                                 wfm.elidedText(segment.word, Qt::ElideRight, cell.width() - 2 * pad));
            }
            x += w;
        }
    }
    static void drawIcon(QPainter &painter, int icon, const QPointF &centre, const QColor &ink)
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        QColor line = ink;
        line.setAlphaF(0.8);
        painter.setPen(QPen(line, 1.3));
        painter.setBrush(Qt::NoBrush);
        if (icon == 1) { // aperture: a ring with blades
            painter.drawEllipse(centre, 5.5, 5.5);
            for (int i = 0; i < 4; ++i) {
                const qreal a = i * M_PI / 2 + 0.5;
                painter.drawLine(centre + QPointF(std::cos(a) * 5.5, std::sin(a) * 5.5), centre + QPointF(std::cos(a + 1.9) * 2.2, std::sin(a + 1.9) * 2.2));
            }
        } else if (icon == 2) { // shutter time: a stopwatch
            painter.drawEllipse(centre + QPointF(0, 0.8), 5, 5);
            painter.drawLine(centre + QPointF(0, 0.8), centre + QPointF(0, -2.2));
            painter.drawLine(centre + QPointF(-1.6, -5.6), centre + QPointF(1.6, -5.6));
        }
        painter.restore();
    }
    QFont monoFont(qreal scale) const
    {
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSizeF(font().pointSizeF() * scale);
        return mono;
    }

    void paintPhotoBar(QPainter &painter)
    {
        // a camera's display: aperture, time, ISO, pixels in one band of fixed-width type; the camera and the date written
        // on the picture just above, as a print's stamp; a small histogram of the picture at the end
        QFont mono = monoFont(0.86);
        mono.setWeight(QFont::DemiBold);
        const qreal h = QFontMetricsF(mono).height() + 26;
        const QRectF band(0, height() - h, width(), h);
        QList<Segment> segments;
        if (const QString f = detailText("fnumber"); !f.isEmpty()) {
            segments.append({f, QString(), 6, false, 1});
        }
        if (const QString t = detailText("shutter"); !t.isEmpty()) {
            segments.append({t, QString(), 5, false, 2});
        }
        if (const int iso = m_detail.value(QStringLiteral("iso")).toInt(); iso > 0) {
            segments.append({QStringLiteral("ISO %1").arg(iso), QString(), 4});
        }
        const int w = m_detail.value(QStringLiteral("width")).toInt(), hh = m_detail.value(QStringLiteral("height")).toInt();
        if (w > 0 && hh > 0) {
            segments.append({QStringLiteral("%1×%2").arg(w).arg(hh), QString(), 3, true});
        }
        const QImage histogram = histogramImage();
        const qreal histogramWidth = !histogram.isNull() && width() > 300 ? 62 : 0;
        drawBand(painter, band, false);
        drawSegments(painter, band.adjusted(0, 0, -histogramWidth, 0), segments, mono, mono, Qt::white, QColor(255, 255, 255, 150), QColor(255, 255, 255, 34));
        if (histogramWidth > 0) {
            painter.fillRect(QRectF(band.right() - histogramWidth, band.top() + 7, 1, band.height() - 14), QColor(255, 255, 255, 34));
            painter.drawImage(QRectF(band.right() - histogramWidth + 8, band.top() + 7, histogramWidth - 16, band.height() - 14), histogram);
        }
        // the stamp: camera and lens on the left, when it was taken on the right
        QStringList left;
        if (!m_camera.isEmpty()) {
            left << m_camera;
        }
        if (const QString focal = detailText("focal"); !focal.isEmpty()) {
            left << focal;
        }
        const QDateTime taken = m_detail.value(QStringLiteral("taken")).toDateTime();
        const QString right = taken.isValid() ? QLocale().toString(taken.date(), QLocale::ShortFormat) + QStringLiteral(" · ") + taken.time().toString(QStringLiteral("HH:mm")) : QString();
        if (!left.isEmpty() || !right.isEmpty()) {
            QFont stamp = font();
            stamp.setPointSizeF(stamp.pointSizeF() * 0.8);
            painter.setFont(stamp);
            const QFontMetricsF fm(stamp);
            const QRectF line(12, band.top() - fm.height() - 5, width() - 24, fm.height());
            QLinearGradient veil(0, line.top() - 14, 0, band.top());
            veil.setColorAt(0, QColor(0, 0, 0, 0));
            veil.setColorAt(1, QColor(0, 0, 0, 120));
            painter.fillRect(QRectF(0, line.top() - 14, width(), band.top() - line.top() + 14), veil);
            const QString l = left.join(QStringLiteral(" · "));
            for (int pass = 0; pass < 2; ++pass) {
                painter.setPen(pass == 0 ? QColor(0, 0, 0, 150) : QColor(255, 255, 255, 225));
                const QRectF at = line.translated(0, pass == 0 ? 1 : 0);
                painter.drawText(at, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(l, Qt::ElideRight, line.width() - fm.horizontalAdvance(right) - 12));
                painter.drawText(at, Qt::AlignRight | Qt::AlignVCenter, right);
            }
        }
    }
    // a small luminance histogram of the picture shown, kept for that picture
    QImage histogramImage()
    {
        if (m_pixmap.isNull()) {
            return QImage();
        }
        if (m_histogramKey == m_pixmap.cacheKey() && !m_histogram.isNull()) {
            return m_histogram;
        }
        const QImage small = m_pixmap.toImage().scaled(96, 96, Qt::KeepAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
        int bins[48] = {};
        for (int y = 0; y < small.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
            for (int x = 0; x < small.width(); ++x) {
                const int l = (qRed(line[x]) * 299 + qGreen(line[x]) * 587 + qBlue(line[x]) * 114) / 1000;
                ++bins[qBound(0, l * 48 / 256, 47)];
            }
        }
        const int top = *std::max_element(std::begin(bins), std::end(bins));
        QImage out(48 * 2, 48, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.moveTo(0, 48);
        for (int i = 0; i < 48; ++i) {
            path.lineTo(i * 2 + 1, 48 - (top > 0 ? 46.0 * std::sqrt(double(bins[i]) / top) : 0));
        }
        path.lineTo(96, 48);
        path.closeSubpath();
        p.fillPath(path, QColor(255, 255, 255, 150));
        p.end();
        m_histogram = out;
        m_histogramKey = m_pixmap.cacheKey();
        return m_histogram;
    }

    void paintPagesBar(QPainter &painter)
    {
        // the foot of a page: a band the colour of paper, the pages turned from it (a PDF), the words and the reading
        QFont value = font();
        value.setPointSizeF(value.pointSizeF() * 0.9);
        value.setWeight(QFont::Bold);
        QFont word = font();
        word.setPointSizeF(word.pointSizeF() * 0.74);
        const qreal h = QFontMetricsF(value).height() + QFontMetricsF(word).height() + 18;
        const QRectF band(0, height() - h, width(), h);
        drawBand(painter, band, true);
        const QColor ink(42, 42, 48), dim(42, 42, 48, 140), hair(0, 0, 0, 28);
        const int pages = m_detail.value(QStringLiteral("pages")).toInt();
        qreal left = 0;
        if (m_pageable && pages > 1) {
            // ‹ 3 / 12 ›
            const QFontMetricsF fm(value);
            const QString label = QStringLiteral("%1 / %2").arg(m_page).arg(pages);
            const qreal arrow = 24, labelWidth = fm.horizontalAdvance(QStringLiteral("%1 / %2").arg(pages).arg(pages)) + 8;
            m_pagerPrev = QRectF(6, band.center().y() - arrow / 2, arrow, arrow);
            const QRectF labelRect(m_pagerPrev.right(), band.top(), labelWidth, band.height());
            m_pagerNext = QRectF(labelRect.right(), m_pagerPrev.top(), arrow, arrow);
            painter.setFont(value);
            painter.setPen(m_page > 1 ? ink : QColor(42, 42, 48, 70));
            painter.drawText(m_pagerPrev, Qt::AlignCenter, QStringLiteral("‹"));
            painter.setPen(m_page < pages ? ink : QColor(42, 42, 48, 70));
            painter.drawText(m_pagerNext, Qt::AlignCenter, QStringLiteral("›"));
            painter.setPen(ink);
            painter.drawText(labelRect, Qt::AlignCenter, label);
            left = m_pagerNext.right() + 6;
            painter.fillRect(QRectF(left, band.top() + 7, 1, band.height() - 14), hair);
        }
        QList<Segment> segments;
        if (pages > 0 && left == 0) {
            segments.append({QLocale().toString(pages), pages == 1 ? ngTr("page", "pagina") : ngTr("pages", "pagine"), 5});
        }
        if (const qint64 words = m_detail.value(QStringLiteral("words")).toLongLong(); words > 0) {
            segments.append({QLocale().toString(words), ngTr("words", "parole"), 4, true});
        }
        if (const qint64 minutes = m_detail.value(QStringLiteral("minutes")).toLongLong(); minutes > 0) {
            segments.append({QStringLiteral("%1 min").arg(minutes), ngTr("to read", "di lettura"), 3});
        }
        if (segments.isEmpty() && !m_sizeText.isEmpty()) {
            segments.append({m_sizeText, ngTr("size", "dimensione"), 1, true});
        }
        drawSegments(painter, band.adjusted(left, 0, 0, 0), segments, value, word, ink, dim, hair);
    }

    void paintPlayerBar(QPainter &painter)
    {
        // a player's bar: the line of time across, under it what the file is on the left and how long it lasts on the right
        const bool video = m_bar == NgFileFacts::Video;
        QFont value = font();
        value.setPointSizeF(value.pointSizeF() * 0.9);
        value.setWeight(QFont::Bold);
        QFont word = font();
        word.setPointSizeF(word.pointSizeF() * 0.8);
        const QFontMetricsF vfm(value), wfm(word);
        const qreal h = vfm.height() + 34;
        const QRectF band(0, height() - h, width(), h);
        drawBand(painter, band, false);
        // ▶ / ⏸ on the left, the line of time beside it (it seeks), the time played and the length under it
        const qreal button = 26;
        m_playRect = m_mediaUrl.isEmpty() ? QRectF() : QRectF(10, band.top() + (band.height() - button) / 2, button, button);
        const qreal lineLeft = m_playRect.isNull() ? 12 : m_playRect.right() + 10;
        const QColor accent = m_badgeTint.isValid() ? m_badgeTint : QColor(240, 160, 64);
        if (!m_playRect.isNull()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(bandInk(playing() ? 235 : 215));
            painter.drawEllipse(m_playRect);
            painter.setBrush(lightBand() ? QColor(255, 255, 255) : QColor(12, 14, 20));
            const QPointF c = m_playRect.center();
            if (playing()) {
                painter.drawRect(QRectF(c.x() - 4.5, c.y() - 5, 3, 10));
                painter.drawRect(QRectF(c.x() + 1.5, c.y() - 5, 3, 10));
            } else {
                const QPointF triangle[3] = {c + QPointF(-3.5, -6), c + QPointF(6, 0), c + QPointF(-3.5, 6)};
                painter.drawPolygon(triangle, 3);
            }
        }
        const QRectF track(lineLeft, band.top() + 11, width() - lineLeft - 12, 4);
        m_trackRect = m_mediaUrl.isEmpty() ? QRectF() : track;
        const double seconds = m_player && m_player->duration() > 0 ? m_player->duration() / 1000.0 : m_detail.value(QStringLiteral("duration")).toDouble();
        const double at = m_player ? m_player->position() / 1000.0 : 0;
        const qreal done = seconds > 0 ? qBound(0.0, at / seconds, 1.0) : 0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(bandInk(60));
        painter.drawRoundedRect(track, 2, 2);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * done, track.height()), 2, 2);
        painter.drawEllipse(QPointF(track.left() + track.width() * done, track.center().y()), 5, 5);
        const QString length = seconds > 0 ? (m_player ? clock(at) + QStringLiteral(" / ") + clock(seconds) : clock(seconds)) : QString();
        QStringList what;
        if (video) {
            const int w = m_detail.value(QStringLiteral("width")).toInt(), hh = m_detail.value(QStringLiteral("height")).toInt();
            if (hh >= 2160) {
                what << QStringLiteral("4K");
            } else if (hh > 0) {
                what << (w * 9 == hh * 16 ? QStringLiteral("%1p").arg(hh) : QStringLiteral("%1×%2").arg(w).arg(hh));
            }
            if (const double fps = m_detail.value(QStringLiteral("fps")).toDouble(); fps > 0) {
                what << QStringLiteral("%1 fps").arg(QLocale().toString(fps, 'g', 3));
            }
            if (const qint64 bitrate = m_detail.value(QStringLiteral("bitrate")).toLongLong(); bitrate > 0) {
                what << QStringLiteral("%1 Mbit/s").arg(QLocale().toString(bitrate / 1e6, 'f', 1));
            }
        } else {
            const QString artist = detailText("artist"), title = detailText("title");
            if (!artist.isEmpty() || !title.isEmpty()) {
                what << (artist.isEmpty() ? title : title.isEmpty() ? artist : artist + QStringLiteral(" · ") + title);
            }
        }
        QStringList quality;
        if (!video) {
            if (const int rate = m_detail.value(QStringLiteral("sampleRate")).toInt(); rate > 0) {
                quality << QStringLiteral("%1 kHz").arg(QLocale().toString(rate / 1000.0, 'g', 3));
            }
            const int channels = m_detail.value(QStringLiteral("channels")).toInt();
            if (channels > 0) {
                quality << (channels == 1 ? ngTr("mono", "mono") : channels == 2 ? ngTr("stereo", "stereo") : QStringLiteral("%1 ch").arg(channels));
            }
        }
        const QRectF row(lineLeft, track.bottom() + 3, width() - lineLeft - 12, band.bottom() - track.bottom() - 6);
        painter.setFont(value);
        painter.setPen(bandInk());
        const qreal lengthWidth = vfm.horizontalAdvance(length);
        painter.drawText(row, Qt::AlignRight | Qt::AlignVCenter, length);
        const QString q = quality.join(QStringLiteral(" · "));
        painter.setFont(word);
        painter.setPen(bandInk(170));
        const qreal qWidth = q.isEmpty() ? 0 : qMin(wfm.horizontalAdvance(q), row.width() * 0.4);
        if (!q.isEmpty()) {
            painter.drawText(row.adjusted(0, 0, -lengthWidth - 10, 0), Qt::AlignRight | Qt::AlignVCenter, wfm.elidedText(q, Qt::ElideRight, qWidth));
        }
        painter.setPen(bandInk(215));
        painter.drawText(row.adjusted(0, 0, -lengthWidth - qWidth - 20, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         wfm.elidedText(what.join(QStringLiteral(" · ")), Qt::ElideRight, row.width() - lengthWidth - qWidth - 20));
    }

    void paintCodeBar(QPainter &painter)
    {
        // an editor's status line: the language in its colour, then lines, encoding and line ends, indentation
        QFont mono = monoFont(0.8);
        const qreal h = QFontMetricsF(mono).height() + 16;
        const QRectF band(0, height() - h, width(), h);
        painter.fillRect(band, QColor(27, 31, 42, 240));
        QList<Segment> segments;
        const QString language = detailText("language");
        if (const qint64 lines = m_detail.value(QStringLiteral("lines")).toLongLong(); lines > 0) {
            segments.append({lines == 1 ? ngTr("1 line", "1 riga") : ngTr("%1 lines", "%1 righe").arg(QLocale().toString(lines)), QString(), 4});
        }
        QStringList encoding;
        if (const QString e = detailText("encoding"); !e.isEmpty()) {
            encoding << e;
        }
        if (const QString e = detailText("eol"); !e.isEmpty()) {
            encoding << e;
        }
        if (!encoding.isEmpty()) {
            segments.append({encoding.join(QStringLiteral(" · ")), QString(), 3, true});
        }
        if (const QString indent = detailText("indent"); !indent.isEmpty()) {
            segments.append({indent, QString(), 2});
        }
        qreal left = 0;
        if (!language.isEmpty()) {
            const QFontMetricsF fm(mono);
            const QString label = QStringLiteral("● ") + language;
            const QRectF chip(0, band.top(), qMin(fm.horizontalAdvance(label) + 20, width() * 0.45), band.height());
            painter.fillRect(chip, m_badgeTint.isValid() ? m_badgeTint : QColor(76, 195, 138));
            painter.setFont(mono);
            painter.setPen(QColor(13, 26, 18));
            painter.drawText(chip.adjusted(10, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(label, Qt::ElideRight, chip.width() - 16));
            left = chip.width();
        }
        drawSegments(painter, band.adjusted(left, 0, 0, 0), segments, mono, mono, QColor(220, 226, 236), QColor(220, 226, 236, 150), QColor(255, 255, 255, 22));
    }

    void paintArchiveBar(QPainter &painter)
    {
        // a gauge of the squeeze: how much it weighed, how much it weighs; how many files and folders
        QFont value = font();
        value.setPointSizeF(value.pointSizeF() * 0.86);
        QFont strong = value;
        strong.setWeight(QFont::Bold);
        const QFontMetricsF fm(value);
        const qreal h = fm.height() + 32;
        const QRectF band(0, height() - h, width(), h);
        drawBand(painter, band, false);
        const qint64 unpacked = m_detail.value(QStringLiteral("unpacked")).toLongLong(), packed = m_detail.value(QStringLiteral("packed")).toLongLong();
        const QRectF meter(12, band.top() + 12, width() - 24, 5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bandInk(40));
        painter.drawRoundedRect(meter, 2.5, 2.5);
        if (unpacked > 0 && packed > 0) {
            painter.setBrush(m_badgeTint.isValid() ? m_badgeTint : QColor(217, 179, 74));
            painter.drawRoundedRect(QRectF(meter.left(), meter.top(), meter.width() * qBound(0.03, double(packed) / unpacked, 1.0), meter.height()), 2.5, 2.5);
        }
        const QRectF row(12, meter.bottom() + 2, width() - 24, band.bottom() - meter.bottom() - 4);
        const qint64 files = m_detail.value(QStringLiteral("files")).toLongLong(), folders = m_detail.value(QStringLiteral("folders")).toLongLong();
        QString count = files == 1 ? ngTr("1 file", "1 file") : ngTr("%1 files", "%1 file").arg(QLocale().toString(files));
        if (folders > 0) {
            count += QStringLiteral(" · ") + (folders == 1 ? ngTr("1 folder", "1 cartella") : ngTr("%1 folders", "%1 cartelle").arg(QLocale().toString(folders)));
        }
        painter.setFont(value);
        painter.setPen(bandInk(190));
        const qreal countWidth = fm.horizontalAdvance(count);
        painter.drawText(row, Qt::AlignRight | Qt::AlignVCenter, count);
        if (unpacked > 0) {
            QString squeeze = KIO::convertSize(KIO::filesize_t(unpacked)) + QStringLiteral(" → ") + KIO::convertSize(KIO::filesize_t(packed));
            if (const int saved = unpacked > packed ? qRound(100.0 * (unpacked - packed) / unpacked) : 0; saved >= 1) {
                squeeze += ngTr(" · %1% less", " · %1% in meno").arg(saved); // (pictures do not shrink: nothing said)
            }
            painter.setFont(strong);
            painter.setPen(bandInk());
            painter.drawText(row.adjusted(0, 0, -countWidth - 12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                             QFontMetricsF(strong).elidedText(squeeze, Qt::ElideRight, row.width() - countWidth - 12));
        }
    }

    void paintGenericBar(QPainter &painter)
    {
        // anything else (a folder, several items): the facts given, in the same band
        if (m_chips.isEmpty()) {
            return;
        }
        QFont value = font();
        value.setPointSizeF(value.pointSizeF() * 0.9);
        value.setWeight(QFont::Bold);
        QFont word = font();
        word.setPointSizeF(word.pointSizeF() * 0.74);
        bool words = false;
        for (const auto &chip : std::as_const(m_chips)) {
            words = words || !chip.second.isEmpty();
        }
        const qreal h = QFontMetricsF(value).height() + (words ? QFontMetricsF(word).height() + 14 : 24);
        const QRectF band(0, height() - h, width(), h);
        drawBand(painter, band, false);
        QList<Segment> segments;
        int keep = m_chips.size();
        for (const auto &chip : std::as_const(m_chips)) {
            segments.append({chip.first, chip.second, keep--, false});
        }
        segments.first().grow = true;
        drawSegments(painter, band, segments, value, word, bandInk(), bandInk(150), bandInk(34));
    }

private:
    QString m_badge;
    QColor m_badgeTint;
    QList<std::pair<QString, QString>> m_chips;
    QString m_sizeText;
    NgFileFacts::Bar m_bar = NgFileFacts::NoBar;
    QVariantMap m_detail;
    QList<NgFileFacts::Entry> m_entries;
    QString m_camera;
    bool m_pageable = false; // a PDF: its pages turned from the bar
    int m_page = 1;
    QRectF m_pagerPrev, m_pagerNext;
    std::function<void(int)> m_pageWanted;
    QImage m_histogram;
    qint64 m_histogramKey = 0;
    QUrl m_mediaUrl;
    bool m_mediaVideo = false;
    QMediaPlayer *m_player = nullptr;
    QImage m_frame;
    QElapsedTimer m_frameClock;
    QRectF m_playRect, m_trackRect;
    bool m_seeking = false;
    bool m_listing = false;
    QPixmap m_pixmap;
    QList<QPixmap> m_stack;
    QStringList m_lines;
    QString m_icon;
    QColor m_tint;
    bool m_canOpen = false;
    QToolButton *m_eye = nullptr;
    static constexpr qreal MinRatio = 0.4;
    static constexpr qreal MaxRatio = 1.6;
    qreal m_ratio = 0.75;
    qreal m_dragFrom = -1;
    int m_dragHeight = 0;
    static constexpr qreal MaxZoom = 8.0;
    qreal m_zoom = 1.0;
    QPointF m_zoomTopLeft;
    QToolButton *m_zoomIn = nullptr;
    QToolButton *m_zoomOut = nullptr;
    std::optional<QPointF> m_panFrom;
    std::function<void(int)> m_sharperWanted;
    QTimer *m_sharpTimer = nullptr;
};

// ---- the three facts of the Dossier: a value large, its word under it
class NgInfoFacts : public QWidget
{
public:
    using QWidget::QWidget;
    void setFacts(const QList<std::pair<QString, QString>> &facts)
    {
        m_facts = facts;
        setVisible(!facts.isEmpty());
        update();
    }
    QSize sizeHint() const override
    {
        return QSize(260, fontMetrics().height() * 2 + 30);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_facts.isEmpty()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int count = int(m_facts.count());
        const qreal gap = 8, tileWidth = (width() - gap * (count - 1)) / count;
        QFont big = font();
        big.setPointSizeF(big.pointSizeF() * 1.15);
        big.setWeight(QFont::Bold);
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.82);
        QColor dim = palette().color(QPalette::WindowText);
        dim.setAlphaF(0.62);
        for (int i = 0; i < count; ++i) {
            const QRectF tile(i * (tileWidth + gap), 0, tileWidth, height());
            QPainterPath path;
            path.addRoundedRect(tile, 10, 10);
            painter.fillPath(path, ngVeil(palette()));
            const QRectF inner = tile.adjusted(12, 10, -10, -10);
            painter.setFont(big);
            painter.setPen(palette().color(QPalette::WindowText));
            // (a long value shrinks rather than losing its end: "4032×3024" whole)
            QFont fitted = big;
            while (QFontMetricsF(fitted).horizontalAdvance(m_facts.at(i).first) > inner.width() && fitted.pointSizeF() > big.pointSizeF() * 0.7) {
                fitted.setPointSizeF(fitted.pointSizeF() * 0.94);
            }
            painter.setFont(fitted);
            ngInfoText(painter, inner, Qt::AlignLeft | Qt::AlignTop, QFontMetricsF(fitted).elidedText(m_facts.at(i).first, Qt::ElideRight, inner.width()));
            painter.setFont(small);
            painter.setPen(dim);
            ngInfoText(painter, inner, Qt::AlignLeft | Qt::AlignBottom, QFontMetricsF(small).elidedText(m_facts.at(i).second, Qt::ElideRight, inner.width()), 0.5);
        }
    }

private:
    QList<std::pair<QString, QString>> m_facts;
};

// ---- what a folder (or a selection) holds: the bar of its kinds, then one row per kind (its colour, its name, how
// many), each row with a faint band of its colour as long as its share: a small chart that reads as a list. The six
// largest; the rest in one row
class NgInfoMix : public QWidget
{
public:
    using QWidget::QWidget;
    struct Part {
        QColor colour;
        int count;
        QString label; // "190 documents": the number first, then the word
    };
    void setParts(const QList<Part> &parts)
    {
        m_parts = parts;
        std::sort(m_parts.begin(), m_parts.end(), [](const Part &a, const Part &b) {
            return a.count > b.count;
        });
        setVisible(!m_parts.isEmpty());
        updateGeometry();
        update();
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int) const override
    {
        if (m_parts.isEmpty()) {
            return 0;
        }
        return qMax(kRing, rowCount() * rowHeight()) + 2 * kPad;
    }
    QSize sizeHint() const override
    {
        return QSize(260, heightForWidth(260));
    }

protected:
    // a ring of the kinds a folder holds, its total in the middle, and beside it the first kinds with how many and what
    // share; the rest together in a last row. (A bar and a row per kind took some 230 px for little.)
    void paintEvent(QPaintEvent *) override
    {
        if (m_parts.isEmpty()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        int total = 0;
        for (const Part &part : std::as_const(m_parts)) {
            total += part.count;
        }
        const QColor ink = palette().color(QPalette::WindowText);
        // the card
        QPainterPath card;
        card.addRoundedRect(QRectF(rect()), 12, 12);
        painter.fillPath(card, ngVeil(palette()));
        // the ring: thin, round ends, a little air between the kinds, from the top clockwise
        const QRectF ringBox(kPad + kStroke / 2.0, (height() - kRing) / 2.0 + kStroke / 2.0, kRing - kStroke, kRing - kStroke);
        const qreal radius = ringBox.width() / 2.0;
        const qreal capDegrees = qRadiansToDegrees((kStroke / 2.0) / radius); // (how far a round end reaches)
        const qreal gap = m_parts.count() > 1 ? 3.0 : 0.0;
        QColor track = ink;
        track.setAlphaF(0.07);
        painter.setPen(QPen(track, kStroke));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ringBox);
        qreal start = 90.0;
        for (const Part &part : std::as_const(m_parts)) {
            const qreal share = 360.0 * part.count / qMax(1, total);
            const qreal arc = share - gap - (m_parts.count() > 1 ? 2 * capDegrees : 0);
            if (arc > 0.2) {
                painter.setPen(QPen(part.colour, kStroke, Qt::SolidLine, Qt::RoundCap));
                painter.drawArc(ringBox, qRound((start - (m_parts.count() > 1 ? capDegrees + gap / 2 : 0)) * 16), -qRound(arc * 16));
            } else {
                // (a sliver: a dot of its colour on the ring)
                const qreal mid = qDegreesToRadians(start - share / 2);
                painter.setPen(Qt::NoPen);
                painter.setBrush(part.colour);
                painter.drawEllipse(ringBox.center() + QPointF(std::cos(mid) * radius, -std::sin(mid) * radius), kStroke / 2.6, kStroke / 2.6);
            }
            start -= share;
        }
        // the total in the middle
        QFont big = font();
        big.setPointSizeF(big.pointSizeF() * 1.15);
        big.setWeight(QFont::Bold);
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.74);
        const QFontMetricsF bigFm(big), smallFm(small);
        const QString totalText = QLocale().toString(total);
        const qreal block = bigFm.height() + smallFm.height() - 4;
        painter.setPen(ink);
        painter.setFont(big);
        painter.drawText(QRectF(ringBox.left(), ringBox.center().y() - block / 2, ringBox.width(), bigFm.height()), Qt::AlignCenter, totalText);
        painter.setFont(small);
        QColor dim = ink;
        dim.setAlphaF(0.55);
        painter.setPen(dim);
        painter.drawText(QRectF(ringBox.left() - 8, ringBox.center().y() - block / 2 + bigFm.height() - 4, ringBox.width() + 16, smallFm.height()), Qt::AlignCenter,
                         total == 1 ? ngTr("item", "elemento") : ngTr("items", "elementi"));
        // the first kinds, the rest together
        QList<Part> rows = m_parts.mid(0, kMaxRows);
        if (m_parts.count() > kMaxRows) {
            rows.removeLast();
            int rest = 0;
            for (int i = kMaxRows - 1; i < m_parts.count(); ++i) {
                rest += m_parts.at(i).count;
            }
            QColor grey = ink;
            grey.setAlphaF(0.30);
            const int kinds = int(m_parts.count()) - kMaxRows + 1;
            rows.append({grey, rest, QStringLiteral("%1 ").arg(rest) + ngTr("%1 other kinds", "altri %1 tipi").arg(kinds)});
        }
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        bold.setFeature(QFont::Tag("tnum"), 1);
        QFont label = font();
        label.setPointSizeF(label.pointSizeF() * 0.94);
        const QFontMetricsF boldFm(bold), labelFm(label);
        const qreal left = kPad + kRing + 16, right = width() - kPad;
        const qreal percentWidth = smallFm.horizontalAdvance(QStringLiteral("100%")) + 4;
        qreal y = (height() - rows.count() * rowHeight()) / 2.0;
        for (const Part &part : std::as_const(rows)) {
            const QRectF row(left, y, right - left, rowHeight());
            painter.setPen(Qt::NoPen);
            painter.setBrush(part.colour);
            painter.drawRoundedRect(QRectF(row.left(), row.center().y() - 4, 8, 8), 2.5, 2.5);
            const QString number = QLocale().toString(part.count);
            const QString percent = QStringLiteral("%1%").arg(qRound(100.0 * part.count / qMax(1, total)));
            QString word = part.label.section(QLatin1Char(' '), 1);
            if (!word.isEmpty()) {
                word[0] = word.at(0).toUpper();
            }
            painter.setFont(small);
            painter.setPen(dim);
            painter.drawText(QRectF(row.right() - percentWidth, row.top(), percentWidth, row.height()), Qt::AlignRight | Qt::AlignVCenter, percent);
            const qreal numberWidth = boldFm.horizontalAdvance(number);
            painter.setFont(bold);
            painter.setPen(ink);
            ngInfoText(painter, QRectF(row.right() - percentWidth - 8 - numberWidth, row.top(), numberWidth, row.height()), Qt::AlignRight | Qt::AlignVCenter, number);
            painter.setFont(label);
            const QRectF wordRect(row.left() + 15, row.top(), row.width() - 15 - percentWidth - 16 - numberWidth, row.height());
            ngInfoText(painter, wordRect, Qt::AlignLeft | Qt::AlignVCenter, labelFm.elidedText(word, Qt::ElideRight, wordRect.width()));
            y += rowHeight();
        }
    }

private:
    static constexpr int kMaxRows = 5;
    static constexpr int kRing = 86;
    static constexpr int kStroke = 9;
    static constexpr int kPad = 12;
    int rowCount() const
    {
        return qMin(int(m_parts.count()), kMaxRows);
    }
    int rowHeight() const
    {
        return fontMetrics().height() + 3;
    }
    QList<Part> m_parts;
};

// ---- the dates on one line: created, modified, opened
class NgInfoDates : public QWidget
{
public:
    using QWidget::QWidget;
    void setDates(const QList<std::pair<QString, QString>> &dates)
    {
        m_dates = dates;
        setVisible(!dates.isEmpty());
        update();
    }
    QSize sizeHint() const override
    {
        return QSize(260, fontMetrics().height() * 2 + 22);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_dates.isEmpty()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 10, 10);
        painter.fillPath(path, ngVeil(palette()));
        const qreal w = width() / qreal(m_dates.count());
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.82);
        QFont bold = font();
        bold.setPointSizeF(bold.pointSizeF() * 0.9);
        bold.setWeight(QFont::DemiBold);
        QColor dim = palette().color(QPalette::WindowText);
        dim.setAlphaF(0.62);
        QColor line = palette().color(QPalette::WindowText);
        line.setAlphaF(0.12);
        for (int i = 0; i < m_dates.count(); ++i) {
            const QRectF cell(i * w + 12, 9, w - 20, height() - 18);
            if (i > 0) {
                painter.setPen(QPen(line, 1));
                painter.drawLine(QPointF(i * w + 0.5, 9), QPointF(i * w + 0.5, height() - 9));
            }
            painter.setFont(small);
            painter.setPen(dim);
            ngInfoText(painter, cell, Qt::AlignLeft | Qt::AlignTop, m_dates.at(i).first, 0.5);
            painter.setFont(bold);
            painter.setPen(palette().color(QPalette::WindowText));
            ngInfoText(painter, cell, Qt::AlignLeft | Qt::AlignBottom, QFontMetricsF(bold).elidedText(m_dates.at(i).second, Qt::ElideRight, cell.width()));
        }
    }

private:
    QList<std::pair<QString, QString>> m_dates;
};

// ---- the view
NgInfoView::NgInfoView(QWidget *parent)
    : QScrollArea(parent)
{
    // (Dolphin's panel opened at its narrow size hint: the preview, the facts and the pill need about this much)
    setMinimumWidth(260);
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);
    viewport()->installEventFilter(this); // (its width changes alone when the scroll bar comes or goes)
    build();
    connect(NgActivity::self(), &NgActivity::changed, this, &NgInfoView::showActivity);
}

NgInfoView::~NgInfoView()
{
    if (m_sizeJob) {
        m_sizeJob->kill();
    }
    if (m_previewJob) {
        m_previewJob->kill();
    }
}

void NgInfoView::setPinnedQuery(std::function<bool(const KFileItemList &)> query)
{
    m_pinnedQuery = std::move(query);
}

bool NgInfoView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_who && event->type() == QEvent::MouseButtonRelease && m_lastChanged.isValid()
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        Q_EMIT selectRequested(m_lastChanged);
        return true;
    }
    if (watched == viewport() && event->type() == QEvent::Resize && m_body) {
        m_body->setFixedWidth(stableWidth());
        layoutActions();
    }
    // the name of the quick action under the pointer, under the pill
    if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) {
        const QString caption = watched->property("_ng_caption").toString();
        if (!caption.isEmpty()) {
            m_actionCaption->setText(caption);
        }
    } else if (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut) {
        if (!watched->property("_ng_caption").toString().isEmpty()) {
            m_actionCaption->setText(ngTr("Quick actions", "Azioni rapide"));
        }
    }
    return QScrollArea::eventFilter(watched, event);
}

void NgInfoView::build()
{
    m_body = new QWidget(this);
    m_body->setAutoFillBackground(false);
    m_layout = new QVBoxLayout(m_body);
    // (room around and between the blocks: packed close they read as one crowded column)
    m_layout->setContentsMargins(16, 12, 16, 20);
    m_layout->setSpacing(16);
    setWidget(m_body);

    m_hero = new NgInfoHero(m_body);
    m_layout->addWidget(m_hero);
    m_hero->setSharperWanted([this](int side) {
        renderSharper(side);
    });
    connect(m_hero->eye(), &QToolButton::clicked, this, [this] {
        if (!m_item.isNull()) {
            Q_EMIT lightboxRequested(m_item);
        }
    });

    m_media = new MediaWidget(m_body);
    // (its video asked for the video's own size, as wide as 1280: the whole panel grew past its edge)
    m_media->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_media->setMinimumWidth(0);
    m_media->hide();
    m_layout->addWidget(m_media);

    // the name, then the kind, the size and pinning on one line
    m_name = new QLabel(m_body);
    m_name->setWordWrap(true);
    QFont nameFont = m_name->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.12);
    nameFont.setWeight(QFont::Bold);
    m_name->setFont(nameFont);
    auto *headGroup = new QVBoxLayout();
    headGroup->setSpacing(8);
    // the name, and beside it a button that copies it: the real name (the label carries invisible break points, and
    // is not selectable)
    auto *nameRow = new QHBoxLayout();
    nameRow->setSpacing(6);
    nameRow->addWidget(m_name, 1);
    m_copyName = new QToolButton(m_body);
    m_copyName->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    m_copyName->setIconSize(QSize(16, 16));
    m_copyName->setAutoRaise(true);
    m_copyName->setToolTip(ngTr("Copy the name", "Copia il nome"));
    connect(m_copyName, &QToolButton::clicked, this, [this] {
        if (m_item.isNull()) {
            return;
        }
        QGuiApplication::clipboard()->setText(m_item.text());
        m_copyName->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok-apply"))); // a moment of confirmation
        m_copyName->setToolTip(ngTr("Copied", "Copiato"));
        QTimer::singleShot(1500, m_copyName, [this] {
            m_copyName->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
            m_copyName->setToolTip(ngTr("Copy the name", "Copia il nome"));
        });
    });
    nameRow->addWidget(m_copyName, 0, Qt::AlignTop);
    // pinning as an icon beside it, filled with the accent while pinned (a pill of its own took a line)
    m_pin = new QToolButton(m_body);
    m_pin->setCheckable(true);
    m_pin->setAutoRaise(true);
    m_pin->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_pin->setIconSize(QSize(16, 16));
    m_pin->setIcon(QIcon::fromTheme(QStringLiteral("window-pin"), QIcon::fromTheme(QStringLiteral("pin"))));
    nameRow->addWidget(m_pin, 0, Qt::AlignTop);
    headGroup->addLayout(nameRow);
    auto *chips = new QHBoxLayout();
    chips->setSpacing(8);
    m_kind = new QLabel(m_body);
    m_size = new QLabel(m_body);
    chips->addWidget(m_kind);
    chips->addStretch();
    headGroup->addLayout(chips);
    // the size on a line of its own (a folder's, with what it holds, took the room of the pin button)
    m_size->setWordWrap(true);
    m_size->setContentsMargins(2, 0, 0, 0);
    headGroup->addWidget(m_size);
    // (first in the panel, above the preview: what it is and its buttons, then what it looks like)
    m_layout->insertLayout(0, headGroup);
    connect(m_pin, &QToolButton::clicked, this, [this](bool on) {
        const KFileItemList items = m_items.isEmpty() ? KFileItemList{m_item} : m_items;
        Q_EMIT pinRequested(items, on);
        updatePinTip();
    });

    // the quick actions: one pill of icons, the name of the one under the pointer below it
    m_actions = new QWidget(m_body);
    m_actions->setObjectName(QStringLiteral("ngInfoActions"));
    m_actions->setAttribute(Qt::WA_StyledBackground);
    m_actionsLayout = new QHBoxLayout(m_actions);
    m_actionsLayout->setContentsMargins(4, 4, 4, 4);
    m_actionsLayout->setSpacing(2);
    auto *actionsGroup = new QVBoxLayout();
    actionsGroup->setSpacing(4);
    auto *actionsRow = new QHBoxLayout();
    actionsRow->addWidget(m_actions);
    actionsRow->addStretch();
    actionsGroup->addLayout(actionsRow);
    m_actionCaption = new QLabel(m_body);
    m_actionCaption->setContentsMargins(8, 0, 0, 0);
    m_actionCaption->setProperty("_ng_dim_label", true); // (dimmed by restyle(), not disabled: see there)
    actionsGroup->addWidget(m_actionCaption);
    m_layout->addLayout(actionsGroup);

    // the work in progress on the file
    m_activity = new QWidget(m_body);
    m_activity->setObjectName(QStringLiteral("ngInfoActivity"));
    m_activity->setAttribute(Qt::WA_StyledBackground);
    auto *activityLayout = new QVBoxLayout(m_activity);
    activityLayout->setContentsMargins(14, 10, 14, 12);
    activityLayout->setSpacing(7);
    m_activityText = new QLabel(m_activity);
    m_activityText->setWordWrap(true);
    m_activityBar = new QProgressBar(m_activity);
    m_activityBar->setTextVisible(false);
    m_activityBar->setFixedHeight(6);
    m_activityBar->setRange(0, 100);
    activityLayout->addWidget(m_activityText);
    activityLayout->addWidget(m_activityBar);
    m_activity->hide();
    m_layout->addWidget(m_activity);

    m_facts = new NgInfoFacts(m_body);
    m_facts->hide();
    m_layout->addWidget(m_facts);
    m_mix = new NgInfoMix(m_body);
    m_mix->hide();
    m_layout->addWidget(m_mix);
    // the dates right under what it is (or what a folder holds), before rating and tags
    m_dates = new NgInfoDates(m_body);
    m_dates->hide();
    m_layout->addWidget(m_dates);

    // where it came from, who made it, its rating and tags
    m_lines = new QWidget(m_body);
    // each label above its value (side by side, a long label and a long value both had half the width)
    m_linesLayout = new QGridLayout(m_lines);
    m_linesLayout->setContentsMargins(4, 0, 4, 0);
    m_linesLayout->setVerticalSpacing(0);
    auto caption = [this](const QString &text) {
        auto *label = new QLabel(text, m_lines);
        label->setProperty("_ng_dim_label", true);
        return label;
    };
    m_originLabel = caption(ngTr("Downloaded from", "Scaricato da"));
    m_origin = new QLabel(m_lines);
    m_origin->setWordWrap(true);
    m_whoLabel = caption(QString());
    m_who = new QLabel(m_lines);
    m_who->setWordWrap(true);
    m_who->installEventFilter(this); // (a folder's "Changed last": a click selects the item, see eventFilter)
    m_stars = new QWidget(m_lines);
    auto *starsLayout = new QHBoxLayout(m_stars);
    starsLayout->setContentsMargins(0, 0, 0, 0);
    starsLayout->setSpacing(0);
    for (int i = 1; i <= 5; ++i) {
        auto *star = new QToolButton(m_stars);
        star->setAutoRaise(true);
        star->setIconSize(QSize(16, 16));
        star->setToolTip(ngTr("%1 of 5", "%1 su 5").arg(i));
        starsLayout->addWidget(star);
        m_starButtons.append(star);
        connect(star, &QToolButton::clicked, this, [this, i] {
            // a click on the rating already given takes it away
            const int rating = m_rating == i * 2 ? 0 : i * 2;
            if (!m_item.isNull() && m_item.isLocalFile()) {
                KFileMetaData::UserMetaData(m_item.localPath()).setRating(rating);
                NgFacts::instance()->attributesChanged(m_item.localPath());
            }
            showRating(rating);
        });
    }
    starsLayout->addStretch();
    m_tags = new QWidget(m_lines);
    m_tagsLayout = new NgFlowLayout(m_tags);
    // (a label: smaller, with room above it that sets the pair off from the one before; the value right under it)
    auto above = [](QLabel *label, bool first) {
        QFont small = label->font();
        small.setPointSizeF(small.pointSizeF() * 0.86);
        label->setFont(small);
        label->setContentsMargins(0, first ? 0 : 12, 0, 3);
        return label;
    };
    m_linesLayout->addWidget(above(m_originLabel, true), 0, 0);
    m_linesLayout->addWidget(m_origin, 1, 0);
    m_linesLayout->addWidget(above(m_whoLabel, false), 2, 0);
    m_linesLayout->addWidget(m_who, 3, 0);
    // a folder's "Changed last": a small card, the item's icon, its name and when it changed, a click selects it
    m_lastButton = new QToolButton(m_lines);
    m_lastButton->setObjectName(QStringLiteral("ngLastChanged"));
    m_lastButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_lastButton->setIconSize(QSize(28, 28));
    m_lastButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_lastButton->setCursor(Qt::PointingHandCursor);
    m_lastButton->setToolTip(ngTr("Select it in the folder", "Selezionalo nella cartella"));
    m_lastButton->hide();
    {
        // the arrow at its right end: it goes somewhere
        auto *arrowRow = new QHBoxLayout(m_lastButton);
        arrowRow->setContentsMargins(0, 0, 10, 0);
        arrowRow->addStretch();
        auto *arrow = new QLabel(QStringLiteral("›"), m_lastButton);
        QFont big = arrow->font();
        big.setPointSizeF(big.pointSizeF() * 1.5);
        arrow->setFont(big);
        arrow->setEnabled(false);
        arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        arrowRow->addWidget(arrow);
        m_lastArrow = arrow;
        // a folder: its own button at the end, that goes into it (the card itself selects it, as a file)
        m_lastOpen = new QToolButton(m_lastButton);
        m_lastOpen->setText(ngTr("Open", "Apri"));
        m_lastOpen->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
        m_lastOpen->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_lastOpen->setLayoutDirection(Qt::RightToLeft); // (the arrow after the word)
        m_lastOpen->setAutoRaise(true);
        m_lastOpen->setCursor(Qt::PointingHandCursor);
        m_lastOpen->setToolTip(ngTr("Open this folder", "Apri questa cartella"));
        m_lastOpen->hide();
        connect(m_lastOpen, &QToolButton::clicked, this, [this] {
            if (m_lastChanged.isValid()) {
                Q_EMIT urlActivated(m_lastChanged);
            }
        });
        arrowRow->addWidget(m_lastOpen);
    }
    connect(m_lastButton, &QToolButton::clicked, this, [this] {
        if (m_lastChanged.isValid()) {
            Q_EMIT selectRequested(m_lastChanged);
        }
    });
    m_linesLayout->addWidget(m_lastButton, 3, 0);
    m_linesLayout->addWidget(above(caption(ngTr("Rating", "Voto")), false), 4, 0);
    m_linesLayout->addWidget(m_stars, 5, 0);
    m_linesLayout->addWidget(above(caption(ngTr("Tags", "Etichette")), false), 6, 0);
    m_linesLayout->addWidget(m_tags, 7, 0);
    // a folder: the tags of what is inside it, with how many carry each (not to be changed from here)
    m_insideLabel = above(caption(ngTr("Tagged inside", "Etichette dentro la cartella")), false);
    m_insideTags = new QWidget(m_lines);
    new NgFlowLayout(m_insideTags);
    m_linesLayout->addWidget(m_insideLabel, 8, 0);
    m_linesLayout->addWidget(m_insideTags, 9, 0);
    m_insideLabel->hide();
    m_insideTags->hide();
    m_layout->addWidget(m_lines);
    m_layout->addStretch();
    restyle();
}

void NgInfoView::restyle()
{
    const QPalette palette = QApplication::palette();
    // the captions ("Rating", "Tags", "Quick actions"...) in the dimmed colour, solid on glass with the text shadow; not
    // disabled labels any more: the widget style leaves disabled text without its shadow, and over a white window
    // behind the glass they vanished
    QColor captionInk = palette.color(QPalette::Disabled, QPalette::WindowText);
    if (ngTextHalo() || ngSidebarHalo()) { // (Aurora's glass, or the panels' own glass on live blur)
        // (on glass that may show white behind: three quarters of the text's colour, solid; the disabled colour was too
        // faint to read even with its shadow)
        captionInk = palette.color(QPalette::Active, QPalette::WindowText);
        captionInk.setAlphaF(0.75);
    }
    captionInk = ngHaloInk(captionInk, palette);
    // on glass, the shadow of the sidebar's text under the panel's labels too (names, captions, dates, the legend):
    // light text over a light wallpaper behind the glass could not be read. Not under the chips, which have a fill
    const bool halo = ngSidebarHalo() || ngTextHalo();
    const QColor shadowColour = ngIsDark(palette) ? QColor(0, 0, 0, 190) : QColor(255, 255, 255, 215);
    const auto labels = findChildren<QLabel *>();
    for (QLabel *label : labels) {
        if (label->property("_ng_dim_label").toBool()) {
            QPalette own = label->palette();
            own.setColor(QPalette::WindowText, captionInk);
            label->setPalette(own);
        }
        const bool filled = label->styleSheet().contains(QLatin1String("background"));
        auto *effect = qobject_cast<QGraphicsDropShadowEffect *>(label->graphicsEffect());
        if (halo && !filled) {
            if (!effect) {
                effect = new QGraphicsDropShadowEffect(label);
                label->setGraphicsEffect(effect);
            }
            effect->setBlurRadius(6);
            effect->setOffset(0, 1);
            effect->setColor(shadowColour);
        } else if (effect) {
            label->setGraphicsEffect(nullptr);
        }
    }
    const bool dark = ngIsDark(palette);
    const QColor veil = ngVeil(palette);
    const QString veilCss = QStringLiteral("rgba(%1, %2, %3, %4)").arg(veil.red()).arg(veil.green()).arg(veil.blue()).arg(QString::number(veil.alphaF(), 'f', 3));
    const QString ink = dark ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
    m_actions->setStyleSheet(QStringLiteral("QWidget#ngInfoActions { background: %1; border-radius: 17px; }"
                                            "QToolButton { border: 0; border-radius: 14px; padding: 5px 9px; }"
                                            "QToolButton:hover { background: rgba(%2, 0.12); }")
                                 .arg(veilCss, ink));
    const QColor accent = palette.color(QPalette::Highlight);
    m_activity->setStyleSheet(QStringLiteral("QWidget#ngInfoActivity { background: rgba(%1, %2, %3, 0.16); border-radius: 12px; }"
                                             "QProgressBar { border: 0; border-radius: 3px; background: rgba(%4, 0.12); }"
                                             "QProgressBar::chunk { border-radius: 3px; background: %5; }")
                                  .arg(accent.red())
                                  .arg(accent.green())
                                  .arg(accent.blue())
                                  .arg(ink, accent.name()));
    m_lastButton->setStyleSheet(QStringLiteral("QToolButton#ngLastChanged { background: %1; border: 0; border-radius: 10px; padding: 7px 30px 7px 8px; text-align: left; }"
                                               "QToolButton#ngLastChanged QToolButton { background: transparent; border: 0; padding: 3px 6px; border-radius: 7px; }"
                                               "QToolButton#ngLastChanged QToolButton:hover { background: rgba(%2, 0.12); }"
                                               "QToolButton#ngLastChanged:hover { background: rgba(%2, 0.12); }"
                                               "QToolButton#ngLastChanged:pressed { background: rgba(%2, 0.18); }")
                                    .arg(veilCss, ink));
    m_pin->setStyleSheet(QStringLiteral("QToolButton { border: 0; border-radius: 12px; padding: 3px; }"
                                        "QToolButton:hover { background: rgba(%1, 0.12); }"
                                        "QToolButton:checked { background: %2; }")
                             .arg(ink, accent.name()));
    // the pin's glyph: the text colour, and the selection's text colour on the accent while pinned (it stayed dark on
    // the blue). Both states in one icon, the button picks by its checked state
    {
        const QIcon themed = QIcon::fromTheme(QStringLiteral("window-pin"), QIcon::fromTheme(QStringLiteral("pin")));
        const qreal dpr = devicePixelRatioF();
        auto tinted = [&](const QColor &colour) {
            QPixmap glyph = themed.pixmap(QSize(16, 16), dpr);
            if (!glyph.isNull()) {
                QPainter tint(&glyph);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(glyph.rect(), colour);
            }
            return glyph;
        };
        QIcon pin;
        pin.addPixmap(tinted(palette.color(QPalette::WindowText)), QIcon::Normal, QIcon::Off);
        pin.addPixmap(tinted(ngAurora() ? ngAuroraSelectionText(palette) : ngSelectionText(palette)), QIcon::Normal, QIcon::On);
        m_pin->setIcon(pin);
    }
}

void NgInfoView::updatePinTip()
{
    const bool many = m_items.count() > 1;
    m_pin->setToolTip(m_pin->isChecked() ? (many ? ngTr("Pinned to the top: click to unpin them", "Fissati in cima: clic per toglierli")
                                                 : ngTr("Pinned to the top: click to unpin it", "Fissato in cima: clic per toglierlo"))
                                         : ngTr("Pin to the top", "Fissa in cima"));
}

void NgInfoView::layoutActions()
{
    // As many quick actions as fit in the panel's width, the rest in a menu at the end of the pill (all of them in a
    // row made the pill, and the whole panel, wider than the panel: nine actions for a video)
    const int available = qMax(80, stableWidth() - 32);
    if (available == m_actionsWidth) {
        return;
    }
    m_actionsWidth = available;
    while (QLayoutItem *child = m_actionsLayout->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    m_actions->setVisible(!m_actionList.isEmpty());
    m_actionCaption->setVisible(!m_actionList.isEmpty());
    m_actionCaption->setText(m_actionList.isEmpty() ? QString() : ngTr("Quick actions", "Azioni rapide"));
    if (m_actionList.isEmpty()) {
        return;
    }
    auto makeButton = [this](const QIcon &icon, const QString &label) {
        auto *button = new QToolButton(m_actions);
        button->setIcon(icon);
        button->setIconSize(QSize(18, 18));
        button->setToolTip(label);
        button->setAutoRaise(true);
        button->installEventFilter(this);
        button->setProperty("_ng_caption", label);
        return button;
    };
    int buttonWidth = 30;
    {
        QToolButton *probe = makeButton(QIcon::fromTheme(QStringLiteral("system-run")), QString()); // (made to measure)
        buttonWidth = qMax(30, probe->sizeHint().width());
        delete probe;
    }
    const int room = (available - 8) / (buttonWidth + 2);
    const int shown = m_actionList.count() <= room ? int(m_actionList.count()) : qMax(1, room - 1);
    for (int i = 0; i < shown; ++i) {
        const Action action = m_actionList.at(i);
        auto *button = makeButton(QIcon::fromTheme(action.icon, QIcon::fromTheme(QStringLiteral("system-run"))), action.label);
        connect(button, &QToolButton::clicked, this, [this, action] {
            Q_EMIT quickActionRequested(m_item, action.index);
        });
        m_actionsLayout->addWidget(button);
    }
    if (shown < m_actionList.count()) {
        auto *more = makeButton(QIcon::fromTheme(QStringLiteral("overflow-menu"), QIcon::fromTheme(QStringLiteral("application-menu"))), ngTr("More actions", "Altre azioni"));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
        auto *menu = new QMenu(more);
        for (int i = shown; i < m_actionList.count(); ++i) {
            const Action action = m_actionList.at(i);
            menu->addAction(QIcon::fromTheme(action.icon, QIcon::fromTheme(QStringLiteral("system-run"))), action.label, this, [this, action] {
                Q_EMIT quickActionRequested(m_item, action.index);
            });
        }
        more->setMenu(menu);
        m_actionsLayout->addWidget(more);
    }
}

void NgInfoView::updateOverlay()
{
    // over the preview: the kind, the size, then the facts that are not those two again (the Dossier ends its facts with
    // the size and the format when the file tells little)
    if (m_item.isNull()) {
        return;
    }
    QList<std::pair<QString, QString>> chips;
    const QString size = m_size->text();
    if (m_item.isDir()) {
        chips.append({size, QString()}); // (a folder: its size with what it holds is the band's first fact)
    } else {
        m_hero->setSizeText(size); // (a file: its size beside its kind, in the badge)
    }
    static const QStringList repeated{QStringLiteral("size"), QStringLiteral("dimensione"), QStringLiteral("format"), QStringLiteral("formato")};
    for (const auto &fact : std::as_const(m_overlayFacts)) {
        if (!repeated.contains(fact.second.toLower())) {
            chips.append(fact);
        }
    }
    m_hero->setOverlay(m_kind->text(), m_overlayTint, chips);
    m_size->hide();
}

void NgInfoView::showEvent(QShowEvent *event)
{
    QScrollArea::showEvent(event);
    restyle(); // (built before the window told the views it is Aurora glass: the captions took the colour without glass)
}

void NgInfoView::changeEvent(QEvent *event)
{
    QScrollArea::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        restyle();
    }
}

// the width the content is laid out in: the panel's, less the room of a vertical scroll bar whether it is shown or not.
// Taken from the viewport, it changed when the scroll bar came and went; at a width where a text was on the edge of
// being shortened, that changed the content's height, which brought the scroll bar or took it away again: the panel
// flickered and resized itself without end
int NgInfoView::stableWidth() const
{
    if (style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, verticalScrollBar())) {
        return qMax(60, viewport()->width()); // (a scroll bar over the content takes no room)
    }
    // (from the area's own width, which the scroll bar does not change, less the bar's room)
    return qMax(60, contentsRect().width() - style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar()));
}

void NgInfoView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    // the content as wide as the panel, whatever a part of it asks for: nothing pushes past the edge (a part that cannot
    // be narrower is cut, not the whole panel)
    m_body->setFixedWidth(stableWidth());
    const int width = qMax(120, stableWidth() - 32);
    m_media->setVideoSize(QSize(width, width * 9 / 16));
    layoutActions();
}

void NgInfoView::refresh()
{
    if (m_items.count() > 1) {
        showItems(m_items);
    } else if (!m_item.isNull()) {
        showItem(m_item);
    }
}

void NgInfoView::showItem(const KFileItem &item)
{
    // the same file again (the pointer back on it, the selection told again): what is shown stays, its preview and
    // its player are not loaded anew; only what may have changed is looked at
    if (m_items.isEmpty() && !item.isNull() && !m_item.isNull() && item.url() == m_item.url()
        && item.time(KFileItem::ModificationTime) == m_item.time(KFileItem::ModificationTime) && item.size() == m_item.size()) {
        m_item = item;
        showActivity();
        return;
    }
    m_item = item;
    m_items.clear();
    showOne();
}

void NgInfoView::showItems(const KFileItemList &items)
{
    if (items.count() == 1) {
        showItem(items.first());
        return;
    }
    m_item = KFileItem();
    m_items = items;
    showMany();
}

// The preview zoomed in: the file itself drawn again at the size the zoom needs (a thumbnail enlarged is grainy). A
// picture is read from the file, in the background; a PDF's first page is rendered by pdftoppm (poppler-utils, as the
// lightbox does); anything else gets a larger thumbnail, once.
void NgInfoView::renderSharper(int side)
{
    if (m_item.isNull()) {
        return;
    }
    side = qMin(side, 6000); // enough to read a page, without eating memory
    if (side <= m_sharpSide) {
        return; // (asked already, or the file has no more to give)
    }
    m_sharpSide = side;
    const QUrl url = m_item.url();
    const QString path = m_item.isLocalFile() ? m_item.localPath() : QString();
    const QString mime = m_item.mimetype();
    if (!path.isEmpty() && mime.startsWith(QLatin1String("image/")) && m_item.size() < 120 * 1024 * 1024) {
        QPointer<NgInfoView> self(this);
        (void)QtConcurrent::run([self, url, path, side] {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            QSize original = reader.size();
            if (original.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90)) {
                original.transpose();
            }
            if (original.isValid() && qMax(original.width(), original.height()) > side) {
                QSize scaled = original.scaled(side, side, Qt::KeepAspectRatio);
                if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
                    scaled.transpose(); // (the reader scales before it turns the picture)
                }
                reader.setScaledSize(scaled);
            }
            const QImage image = reader.read();
            QMetaObject::invokeMethod(qApp, [self, url, image] {
                if (self && !image.isNull() && self->m_item.url() == url) {
                    self->m_hero->setPixmap(QPixmap::fromImage(image));
                }
            });
        });
        if (m_item.size() > 0) {
            // (the file has no more pixels than its own: not asked again past them)
            QImageReader header(path);
            const QSize original = header.size();
            if (original.isValid() && qMax(original.width(), original.height()) <= side) {
                m_sharpSide = 1 << 30;
            }
        }
        return;
    }
    static const QString pdftoppm = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
    if (!path.isEmpty() && mime == QLatin1String("application/pdf") && !pdftoppm.isEmpty()) {
        if (m_sharpProcess) {
            m_sharpProcess->kill();
            m_sharpProcess->deleteLater();
        }
        auto *process = new QProcess(this);
        m_sharpProcess = process;
        connect(process, &QProcess::finished, this, [this, process, url](int exitCode, QProcess::ExitStatus status) {
            if (status == QProcess::NormalExit && exitCode == 0 && m_item.url() == url) {
                QImage page;
                if (page.loadFromData(process->readAllStandardOutput(), "PNG")) {
                    m_hero->setPixmap(QPixmap::fromImage(page));
                }
            }
            process->deleteLater();
        });
        // (no output name: the page comes on stdout)
        const QString page = QString::number(m_hero->page());
        process->start(pdftoppm, {QStringLiteral("-png"), QStringLiteral("-f"), page, QStringLiteral("-l"), page,
                                  QStringLiteral("-singlefile"), QStringLiteral("-scale-to"), QString::number(side), path});
        return;
    }
    if (m_sharpSide < (1 << 30)) {
        m_sharpSide = 1 << 30; // a thumbnail is asked larger only once
        askPreview({m_item}, 3);
    }
}

// a page of a PDF in the preview, at about the preview's size (its first is the thumbnail; zooming in draws it again
// larger, see renderSharper)
void NgInfoView::renderPage(int page)
{
    static const QString pdftoppm = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
    if (m_item.isNull() || !m_item.isLocalFile() || pdftoppm.isEmpty()) {
        return;
    }
    m_sharpSide = 0;
    if (m_sharpProcess) {
        m_sharpProcess->kill();
        m_sharpProcess->deleteLater();
    }
    const QUrl url = m_item.url();
    const int side = qBound(256, qRound(qMax(m_hero->width(), m_hero->height()) * devicePixelRatioF() * 1.2), 2048);
    auto *process = new QProcess(this);
    m_sharpProcess = process;
    connect(process, &QProcess::finished, this, [this, process, url](int exitCode, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && exitCode == 0 && m_item.url() == url) {
            QImage image;
            if (image.loadFromData(process->readAllStandardOutput(), "PNG")) {
                m_hero->setPixmap(QPixmap::fromImage(image));
            }
        }
        process->deleteLater();
    });
    process->start(pdftoppm, {QStringLiteral("-png"), QStringLiteral("-f"), QString::number(page), QStringLiteral("-l"), QString::number(page), QStringLiteral("-singlefile"),
                              QStringLiteral("-scale-to"), QString::number(side), m_item.localPath()});
}

void NgInfoView::askPreview(const KFileItemList &items, int sharper)
{
    if (m_previewJob) {
        m_previewJob->kill();
    }
    const qreal dpr = devicePixelRatioF();
    // (sharper: for a picture zoomed in, larger and not kept)
    const int side = qBound(256, qRound(qMax(qMax(m_hero->width(), m_hero->height()), 260) * dpr * 1.2 * sharper), 1024 * sharper);
    // (kept for the files seen last: coming back to one shows it at once)
    static QHash<QString, QPixmap> kept;
    if (items.count() == 1 && sharper == 1) {
        const QString key = items.first().url().toString() + QLatin1Char('|') + QString::number(items.first().time(KFileItem::ModificationTime).toSecsSinceEpoch());
        const auto found = kept.constFind(key);
        if (found != kept.constEnd()) {
            m_hero->setPixmap(*found);
            return;
        }
    }
    m_previewJob = new KIO::PreviewJob(items, QSize(side, side));
    m_previewJob->setIgnoreMaximumSize(items.count() == 1);
    m_previewJob->setDevicePixelRatio(dpr);
    const bool stack = items.count() > 1;
    connect(m_previewJob, &KIO::PreviewJob::gotPreview, this, [this, stack, sharper](const KFileItem &item, const QPixmap &pixmap) {
        if (stack) {
            m_hero->addToStack(pixmap);
        } else {
            m_hero->setPixmap(pixmap);
            if (sharper > 1) {
                return;
            }
            if (kept.size() > 40) {
                kept.clear();
            }
            kept.insert(item.url().toString() + QLatin1Char('|') + QString::number(item.time(KFileItem::ModificationTime).toSecsSinceEpoch()), pixmap);
        }
    });
}

void NgInfoView::showOne()
{
    const KFileItem &item = m_item;
    m_hero->clear();
    m_sharpSide = 0;
    if (m_sharpProcess) {
        m_sharpProcess->kill();
    }
    m_media->clearUrl();
    m_media->hide();
    m_facts->setFacts({});
    m_mix->setParts({});
    if (item.isNull()) {
        return;
    }
    if (!item.isMimeTypeKnown()) {
        item.determineMimeType();
    }
    const QString mime = item.mimetype();
    const NgKind kind = ngKindFor(item.text(), item.isDir());

    // ---- the preview
    m_hero->setIcon(item.iconName(), kind.tint);
    const bool image = mime.startsWith(QLatin1String("image/"));
    const bool audio = mime.startsWith(QLatin1String("audio/"));
    const bool video = mime.startsWith(QLatin1String("video/"));
    const bool text = !item.isDir() && (mime.startsWith(QLatin1String("text/")) || kind.category == NgKind::Data) && item.size() < 2 * 1024 * 1024;
    if (text && item.isLocalFile()) {
        QFile file(item.localPath());
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray head = file.read(16 * 1024);
            if (!head.contains('\0')) {
                QStringDecoder decoder(QStringDecoder::Utf8);
                QString decoded = decoder(head);
                decoded.replace(QLatin1Char('\t'), QStringLiteral("    "));
                m_hero->setLines(decoded.split(QLatin1Char('\n')).mid(0, 40));
            }
        }
    }
    if (!m_hero->hasPicture()) {
        askPreview({item});
    }
    m_hero->setCanOpen(image && item.isLocalFile());
    // a PDF: its pages turned from the band at the foot of the preview, each drawn by pdftoppm (poppler-utils)
    if (mime == QLatin1String("application/pdf") && item.isLocalFile() && !QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()) {
        m_hero->setPageable(true, [this](int page) {
            renderPage(page);
        });
    }
    if ((audio || video) && item.isLocalFile()) {
        // played inside the preview, from the bar at its foot (a player under it was a second picture of the same video,
        // and its native video surface broke the panel's glass)
        m_hero->setMedia(item.url(), video);
    }

    // ---- name, kind, size, pinned
    m_name->setText(ngBreakable(item.text()));
    m_copyName->show();
    m_kind->setText(kind.sigla.isEmpty() ? item.mimeComment() : kind.sigla);
    m_kind->hide(); // (told over the preview, see updateOverlay)
    m_overlayTint = kind.tint;
    m_overlayFacts.clear();
    m_kind->setStyleSheet(QStringLiteral("QLabel { color: white; background: %1; border-radius: 9px; padding: 1px 8px; font-weight: bold; font-size: 9pt; }").arg(kind.tint.name()));
    m_size->setText(item.isDir() ? item.mimeComment() : KIO::convertSize(item.size()));
    if (item.isDir() && item.isLocalFile()) {
        // a folder: its first items as the preview, read in the background (a folder's count is stopped at 9999: a large
        // tree is not walked for a number in a list)
        QPointer<NgInfoView> self(this);
        const QString folderPath = item.localPath();
        const QUrl folderUrl = item.url();
        (void)QtConcurrent::run([self, folderPath, folderUrl] {
            QList<NgFileFacts::Entry> entries;
            const QFileInfoList list = QDir(folderPath).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo &info : list.mid(0, 10)) {
                NgFileFacts::Entry entry;
                if (info.isDir()) {
                    entry.name = info.fileName() + QLatin1Char('/');
                    QDirIterator it(info.filePath(), QDir::AllEntries | QDir::NoDotAndDotDot);
                    while (it.hasNext() && entry.count < 9999) {
                        it.next();
                        ++entry.count;
                    }
                } else {
                    entry.name = info.fileName();
                    entry.size = info.size();
                }
                entries.append(entry);
            }
            QMetaObject::invokeMethod(qApp, [self, folderUrl, entries] {
                if (self && !self->m_item.isNull() && self->m_item.url() == folderUrl) {
                    self->m_hero->setListing(entries);
                }
            });
        });
        // what it holds in all, counted in the background (a large one takes a while: "counting…" until then)
        if (m_sizeJob) {
            m_sizeJob->kill();
        }
        m_size->setText(ngTr("Folder · counting…", "Cartella · conteggio…"));
        m_sizeJob = KIO::directorySize(item.url());
        const QUrl url = item.url();
        connect(m_sizeJob, &KJob::result, this, [this, url](KJob *job) {
            auto *sizeJob = static_cast<KIO::DirectorySizeJob *>(job);
            if (job->error() || m_item.isNull() || m_item.url() != url) {
                return;
            }
            const qulonglong files = sizeJob->totalFiles(), folders = sizeJob->totalSubdirs();
            QString text = KIO::convertSize(sizeJob->totalSize()) + QStringLiteral(" · ")
                + (files == 1 ? ngTr("1 file", "1 file") : ngTr("%1 files", "%1 file").arg(QLocale().toString(files)));
            if (folders > 0) {
                text += QStringLiteral(", ") + (folders == 1 ? ngTr("1 folder", "1 cartella") : ngTr("%1 folders", "%1 cartelle").arg(QLocale().toString(folders)));
            }
            m_size->setText(text);
            updateOverlay();
        });
    }
    const bool pinned = m_pinnedQuery ? m_pinnedQuery({item}) : false;
    m_pin->setChecked(pinned);
    updatePinTip();

    // ---- quick actions
    m_actionList.clear();
    const QList<NgQuickAction> actions = item.isLocalFile() ? ngQuickActionsFor(item.localPath(), mime) : QList<NgQuickAction>();
    for (int i = 0; i < actions.count(); ++i) {
        m_actionList.append({actions.at(i).label, actions.at(i).icon, i});
    }
    m_actionsWidth = -1;
    layoutActions();

    // ---- the work in progress, the facts, what it holds, where it came from
    showActivity();
    showFacts();
    updateOverlay();

    // ---- the dates
    auto date = [](const QDateTime &when) {
        if (!when.isValid()) {
            return QString();
        }
        return ngAgoText(when);
    };
    QList<std::pair<QString, QString>> dates;
    const QDateTime created = item.time(KFileItem::CreationTime), modified = item.time(KFileItem::ModificationTime), accessed = item.time(KFileItem::AccessTime);
    if (created.isValid()) {
        dates.append({ngTr("Created", "Creato"), date(created)});
    }
    if (modified.isValid()) {
        dates.append({ngTr("Modified", "Modificato"), date(modified)});
    }
    if (accessed.isValid()) {
        dates.append({ngTr("Opened", "Aperto"), date(accessed)});
    }
    m_dates->setDates(dates);
}

void NgInfoView::showFacts()
{
    if (m_item.isNull() || !m_item.isLocalFile()) {
        m_lines->setVisible(false);
        return;
    }
    const qint64 modified = m_item.time(KFileItem::ModificationTime).toSecsSinceEpoch();
    const NgFileFacts *facts = NgFacts::instance()->facts(m_item.localPath(), m_item.mimetype(), m_item.isDir(), modified, m_item.isDir() ? 0 : qint64(m_item.size()));
    if (!m_hookedFacts) {
        m_hookedFacts = true;
        connect(NgFacts::instance(), &NgFacts::ready, this, [this](const QString &path) {
            if (!m_item.isNull() && m_item.isLocalFile() && m_item.localPath() == path) {
                showFacts();
            }
        });
    }
    // the rating and tags: from the file itself, now (the facts are kept until the file changes, and they do not
    // change it)
    const KFileMetaData::UserMetaData user(m_item.localPath());
    m_tagList = user.tags();
    showRating(user.rating());
    showTags();
    m_lines->setVisible(true);
    if (!facts) {
        m_origin->hide();
        m_originLabel->hide();
        m_who->hide();
        m_whoLabel->hide();
        m_insideLabel->hide();
        m_insideTags->hide();
        m_lastButton->hide();
        return; // being read: NgFacts::ready brings it here again
    }
    QList<std::pair<QString, QString>> shown;
    for (const NgFact &fact : facts->facts) {
        shown.append({fact.value, fact.word});
    }
    m_facts->setFacts({}); // (the facts are told over the preview, see updateOverlay)
    m_overlayFacts = shown;
    m_hero->setBar(facts->bar, facts->detail, facts->entries, facts->whoKind == NgFileFacts::Camera ? facts->who : QString(), m_item.isDir() ? QString() : m_size->text());
    updateOverlay();
    QList<NgInfoMix::Part> parts;
    for (const NgMixPart &part : facts->mix) {
        parts.append({part.colour, part.count, part.label});
    }
    m_mix->setParts(parts);
    // a folder: the tags found inside it, as chips with their count
    while (QLayoutItem *child = m_insideTags->layout()->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    for (int i = 0; i < facts->innerTags.size() && i < 12; ++i) {
        const auto &tag = facts->innerTags.at(i);
        const QColor colour = ngTagColour(tag.first);
        auto *chip = new QLabel(QStringLiteral("%1  <b>%2</b>").arg(tag.first.toHtmlEscaped()).arg(tag.second), m_insideTags);
        // (with a border of its own colour: without one, some widget styles left the label's background square)
        chip->setStyleSheet(QStringLiteral("QLabel { background: rgba(%1, %2, %3, 0.80); border: 1px solid rgba(%1, %2, %3, 0.95); color: white; border-radius: 10px; padding: 1px 9px; }")
                                .arg(colour.red())
                                .arg(colour.green())
                                .arg(colour.blue()));
        chip->setToolTip(tag.second == 1 ? ngTr("1 item inside has this tag", "1 elemento dentro ha questa etichetta")
                                         : ngTr("%1 items inside have this tag", "%1 elementi dentro hanno questa etichetta").arg(tag.second));
        m_insideTags->layout()->addWidget(chip);
        chip->show();
    }
    m_insideLabel->setVisible(m_item.isDir() && !facts->innerTags.isEmpty());
    m_insideTags->setVisible(m_item.isDir() && !facts->innerTags.isEmpty());
    m_origin->setText(ngBreakable(facts->origin));
    m_origin->setVisible(!facts->origin.isEmpty());
    m_originLabel->setVisible(!facts->origin.isEmpty());
    const QString whoCaption = facts->whoKind == NgFileFacts::Camera ? ngTr("Camera", "Fotocamera")
        : facts->whoKind == NgFileFacts::Artist                      ? ngTr("Artist", "Artista")
        : facts->whoKind == NgFileFacts::Author                      ? ngTr("Author", "Autore")
        : facts->whoKind == NgFileFacts::Program                     ? ngTr("Made with", "Creato con")
                                                                     : QString();
    m_whoLabel->setText(whoCaption);
    m_who->setText(ngBreakable(facts->who));
    m_who->setVisible(!facts->who.isEmpty());
    m_whoLabel->setVisible(!facts->who.isEmpty());
    m_lastChanged = QUrl();
    m_who->setCursor(Qt::ArrowCursor);
    m_who->setToolTip(QString());
    if (m_item.isDir() && !facts->lastName.isEmpty()) {
        m_whoLabel->setText(ngTr("Changed last", "Cambiato per ultimo"));
        m_who->setText(ngBreakable(facts->lastName));
        m_who->setVisible(true);
        m_whoLabel->setVisible(true);
        // a click on its name selects it in the view (opening the folder first when it is not the one shown)
        QUrl url = m_item.url();
        QString path = url.path();
        if (!path.endsWith(QLatin1Char('/'))) {
            path += QLatin1Char('/');
        }
        url.setPath(path + facts->lastName);
        m_lastChanged = url;
        m_who->hide();
        // the card: icon of its kind, the name (shortened in its middle), when it changed, an arrow
        // (a folder too can be what changed last: its icon and words, and a button to go into it)
        const bool folder = m_lastChanged.isLocalFile() && QFileInfo(m_lastChanged.toLocalFile()).isDir();
        const QString icon = folder ? QStringLiteral("folder") : QMimeDatabase().mimeTypeForFile(facts->lastName, QMimeDatabase::MatchExtension).iconName();
        m_lastButton->setIcon(QIcon::fromTheme(icon, QIcon::fromTheme(QStringLiteral("unknown"))));
        m_lastArrow->setVisible(!folder);
        m_lastOpen->setVisible(folder);
        const int room = qMax(80, stableWidth() - 32 - 28 - (folder ? m_lastOpen->sizeHint().width() + 20 : 48));
        const QString name = fontMetrics().elidedText(facts->lastName, Qt::ElideMiddle, room);
        const QString ago = facts->lastWhen.isValid() ? ngAgoText(facts->lastWhen) : QString();
        const QString when = ago.isEmpty() ? (folder ? ngTr("folder", "cartella") : QString())
                                           : folder ? ngTr("folder · changed %1", "cartella · modificata %1").arg(ago) : ngTr("changed %1", "modificato %1").arg(ago);
        m_lastButton->setText(when.isEmpty() ? name : name + QLatin1Char('\n') + when);
        m_lastButton->show();
    } else {
        m_lastButton->hide();
    }
}

void NgInfoView::showRating(int rating)
{
    m_rating = rating;
    const QIcon full = QIcon::fromTheme(QStringLiteral("rating")), empty = QIcon::fromTheme(QStringLiteral("rating-unrated"));
    for (int i = 0; i < m_starButtons.count(); ++i) {
        m_starButtons.at(i)->setIcon((i + 1) * 2 <= rating ? full : empty);
    }
}

void NgInfoView::showTags()
{
    while (QLayoutItem *child = m_tagsLayout->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    const QPalette palette = QApplication::palette();
    const QColor veil = ngVeil(palette);
    Q_UNUSED(veil)
    const bool dark = ngIsDark(palette);
    for (const QString &tag : std::as_const(m_tagList)) {
        // in the tag's colour, as on the cards; the cross takes it away
        const QColor colour = ngTagColour(tag);
        QColor text = dark ? colour.lighter(125) : colour.darker(135);
        // on glass with the text shadow: a solid chip (its colour over the window's), white text; a see-through one was
        // pastel on whatever lay behind the window
        const bool solid = true; // (solid in every look, as on the cards)
        if (solid) {
            text = QColor(255, 255, 255);
        }
        auto *chip = new QToolButton(m_tags);
        chip->setText(tag + QStringLiteral("  ×"));
        chip->setToolButtonStyle(Qt::ToolButtonTextOnly);
        chip->setToolTip(ngTr("Remove the tag “%1”", "Togli l'etichetta «%1»").arg(tag));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(QStringLiteral("QToolButton { background: rgba(%1, %2, %3, %4); border: 1px solid rgba(%1, %2, %3, 0.43); border-radius: 10px;"
                                           " padding: 1px 6px 1px 8px; color: %5; }"
                                           "QToolButton:hover { background: rgba(%1, %2, %3, 0.30); }")
                                .arg(colour.red())
                                .arg(colour.green())
                                .arg(colour.blue())
                                .arg(solid ? QStringLiteral("0.78") : dark ? QStringLiteral("0.14") : QStringLiteral("0.16"))
                                .arg(text.name()));
        connect(chip, &QToolButton::clicked, this, [this, tag] {
            QStringList tags = m_tagList;
            tags.removeAll(tag);
            writeTags(tags);
        });
        m_tagsLayout->addWidget(chip);
        chip->show(); // (a child added to a shown panel stays hidden until later, and the layout passed it over)
    }
    auto *add = new QToolButton(m_tags);
    add->setText(ngTr("+ tag", "+ etichetta"));
    add->setAutoRaise(true);
    add->setEnabled(!m_item.isNull() && m_item.isLocalFile());
    connect(add, &QToolButton::clicked, this, &NgInfoView::editTags);
    m_tagsLayout->addWidget(add);
    add->show();
    m_tagsLayout->invalidate();
}

void NgInfoView::editTags()
{
    if (m_item.isNull() || !m_item.isLocalFile()) {
        return;
    }
    bool ok = false;
    const QString tag = QInputDialog::getText(this, ngTr("New tag", "Nuova etichetta"), ngTr("Tag:", "Etichetta:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || tag.isEmpty() || m_tagList.contains(tag)) {
        return;
    }
    writeTags(m_tagList + QStringList{tag});
}

void NgInfoView::writeTags(const QStringList &tags)
{
    if (m_item.isNull() || !m_item.isLocalFile()) {
        return;
    }
    const QString path = m_item.localPath();
    KFileMetaData::UserMetaData data(path);
    if (!data.isSupported() || data.setTags(tags) != KFileMetaData::UserMetaData::NoError) {
        // (a file system without extended attributes: FAT, some network shares)
        QToolTip::showText(QCursor::pos(), ngTr("Tags cannot be kept on this file: its file system does not hold them.",
                                                "Le etichette non si possono salvare su questo file: il suo file system non le supporta."),
                           this);
        return;
    }
    m_tagList = tags;
    showTags();
    NgFacts::instance()->attributesChanged(path);
}

void NgInfoView::showActivity()
{
    const bool busy = !m_item.isNull() && NgActivity::self()->contains(m_item.url());
    if (busy) {
        const NgActivity::Entry entry = NgActivity::self()->entry(m_item.url());
        m_activityText->setText(entry.text.isEmpty() ? ngTr("Working on it…", "Lavoro in corso…") : entry.text);
        if (entry.percent >= 0) {
            m_activityBar->setRange(0, 100);
            m_activityBar->setValue(entry.percent);
        } else {
            m_activityBar->setRange(0, 0); // (no percentage: it moves without one)
        }
    }
    m_activity->setVisible(busy);
}

void NgInfoView::showMany()
{
    m_hero->clear();
    m_media->clearUrl();
    m_media->hide();
    m_facts->setFacts({});
    m_lines->setVisible(false);
    m_activity->setVisible(false);
    m_dates->setDates({});
    m_actionList.clear();
    m_actionsWidth = -1;
    layoutActions();

    KIO::filesize_t total = 0;
    int folders = 0;
    QHash<int, int> kinds;
    for (const KFileItem &item : std::as_const(m_items)) {
        if (item.isDir()) {
            ++folders;
        } else {
            total += item.size();
        }
        ++kinds[int(ngKindFor(item.text(), item.isDir()).category)];
    }
    m_name->setText(ngTr("%1 items selected", "%1 elementi selezionati").arg(m_items.count()));
    m_copyName->hide();
    m_kind->hide();
    m_overlayFacts.clear();
    m_size->setText(folders == m_items.count() ? ngTr("%1 folders", "%1 cartelle").arg(folders)
                                               : KIO::convertSize(total) + (folders ? ngTr(" and %1 folders", " e %1 cartelle").arg(folders) : QString()));
    const bool pinned = m_pinnedQuery ? m_pinnedQuery(m_items) : false;
    m_pin->setChecked(pinned);
    updatePinTip();
    QList<NgInfoMix::Part> parts;
    // (one and many: "1 image", "3 images")
    static const char *const english[] = {"folder", "image", "document", "sheet", "archive", "audio or video", "code file", "package", "other"};
    static const char *const englishMany[] = {"folders", "images", "documents", "sheets", "archives", "audio and video", "code files", "packages", "others"};
    static const char *const italian[] = {"cartella", "immagine", "documento", "foglio", "archivio", "audio o video", "file di codice", "pacchetto", "altro"};
    static const char *const italianMany[] = {"cartelle", "immagini", "documenti", "fogli", "archivi", "audio e video", "file di codice", "pacchetti", "altri"};
    for (auto it = kinds.constBegin(); it != kinds.constEnd(); ++it) {
        const int category = qBound(0, it.key(), int(NgKind::CategoryCount) - 1);
        const bool one = it.value() == 1;
        parts.append({ngCategoryTint(NgKind::Category(category)), it.value(),
                      QStringLiteral("%1 %2").arg(it.value()).arg(one ? ngTr(english[category], italian[category]) : ngTr(englishMany[category], italianMany[category]))});
    }
    std::sort(parts.begin(), parts.end(), [](const NgInfoMix::Part &a, const NgInfoMix::Part &b) {
        return a.count > b.count;
    });
    m_mix->setParts(parts);
    m_hero->setOverlay(QString(), QColor(), {{QString::number(m_items.count()), ngTr("items", "elementi")}, {m_size->text(), QString()}});
    m_size->hide();
    askPreview(m_items.mid(0, 3));
}

#include "moc_nginfoview.cpp"
