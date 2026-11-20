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
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
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
        return !m_pixmap.isNull() && m_stack.isEmpty();
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
        if (m_panFrom) {
            m_zoomTopLeft += event->position() - *m_panFrom;
            m_panFrom = event->position();
            keepInside();
            update();
            return;
        }
        setCursor(onGrip(event->position()) ? Qt::SizeVerCursor : zoomed() ? Qt::OpenHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_dragFrom >= 0) {
            m_dragFrom = -1;
            setNgSetting("INFO_PREVIEW_RATIO", QString::number(m_ratio, 'f', 3));
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
        if (m_pixmap.isNull() || !m_stack.isEmpty() || delta == 0 || (!zoomed() && delta < 0)) {
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
        return m_zoom > 1.0 && !m_pixmap.isNull() && m_stack.isEmpty();
    }
    QRectF fitRect() const
    {
        const QSizeF size = QSizeF(m_pixmap.size()) / m_pixmap.devicePixelRatio();
        const qreal scale = qMin((width() - 16.0) / size.width(), (height() - 16.0) / size.height());
        const QSizeF shown = size * qMin(scale, 3.0);
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
            // a text: its first lines, as the preview card shows them
            painter.fillRect(rect(), dark ? QColor(16, 19, 28, 230) : QColor(250, 249, 246, 235));
            QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            mono.setPointSizeF(mono.pointSizeF() * 0.85);
            painter.setFont(mono);
            const QFontMetricsF metrics(mono);
            QColor ink = palette().color(QPalette::WindowText);
            ink.setAlphaF(0.85);
            painter.setPen(dark ? QColor(205, 212, 228) : QColor(40, 44, 52));
            qreal y = 14 + metrics.ascent();
            for (const QString &line : std::as_const(m_lines)) {
                if (y > height() - 8) {
                    break;
                }
                painter.drawText(QPointF(14, y), metrics.elidedText(line, Qt::ElideRight, width() - 28));
                y += metrics.height() + 1;
            }
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
            const QRectF pill(width() - metrics.horizontalAdvance(text) - 30, height() - metrics.height() - 20, metrics.horizontalAdvance(text) + 18, metrics.height() + 8);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(10, 12, 20, 170));
            painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            painter.setPen(Qt::white);
            painter.drawText(pill, Qt::AlignCenter, text);
            return;
        }
        painter.setClipPath(shape);
        QFont bold = font();
        bold.setPointSizeF(bold.pointSizeF() * 0.92);
        bold.setWeight(QFont::Bold);
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.82);
        const QFontMetricsF boldFm(bold), smallFm(small);
        // the kind, top left
        if (!m_badge.isEmpty()) {
            const QRectF badge(10, 10, boldFm.horizontalAdvance(m_badge) + 16, boldFm.height() + 6);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_badgeTint);
            painter.drawRoundedRect(badge, badge.height() / 2, badge.height() / 2);
            painter.setFont(bold);
            painter.setPen(Qt::white);
            painter.drawText(badge, Qt::AlignCenter, m_badge);
        }
        if (m_chips.isEmpty()) {
            return;
        }
        // the pills, laid from the bottom left, on as many rows as they need
        const qreal pad = 10, gap = 6, chipHeight = boldFm.height() + 8;
        struct Placed {
            QRectF rect;
            int index;
        };
        QList<Placed> placed;
        qreal x = pad;
        int row = 0;
        for (int i = 0; i < m_chips.count(); ++i) {
            const qreal w = qMin(width() - 2 * pad, boldFm.horizontalAdvance(m_chips.at(i).first) + (m_chips.at(i).second.isEmpty() ? 0 : 5 + smallFm.horizontalAdvance(m_chips.at(i).second)) + 18);
            if (x > pad && x + w > width() - pad) {
                x = pad;
                ++row;
            }
            placed.append({QRectF(x, 0, w, chipHeight), row});
            x += w + gap;
        }
        const int rows = row + 1;
        // a dark veil from the bottom up, under the pills
        const qreal veilTop = height() - pad - rows * (chipHeight + gap) - 26;
        QLinearGradient veil(0, veilTop, 0, height());
        veil.setColorAt(0, QColor(0, 0, 0, 0));
        veil.setColorAt(1, QColor(0, 0, 0, 150));
        painter.fillRect(QRectF(0, veilTop, width(), height() - veilTop), veil);
        for (int i = 0; i < placed.count(); ++i) {
            QRectF rect = placed.at(i).rect;
            rect.moveTop(height() - pad - (rows - placed.at(i).index) * (chipHeight + gap) + gap);
            painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
            painter.setBrush(QColor(10, 12, 20, 140));
            painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), rect.height() / 2, rect.height() / 2);
            const QRectF text = rect.adjusted(9, 0, -9, 0);
            painter.setFont(bold);
            painter.setPen(Qt::white);
            const QString value = boldFm.elidedText(m_chips.at(i).first, Qt::ElideRight, text.width());
            painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, value);
            if (!m_chips.at(i).second.isEmpty()) {
                const qreal used = boldFm.horizontalAdvance(value) + 5;
                painter.setFont(small);
                painter.setPen(QColor(255, 255, 255, 190));
                painter.drawText(text.adjusted(used, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, smallFm.elidedText(m_chips.at(i).second, Qt::ElideRight, text.width() - used));
            }
        }
    }

