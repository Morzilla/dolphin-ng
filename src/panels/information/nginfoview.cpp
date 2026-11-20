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
#include <KIO/PreviewJob>

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
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

// text painted by the panel itself: with the shadow of the sidebar's text on glass (see ngSidebarHalo)
void ngInfoText(QPainter &painter, const QRectF &rect, int flags, const QString &text, qreal strength = 1.0)
{
    if (ngSidebarHalo()) {
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
        setMouseTracking(true);
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
        m_eye->hide();
        update();
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
        update();
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
    void setCanOpen(bool canOpen)
    {
        m_canOpen = canOpen;
        m_eye->setVisible(canOpen && underMouse());
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
        QWidget::resizeEvent(event);
        const int height = qRound(width() * (m_stack.isEmpty() && m_pixmap.isNull() && m_lines.isEmpty() ? 0.5 : 0.75));
        if (height != this->height()) {
            setFixedHeight(qMax(110, height));
        }
        m_eye->move(width() - m_eye->sizeHint().width() - 10, 10);
    }
    void enterEvent(QEnterEvent *event) override
    {
        m_eye->setVisible(m_canOpen);
        QWidget::enterEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        m_eye->hide();
        QWidget::leaveEvent(event);
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
            // the picture whole, as large as it fits
            const QSizeF size = QSizeF(m_pixmap.size()) / m_pixmap.devicePixelRatio();
            const qreal scale = qMin((width() - 16.0) / size.width(), (height() - 16.0) / size.height());
            const QSizeF shown = size * qMin(scale, 3.0);
            const QRectF target(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown);
            QPainterPath picture;
            picture.addRoundedRect(target, 6, 6);
            painter.setClipPath(picture, Qt::IntersectClip);
            painter.drawPixmap(target, m_pixmap, QRectF(QPointF(0, 0), QSizeF(m_pixmap.size())));
        } else if (!m_icon.isEmpty()) {
            const int side = qMin(96, qMin(width(), height()) - 24);
            QIcon::fromTheme(m_icon, QIcon::fromTheme(QStringLiteral("unknown"))).paint(&painter, QRect((width() - side) / 2, (height() - side) / 2, side, side));
        }
        painter.restore();
    }

private:
    QPixmap m_pixmap;
    QList<QPixmap> m_stack;
    QStringList m_lines;
    QString m_icon;
    QColor m_tint;
    bool m_canOpen = false;
    QToolButton *m_eye = nullptr;
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

// ---- what a folder (or a selection) holds: a bar of its kinds and their counts
class NgInfoMix : public QWidget
{
public:
    using QWidget::QWidget;
    struct Part {
        QColor colour;
        int count;
        QString label;
    };
    void setParts(const QList<Part> &parts)
    {
        m_parts = parts;
        setVisible(!parts.isEmpty());
        updateGeometry();
        update();
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int width) const override
    {
        return 14 + legendRows(width) * (fontMetrics().height() + 2);
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
        qreal x = 0;
        for (const Part &part : std::as_const(m_parts)) {
            const qreal w = (width() - 2.0 * (m_parts.count() - 1)) * part.count / qMax(1, total);
            painter.setPen(Qt::NoPen);
            painter.setBrush(part.colour);
            painter.drawRoundedRect(QRectF(x, 0, qMax(3.0, w), 8), 3, 3);
            x += w + 2;
        }
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.86);
        painter.setFont(small);
        const QFontMetricsF metrics(small);
        QColor dim = palette().color(QPalette::WindowText);
        dim.setAlphaF(0.7);
        qreal lx = 0, ly = 14;
        for (const Part &part : std::as_const(m_parts)) {
            const qreal w = 13 + metrics.horizontalAdvance(part.label) + 12;
            if (lx > 0 && lx + w > width()) {
                lx = 0;
                ly += metrics.height() + 2;
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(part.colour);
            painter.drawRoundedRect(QRectF(lx, ly + (metrics.height() - 8) / 2, 8, 8), 2, 2);
            painter.setPen(dim);
            ngInfoText(painter, QRectF(lx + 13, ly, w, metrics.height()), Qt::AlignLeft | Qt::AlignVCenter, part.label, 0.5);
            lx += w;
        }
    }

private:
    int legendRows(int width) const
    {
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.86);
        const QFontMetricsF metrics(small);
        int rows = m_parts.isEmpty() ? 0 : 1;
        qreal lx = 0;
        for (const Part &part : std::as_const(m_parts)) {
            const qreal w = 13 + metrics.horizontalAdvance(part.label) + 12;
            if (lx > 0 && lx + w > width) {
                lx = 0;
                ++rows;
            }
            lx += w;
        }
        return rows;
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
    build();
    connect(NgActivity::self(), &NgActivity::changed, this, &NgInfoView::showActivity);
}

NgInfoView::~NgInfoView()
{
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
    connect(m_hero->eye(), &QToolButton::clicked, this, [this] {
        if (!m_item.isNull()) {
            Q_EMIT lightboxRequested(m_item);
        }
    });

    m_media = new MediaWidget(m_body);
    m_media->hide();
    m_layout->addWidget(m_media);

    // the name, then the kind, the size and pinning on one line
    m_name = new QLabel(m_body);
    m_name->setWordWrap(true);
    m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont nameFont = m_name->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.12);
    nameFont.setWeight(QFont::Bold);
    m_name->setFont(nameFont);
    auto *headGroup = new QVBoxLayout();
    headGroup->setSpacing(8);
    headGroup->addWidget(m_name);
    auto *chips = new QHBoxLayout();
    chips->setSpacing(8);
    m_kind = new QLabel(m_body);
    m_size = new QLabel(m_body);
    m_pin = new QToolButton(m_body);
    m_pin->setCheckable(true);
    m_pin->setAutoRaise(true);
    m_pin->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pin->setIcon(QIcon::fromTheme(QStringLiteral("window-pin"), QIcon::fromTheme(QStringLiteral("pin"))));
    chips->addWidget(m_kind);
    chips->addWidget(m_size);
    chips->addStretch();
    chips->addWidget(m_pin);
    headGroup->addLayout(chips);
    m_layout->addLayout(headGroup);
    connect(m_pin, &QToolButton::clicked, this, [this](bool on) {
        const KFileItemList items = m_items.isEmpty() ? KFileItemList{m_item} : m_items;
        Q_EMIT pinRequested(items, on);
        m_pin->setText(on ? ngTr("Pinned", "Fissato") : ngTr("Pin to the top", "Fissa in cima"));
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
    m_actionCaption->setEnabled(false);
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

    // where it came from, who made it, its rating and tags
    m_lines = new QWidget(m_body);
    m_linesLayout = new QGridLayout(m_lines);
    m_linesLayout->setContentsMargins(4, 0, 4, 0);
    m_linesLayout->setHorizontalSpacing(16);
    m_linesLayout->setVerticalSpacing(10);
    m_linesLayout->setColumnStretch(1, 1);
    auto caption = [this](const QString &text) {
        auto *label = new QLabel(text, m_lines);
        label->setEnabled(false);
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
            }
            showRating(rating);
        });
    }
    starsLayout->addStretch();
    m_tags = new QWidget(m_lines);
    m_tagsLayout = new QHBoxLayout(m_tags);
    m_tagsLayout->setContentsMargins(0, 0, 0, 0);
    m_tagsLayout->setSpacing(4);
    m_linesLayout->addWidget(m_originLabel, 0, 0, Qt::AlignTop);
    m_linesLayout->addWidget(m_origin, 0, 1);
    m_linesLayout->addWidget(m_whoLabel, 1, 0, Qt::AlignTop);
    m_linesLayout->addWidget(m_who, 1, 1);
    m_linesLayout->addWidget(caption(ngTr("Rating", "Voto")), 2, 0, Qt::AlignVCenter);
    m_linesLayout->addWidget(m_stars, 2, 1);
    m_linesLayout->addWidget(caption(ngTr("Tags", "Etichette")), 3, 0, Qt::AlignVCenter);
    m_linesLayout->addWidget(m_tags, 3, 1);
    m_layout->addWidget(m_lines);

    m_dates = new NgInfoDates(m_body);
    m_dates->hide();
    m_layout->addWidget(m_dates);
    m_layout->addStretch();
    restyle();
}