private:
    QString m_badge;
    QColor m_badgeTint;
    QList<std::pair<QString, QString>> m_chips;
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
        return m_parts.isEmpty() ? 0 : 18 + rowCount() * (rowHeight() + 3);
    }
    QSize sizeHint() const override
    {
        return QSize(260, heightForWidth(260));
    }

protected:
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
        // the bar
        qreal x = 0;
        for (const Part &part : std::as_const(m_parts)) {
            const qreal w = (width() - 2.0 * (m_parts.count() - 1)) * part.count / qMax(1, total);
            painter.setPen(Qt::NoPen);
            painter.setBrush(part.colour);
            painter.drawRoundedRect(QRectF(x, 0, qMax(3.0, w), 8), 3, 3);
            x += w + 2;
        }
        // the rows
        QList<Part> rows = m_parts.mid(0, kMaxRows);
        if (m_parts.count() > kMaxRows) {
            // the rest together, in a neutral colour
            rows.removeLast();
            int rest = 0;
            for (int i = kMaxRows - 1; i < m_parts.count(); ++i) {
                rest += m_parts.at(i).count;
            }
            QColor grey = palette().color(QPalette::WindowText);
            grey.setAlphaF(0.45);
            const int kinds = int(m_parts.count()) - kMaxRows + 1;
            rows.append({grey, rest, QStringLiteral("%1 ").arg(rest) + ngTr("in %1 other kinds", "in altri %1 tipi").arg(kinds)});
        }
        const int largest = qMax(1, rows.isEmpty() ? 1 : rows.first().count);
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        bold.setFeature(QFont::Tag("tnum"), 1); // (figures of one width, lined up)
        const QFontMetricsF boldFm(bold), fm(font());
        const QColor ink = palette().color(QPalette::WindowText);
        qreal y = 18;
        for (const Part &part : std::as_const(rows)) {
            const QRectF row(0, y, width(), rowHeight());
            QPainterPath shape;
            shape.addRoundedRect(row, 8, 8);
            painter.fillPath(shape, ngVeil(palette()));
            // the band of its share
            QColor band = part.colour;
            band.setAlphaF(0.16);
            painter.save();
            painter.setClipPath(shape);
            painter.fillRect(QRectF(row.left(), row.top(), row.width() * part.count / largest, row.height()), band);
            painter.restore();
            // its colour, its name, how many
            painter.setPen(Qt::NoPen);
            painter.setBrush(part.colour);
            painter.drawEllipse(QRectF(row.left() + 10, row.center().y() - 4, 8, 8));
            const QString number = QLocale().toString(part.count);
            QString word = part.label.section(QLatin1Char(' '), 1);
            if (!word.isEmpty()) {
                word[0] = word.at(0).toUpper();
            }
            const qreal numberWidth = boldFm.horizontalAdvance(number);
            painter.setFont(bold);
            painter.setPen(ink);
            ngInfoText(painter, QRectF(row.right() - 10 - numberWidth, row.top(), numberWidth, row.height()), Qt::AlignRight | Qt::AlignVCenter, number);
            painter.setFont(font());
            const QRectF wordRect(row.left() + 26, row.top(), row.width() - 26 - 18 - numberWidth, row.height());
            ngInfoText(painter, wordRect, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(word, Qt::ElideRight, wordRect.width()));
            y += rowHeight() + 3;
        }
    }

private:
    static constexpr int kMaxRows = 6;
    int rowCount() const
    {
        return qMin(int(m_parts.count()), kMaxRows);
    }
    int rowHeight() const
    {
        return fontMetrics().height() + 10;
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
    if (watched == viewport() && event->type() == QEvent::Resize && m_body) {
        m_body->setFixedWidth(viewport()->width());
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
    m_layout->addLayout(headGroup);
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
    m_linesLayout->addWidget(above(caption(ngTr("Rating", "Voto")), false), 4, 0);
    m_linesLayout->addWidget(m_stars, 5, 0);
    m_linesLayout->addWidget(above(caption(ngTr("Tags", "Etichette")), false), 6, 0);
    m_linesLayout->addWidget(m_tags, 7, 0);
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
    if (ngTextHalo()) {
        // (on glass that may show white behind: three quarters of the text's colour, solid; the disabled colour was too
        // faint to read even with its shadow)
        captionInk = palette.color(QPalette::Active, QPalette::WindowText);
        captionInk.setAlphaF(0.75);
    }
    captionInk = ngHaloInk(captionInk, palette);
    const auto labels = findChildren<QLabel *>();
    for (QLabel *label : labels) {
        if (label->property("_ng_dim_label").toBool()) {
            QPalette own = label->palette();
            own.setColor(QPalette::WindowText, captionInk);
            label->setPalette(own);
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
    m_pin->setStyleSheet(QStringLiteral("QToolButton { border: 0; border-radius: 12px; padding: 3px; }"
                                        "QToolButton:hover { background: rgba(%1, 0.12); }"
                                        "QToolButton:checked { background: %2; }")
                             .arg(ink, accent.name()));
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
    const int available = qMax(80, viewport()->width() - 32);
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
    chips.append({size, QString()});
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

void NgInfoView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    // the content as wide as the panel, whatever a part of it asks for: nothing pushes past the edge (a part that cannot
    // be narrower is cut, not the whole panel)
    m_body->setFixedWidth(viewport()->width());
    const int width = qMax(120, viewport()->width() - 32);
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
        process->start(pdftoppm, {QStringLiteral("-png"), QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"), QStringLiteral("1"),
                                  QStringLiteral("-singlefile"), QStringLiteral("-scale-to"), QString::number(side), path});
        return;
    }
    if (m_sharpSide < (1 << 30)) {
        m_sharpSide = 1 << 30; // a thumbnail is asked larger only once
        askPreview({m_item}, 3);
    }
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
    if ((audio || video) && item.isLocalFile()) {
        m_media->setUrl(item.url(), video ? MediaWidget::Video : MediaWidget::Audio);
        m_media->show();
    }

    // ---- name, kind, size, pinned
    m_name->setText(ngBreakable(item.text()));
    m_copyName->show();
    m_kind->setText(kind.sigla.isEmpty() ? item.mimeComment() : kind.sigla);
    m_kind->hide(); // (told over the preview, see updateOverlay)
    m_overlayTint = kind.tint;
    m_overlayFacts.clear();
    m_kind->setStyleSheet(QStringLiteral("QLabel { color: white; background: %1; border-radius: 8px; padding: 1px 8px; font-weight: bold; }").arg(kind.tint.name()));
    m_size->setText(item.isDir() ? item.mimeComment() : KIO::convertSize(item.size()));
    if (item.isDir() && item.isLocalFile()) {
        // a folder: what it holds in all, counted in the background (a large one takes a while: "counting…" until then)
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
        return; // being read: NgFacts::ready brings it here again
    }
    QList<std::pair<QString, QString>> shown;
    for (const NgFact &fact : facts->facts) {
        shown.append({fact.value, fact.word});
    }
    m_facts->setFacts({}); // (the facts are told over the preview, see updateOverlay)
    m_overlayFacts = shown;
    updateOverlay();
    QList<NgInfoMix::Part> parts;
    for (const NgMixPart &part : facts->mix) {
        parts.append({part.colour, part.count, part.label});
    }
    m_mix->setParts(parts);
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
    if (m_item.isDir() && !facts->lastName.isEmpty()) {
        m_whoLabel->setText(ngTr("Changed last", "Cambiato per ultimo"));
        m_who->setText(ngBreakable(facts->lastName));
        m_who->setVisible(true);
        m_whoLabel->setVisible(true);
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
        const bool solid = ngTextHalo();
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