void NgInfoView::restyle()
{
    const QPalette palette = QApplication::palette();
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
    m_pin->setStyleSheet(QStringLiteral("QToolButton { border-radius: 12px; padding: 3px 10px; background: %1; }"
                                        "QToolButton:checked { background: %2; color: %3; }")
                             .arg(veilCss, accent.name(), ngOnAccent(accent).name()));
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

void NgInfoView::askPreview(const KFileItemList &items)
{
    if (m_previewJob) {
        m_previewJob->kill();
    }
    const qreal dpr = devicePixelRatioF();
    const int side = qBound(256, qRound(qMax(m_hero->width(), 260) * dpr * 1.2), 1024);
    m_previewJob = new KIO::PreviewJob(items, QSize(side, side));
    m_previewJob->setIgnoreMaximumSize(items.count() == 1);
    m_previewJob->setDevicePixelRatio(dpr);
    const bool stack = items.count() > 1;
    connect(m_previewJob, &KIO::PreviewJob::gotPreview, this, [this, stack](const KFileItem &, const QPixmap &pixmap) {
        if (stack) {
            m_hero->addToStack(pixmap);
        } else {
            m_hero->setPixmap(pixmap);
        }
    });
}

void NgInfoView::showOne()
{
    const KFileItem &item = m_item;
    m_hero->clear();
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
    m_name->setText(item.text());
    m_kind->setText(kind.sigla.isEmpty() ? item.mimeComment() : kind.sigla);
    m_kind->show();
    m_kind->setStyleSheet(QStringLiteral("QLabel { color: white; background: %1; border-radius: 8px; padding: 1px 8px; font-weight: bold; }").arg(kind.tint.name()));
    m_size->setText(item.isDir() ? item.mimeComment() : KIO::convertSize(item.size()));
    const bool pinned = m_pinnedQuery ? m_pinnedQuery({item}) : false;
    m_pin->setChecked(pinned);
    m_pin->setText(pinned ? ngTr("Pinned", "Fissato") : ngTr("Pin to the top", "Fissa in cima"));

    // ---- quick actions
    while (QLayoutItem *child = m_actionsLayout->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    const QList<NgQuickAction> actions = item.isLocalFile() ? ngQuickActionsFor(item.localPath(), mime) : QList<NgQuickAction>();
    for (int i = 0; i < actions.count(); ++i) {
        auto *button = new QToolButton(m_actions);
        button->setIcon(QIcon::fromTheme(actions.at(i).icon, QIcon::fromTheme(QStringLiteral("system-run"))));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(actions.at(i).label);
        button->setAutoRaise(true);
        button->installEventFilter(this);
        button->setProperty("_ng_caption", actions.at(i).label);
        connect(button, &QToolButton::clicked, this, [this, i] {
            Q_EMIT quickActionRequested(m_item, i);
        });
        m_actionsLayout->addWidget(button);
    }
    m_actions->setVisible(!actions.isEmpty());
    m_actionCaption->setText(actions.isEmpty() ? QString() : ngTr("Quick actions", "Azioni rapide"));
    m_actionCaption->setVisible(!actions.isEmpty());

    // ---- the work in progress, the facts, what it holds, where it came from
    showActivity();
    showFacts();

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
    m_facts->setFacts(shown);
    QList<NgInfoMix::Part> parts;
    for (const NgMixPart &part : facts->mix) {
        parts.append({part.colour, part.count, part.label});
    }
    m_mix->setParts(parts);
    m_origin->setText(facts->origin);
    m_origin->setVisible(!facts->origin.isEmpty());
    m_originLabel->setVisible(!facts->origin.isEmpty());
    const QString whoCaption = facts->whoKind == NgFileFacts::Camera ? ngTr("Camera", "Fotocamera")
        : facts->whoKind == NgFileFacts::Artist                      ? ngTr("Artist", "Artista")
        : facts->whoKind == NgFileFacts::Author                      ? ngTr("Author", "Autore")
        : facts->whoKind == NgFileFacts::Program                     ? ngTr("Made with", "Creato con")
                                                                     : QString();
    m_whoLabel->setText(whoCaption);
    m_who->setText(facts->who);
    m_who->setVisible(!facts->who.isEmpty());
    m_whoLabel->setVisible(!facts->who.isEmpty());
    if (m_item.isDir() && !facts->lastName.isEmpty()) {
        m_whoLabel->setText(ngTr("Changed last", "Cambiato per ultimo"));
        m_who->setText(facts->lastName);
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
    for (const QString &tag : std::as_const(m_tagList)) {
        auto *chip = new QLabel(tag, m_tags);
        chip->setStyleSheet(QStringLiteral("QLabel { background: rgba(%1, %2, %3, %4); border-radius: 9px; padding: 1px 8px; }")
                                .arg(veil.red())
                                .arg(veil.green())
                                .arg(veil.blue())
                                .arg(QString::number(qMin(1.0, veil.alphaF() * 1.8), 'f', 3)));
        m_tagsLayout->addWidget(chip);
    }
    auto *add = new QToolButton(m_tags);
    add->setText(ngTr("+ tag", "+ etichetta"));
    add->setAutoRaise(true);
    add->setEnabled(!m_item.isNull() && m_item.isLocalFile());
    connect(add, &QToolButton::clicked, this, &NgInfoView::editTags);
    m_tagsLayout->addWidget(add);
    m_tagsLayout->addStretch();
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
    m_tagList.append(tag);
    KFileMetaData::UserMetaData(m_item.localPath()).setTags(m_tagList);
    showTags();
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
    while (QLayoutItem *child = m_actionsLayout->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    m_actions->setVisible(false);
    m_actionCaption->setVisible(false);

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
    m_kind->hide();
    m_size->setText(folders == m_items.count() ? ngTr("%1 folders", "%1 cartelle").arg(folders)
                                               : KIO::convertSize(total) + (folders ? ngTr(" and %1 folders", " e %1 cartelle").arg(folders) : QString()));
    const bool pinned = m_pinnedQuery ? m_pinnedQuery(m_items) : false;
    m_pin->setChecked(pinned);
    m_pin->setText(pinned ? ngTr("Pinned", "Fissati") : ngTr("Pin to the top", "Fissa in cima"));
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
    askPreview(m_items.mid(0, 3));
}

#include "moc_nginfoview.cpp"
