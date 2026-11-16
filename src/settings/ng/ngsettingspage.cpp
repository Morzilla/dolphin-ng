/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ngsettingspage.h"

#include "kitemviews/kitemlistwidget.h"
#include "dolphin_contentdisplaysettings.h"
#include "dolphinplacesmodelsingleton.h"
#include "panels/places/placespanel.h"

#include <QListWidget>

#include <Solid/Device>

#include <KColorButton>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <KUrlRequester>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QToolButton>
#include <functional>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QFormLayout>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QLabel>
#include <KMessageWidget>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

// The labels are in Italian for now, like the other dolphin-ng strings; they should go through i18n.

namespace
{
QSlider *percentSlider(QWidget *parent, int from, int to)
{
    auto *slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(from, to);
    slider->setTickPosition(QSlider::NoTicks);
    return slider;
}
// The blur behind the live sidebar is the compositor's: Better Blur keeps its strength (1-15) in
// kwinrc [Effect-blurplus], KWin's own blur in [Effect-blur].
KConfigGroup kwinBlurGroup()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    return config->group(config->hasGroup(QStringLiteral("Effect-blurplus")) ? QStringLiteral("Effect-blurplus") : QStringLiteral("Effect-blur"));
}
void reloadKWinBlur()
{
    const QString kwin = QStringLiteral("org.kde.KWin"), path = QStringLiteral("/Effects"), iface = QStringLiteral("org.kde.kwin.Effects");
    const QStringList effects = {QStringLiteral("forceblur_x11"), QStringLiteral("forceblur"), QStringLiteral("blur")};
    for (const QString &effect : effects) {
        QDBusMessage call = QDBusMessage::createMethodCall(kwin, path, iface, QStringLiteral("reconfigureEffect"));
        call.setArguments({effect});
        QDBusConnection::sessionBus().asyncCall(call);
    }
}
}

// ---------------------------------------------------------------------------------------------------------
// dolphin-ng: the page is graphical. Every choice between looks is a row of tiles with a small drawing of the
// look, on/off options are switches, and a miniature of the window on the left follows the choices. The combo
// boxes and check boxes of the old page are still there, unseen, as the values the tiles and switches drive:
// loading, applying, the presets and the defaults keep working on them.
namespace
{
const QColor kindPdf(0xe0, 0x52, 0x4a), kindDoc(0x4a, 0x8f, 0xe0), kindSheet(0x4c, 0xaf, 0x6a), kindImage(0x5b, 0x7b, 0xb0), kindCode(0xc9, 0xa2, 0x4d),
    kindFolder(0x8f, 0x99, 0xa6);

// KDE gives every button a keyboard accelerator by putting "&" in its text; these widgets draw their own text
QString ngPlainText(QString text)
{
    return text.remove(QLatin1Char('&'));
}
QColor ngInk(const QPalette &palette, qreal alpha)
{
    QColor colour = palette.color(QPalette::WindowText);
    colour.setAlphaF(alpha);
    return colour;
}
QColor ngWithAlpha(QColor colour, qreal alpha)
{
    colour.setAlphaF(alpha);
    return colour;
}
void ngBar(QPainter &painter, const QRectF &rect, const QColor &colour)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);
}
// a picture standing for "an image": the same in the tiles and in the miniature
QBrush ngPictureBrush(const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, QColor(0x3f, 0xb0, 0xc9));
    gradient.setColorAt(0.6, QColor(0x6b, 0x5f, 0xb5));
    gradient.setColorAt(1.0, QColor(0xe0, 0xa0, 0x3a));
    return gradient;
}
// what is behind the sidebar card: plain | image ("") | lava | live
QBrush ngBackdropBrush(const QString &side, const QRectF &rect, const QPalette &palette)
{
    if (side == QLatin1String("plain")) {
        return ngInk(palette, 0.08);
    }
    if (side == QLatin1String("live")) {
        return QColor(255, 255, 255, 40);
    }
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    if (side == QLatin1String("lava")) {
        gradient.setColorAt(0.0, QColor(0x1f, 0x7a, 0x78));
        gradient.setColorAt(0.5, QColor(0x2b, 0x2f, 0x5c));
        gradient.setColorAt(1.0, QColor(0xb3, 0x74, 0x2c));
    } else {
        gradient.setColorAt(0.0, QColor(0x47, 0x61, 0x7a));
        gradient.setColorAt(0.55, QColor(0x2b, 0x3a, 0x4d));
        gradient.setColorAt(1.0, QColor(0x5a, 0x4a, 0x63));
    }
    return gradient;
}
// the desktop showing through, behind a "live" sidebar
void ngWallpaperWedge(QPainter &painter, const QRectF &rect, qreal fraction)
{
    QPainterPath wedge;
    wedge.moveTo(rect.topLeft());
    wedge.lineTo(rect.left() + rect.width() * (fraction + 0.08), rect.top());
    wedge.lineTo(rect.left() + rect.width() * (fraction - 0.04), rect.bottom());
    wedge.lineTo(rect.bottomLeft());
    wedge.closeSubpath();
    painter.fillPath(wedge, QColor(0x5d, 0x3f, 0x6e));
}
void ngRim(QPainter &painter, const QRectF &card, qreal radius, const QString &rim, const QPalette &palette)
{
    painter.setBrush(Qt::NoBrush);
    const QRectF line = card.adjusted(0.5, 0.5, -0.5, -0.5);
    if (rim == QLatin1String("soft")) {
        painter.setPen(QPen(ngInk(palette, 0.32), 1));
        painter.drawRoundedRect(line, radius, radius);
    } else if (rim == QLatin1String("diagonal")) {
        QLinearGradient gradient(card.topLeft(), card.bottomRight());
        gradient.setColorAt(0.0, ngInk(palette, 0.85));
        gradient.setColorAt(0.6, ngInk(palette, 0.0));
        painter.setPen(QPen(QBrush(gradient), 1.4));
        painter.drawRoundedRect(line, radius, radius);
    } else if (rim == QLatin1String("bevel")) {
        painter.setPen(QPen(QColor(0, 0, 0, 170), 1));
        painter.drawRoundedRect(line, radius, radius);
        painter.setPen(QPen(ngInk(palette, 0.30), 1));
        painter.drawRoundedRect(line.adjusted(1, 1, -1, -1), radius - 1, radius - 1);
    } else if (rim == QLatin1String("glow")) {
        for (int i = 3; i >= 1; --i) {
            painter.setPen(QPen(ngInk(palette, 0.07 * (4 - i)), i * 2));
            painter.drawRoundedRect(line, radius, radius);
        }
    } else if (rim == QLatin1String("top")) {
        painter.save();
        painter.setClipRect(QRectF(card.left(), card.top(), card.width(), radius + 1));
        painter.setPen(QPen(ngInk(palette, 0.8), 1.4));
        painter.drawRoundedRect(line, radius, radius);
        painter.restore();
    }
}
void ngLetters(QPainter &painter, const QRectF &rect, const QString &text, const QColor &colour, qreal pixelSize, int flags = Qt::AlignCenter)
{
    QFont font = painter.font();
    font.setPixelSize(qMax(6, qRound(pixelSize)));
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(colour);
    painter.drawText(rect, flags, text);
}
void ngGlyph(QPainter &painter, const QRectF &rect, const QColor &colour, qreal width = 1.6)
{
    painter.setPen(QPen(colour, width));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, rect.width() * 0.22, rect.width() * 0.22);
}

// One row of the miniature list: `background` is card | open | line | stripe | none
// (`washKind` invalid: no colour wash; the mark and the chip keep the colour of the type, `markKind`)
void ngMiniRow(QPainter &painter, const QRectF &row, const QString &background, const QString &mark, const QColor &washKind, bool odd, bool twoLines, bool thumb,
               bool actions, const QPalette &palette, const QColor &markKind = QColor())
{
    const QColor kind = markKind.isValid() ? markKind : washKind;
    painter.setPen(Qt::NoPen);
    if (background == QLatin1String("card")) {
        painter.setBrush(ngInk(palette, 0.06));
        painter.drawRoundedRect(row, 3, 3);
    } else if (background == QLatin1String("stripe") && odd) {
        painter.fillRect(row, ngInk(palette, 0.07));
    }
    if (washKind.isValid() && (background == QLatin1String("card") || background == QLatin1String("open") || background == QLatin1String("line"))) {
        QLinearGradient wash(row.left(), 0, row.left() + row.width() * 0.5, 0);
        wash.setColorAt(0.0, ngWithAlpha(kind, 0.26));
        wash.setColorAt(1.0, ngWithAlpha(kind, 0.0));
        painter.fillRect(row, wash);
    }
    if (background == QLatin1String("line")) {
        painter.fillRect(QRectF(row.left(), row.bottom() - 1, row.width(), 1), ngInk(palette, 0.14));
    }
    const qreal side = qMin(row.height() - 4, 12.0);
    QRectF markRect(row.left() + 5, row.center().y() - side / 2, side, side);
    if (thumb) {
        const QRectF picture = markRect.adjusted(-1, 0, 4, 0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ngPictureBrush(picture));
        painter.drawRoundedRect(picture, 2, 2);
        markRect = picture;
    } else if (mark == QLatin1String("tile")) {
        painter.setPen(QPen(ngWithAlpha(kind, 0.5), 1));
        painter.setBrush(ngWithAlpha(kind, 0.22));
        painter.drawRoundedRect(markRect, 3, 3);
    } else if (mark == QLatin1String("pill")) {
        const QRectF pill(markRect.left(), row.center().y() - side * 0.32, side * 1.3, side * 0.64);
        painter.setPen(QPen(ngWithAlpha(kind, 0.5), 1));
        painter.setBrush(ngWithAlpha(kind, 0.22));
        painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        markRect = pill;
    } else if (mark == QLatin1String("header")) {
        const QRectF head(row.left(), row.top(), side + 6, row.height());
        painter.fillRect(head, ngWithAlpha(kind, 0.36));
        markRect = head;
    } else if (mark == QLatin1String("signs")) {
        ngGlyph(painter, markRect.adjusted(1, 1, -1, -1), kind, 1.4);
    } else if (mark == QLatin1String("object") || mark.isEmpty()) {
        const QRectF page(markRect.left(), row.top() + 2, side + 4, row.height() - 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(236, 238, 241));
        painter.drawRoundedRect(page, 2, 2);
        ngBar(painter, QRectF(page.left() + 2, page.top() + 2, page.width() * 0.6, 2.5), kind);
        markRect = page;
    } else { // watermark
        painter.setPen(Qt::NoPen);
        painter.setBrush(ngWithAlpha(kind, 0.42));
        painter.drawRoundedRect(markRect.adjusted(0, -1, 5, 1), 2, 2);
    }
    const qreal textLeft = markRect.right() + 6;
    const qreal lineHeight = qMin(4.0, row.height() / 5.0);
    if (twoLines) {
        ngBar(painter, QRectF(textLeft, row.center().y() - lineHeight - 1.5, row.width() * 0.34, lineHeight), ngInk(palette, 0.6));
        ngBar(painter, QRectF(textLeft, row.center().y() + 1.5, row.width() * 0.14, lineHeight + 1), ngWithAlpha(kind, 0.6));
    } else {
        ngBar(painter, QRectF(textLeft, row.center().y() - lineHeight / 2, row.width() * 0.34, lineHeight), ngInk(palette, 0.6));
        ngBar(painter, QRectF(row.left() + row.width() * 0.62, row.center().y() - lineHeight / 2, row.width() * 0.12, lineHeight), ngInk(palette, 0.25));
    }
    if (actions && twoLines) {
        const QRectF pill(row.right() - row.width() * 0.34, row.center().y() - 4, row.width() * 0.16, 8);
        painter.setPen(QPen(ngInk(palette, 0.16), 1));
        painter.setBrush(QColor(0, 0, 0, 80));
        painter.drawRoundedRect(pill, 4, 4);
    }
    ngBar(painter, QRectF(row.right() - row.width() * 0.13, row.center().y() - lineHeight / 2, row.width() * 0.10, lineHeight), ngInk(palette, 0.3));
}

// One card of the miniature: `look` is a | b | c
void ngMiniCard(QPainter &painter, const QRectF &card, const QString &look, const QColor &kind, bool picture, bool typeBackground, const QPalette &palette)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(ngInk(palette, 0.07));
    painter.drawRoundedRect(card, 5, 5);
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(card, 5, 5);
    painter.setClipPath(clip);
    QLinearGradient wash(card.topLeft(), look == QLatin1String("a") ? card.bottomLeft() : card.topRight());
    wash.setColorAt(0.0, ngWithAlpha(kind, typeBackground ? 0.36 : 0.22));
    wash.setColorAt(0.7, ngWithAlpha(kind, 0.0));
    painter.fillRect(card, wash);
    const qreal pad = qMax(3.0, card.width() * 0.07);
    const qreal lineHeight = qMax(2.5, card.height() * 0.07);
    if (look == QLatin1String("a")) {
        const QRectF band(card.left(), card.top(), card.width(), card.height() * 0.46);
        if (picture) {
            painter.fillRect(band, ngPictureBrush(band));
        } else if (typeBackground) {
            ngLetters(painter, band.adjusted(0, 0, -pad, 0), QStringLiteral("PDF"), ngWithAlpha(kind, 0.55), band.height() * 0.6, Qt::AlignRight | Qt::AlignVCenter);
        } else {
            const qreal side = band.height() * 0.5;
            ngGlyph(painter, QRectF(band.center().x() - side / 2, band.center().y() - side / 2, side, side), kind);
        }
        ngBar(painter, QRectF(card.left() + pad, band.bottom() + pad, card.width() * 0.6, lineHeight), ngInk(palette, 0.62));
        ngBar(painter, QRectF(card.left() + pad, band.bottom() + pad + lineHeight * 2, card.width() * 0.3, lineHeight), ngWithAlpha(kind, 0.6));
        ngBar(painter, QRectF(card.left() + pad, card.bottom() - pad - lineHeight, card.width() * 0.45, lineHeight), ngInk(palette, 0.22));
    } else if (look == QLatin1String("b")) {
        const qreal side = qMin(card.height() * 0.42, card.width() * 0.24);
        const QRectF icon(card.left() + pad, card.top() + pad, side, side);
        if (picture) {
            painter.setBrush(ngPictureBrush(icon));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(icon, 2, 2);
        } else {
            ngGlyph(painter, icon, kind);
        }
        const qreal x = icon.right() + pad;
        ngBar(painter, QRectF(x, card.top() + pad, card.right() - pad - x, lineHeight), ngInk(palette, 0.62));
        ngBar(painter, QRectF(x, card.top() + pad + lineHeight * 2, (card.right() - pad - x) * 0.5, lineHeight), ngInk(palette, 0.25));
        ngBar(painter, QRectF(x, card.top() + pad + lineHeight * 4, (card.right() - pad - x) * 0.8, lineHeight), ngInk(palette, 0.25));
        ngBar(painter, QRectF(card.left() + pad, card.bottom() - pad - lineHeight * 1.6, side, lineHeight * 1.6), ngWithAlpha(kind, 0.6));
    } else {
        ngGlyph(painter, QRectF(card.left() + pad, card.top() + pad, lineHeight * 2.4, lineHeight * 2.4), kind, 1.2);
        ngBar(painter, QRectF(card.left() + pad + lineHeight * 3.4, card.top() + pad + lineHeight * 0.7, card.width() * 0.45, lineHeight), ngInk(palette, 0.62));
        ngBar(painter, QRectF(card.left() + pad, card.top() + pad + lineHeight * 4, card.width() * 0.24, lineHeight * 1.4), ngWithAlpha(kind, 0.6));
        ngBar(painter, QRectF(card.right() - pad - card.width() * 0.46, card.bottom() - pad - lineHeight * 2.4, card.width() * 0.46, lineHeight * 2.4), ngInk(palette, 0.75));
        ngBar(painter, QRectF(card.left() + pad, card.bottom() - pad - lineHeight, card.width() * 0.26, lineHeight), ngInk(palette, 0.22));
    }
    painter.restore();
}

// The drawing inside a tile: `key` names the option, `id` the choice
void ngDrawChoice(QPainter &painter, const QRectF &rect, const QString &key, const QString &id, const QPalette &palette)
{
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF in = rect.adjusted(6, 6, -6, -6);
    if (key == QLatin1String("preset")) {
        if (id == QLatin1String("glass")) {
            ngWallpaperWedge(painter, rect, 0.34);
        }
        const QRectF panel(in.left(), in.top(), in.width() * 0.26, in.height());
        painter.setPen(id == QLatin1String("glass") ? QPen(ngInk(palette, 0.3), 1) : QPen(Qt::NoPen));
        painter.setBrush(ngBackdropBrush(id == QLatin1String("glass") ? QStringLiteral("live") : QStringLiteral("plain"), panel, palette));
        painter.drawRoundedRect(panel, 4, 4);
        const QRectF main(panel.right() + 6, in.top(), in.right() - panel.right() - 6, in.height());
        static const QColor kinds[4] = {kindDoc, kindPdf, kindSheet, kindCode};
        if (id == QLatin1String("classic")) {
            for (int i = 0; i < 4; ++i) {
                ngMiniRow(painter, QRectF(main.left(), main.top() + i * main.height() / 4, main.width(), main.height() / 4 - 1), QStringLiteral("stripe"), QStringLiteral("signs"),
                          kinds[i], i % 2, false, false, false, palette);
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                const QRectF card(main.left() + (i % 2) * (main.width() / 2 + 2), main.top() + (i / 2) * (main.height() / 2 + 2), main.width() / 2 - 4, main.height() / 2 - 4);
                ngMiniCard(painter, card, QStringLiteral("b"), kinds[i], false, id == QLatin1String("glass"), palette);
            }
        }
    } else if (key == QLatin1String("side")) {
        if (id == QLatin1String("live")) {
            ngWallpaperWedge(painter, rect, 0.42);
        }
        const QRectF panel(in.left(), in.top(), in.width() * 0.36, in.height());
        painter.setPen(id == QLatin1String("plain") ? QPen(Qt::NoPen) : QPen(ngInk(palette, 0.28), 1));
        painter.setBrush(ngBackdropBrush(id, panel, palette));
        painter.drawRoundedRect(panel, 4, 4);
        ngBar(painter, QRectF(panel.right() + 8, in.center().y() - 6, in.width() * 0.4, 4), ngInk(palette, 0.5));
        ngBar(painter, QRectF(panel.right() + 8, in.center().y() + 2, in.width() * 0.24, 4), ngInk(palette, 0.25));
    } else if (key == QLatin1String("rim")) {
        const QRectF card(in.center().x() - 27, in.center().y() - 16, 54, 32);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ngInk(palette, 0.12));
        painter.drawRoundedRect(card, 7, 7);
        ngRim(painter, card, 7, id, palette);
    } else if (key == QLatin1String("rows")) {
        static const QColor kinds[3] = {kindDoc, kindPdf, kindSheet};
        for (int i = 0; i < 3; ++i) {
            ngMiniRow(painter, QRectF(in.left(), in.top() + i * in.height() / 3, in.width(), in.height() / 3 - 2), id, QStringLiteral("signs"), kinds[i], i % 2, false, false,
                      false, palette);
        }
    } else if (key == QLatin1String("mark")) {
        const QColor kind = kindPdf;
        if (id == QLatin1String("tile")) {
            const QRectF tile(in.center().x() - 19, in.center().y() - 19, 38, 38);
            painter.setPen(QPen(ngWithAlpha(kind, 0.5), 1));
            painter.setBrush(ngWithAlpha(kind, 0.2));
            painter.drawRoundedRect(tile, 9, 9);
            ngLetters(painter, tile, QStringLiteral("PDF"), kind, 11);
        } else if (id == QLatin1String("pill")) {
            const QRectF pill(in.center().x() - 26, in.center().y() - 11, 52, 22);
            painter.setPen(QPen(ngWithAlpha(kind, 0.5), 1));
            painter.setBrush(ngWithAlpha(kind, 0.2));
            painter.drawRoundedRect(pill, 11, 11);
            ngLetters(painter, pill, QStringLiteral("PDF"), kind, 11);
        } else if (id == QLatin1String("header")) {
            const QRectF head(rect.left(), rect.top(), 38, rect.height());
            painter.fillRect(head, ngWithAlpha(kind, 0.3));
            ngLetters(painter, head, QStringLiteral("PDF"), kind, 11);
            ngBar(painter, QRectF(head.right() + 8, in.center().y() - 2, in.width() * 0.4, 4), ngInk(palette, 0.5));
        } else if (id == QLatin1String("signs")) {
            ngGlyph(painter, QRectF(in.left() + 6, in.center().y() - 8, 16, 16), kind, 1.8);
            ngLetters(painter, QRectF(in.left() + 28, in.top(), 60, in.height()), QStringLiteral("PDF"), kind, 13, Qt::AlignLeft | Qt::AlignVCenter);
        } else if (id == QLatin1String("object")) {
            // a small page with its type as the title and its size at the foot, as drawn on the cards
            const QRectF page(in.center().x() - 24, in.top() + 4, 48, in.height() + 6);
            painter.save();
            painter.setClipRect(rect);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(236, 238, 241));
            painter.drawRoundedRect(page, 3, 3);
            ngLetters(painter, QRectF(page.left() + 5, page.top() + 2, 40, 16), QStringLiteral("PDF"), kind, 11, Qt::AlignLeft | Qt::AlignVCenter);
            ngBar(painter, QRectF(page.left() + 5, page.top() + 21, 34, 3), QColor(190, 195, 202));
            ngBar(painter, QRectF(page.left() + 5, page.top() + 27, 26, 3), QColor(190, 195, 202));
            painter.restore();
        } else {
            ngLetters(painter, in, QStringLiteral("PDF"), ngWithAlpha(kind, 0.4), in.height() * 0.8, Qt::AlignLeft | Qt::AlignVCenter);
        }
    } else if (key == QLatin1String("sigla")) {
        const QColor kind = kindPdf;
        if (id == QLatin1String("outline")) {
            QFont font = painter.font();
            font.setPixelSize(qRound(in.height() * 0.78));
            font.setWeight(QFont::Black);
            QPainterPath letters;
            letters.addText(QPointF(in.left() + 4, in.bottom() - in.height() * 0.2), font, QStringLiteral("PDF"));
            painter.fillPath(letters, ngWithAlpha(kind, 0.08));
            painter.strokePath(letters, QPen(ngWithAlpha(kind, 0.8), 1.2));
        } else if (id == QLatin1String("label")) {
            ngLetters(painter, in, QStringLiteral("PDF"), kind, 12, Qt::AlignLeft | Qt::AlignVCenter);
        } else {
            ngLetters(painter, in, QStringLiteral("PDF"), ngWithAlpha(kind, 0.4), in.height() * 0.8, Qt::AlignLeft | Qt::AlignVCenter);
        }
    } else if (key == QLatin1String("cards")) {
        const QSizeF size = id == QLatin1String("a") ? QSizeF(52, in.height()) : id == QLatin1String("b") ? QSizeF(96, in.height() * 0.66) : QSizeF(100, in.height() * 0.8);
        ngMiniCard(painter, QRectF(in.center().x() - size.width() / 2, in.center().y() - size.height() / 2, size.width(), size.height()), id, kindImage,
                   id == QLatin1String("a"), false, palette);
    } else if (key == QLatin1String("fit")) {
        const bool stretch = id == QLatin1String("stretch");
        const qreal width = stretch ? (in.width() - 8) / 3 : in.width() * 0.24;
        const qreal step = stretch ? width + 4 : (in.width() - width) / 2;
        for (int i = 0; i < 3; ++i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ngInk(palette, 0.16));
            painter.drawRoundedRect(QRectF(in.left() + i * step, in.top() + 4, width, in.height() - 8), 4, 4);
        }
    } else if (key == QLatin1String("cardsmark")) {
        ngMiniCard(painter, QRectF(in.center().x() - 26, in.top(), 52, in.height()), QStringLiteral("a"), kindPdf, false, id == QLatin1String("type"), palette);
    } else if (key == QLatin1String("activity")) {
        painter.fillRect(rect, ngPictureBrush(rect));
        painter.fillRect(rect, QColor(10, 11, 14, 110));
        const qreal k = id == QLatin1String("small") ? 0.8 : id == QLatin1String("large") ? 1.25 : 1.0;
        const qreal h = 16 * k, w = 64 * k;
        const QRectF capsule(rect.center().x() - w / 2, rect.center().y() - h / 2, w, h);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 20, 24, 238));
        painter.drawRoundedRect(capsule, h / 2, h / 2);
        const qreal d = h - 4;
        const QRectF ring(capsule.left() + 2.5, capsule.top() + 2.5, d - 1, d - 1);
        painter.setPen(QPen(QColor(255, 255, 255, 40), 1.6));
        painter.drawEllipse(ring);
        painter.setPen(QPen(palette.color(QPalette::Highlight).lighter(130), 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(ring, 90 * 16, -150 * 16);
        ngBar(painter, QRectF(capsule.left() + d + 6, capsule.center().y() - 1.5, w * 0.4, 3), QColor(230, 232, 236, 200));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 30));
        painter.drawEllipse(QRectF(capsule.right() - d - 2, capsule.top() + 2, d, d));
    } else if (key == QLatin1String("badge")) {
        if (id == QLatin1String("inside")) {
            // a box with its type printed on the side
            painter.fillRect(rect, ngWithAlpha(kindPdf, 0.12));
            const QRectF box(rect.center().x() - 26, rect.top() + 14, 52, rect.height());
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(120, 98, 190));
            painter.drawRoundedRect(box, 3, 3);
            painter.setBrush(QColor(168, 146, 232));
            painter.drawRoundedRect(QRectF(box.left() - 3, box.top() - 4, box.width() + 6, 9), 3, 3);
            QFont f = painter.font();
            f.setPixelSize(12);
            f.setWeight(QFont::Black);
            painter.setFont(f);
            painter.setPen(QColor(232, 226, 250));
            painter.drawText(QRectF(box.left(), box.top() + 8, box.width(), 16), Qt::AlignCenter, QStringLiteral("RPM"));
            return;
        }
        if (id == QLatin1String("fold")) {
            // a picture with its bottom right corner folded over, in glass
            painter.fillRect(rect, ngPictureBrush(rect));
            const QPointF corner = rect.bottomRight(), a(corner.x() - 40, corner.y()), b(corner.x(), corner.y() - 30);
            QPainterPath fold;
            fold.moveTo(corner);
            fold.lineTo(a);
            fold.lineTo(b);
            fold.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 60));
            painter.drawPath(fold.translated(-2, -2));
            painter.setBrush(QColor(255, 255, 255, 110));
            painter.drawPath(fold);
            painter.setPen(QPen(QColor(255, 255, 255, 220), 1.2));
            painter.drawLine(a, b);
            return;
        }
        if (id == QLatin1String("row")) {
            // the picture above, the two chips in their own row under it
            const QRectF picture(rect.left(), rect.top(), rect.width(), rect.height() * 0.5);
            painter.fillRect(picture, ngPictureBrush(picture));
            ngBar(painter, QRectF(rect.left() + 8, picture.bottom() + 6, rect.width() * 0.5, 4), ngInk(palette, 0.6));
            painter.setPen(QPen(ngWithAlpha(kindPdf, 0.6), 1));
            painter.setBrush(ngWithAlpha(kindPdf, 0.22));
            painter.drawRoundedRect(QRectF(rect.left() + 8, picture.bottom() + 15, 26, 10), 5, 5);
            painter.setPen(QPen(ngInk(palette, 0.25), 1));
            painter.setBrush(ngInk(palette, 0.08));
            painter.drawRoundedRect(QRectF(rect.left() + 38, picture.bottom() + 15, 32, 10), 5, 5);
            return;
        }
        // a picture with the badge at its bottom right (in the middle for "center")
        painter.fillRect(rect, ngPictureBrush(rect));
        const QRectF pill(id == QLatin1String("center") ? rect.center().x() - 27 : rect.right() - 62, rect.bottom() - 26, 54, 18);
        QPainterPath shape;
        shape.addRoundedRect(pill, 9, 9);
        painter.save();
        painter.setClipPath(shape, Qt::IntersectClip);
        if ((id == QLatin1String("glass") || id == QLatin1String("center"))) {
            painter.fillRect(pill, QColor(255, 255, 255, 70));
        } else {
            painter.fillRect(pill, QColor(14, 15, 18, 240));
            painter.fillRect(QRectF(pill.left(), pill.top(), 26, pill.height()), kindPdf);
        }
        painter.restore();
        if ((id == QLatin1String("glass") || id == QLatin1String("center"))) {
            painter.setPen(QPen(QColor(255, 255, 255, 130), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(pill.adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
            painter.setPen(Qt::NoPen);
            painter.setBrush(kindPdf);
            painter.drawEllipse(QPointF(pill.left() + 9, pill.center().y()), 3, 3);
        }
        ngBar(painter, QRectF(pill.left() + ((id == QLatin1String("glass") || id == QLatin1String("center")) ? 16 : 7), pill.center().y() - 1.5, 12, 3), (id == QLatin1String("glass") || id == QLatin1String("center")) ? QColor(255, 255, 255) : QColor(20, 22, 26));
        ngBar(painter, QRectF(pill.left() + 33, pill.center().y() - 1.5, 14, 3), QColor(255, 255, 255, 220));
    } else if (key == QLatin1String("quickside")) {
        const QPointF cursor(in.center().x(), in.center().y() + 4);
        const QSizeF size(in.width() * 0.32, in.height() * 0.62);
        const QRectF popup = id == QLatin1String("right") ? QRectF(cursor.x() + 10, cursor.y() - size.height() / 2, size.width(), size.height())
            : id == QLatin1String("auto")                 ? QRectF(cursor.x() - size.width() / 2, in.top(), size.width(), size.height() * 0.6)
                                                           : QRectF(cursor.x() - 10 - size.width(), cursor.y() - size.height() / 2, size.width(), size.height());
        painter.setPen(QPen(ngInk(palette, 0.4), 1));
        painter.setBrush(ngPictureBrush(popup));
        painter.drawRoundedRect(popup, 4, 4);
        QPainterPath arrow;
        arrow.moveTo(cursor);
        arrow.lineTo(cursor + QPointF(0, 11));
        arrow.lineTo(cursor + QPointF(3, 8));
        arrow.lineTo(cursor + QPointF(8, 8));
        arrow.closeSubpath();
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1));
        painter.setBrush(palette.color(QPalette::WindowText));
        painter.drawPath(arrow);
    }
}

// Lays its items out left to right and starts a new line when the width runs out
class NgWrapLayout : public QLayout
{
public:
    explicit NgWrapLayout(QWidget *parent, int spacing)
        : QLayout(parent)
        , m_gap(spacing)
    {
        setContentsMargins(0, 0, 0, 0);
    }
    ~NgWrapLayout() override
    {
        qDeleteAll(m_items);
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
        return place(QRect(0, 0, width, 0), false);
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        place(rect, true);
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
        return size;
    }

private:
    int place(const QRect &rect, bool apply) const
    {
        int x = rect.x(), y = rect.y(), lineHeight = 0;
        for (QLayoutItem *item : m_items) {
            if (item->widget() && item->widget()->isHidden()) {
                continue;
            }
            const QSize hint = item->sizeHint();
            if (x > rect.x() && x + hint.width() > rect.right() + 1) {
                x = rect.x();
                y += lineHeight + m_gap;
                lineHeight = 0;
            }
            if (apply) {
                item->setGeometry(QRect(QPoint(x, y), hint));
            }
            x += hint.width() + m_gap;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y();
    }
    QList<QLayoutItem *> m_items;
    int m_gap;
};

// A choice: the drawing, its name and, on the wide tiles, a line of description
class NgTile : public QAbstractButton
{
public:
    NgTile(const QString &key, const QString &id, const QString &name, const QString &description, QWidget *parent)
        : QAbstractButton(parent)
        , m_key(key)
        , m_id(id)
        , m_description(description)
    {
        setText(name);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setAttribute(Qt::WA_Hover);
    }
    QString id() const
    {
        return m_id;
    }
    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        const int thumb = m_description.isEmpty() ? 56 : 76;
        // a plain tile has room for a name of two lines; a wide one has its name and two lines of description
        return QSize(m_description.isEmpty() ? 106 : 176, 8 + thumb + 6 + (m_description.isEmpty() ? 2 * metrics.height() : 3 * metrics.height() + 2) + 8);
    }
    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(isEnabled() ? 1.0 : 0.45);
        const QPalette &pal = palette();
        const QColor accent = pal.color(QPalette::Highlight);
        const QRectF frame = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
        painter.setPen(isChecked() ? QPen(accent, 1.5) : hasFocus() ? QPen(ngWithAlpha(accent, 0.6), 1) : QPen(Qt::NoPen));
        painter.setBrush(isChecked() ? ngWithAlpha(accent, 0.18) : ngInk(pal, underMouse() ? 0.10 : 0.055));
        painter.drawRoundedRect(frame, 10, 10);

        const QFontMetrics metrics(font());
        const int thumbHeight = m_description.isEmpty() ? 56 : 76;
        const QRectF thumb(8, 8, width() - 16, thumbHeight);
        QPainterPath clip;
        clip.addRoundedRect(thumb, 6, 6);
        painter.save();
        painter.setClipPath(clip);
        painter.fillRect(thumb, QColor(0, 0, 0, pal.color(QPalette::Window).lightness() < 128 ? 70 : 22));
        ngDrawChoice(painter, thumb, m_key, m_id, pal);
        painter.restore();
        painter.setPen(QPen(ngInk(pal, 0.10), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(thumb.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

        QFont nameFont = font();
        nameFont.setWeight(QFont::DemiBold);
        painter.setFont(nameFont);
        painter.setPen(pal.color(QPalette::WindowText));
        const QRectF nameRect(9, thumb.bottom() + 6, width() - 18, m_description.isEmpty() ? 2 * metrics.height() : metrics.height());
        painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignTop | (m_description.isEmpty() ? Qt::TextWordWrap : Qt::TextSingleLine), ngPlainText(text()));
        if (!m_description.isEmpty()) {
            painter.setFont(font());
            painter.setPen(ngInk(pal, 0.62));
            painter.drawText(QRectF(9, nameRect.bottom() + 2, width() - 18, 2 * metrics.height()), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_description);
        }
    }

private:
    QString m_key;
    QString m_id;
    QString m_description;
};

}

// A row of tiles, one of which is the current choice
class NgTileRow : public QWidget
{
public:
    NgTileRow(const QString &key, int columns, QWidget *parent)
        : QWidget(parent)
        , m_key(key)
        , m_columns(columns)
    {
        Q_UNUSED(m_columns)
        m_flow = new NgWrapLayout(this, 8);
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    void add(const QString &id, const QString &name, const QString &description = QString())
    {
        auto *tile = new NgTile(m_key, id, name, description, this);
        m_flow->addWidget(tile);
        m_tiles.append(tile);
        connect(tile, &QAbstractButton::clicked, this, [this, id] {
            setCurrent(id);
            if (picked) {
                picked(id);
            }
        });
    }
    void finish()
    {
    }
    void setCurrent(const QString &id)
    {
        for (NgTile *tile : std::as_const(m_tiles)) {
            tile->setChecked(tile->id() == id);
        }
    }
    std::function<void(const QString &)> picked;

private:
    QString m_key;
    int m_columns;
    NgWrapLayout *m_flow;
    QList<NgTile *> m_tiles;
};

namespace
{
// A check box drawn as a switch
class NgSwitch : public QCheckBox
{
public:
    using QCheckBox::QCheckBox;
    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        return QSize(34 + 10 + metrics.horizontalAdvance(text()) + 4, qMax(26, metrics.height() + 8));
    }
    QSize minimumSizeHint() const override
    {
        return QSize(80, sizeHint().height());
    }

protected:
    bool hitButton(const QPoint &pos) const override
    {
        return rect().contains(pos);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(isEnabled() ? 1.0 : 0.45);
        const QPalette &pal = palette();
        const QRectF track(0.5, (height() - 20) / 2.0, 34, 20);
        painter.setPen(hasFocus() ? QPen(ngWithAlpha(pal.color(QPalette::Highlight), 0.8), 1) : QPen(Qt::NoPen));
        painter.setBrush(isChecked() ? pal.color(QPalette::Highlight) : ngInk(pal, underMouse() ? 0.22 : 0.16));
        painter.drawRoundedRect(track, 10, 10);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? QColor(Qt::white) : ngInk(pal, 0.85));
        painter.drawEllipse(QRectF(track.left() + (isChecked() ? 17 : 3), track.top() + 3, 14, 14));
        painter.setPen(pal.color(QPalette::WindowText));
        const QRect textRect(44, 0, width() - 44, height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(ngPlainText(text()), Qt::ElideRight, textRect.width()));
    }
};
}

// The miniature of the window: it shows what the choices of the page add up to
class NgSettingsPreview : public QWidget
{
public:
    explicit NgSettingsPreview(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(250, 184);
    }
    QString side = QStringLiteral("plain"), rim, rows = QStringLiteral("card"), mark, card = QStringLiteral("a");
    bool wash = true;
    bool filters = true, twoLines = true, classicIcons = false, stretch = false, typeBackground = false, cardsView = false, quickView = false;
    QString quickSide;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette &pal = palette();
        const QRectF window = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(window, 9, 9);
        painter.save();
        painter.setClipPath(clip);
        painter.fillRect(window, pal.color(QPalette::Window).darker(pal.color(QPalette::Window).lightness() < 128 ? 112 : 104));
        if (side == QLatin1String("live")) {
            ngWallpaperWedge(painter, window, 0.26);
        }
        const qreal barHeight = 18;
        painter.fillRect(QRectF(window.left(), window.top(), window.width(), barHeight), ngInk(pal, 0.07));
        for (int i = 0; i < 3; ++i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ngInk(pal, 0.3));
            painter.drawEllipse(QPointF(window.left() + 12 + i * 11, window.top() + barHeight / 2), 3, 3);
        }
        // the sidebar card
        const QRectF panel(window.left() + 6, window.top() + barHeight + 6, window.width() * 0.25, window.height() - barHeight - 12);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ngBackdropBrush(side, panel, pal));
        painter.drawRoundedRect(panel, 7, 7);
        if (side != QLatin1String("plain")) {
            ngRim(painter, panel, 7, rim, pal);
        }
        for (int i = 0; i < 5; ++i) {
            ngBar(painter, QRectF(panel.left() + 8, panel.top() + 10 + i * 12, panel.width() * (i % 2 ? 0.45 : 0.62), 5), ngInk(pal, i == 1 ? 0.8 : 0.4));
        }
        if (filters) {
            ngBar(painter, QRectF(panel.left() + 8, panel.bottom() - 34, panel.width() * 0.3, 5), ngInk(pal, 0.22));
            ngBar(painter, QRectF(panel.left() + 8, panel.bottom() - 22, panel.width() * 0.6, 5), ngInk(pal, 0.4));
            ngBar(painter, QRectF(panel.left() + 8, panel.bottom() - 11, panel.width() * 0.48, 5), ngInk(pal, 0.4));
        }
        // the view
        const QRectF main(panel.right() + 8, panel.top(), window.right() - panel.right() - 14, panel.height());
        ngBar(painter, QRectF(main.left(), main.top(), main.width() * 0.5, 9), ngInk(pal, 0.10));
        ngBar(painter, QRectF(main.left() + main.width() * 0.54, main.top(), main.width() * 0.18, 9), ngInk(pal, 0.10));
        const QRectF items(main.left(), main.top() + 15, main.width(), main.height() - 15);
        static const QColor kinds[6] = {kindFolder, kindPdf, kindImage, kindSheet, kindDoc, kindCode};
        if (cardsView) {
            const int columns = 3, rowsCount = 2;
            const qreal cellWidth = (items.width() - (columns - 1) * 5) / columns, cellHeight = (items.height() - (rowsCount - 1) * 5) / rowsCount;
            const qreal cardWidth = stretch ? cellWidth : cellWidth * 0.82;
            for (int i = 0; i < 6; ++i) {
                const QRectF cell(items.left() + (i % columns) * (cellWidth + 5), items.top() + (i / columns) * (cellHeight + 5), cellWidth, cellHeight);
                ngMiniCard(painter, QRectF(cell.center().x() - cardWidth / 2, cell.top(), cardWidth, cell.height()), card, kinds[i], i == 2, typeBackground && i != 2, pal);
            }
        } else {
            const qreal rowHeight = items.height() / 6;
            for (int i = 0; i < 6; ++i) {
                ngMiniRow(painter, QRectF(items.left(), items.top() + i * rowHeight, items.width(), rowHeight - (rows == QLatin1String("card") ? 3 : 0)),
                          twoLines ? rows : QStringLiteral("stripe"), classicIcons ? QStringLiteral("tile") : mark, wash ? kinds[i] : QColor(), i % 2, twoLines,
                          twoLines && i == 2, true, pal, kinds[i]);
            }
        }
        if (quickView) {
            const QPointF cursor(items.center().x(), items.center().y());
            const QSizeF size(items.width() * 0.34, items.height() * 0.46);
            const QRectF popup = quickSide == QLatin1String("right") ? QRectF(cursor.x() + 10, cursor.y() - size.height() / 2, size.width(), size.height())
                : quickSide == QLatin1String("auto")                 ? QRectF(cursor.x() - size.width() / 2, cursor.y() - size.height() - 12, size.width(), size.height())
                                                                      : QRectF(cursor.x() - 10 - size.width(), cursor.y() - size.height() / 2, size.width(), size.height());
            painter.setPen(QPen(QColor(255, 255, 255, 90), 1));
            painter.setBrush(ngPictureBrush(popup));
            painter.drawRoundedRect(popup, 5, 5);
            QPainterPath arrow;
            arrow.moveTo(cursor);
            arrow.lineTo(cursor + QPointF(0, 12));
            arrow.lineTo(cursor + QPointF(3.5, 9));
            arrow.lineTo(cursor + QPointF(9, 9));
            arrow.closeSubpath();
            painter.setPen(QPen(QColor(0, 0, 0, 170), 1));
            painter.setBrush(pal.color(QPalette::WindowText));
            painter.drawPath(arrow);
        }
        painter.restore();
        painter.setPen(QPen(ngInk(pal, 0.14), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(window, 9, 9);
    }
};

NgSettingsPage::NgSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(14, 4, 0, 0);
    outer->setSpacing(20);

    // ---- left: the miniature ----------------------------------------------------------------------------
    auto *left = new QVBoxLayout();
    left->setSpacing(8);
    auto *previewCaption = new QLabel(ngTr("Preview", "Anteprima"), this);
    previewCaption->setEnabled(false);
    left->addWidget(previewCaption);
    m_preview = new NgSettingsPreview(this);
    left->addWidget(m_preview);
    auto *previewNote = new QLabel(ngTr("A sketch of the window with the choices on the right. Nothing changes until you apply.",
                                        "Uno schizzo della finestra con le scelte fatte a destra. Niente cambia finché non applichi."),
                                   this);
    previewNote->setWordWrap(true);
    previewNote->setEnabled(false);
    previewNote->setFixedWidth(250);
    left->addWidget(previewNote);
    // Breeze NG's own settings (widget style, decoration, Aurora): the user's build or the system-wide one
    const QString homeBuild = QDir::homePath() + QStringLiteral("/.local/breeze-ng");
    QString breezeSettings = QStandardPaths::findExecutable(QStringLiteral("breezeng-settings"));
    if (breezeSettings.isEmpty()) {
        breezeSettings = QStandardPaths::findExecutable(QStringLiteral("breezeng-settings"),
                                                        {homeBuild + QStringLiteral("/bin"), QStringLiteral("/opt/ng/breeze-ng/bin")});
    }
    if (!breezeSettings.isEmpty()) {
        auto *breezeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("preferences-desktop-theme")),
                                             ngTr("Breeze NG settings…", "Impostazioni di Breeze NG…"), this);
        breezeButton->setToolTip(ngTr("Widget style, window decoration and the Aurora lights", "Stile dei widget, decorazione delle finestre e luci di Aurora"));
        connect(breezeButton, &QPushButton::clicked, this, [breezeSettings, homeBuild] {
            QProcess process;
            process.setProgram(breezeSettings);
            if (breezeSettings.startsWith(homeBuild)) {
                // the user's build finds its plugins through QT_PLUGIN_PATH, set only from the next login after installing
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                QStringList paths = env.value(QStringLiteral("QT_PLUGIN_PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
                for (const QString &lib : {QStringLiteral("/lib64/plugins"), QStringLiteral("/lib/plugins")}) {
                    if (QFileInfo::exists(homeBuild + lib) && !paths.contains(homeBuild + lib)) {
                        paths.prepend(homeBuild + lib);
                    }
                }
                env.insert(QStringLiteral("QT_PLUGIN_PATH"), paths.join(QLatin1Char(':')));
                process.setProcessEnvironment(env);
            }
            process.startDetached();
        });
        left->addWidget(breezeButton, 0, Qt::AlignLeft);
    }
    left->addStretch();
    outer->addLayout(left);

    // ---- right: the tabs, as one pill, and a page of tiles for each ------------------------------------
    auto *right = new QVBoxLayout();
    right->setSpacing(12);
    outer->addLayout(right, 1);
    auto *tabs = new QWidget(this);
    tabs->setObjectName(QStringLiteral("ngTabs"));
    tabs->setAttribute(Qt::WA_StyledBackground);
    auto *tabsLayout = new QHBoxLayout(tabs);
    tabsLayout->setContentsMargins(3, 3, 3, 3);
    tabsLayout->setSpacing(2);
    right->addWidget(tabs, 0, Qt::AlignLeft);
    m_pages = new QStackedWidget(this);
    right->addWidget(m_pages, 1);
    {
        const QString ink = palette().color(QPalette::Window).lightness() < 128 ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
        const int tabHeight = fontMetrics().height() + 12;
        tabs->setStyleSheet(QStringLiteral("QWidget#ngTabs { background: rgba(%1, 0.05); border: 1px solid rgba(%1, 0.10); border-radius: %2px; }"
                                           "QToolButton { border: 1px solid transparent; border-radius: %3px; padding: 0 14px; background: transparent; color: palette(window-text); }"
                                           "QToolButton:hover { background: rgba(%1, 0.08); }"
                                           "QToolButton:checked { background: rgba(%1, 0.16); border-color: rgba(%1, 0.20); }")
                                .arg(ink)
                                .arg((tabHeight + 6) / 2 - 1)
                                .arg(tabHeight / 2 - 1));
    }
    auto *tabGroup = new QButtonGroup(this);
    QVBoxLayout *page = nullptr; // the page being filled
    auto addPage = [&](const QString &title) {
        auto *button = new QToolButton(tabs);
        button->setText(title);
        button->setCheckable(true);
        button->setFixedHeight(fontMetrics().height() + 12);
        button->setMinimumWidth(fontMetrics().horizontalAdvance(title) + 34);
        tabsLayout->addWidget(button);
        tabGroup->addButton(button, m_pages->count());
        auto *scroll = new QScrollArea(m_pages);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->viewport()->setAutoFillBackground(false);
        auto *content = new QWidget(scroll);
        content->setAutoFillBackground(false);
        scroll->setWidget(content);
        m_pages->addWidget(scroll);
        page = new QVBoxLayout(content);
        page->setContentsMargins(0, 0, 10, 0);
        page->setSpacing(8);
    };
    auto heading = [&](const QString &text) {
        auto *label = new QLabel(text, this);
        QFont bold = label->font();
        bold.setWeight(QFont::DemiBold);
        label->setFont(bold);
        label->setContentsMargins(0, page->count() ? 8 : 0, 0, 0);
        page->addWidget(label);
        return label;
    };
    auto note = [&](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setWordWrap(true);
        label->setEnabled(false);
        page->addWidget(label);
        return label;
    };
    // a row of tiles that drives a (hidden) combo box: one tile per entry, `names` gives each its short name
    auto tilesFor = [&](QComboBox *combo, const QString &key, const QString &title, const QList<std::pair<QString, QString>> &names, int columns) {
        combo->hide();
        QLabel *label = heading(title);
        auto *row = new NgTileRow(key, columns, this);
        for (int i = 0; i < combo->count(); ++i) {
            const QString id = combo->itemData(i).toString();
            const auto &name = names.value(i);
            row->add(id, name.first, name.second);
        }
        row->finish();
        page->addWidget(row);
        row->picked = [combo](const QString &id) {
            combo->setCurrentIndex(qMax(0, combo->findData(id)));
        };
        auto sync = [row, combo] {
            row->setCurrent(combo->currentData().toString());
        };
        connect(combo, &QComboBox::currentIndexChanged, row, sync);
        m_syncs.append(sync);
        m_parts.insert(combo, {label, row});
        m_proxies.append({combo, row});
        return row;
    };
    QFormLayout *form = nullptr; // the form of plain rows (sliders, spin boxes) being filled
    auto startForm = [&] {
        form = new QFormLayout();
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setContentsMargins(0, 0, 0, 0);
        page->addLayout(form);
    };
    auto formRow = [&](const QString &label, QWidget *widget) {
        form->addRow(label, widget);
        m_formOf.insert(widget, form);
    };
    auto toggle = [&](const QString &text) {
        auto *box = new NgSwitch(text, this);
        page->addWidget(box);
        return box;
    };

    // ================= Look =================
    addPage(ngTr("Look", "Aspetto"));
    m_preset = new QComboBox(this);
    m_preset->addItem(QString(), QStringLiteral("classic"));
    m_preset->addItem(QString(), QStringLiteral("soft"));
    m_preset->addItem(QString(), QStringLiteral("glass"));
    NgTileRow *presetRow = tilesFor(m_preset, QStringLiteral("preset"), ngTr("Start from a look", "Parti da un aspetto"),
                                    {{ngTr("Classic", "Classico"), ngTr("Like Dolphin: plain panel, columns, icons", "Come Dolphin: pannello normale, colonne, icone")},
                                     {QStringLiteral("Soft"), ngTr("NG rows and cards, panel without glass", "Righe e card NG, pannello senza vetro")},
                                     {QStringLiteral("Glass"), ngTr("Everything: glass panel, NG rows and cards", "Tutto: pannello a vetro, righe e card NG")}},
                                    3);
    presetRow->picked = [this](const QString &id) {
        applyPreset(id);
        Q_EMIT changed();
    };
    note(ngTr("A look fills in the other tabs; you can adjust anything before applying.", "Un aspetto compila le altre schede; puoi ritoccare tutto prima di applicare."));
    heading(ngTr("Icons view", "Vista a icone"));
    m_tiles = toggle(ngTr("Dark tiles with rounded previews", "Tessere scure con anteprime arrotondate"));
    page->addStretch();
    note(ngTr("Changes take effect as soon as they are applied. A DOLPHIN_NG_… variable set at start-up wins: the matching choice is disabled.",
              "Le modifiche valgono appena applicate. Una variabile DOLPHIN_NG_… impostata all'avvio ha la precedenza: la scelta corrispondente è disattivata."));

    // ================= Sidebar =================
    addPage(ngTr("Sidebar", "Barra laterale"));
    m_glassPanel = new QCheckBox(this);
    m_glassPanel->hide();
    m_backdrop = new QComboBox(this);
    m_backdrop->addItem(QString(), QString());
    m_backdrop->addItem(QString(), QStringLiteral("lava"));
    m_backdrop->addItem(QString(), QStringLiteral("live"));
    m_backdrop->hide();
    {
        // four tiles for two values: the plain panel, or a glass one over one of the three backdrops
        heading(ngTr("Backdrop of the panel", "Sfondo del pannello"));
        m_sideRow = new NgTileRow(QStringLiteral("side"), 4, this);
        m_sideRow->add(QStringLiteral("plain"), ngTr("Plain", "Normale"));
        m_sideRow->add(QStringLiteral("image"), ngTr("Blurred image", "Immagine sfocata"));
        m_sideRow->add(QStringLiteral("lava"), ngTr("Animated gradient", "Gradiente animato"));
        m_sideRow->add(QStringLiteral("live"), ngTr("Real desktop blur", "Blur vero del desktop"));
        m_sideRow->finish();
        page->addWidget(m_sideRow);
        m_sideRow->picked = [this](const QString &id) {
            m_glassPanel->setChecked(id != QLatin1String("plain"));
            if (id != QLatin1String("plain")) {
                m_backdrop->setCurrentIndex(qMax(0, m_backdrop->findData(id == QLatin1String("image") ? QString() : id)));
            }
        };
        auto sync = [this] {
            const QString mode = m_backdrop->currentData().toString();
            m_sideRow->setCurrent(!m_glassPanel->isChecked() ? QStringLiteral("plain") : mode.isEmpty() ? QStringLiteral("image") : mode);
        };
        connect(m_glassPanel, &QCheckBox::toggled, m_sideRow, sync);
        connect(m_backdrop, &QComboBox::currentIndexChanged, m_sideRow, sync);
        m_syncs.append(sync);
    }
    startForm();
    m_image = new KUrlRequester(this);
    m_image->setMode(KFile::File | KFile::ExistingOnly | KFile::LocalOnly);
    m_image->setPlaceholderText(ngTr("Desktop wallpaper", "Sfondo del desktop"));
    m_image->setToolTip(ngTr("Image shown, blurred, behind the sidebar. Empty: the wallpaper of each screen.", "Immagine mostrata, sfocata, dietro la barra laterale. Vuoto: lo sfondo del desktop di ogni schermo."));
    formRow(ngTr("Image:", "Immagine:"), m_image);
    m_glass = percentSlider(this, 0, 100);
    formRow(ngTr("How much shows:", "Quanto si vede:"), m_glass);
    m_blur = percentSlider(this, 0, 100);
    formRow(ngTr("Blur:", "Sfocatura:"), m_blur);
    m_liveBlur = percentSlider(this, 1, 15);
    m_liveBlur->setPageStep(1);
    m_liveBlur->setToolTip(ngTr("The blur strength of the compositor (KWin's Blur or Better Blur effect). It is a desktop setting: every blurred window changes with it.",
                                "L'intensità del blur del compositor (effetto Sfocatura di KWin o Better Blur). È un'impostazione del desktop: cambia per tutte le finestre sfocate."));
    formRow(ngTr("Blur (whole desktop):", "Sfocatura (tutto il desktop):"), m_liveBlur);
    m_lavaStyle = new QComboBox(this);
    m_lavaStyle->addItem(ngTr("Flowing gradient", "Gradiente che scorre"), QString());
    m_lavaStyle->addItem(ngTr("Blobs (lava lamp)", "Macchie (lava lamp)"), QStringLiteral("blobs"));
    formRow(ngTr("Animation:", "Animazione:"), m_lavaStyle);
    m_lavaSize = percentSlider(this, 10, 100);
    formRow(ngTr("Size of the colour areas:", "Ampiezza delle zone di colore:"), m_lavaSize);
    m_lavaColours = new QComboBox(this);
    m_lavaColours->addItem(ngTr("Teal, amber and indigo", "Petrolio, ambra e indaco"), QString());
    m_lavaColours->addItem(ngTr("From the desktop wallpaper", "Dallo sfondo del desktop"), QStringLiteral("wallpaper"));
    m_lavaColours->addItem(ngTr("Custom", "Personalizzati"), QStringLiteral("custom"));
    formRow(ngTr("Colours:", "Colori:"), m_lavaColours);
    m_lavaColourRow = new QWidget(this);
    auto *colourRow = new QHBoxLayout(m_lavaColourRow);
    colourRow->setContentsMargins(0, 0, 0, 0);
    for (KColorButton *&button : m_lavaColour) {
        button = new KColorButton(m_lavaColourRow);
        colourRow->addWidget(button);
        connect(button, &KColorButton::changed, this, &NgSettingsPage::changed);
    }
    colourRow->addStretch();
    formRow(QString(), m_lavaColourRow);
    m_lavaSpeed = percentSlider(this, 5, 100);
    formRow(ngTr("Speed of the motion:", "Velocità del movimento:"), m_lavaSpeed);
    m_motion = toggle(ngTr("The image moves slowly (zoom and pan)", "L'immagine si muove lentamente (zoom e scorrimento)"));

    m_rim = new QComboBox(this);
    for (const char *id : {"", "diagonal", "soft", "bevel", "glow", "top"}) {
        m_rim->addItem(QString(), QString::fromLatin1(id));
    }
    tilesFor(m_rim, QStringLiteral("rim"), ngTr("Edge of the panel", "Bordo del pannello"),
             {{ngTr("None", "Nessuno"), {}}, {ngTr("Diagonal", "Diagonale"), {}}, {ngTr("Thin line", "Linea sottile"), {}}, {ngTr("Bevel", "Smusso"), {}},
              {ngTr("Glow", "Alone"), {}}, {ngTr("Light from above", "Luce dall'alto"), {}}},
             6);
    heading(ngTr("Sections", "Sezioni"));
    note(ngTr("Drag them into their order: what is above the places stays at the top of the panel, what is below at its foot. The check shows or hides a section.",
              "Trascinale nel loro ordine: quello che sta sopra le risorse resta in alto nel pannello, quello sotto in fondo. La spunta mostra o nasconde una sezione."));
    m_sectionOrder = new QListWidget(this);
    m_sectionOrder->setDragDropMode(QAbstractItemView::InternalMove);
    m_sectionOrder->setDefaultDropAction(Qt::MoveAction);
    m_sectionOrder->setSelectionMode(QAbstractItemView::SingleSelection);
    page->addWidget(m_sectionOrder);
    connect(m_sectionOrder->model(), &QAbstractItemModel::rowsMoved, this, &NgSettingsPage::changed);
    connect(m_sectionOrder, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        // the checks drive the switches the rest of the page knows
        const QString name = item->data(Qt::UserRole).toString();
        const bool on = item->checkState() == Qt::Checked;
        if (name == QLatin1String("pinned")) {
            m_sidebarPinned->setChecked(on);
        } else if (name == QLatin1String("filters")) {
            m_sidebarFilters->setChecked(on);
        }
        Q_EMIT changed();
    });
    m_sidebarPinned = toggle(ngTr("Pinned folders and files", "Cartelle e file fissati in alto"));
    m_sidebarFilters = toggle(ngTr("Filters, at the bottom of the panel", "Filtri, in fondo al pannello"));
    m_sidebarPinned->hide(); // driven by the list above
    m_sidebarFilters->hide();
    m_devices = new QComboBox(this);
    m_devices->addItem(ngTr("NG: boxes with their usage, at the foot", "NG: riquadri con l'uso, in fondo"), QString());
    m_devices->addItem(ngTr("Classic: a section of the sidebar", "Classico: una sezione della barra"), QStringLiteral("list"));
    m_devices->addItem(ngTr("Hidden (removable ones stay)", "Nascosti (restano i rimovibili)"), QStringLiteral("hide"));
    {
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(new QLabel(ngTr("Disks and partitions:", "Dischi e partizioni:"), row));
        rowLayout->addWidget(m_devices);
        rowLayout->addStretch();
        page->addWidget(row);
        m_parts.insert(m_devices, {row});
    }
    m_disksPos = new QComboBox(this);
    m_disksPos->addItem(ngTr("At the foot of the panel", "In fondo al pannello"), QString());
    m_disksPos->addItem(ngTr("Above the filters", "Sopra i filtri"), QStringLiteral("above"));
    {
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(new QLabel(ngTr("Where the disks go:", "Posizione dei dischi:"), row));
        rowLayout->addWidget(m_disksPos);
        rowLayout->addStretch();
        page->addWidget(row);
        m_parts.insert(m_disksPos, {row});
    }
    m_deviceList = new QWidget(this);
    {
        auto *deviceLayout = new QVBoxLayout(m_deviceList);
        deviceLayout->setContentsMargins(28, 0, 0, 4);
        deviceLayout->setSpacing(2);
    }
    page->addWidget(m_deviceList);
    page->addStretch();

    // ================= Rows =================
    addPage(ngTr("Rows", "Righe"));
    m_twoLines = toggle(ngTr("Two-line rows, without columns", "Righe su due linee, senza colonne"));
    m_rows = new QComboBox(this);
    for (const char *id : {"card", "open", "line", "stripe", "none"}) {
        m_rows->addItem(QString(), QString::fromLatin1(id));
    }
    tilesFor(m_rows, QStringLiteral("rows"), ngTr("Row background", "Sfondo delle righe"),
             {{ngTr("Cards", "Schede"), {}}, {ngTr("Open", "Aperte"), {}}, {ngTr("Line", "Filo"), {}}, {ngTr("Stripes", "Strisce"), {}}, {ngTr("None", "Nessuno"), {}}}, 5);
    m_mark = new QComboBox(this);
    for (const char *id : {"object", "watermark", "tile", "pill", "signs", "header"}) {
        m_mark->addItem(QString(), QString::fromLatin1(id));
    }
    tilesFor(m_mark, QStringLiteral("mark"), ngTr("File type mark", "Segno del tipo di file"),
             {{ngTr("Drawing, as on the cards", "Disegno, come le tessere"), {}}, {ngTr("Watermark", "Filigrana"), {}}, {ngTr("Tile", "Tessera"), {}}, {ngTr("Pill", "Pillola"), {}},
              {ngTr("Marks only", "Solo segni"), {}}, {ngTr("Header", "Testata"), {}}},
             6);
    m_sigla = new QComboBox(this);
    for (const char *id : {"fill", "outline", "label"}) {
        m_sigla->addItem(QString(), QString::fromLatin1(id));
    }
    tilesFor(m_sigla, QStringLiteral("sigla"), ngTr("Watermark letters", "Sigla della filigrana"),
             {{ngTr("Filled and faint", "Piena e tenue"), {}}, {ngTr("Outline only", "Solo contorno"), {}}, {ngTr("Small label", "Etichetta piccola"), {}}}, 5);
    heading(ngTr("What each row shows", "Cosa mostra ogni riga"));
    m_rowWash = toggle(ngTr("The colour of the type fades into the row", "Il colore del tipo sfuma nella riga"));
    m_icons = toggle(ngTr("Classic icon instead of the type mark", "Icona classica invece del segno del tipo"));
    m_ago = toggle(ngTr("Days elapsed, under the date", "Giorni trascorsi, sotto la data"));
    m_rowDates = toggle(ngTr("Created and modified, with the days elapsed", "Creato e modificato, con i giorni trascorsi"));
    m_folderContentDate = toggle(ngTr("Folders sorted by date: by the newest thing inside them", "Cartelle ordinate per data: secondo l'ultima modifica del contenuto"));
    m_folderContentDate->setToolTip(ngTr("Sorted by date modified, a folder counts as modified when something inside it was (its own date changes only when files are added or removed).\n\n"
                                         "It reuses the walk that measures folder sizes, so it goes only as deep as that (Dolphin's depth limit), on local folders only. "
                                         "Changes made by Dolphin or other KDE applications are taken into account at once; changes made by other programs when the folder is shown again, a few seconds later.",
                                         "Ordinando per data di modifica, una cartella vale come modificata quando lo è qualcosa al suo interno (la sua data cambia solo quando si aggiungono o tolgono file).\n\n"
                                         "Usa lo stesso giro che misura le dimensioni delle cartelle, quindi scende solo fin lì (il limite di profondità di Dolphin), e solo nelle cartelle locali. "
                                         "Le modifiche fatte da Dolphin o da altre applicazioni KDE contano subito; quelle fatte da altri programmi quando la cartella viene mostrata di nuovo, qualche secondo dopo."));
    m_folderSizes = toggle(ngTr("Folders: space used by the contents", "Cartelle: spazio occupato dal contenuto"));
    m_folderSizes->setToolTip(ngTr("Measures the contents of folders (Dolphin's setting “Folder size: size of contents”, with its depth limit). Otherwise only the number of items.", "Misura il contenuto delle cartelle (impostazione di Dolphin «Dimensione delle cartelle: dimensione del contenuto», con il suo limite di profondità). Altrimenti solo il numero di elementi."));
    {
        auto *warning = new KMessageWidget(ngTr("This can slow the computer down for a while: every folder you open is measured down to its "
                                                "subfolders, which keeps the disk busy (a few seconds for large folders, longer on network "
                                                "or slow disks). The window stays usable meanwhile; the sizes appear one by one.",
                                                "Può rallentare il computer per un po': ogni cartella che apri viene misurata fino alle "
                                                "sottocartelle e il disco resta occupato (qualche secondo per le cartelle grandi, di più su "
                                                "dischi di rete o lenti). Intanto la finestra resta utilizzabile; le dimensioni compaiono "
                                                "una alla volta."),
                                           this);
        warning->setMessageType(KMessageWidget::Warning);
        warning->setCloseButtonVisible(false);
        warning->setWordWrap(true);
        page->addWidget(warning);
    }
    page->addStretch();

    // ================= Cards =================
    addPage(ngTr("Cards", "Card"));
    m_cards = new QComboBox(this);
    for (const char *id : {"a", "b", "c"}) {
        m_cards->addItem(QString(), QString::fromLatin1(id));
    }
    tilesFor(m_cards, QStringLiteral("cards"), ngTr("Kind of card", "Tipo di card"),
             {{QStringLiteral("Tessera"), ngTr("Preview on top, details below", "Anteprima in alto, dettagli sotto")},
              {QStringLiteral("Scheda"), ngTr("Icon on the left, everything in three lines", "Icona a sinistra, tutto in tre righe")},
              {QStringLiteral("Dossier"), ngTr("The days elapsed, large", "I giorni trascorsi in grande")}},
             3);
    m_cardsFit = new QComboBox(this);
    m_cardsFit->addItem(QString(), QString());
    m_cardsFit->addItem(QString(), QStringLiteral("stretch"));
    tilesFor(m_cardsFit, QStringLiteral("fit"), ngTr("Grid", "Griglia"), {{ngTr("Fixed size", "Misura fissa"), {}}, {ngTr("Fill the row", "Riempi la riga"), {}}}, 5);
    m_cardsMark = new QComboBox(this);
    m_cardsMark->addItem(QString(), QString());
    m_cardsMark->addItem(QString(), QStringLiteral("type"));
    tilesFor(m_cardsMark, QStringLiteral("cardsmark"), ngTr("Type mark", "Segno del tipo"),
             {{ngTr("Type icon", "Icona del tipo"), {}}, {ngTr("Type as background", "Tipo come sfondo"), {}}}, 5);
    m_cardsBadge = new QComboBox(this);
    m_cardsBadge->addItem(QString(), QString());
    m_cardsBadge->addItem(QString(), QStringLiteral("glass"));
    m_cardsBadge->addItem(QString(), QStringLiteral("row"));
    m_cardsBadge->addItem(QString(), QStringLiteral("fold"));
    m_cardsBadge->addItem(QString(), QStringLiteral("center"));
    m_cardsBadge->addItem(QString(), QStringLiteral("inside"));
    tilesFor(m_cardsBadge, QStringLiteral("badge"), ngTr("Type and size (Tessera)", "Tipo e dimensione (Tessera)"),
             {{ngTr("Solid badge", "Etichetta piena"), {}}, {ngTr("Glass badge", "Etichetta di vetro"), {}}, {ngTr("A row of their own", "Riga separata"), {}},
              {ngTr("Folded corner", "Angolo piegato"), ngTr("In glass; larger for heavier files", "In vetro; più grande per i file pesanti")},
              {ngTr("Pill in the middle", "Pillola centrale"), ngTr("Glass, flat, at the foot of the picture", "Vetro piatto, in basso al centro")},
              {ngTr("In the drawing", "Nel disegno"), ngTr("Written on the box, the page, the folder", "Scritti sulla scatola, sulla pagina, sulla cartella")}}, 5);
    m_activitySize = new QComboBox(this);
    m_activitySize->addItem(QString(), QStringLiteral("small"));
    m_activitySize->addItem(QString(), QString());
    m_activitySize->addItem(QString(), QStringLiteral("large"));
    tilesFor(m_activitySize, QStringLiteral("activity"), ngTr("Work in progress on a card", "Lavoro in corso sulla tessera"),
             {{ngTr("Small", "Piccola"), {}}, {ngTr("Medium", "Media"), {}}, {ngTr("Large", "Grande"), {}}}, 5);
    startForm();
    m_cardsInfo = new QComboBox(this);
    m_cardsInfo->addItem(ngTr("Created and modified", "Creato e modificato"), QString());
    m_cardsInfo->addItem(ngTr("Type and permissions too", "Anche tipo e permessi"), QStringLiteral("full"));
    formRow(ngTr("Details in the card:", "Dettagli nella card:"), m_cardsInfo);
    page->addStretch();
    note(ngTr("Switch to the view from the View menu (Cards, Ctrl+4) or with the “Cards” button in the filter bar; zooming changes the size of the cards.", "La vista si attiva dal menu Visualizza (Card, Ctrl+4) o dal pulsante «Card» nella barra dei filtri; lo zoom cambia la misura delle card."));

    // ================= Previews =================
    addPage(ngTr("Previews", "Anteprime"));
    heading(ngTr("Quick view", "Anteprima al volo"));
    note(ngTr("Shown when the pointer rests on the small picture of an image, a GIF, a video or a PDF, on cards and rows.",
              "Compare fermando il mouse sulla miniatura di immagini, GIF, video e PDF, nelle card e nelle righe."));
    m_quickView = toggle(ngTr("On in Dolphin NG", "Attiva in Dolphin NG"));
    startForm();
    m_quickDelay = new QSpinBox(this);
    m_quickDelay->setRange(0, 3000);
    m_quickDelay->setSingleStep(50);
    m_quickDelay->setSuffix(QStringLiteral(" ms"));
    m_quickDelay->setSpecialValueText(ngTr("At once", "Subito"));
    formRow(ngTr("After:", "Dopo:"), m_quickDelay);
    m_quickSide = new QComboBox(this);
    m_quickSide->addItem(QString(), QString());
    m_quickSide->addItem(QString(), QStringLiteral("right"));
    m_quickSide->addItem(QString(), QStringLiteral("auto"));
    NgTileRow *quickSideRow = tilesFor(m_quickSide, QStringLiteral("quickside"), ngTr("Where it shows", "Dove compare"),
                                       {{ngTr("Left of the cursor", "A sinistra del cursore"), {}}, {ngTr("Right of the cursor", "A destra del cursore"), {}},
                                        {ngTr("Next to the card", "Accanto alla card"), {}}},
                                       5);
    connect(m_quickSide, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_quickView, &QCheckBox::toggled, quickSideRow, &QWidget::setEnabled);
    connect(m_quickView, &QCheckBox::toggled, m_quickSide, &QWidget::setEnabled);
    connect(m_quickView, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    connect(m_quickView, &QCheckBox::toggled, m_quickDelay, &QWidget::setEnabled);
    connect(m_quickDelay, &QSpinBox::valueChanged, this, &NgSettingsPage::changed);
    heading(ngTr("Preview card (the eye)", "Scheda di anteprima (l'occhio)"));
    m_hoverPreview = toggle(ngTr("Opens when the mouse rests on the eye", "Si apre fermando il mouse sull'occhio"));
    startForm();
    m_hoverDelay = new QSpinBox(this);
    m_hoverDelay->setRange(100, 5000);
    m_hoverDelay->setSingleStep(100);
    m_hoverDelay->setSuffix(QStringLiteral(" ms"));
    formRow(ngTr("After:", "Dopo:"), m_hoverDelay);
    m_float = toggle(ngTr("Floats above the window (it can leave the pane)", "Galleggia sopra la finestra (può uscire dal riquadro)"));
    // the open/save window (Dialog NG) has a setting of its own: one may want the quick preview here and not there
    heading(ngTr("Open/Save window (Dialog NG)", "Finestra Apri/Salva (Dialog NG)"));
    note(ngTr("The file window of every application, when Dialog NG is installed.", "La finestra dei file di tutte le applicazioni, se Dialog NG è installato."));
    m_dialogQuick = toggle(ngTr("On in the Open/Save window", "Attiva nella finestra Apri/Salva"));
    startForm();
    m_dialogDelay = new QSpinBox(this);
    m_dialogDelay->setRange(0, 3000);
    m_dialogDelay->setSingleStep(50);
    m_dialogDelay->setSuffix(QStringLiteral(" ms"));
    m_dialogDelay->setSpecialValueText(ngTr("At once", "Subito"));
    formRow(ngTr("After:", "Dopo:"), m_dialogDelay);
    m_dialogSize = new QComboBox(this);
    m_dialogSize->addItem(ngTr("Small", "Piccola"), QStringLiteral("small"));
    m_dialogSize->addItem(ngTr("Medium", "Media"), QStringLiteral("medium"));
    m_dialogSize->addItem(ngTr("Large", "Grande"), QStringLiteral("large"));
    formRow(ngTr("Size:", "Dimensione:"), m_dialogSize);
    connect(m_dialogQuick, &QCheckBox::toggled, m_dialogDelay, &QWidget::setEnabled);
    connect(m_dialogQuick, &QCheckBox::toggled, m_dialogSize, &QWidget::setEnabled);
    connect(m_dialogDelay, &QSpinBox::valueChanged, this, &NgSettingsPage::changed);
    connect(m_dialogSize, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    page->addStretch();

    connect(tabGroup, &QButtonGroup::idClicked, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        updatePreview();
    });
    {
        // DOLPHIN_NG_SETTINGS_TAB=0..4 opens the page on that tab (screenshots, links from elsewhere)
        const int first = qBound(0, qEnvironmentVariableIntValue("DOLPHIN_NG_SETTINGS_TAB"), m_pages->count() - 1);
        tabGroup->button(first)->setChecked(true);
        m_pages->setCurrentIndex(first);
    }

    loadSettings();
    for (const auto &sync : std::as_const(m_syncs)) {
        sync(); // a value that loads as the first entry changes nothing, so no signal marked its tile
    }
    presetRow->setCurrent(QString()); // a look is a starting point, not a stored value: none is marked

    // what the environment decides cannot be changed here
    const struct {
        QWidget *widget;
        const char *name;
    } bound[] = {{m_glassPanel, "SIDEBAR"}, {m_backdrop, "GLASS_MODE"}, {m_image, "GLASS_IMAGE"},   {m_glass, "GLASS"},      {m_blur, "GLASS_BLUR"},
                 {m_motion, "GLASS_MOTION"}, {m_lavaStyle, "LAVA_STYLE"}, {m_lavaSize, "LAVA_SIZE"}, {m_lavaColours, "LAVA_COLORS"}, {m_lavaSpeed, "LAVA_SPEED"},
                 {m_rim, "GLASS_RIM"},      {m_devices, "DEVICES"},     {m_twoLines, "LIST"},        {m_rows, "ROWS"},        {m_icons, "ICONS"},
                 {m_mark, "MARK"},          {m_sigla, "SIGLA"},         {m_ago, "DATE_AGO"},         {m_tiles, "TILES"},          {m_rowDates, "ROW_DATES"},
                 {m_rowWash, "ROW_WASH"}, {m_hoverPreview, "PREVIEW_HOVER"}, {m_hoverDelay, "PREVIEW_HOVER"}, {m_float, "PREVIEW_FLOAT"}, {m_cards, "CARDS"}, {m_cardsFit, "CARDS_FIT"}, {m_cardsInfo, "CARDS_INFO"}, {m_cardsMark, "CARDS_MARK"}, {m_cardsBadge, "CARDS_BADGE"}, {m_activitySize, "ACTIVITY_SIZE"}, {m_sidebarFilters, "SIDEBAR_FILTERS"}, {m_sidebarPinned, "SIDEBAR_PINNED"}};
    for (const auto &entry : bound) {
        entry.widget->setEnabled(!ngSettingFromEnvironment(entry.name));
    }
    for (const auto &[model, proxy] : std::as_const(m_proxies)) {
        proxy->setEnabled(model->isEnabled()); // the tiles follow the value they drive
    }
    m_sideRow->setEnabled(m_glassPanel->isEnabled() && m_backdrop->isEnabled());
    quickSideRow->setEnabled(m_quickView->isChecked());

    connect(m_dialogQuick, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    for (QCheckBox *box : {m_glassPanel, m_motion, m_twoLines, m_icons, m_ago, m_tiles, m_hoverPreview, m_folderSizes, m_rowDates, m_float, m_sidebarFilters, m_sidebarPinned, m_rowWash, m_folderContentDate}) {
        connect(box, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    }
    connect(m_hoverDelay, &QSpinBox::valueChanged, this, &NgSettingsPage::changed);
    for (QComboBox *combo : {m_backdrop, m_devices, m_disksPos, m_lavaStyle, m_lavaColours, m_rim, m_rows, m_mark, m_sigla, m_cards, m_cardsFit, m_cardsInfo, m_cardsMark, m_cardsBadge, m_activitySize}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    }
    for (QSlider *slider : {m_glass, m_blur, m_liveBlur, m_lavaSize, m_lavaSpeed}) {
        connect(slider, &QSlider::valueChanged, this, &NgSettingsPage::changed);
    }
    connect(m_image, &KUrlRequester::textChanged, this, &NgSettingsPage::changed);

    connect(m_devices, &QComboBox::currentIndexChanged, this, &NgSettingsPage::updateDependencies);
    for (QCheckBox *box : {m_glassPanel, m_motion, m_icons, m_hoverPreview}) {
        connect(box, &QCheckBox::toggled, this, &NgSettingsPage::updateDependencies);
    }
    for (QComboBox *combo : {m_backdrop, m_lavaColours, m_mark}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &NgSettingsPage::updateDependencies);
    }
    connect(this, &NgSettingsPage::changed, this, &NgSettingsPage::updatePreview);
    updateDependencies();
    updatePreview();
}

NgSettingsPage::~NgSettingsPage() = default;

void NgSettingsPage::fillSectionOrder(const QStringList &order, bool pinned, bool filters, bool disks)
{
    const QSignalBlocker blocker(m_sectionOrder);
    m_sectionOrder->clear();
    for (const QString &name : order) {
        auto *item = new QListWidgetItem(m_sectionOrder);
        item->setData(Qt::UserRole, name);
        if (name == QLatin1String("places")) {
            item->setText(ngTr("Places (KDE's places, always shown)", "Risorse (i luoghi di KDE, sempre visibili)"));
            item->setIcon(QIcon::fromTheme(QStringLiteral("folder-root")));
            QFont bold = item->font();
            bold.setBold(true);
            item->setFont(bold);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            continue;
        }
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
        if (name == QLatin1String("pinned")) {
            item->setText(ngTr("Pinned folders and files", "Cartelle e file fissati"));
            item->setIcon(QIcon::fromTheme(QStringLiteral("pin")));
            item->setCheckState(pinned ? Qt::Checked : Qt::Unchecked);
        } else if (name == QLatin1String("filters")) {
            item->setText(ngTr("Filters, with their counts", "Filtri, con i numeri"));
            item->setIcon(QIcon::fromTheme(QStringLiteral("view-filter")));
            item->setCheckState(filters ? Qt::Checked : Qt::Unchecked);
        } else if (name == QLatin1String("disks")) {
            item->setText(ngTr("Disks, with their usage", "Dischi, con l'uso"));
            item->setIcon(QIcon::fromTheme(QStringLiteral("drive-harddisk")));
            item->setCheckState(disks ? Qt::Checked : Qt::Unchecked);
        }
    }
    m_sectionOrder->setFixedHeight(m_sectionOrder->sizeHintForRow(0) * m_sectionOrder->count() + 2 * m_sectionOrder->frameWidth() + 4);
}

// the disks and partitions the places know, as boxes: unticking one hides it in the sidebar (and in the file dialogs,
// as the places' own "Hide" does)
void NgSettingsPage::fillDevices()
{
    for (const auto &entry : std::as_const(m_deviceBoxes)) {
        delete entry.second;
    }
    m_deviceBoxes.clear();
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    if (!places) {
        return;
    }
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        const auto group = places->groupType(index);
        if (group != KFilePlacesModel::DevicesType && group != KFilePlacesModel::RemovableDevicesType) {
            continue;
        }
        const QString udi = places->deviceForIndex(index).udi();
        if (udi.isEmpty()) {
            continue;
        }
        auto *box = new QCheckBox(places->text(index), m_deviceList);
        box->setIcon(places->icon(index));
        box->setChecked(!places->isHidden(index));
        connect(box, &QCheckBox::toggled, this, &NgSettingsPage::changed);
        m_deviceList->layout()->addWidget(box);
        m_deviceBoxes.append({udi, box});
    }
    m_deviceList->setVisible(!m_deviceBoxes.isEmpty());
}

void NgSettingsPage::showOption(QWidget *option, bool shown)
{
    if (QFormLayout *form = m_formOf.value(option)) {
        form->setRowVisible(option, shown);
    }
    for (QWidget *part : m_parts.value(option)) {
        part->setVisible(shown);
    }
    if (!m_formOf.contains(option) && !m_parts.contains(option)) {
        option->setVisible(shown);
    }
}

void NgSettingsPage::updatePreview()
{
    const QString mode = m_backdrop->currentData().toString();
    m_preview->side = !m_glassPanel->isChecked() ? QStringLiteral("plain") : mode.isEmpty() ? QStringLiteral("image") : mode;
    m_preview->rim = m_rim->currentData().toString();
    m_preview->filters = m_sidebarFilters->isChecked();
    m_preview->twoLines = m_twoLines->isChecked();
    m_preview->rows = m_rows->currentData().toString();
    m_preview->mark = m_mark->currentData().toString();
    m_preview->classicIcons = m_icons->isChecked();
    m_preview->wash = m_rowWash->isChecked();
    m_preview->card = m_cards->currentData().toString();
    m_preview->stretch = m_cardsFit->currentIndex() == 1;
    m_preview->typeBackground = m_cardsMark->currentIndex() == 1;
    // the tab decides which view the miniature shows: rows on "Rows", cards on "Cards" and "Previews"
    const int tab = m_pages->currentIndex();
    m_preview->cardsView = tab == 3 || tab == 4 || (tab != 2 && m_twoLines->isChecked());
    m_preview->quickView = tab == 4 && m_quickView->isChecked();
    m_preview->quickSide = m_quickSide->currentData().toString();
    m_preview->update();
}

void NgSettingsPage::updateDependencies()
{
    const bool glass = m_glassPanel->isChecked();
    const QString mode = m_backdrop->currentData().toString();
    const bool picture = glass && mode.isEmpty();
    const bool gradient = glass && mode == QLatin1String("lava");

    showOption(m_image, picture);
    showOption(m_glass, picture);
    showOption(m_blur, picture);
    showOption(m_liveBlur, glass && mode == QLatin1String("live"));
    showOption(m_motion, picture);
    showOption(m_lavaStyle, gradient);
    showOption(m_lavaSize, gradient);
    showOption(m_lavaColours, gradient);
    showOption(m_lavaColourRow, gradient && m_lavaColours->currentIndex() == 2);
    showOption(m_lavaSpeed, gradient || (picture && m_motion->isChecked()));
    showOption(m_rim, glass);
    showOption(m_devices, glass);
    showOption(m_disksPos, false); // the disks' place is in the sections' order now
    m_deviceList->setEnabled(!glass || m_devices->currentData().toString() != QLatin1String("hide"));

    const bool marks = !m_icons->isChecked();
    showOption(m_mark, marks);
    showOption(m_sigla, marks && m_mark->currentIndex() == 0);
    showOption(m_hoverDelay, m_hoverPreview->isChecked());
}

void NgSettingsPage::loadSettings()
{
    m_glassPanel->setChecked(ngSetting("SIDEBAR").toLower() != QLatin1String("plain"));
    m_backdrop->setCurrentIndex(qMax(0, m_backdrop->findData(ngSetting("GLASS_MODE").toLower())));
    m_image->setText(ngSetting("GLASS_IMAGE"));
    m_glass->setValue(qBound(0, ngSettingInt("GLASS", 55), 100));
    m_blur->setValue(qBound(0, ngSettingInt("GLASS_BLUR", 45), 100));
    m_liveBlur->setValue(qBound(1, kwinBlurGroup().readEntry("BlurStrength", 15), 15));
    m_motion->setChecked(ngSetting("GLASS_MOTION").toLower() == QLatin1String("drift"));
    m_lavaStyle->setCurrentIndex(ngSetting("LAVA_STYLE").toLower() == QLatin1String("blobs") ? 1 : 0);
    m_lavaSize->setValue(qBound(10, ngSettingInt("LAVA_SIZE", 55), 100));
    m_lavaSpeed->setValue(qBound(5, ngSettingInt("LAVA_SPEED", 30), 100));
    static const char *const defaults[3] = {"#2f6f8f", "#8a5a2c", "#3c4a78"};
    const QString spec = ngSetting("LAVA_COLORS").trimmed().toLower();
    const QStringList custom = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const bool isCustom = !spec.isEmpty() && spec != QLatin1String("wallpaper");
    for (int i = 0; i < 3; ++i) {
        const QColor colour(isCustom && i < custom.size() ? custom.at(i).trimmed() : QString::fromLatin1(defaults[i]));
        m_lavaColour[i]->setColor(colour.isValid() ? colour : QColor(QString::fromLatin1(defaults[i])));
    }
    m_lavaColours->setCurrentIndex(spec.isEmpty() ? 0 : spec == QLatin1String("wallpaper") ? 1 : 2);
    {
        const QString rim = ngSetting("GLASS_RIM").toLower();
        m_rim->setCurrentIndex(qMax(0, m_rim->findData(rim == QLatin1String("show") ? QStringLiteral("diagonal") : rim)));
    }
    {
        const QString devices = ngSetting("DEVICES").toLower();
        m_devices->setCurrentIndex(devices == QLatin1String("hide") ? 2 : devices.isEmpty() ? 0 : 1); // "show" was the old "list"
        m_disksPos->setCurrentIndex(ngSetting("DISKS_POS").toLower() == QLatin1String("above") ? 1 : 0);
    }
    fillSectionOrder(ngSidebarOrder(), ngSetting("SIDEBAR_PINNED").toLower() != QLatin1String("hide"),
                     ngSetting("SIDEBAR_FILTERS").toLower() != QLatin1String("hide"), ngSetting("SIDEBAR_DISKS").toLower() != QLatin1String("hide"));
    fillDevices();
    m_sidebarFilters->setChecked(ngSetting("SIDEBAR_FILTERS").toLower() != QLatin1String("hide"));
    m_sidebarPinned->setChecked(ngSetting("SIDEBAR_PINNED").toLower() != QLatin1String("hide"));
    m_folderContentDate->setChecked(ngSetting("SORT_FOLDERS_BY_CONTENT").toLower() != QLatin1String("off"));

    m_twoLines->setChecked(ngSetting("LIST").toLower() != QLatin1String("columns"));
    static const NgRowStyle order[] = {NgRowStyle::Card, NgRowStyle::Open, NgRowStyle::Line, NgRowStyle::Stripe, NgRowStyle::None};
    for (int i = 0; i < 5; ++i) {
        if (order[i] == ngRowStyle()) {
            m_rows->setCurrentIndex(i);
        }
    }
    m_icons->setChecked(ngSetting("ICONS").toLower() == QLatin1String("show"));
    m_mark->setCurrentIndex(qMax(0, m_mark->findData(ngSetting("MARK").toLower())));
    m_sigla->setCurrentIndex(qMax(0, m_sigla->findData(ngSetting("SIGLA", QStringLiteral("fill")).toLower())));
    m_ago->setChecked(ngSetting("DATE_AGO").toLower() != QLatin1String("hide"));
    m_rowDates->setChecked(ngSetting("ROW_DATES").toLower() != QLatin1String("hide"));
    m_rowWash->setChecked(ngSetting("ROW_WASH").toLower() != QLatin1String("hide"));
    m_folderSizes->setChecked(ContentDisplaySettings::directorySizeMode() == ContentDisplaySettings::EnumDirectorySizeMode::ContentSize);
    const int hover = ngSettingInt("PREVIEW_HOVER", 0);
    m_hoverPreview->setChecked(hover > 0);
    m_hoverDelay->setValue(hover > 0 ? qBound(100, hover, 5000) : 700);
    m_float->setChecked(ngSetting("PREVIEW_FLOAT") == QLatin1String("1"));
    {
        const KConfigGroup dialog(KSharedConfig::openConfig(QStringLiteral("dialog-ngrc")), QStringLiteral("View"));
        m_dialogQuick->setChecked(dialog.readEntry("QuickPreview", true));
        m_dialogDelay->setValue(qBound(0, dialog.readEntry("QuickDelay", 0), 3000));
        m_dialogSize->setCurrentIndex(qMax(0, m_dialogSize->findData(dialog.readEntry("QuickSize", QStringLiteral("medium")))));
        m_dialogDelay->setEnabled(m_dialogQuick->isChecked());
        m_dialogSize->setEnabled(m_dialogQuick->isChecked());
    }
    m_tiles->setChecked(ngSetting("TILES").toLower() != QLatin1String("off"));
    static const char *const looks[] = {"a", "b", "c"};
    m_cards->setCurrentIndex(int(ngCardStyle()));
    Q_UNUSED(looks)
    m_cardsFit->setCurrentIndex(ngCardsStretch() ? 1 : 0);
    m_cardsInfo->setCurrentIndex(ngSetting("CARDS_INFO").toLower() == QLatin1String("full") ? 1 : 0);
    m_cardsMark->setCurrentIndex(ngSetting("CARDS_MARK").toLower() == QLatin1String("type") ? 1 : 0);
    m_activitySize->setCurrentIndex(qMax(0, m_activitySize->findData(ngSetting("ACTIVITY_SIZE").toLower())));
    m_cardsBadge->setCurrentIndex(qMax(0, m_cardsBadge->findData(ngSetting("CARDS_BADGE").toLower())));
    const QString quick = ngSetting("CARDS_QUICKVIEW").toLower();
    m_quickView->setChecked(quick != QLatin1String("off"));
    m_quickDelay->setValue(qBound(0, quick.toInt(), 3000));
    m_quickDelay->setEnabled(m_quickView->isChecked());
    m_quickSide->setCurrentIndex(qMax(0, m_quickSide->findData(ngSetting("CARDS_QUICKVIEW_SIDE").toLower())));
    m_quickSide->setEnabled(m_quickView->isChecked());
}

void NgSettingsPage::applySettings()
{
    setNgSetting("SIDEBAR", m_glassPanel->isChecked() ? QString() : QStringLiteral("plain"));
    setNgSetting("GLASS_MODE", m_backdrop->currentData().toString());
    setNgSetting("GLASS_IMAGE", m_image->text().trimmed());
    setNgSetting("GLASS", QString::number(m_glass->value()));
    setNgSetting("GLASS_BLUR", QString::number(m_blur->value()));
    if (m_backdrop->currentData().toString() == QLatin1String("live") && kwinBlurGroup().readEntry("BlurStrength", 15) != m_liveBlur->value()) {
        KConfigGroup blur = kwinBlurGroup();
        blur.writeEntry("BlurStrength", m_liveBlur->value());
        blur.sync();
        reloadKWinBlur();
    }
    setNgSetting("GLASS_MOTION", m_motion->isChecked() ? QStringLiteral("drift") : QString());
    setNgSetting("LAVA_STYLE", m_lavaStyle->currentData().toString());
    setNgSetting("LAVA_SIZE", QString::number(m_lavaSize->value()));
    setNgSetting("LAVA_SPEED", QString::number(m_lavaSpeed->value()));
    const int colours = m_lavaColours->currentIndex();
    setNgSetting("LAVA_COLORS",
                 colours == 0   ? QString()
                 : colours == 1 ? QStringLiteral("wallpaper")
                                : QStringLiteral("%1,%2,%3").arg(m_lavaColour[0]->color().name(), m_lavaColour[1]->color().name(), m_lavaColour[2]->color().name()));
    setNgSetting("GLASS_RIM", m_rim->currentData().toString());
    setNgSetting("DEVICES", m_devices->currentData().toString());
    setNgSetting("DISKS_POS", m_disksPos->currentData().toString());
    {
        QStringList order;
        bool disks = true;
        for (int row = 0; row < m_sectionOrder->count(); ++row) {
            const QListWidgetItem *item = m_sectionOrder->item(row);
            order.append(item->data(Qt::UserRole).toString());
            if (item->data(Qt::UserRole).toString() == QLatin1String("disks")) {
                disks = item->checkState() == Qt::Checked;
            }
        }
        setNgSetting("SIDEBAR_ORDER", order.join(QLatin1Char(',')));
        setNgSetting("SIDEBAR_DISKS", disks ? QString() : QStringLiteral("hide"));
    }
    if (KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel()) {
        for (const auto &[udi, box] : std::as_const(m_deviceBoxes)) {
            for (int row = 0; row < places->rowCount(); ++row) {
                const QModelIndex index = places->index(row, 0);
                if (places->deviceForIndex(index).udi() == udi) {
                    if (places->isHidden(index) == box->isChecked()) {
                        places->setPlaceHidden(index, !box->isChecked());
                    }
                    break;
                }
            }
        }
    }
    setNgSetting("SIDEBAR_FILTERS", m_sidebarFilters->isChecked() ? QString() : QStringLiteral("hide"));
    setNgSetting("SIDEBAR_PINNED", m_sidebarPinned->isChecked() ? QString() : QStringLiteral("hide"));
    setNgSetting("SORT_FOLDERS_BY_CONTENT", m_folderContentDate->isChecked() ? QString() : QStringLiteral("off"));

    setNgSetting("LIST", m_twoLines->isChecked() ? QString() : QStringLiteral("columns"));
    if (m_rows->isEnabled()) {
        static const NgRowStyle order[] = {NgRowStyle::Card, NgRowStyle::Open, NgRowStyle::Line, NgRowStyle::Stripe, NgRowStyle::None};
        setNgRowStyle(order[qBound(0, m_rows->currentIndex(), 4)]);
    }
    setNgSetting("ICONS", m_icons->isChecked() ? QStringLiteral("show") : QString());
    setNgSetting("MARK", m_mark->currentData().toString());
    setNgSetting("SIGLA", m_sigla->currentData().toString());
    setNgSetting("DATE_AGO", m_ago->isChecked() ? QString() : QStringLiteral("hide"));
    setNgSetting("ROW_DATES", m_rowDates->isChecked() ? QString() : QStringLiteral("hide"));
    setNgSetting("ROW_WASH", m_rowWash->isChecked() ? QString() : QStringLiteral("hide"));
    if (m_folderSizes->isChecked() != (ContentDisplaySettings::directorySizeMode() == ContentDisplaySettings::EnumDirectorySizeMode::ContentSize)) {
        ContentDisplaySettings::setDirectorySizeMode(m_folderSizes->isChecked() ? ContentDisplaySettings::EnumDirectorySizeMode::ContentSize
                                                                                : ContentDisplaySettings::EnumDirectorySizeMode::ContentCount);
        ContentDisplaySettings::self()->save();
    }
    setNgSetting("PREVIEW_FLOAT", m_float->isChecked() ? QStringLiteral("1") : QString());
    {
        KConfigGroup dialog(KSharedConfig::openConfig(QStringLiteral("dialog-ngrc")), QStringLiteral("View"));
        dialog.writeEntry("QuickPreview", m_dialogQuick->isChecked());
        dialog.writeEntry("QuickDelay", m_dialogDelay->value());
        dialog.writeEntry("QuickSize", m_dialogSize->currentData().toString());
        dialog.sync();
    }
    setNgSetting("PREVIEW_HOVER", m_hoverPreview->isChecked() ? QString::number(m_hoverDelay->value()) : QString());
    setNgSetting("TILES", m_tiles->isChecked() ? QString() : QStringLiteral("off"));
    setNgSetting("CARDS", m_cards->currentData().toString());
    setNgSetting("CARDS_FIT", m_cardsFit->currentData().toString());
    setNgSetting("CARDS_INFO", m_cardsInfo->currentData().toString());
    setNgSetting("CARDS_MARK", m_cardsMark->currentData().toString());
    setNgSetting("ACTIVITY_SIZE", m_activitySize->currentData().toString());
    setNgSetting("CARDS_BADGE", m_cardsBadge->currentData().toString());
    setNgSetting("CARDS_QUICKVIEW_SIDE", m_quickSide->currentData().toString());
    setNgSetting("CARDS_QUICKVIEW", !m_quickView->isChecked() ? QStringLiteral("off") : m_quickDelay->value() > 0 ? QString::number(m_quickDelay->value()) : QString());

    ngSettingsInvalidate(); // everything that depends on these re-reads them now
}

void NgSettingsPage::restoreDefaults()
{
    // the same look as ngDefault() in kitemlistwidget.cpp
    m_glassPanel->setChecked(true);
    m_backdrop->setCurrentIndex(qMax(0, m_backdrop->findData(QStringLiteral("live"))));
    m_image->clear();
    m_glass->setValue(55);
    m_blur->setValue(45);
    m_motion->setChecked(false);
    m_lavaStyle->setCurrentIndex(0);
    m_lavaSize->setValue(55);
    m_lavaSpeed->setValue(30);
    m_lavaColours->setCurrentIndex(0);
    m_rim->setCurrentIndex(qMax(0, m_rim->findData(QStringLiteral("diagonal"))));
    m_devices->setCurrentIndex(0);
    m_disksPos->setCurrentIndex(0);
    fillSectionOrder({QStringLiteral("places"), QStringLiteral("pinned"), QStringLiteral("filters"), QStringLiteral("disks")}, true, true, true);
    for (const auto &entry : std::as_const(m_deviceBoxes)) {
        entry.second->setChecked(true);
    }
    m_twoLines->setChecked(true);
    m_rows->setCurrentIndex(0);
    m_icons->setChecked(false);
    m_mark->setCurrentIndex(qMax(0, m_mark->findData(QStringLiteral("object"))));
    m_sigla->setCurrentIndex(0);
    m_ago->setChecked(true);
    m_rowDates->setChecked(true);
    m_rowWash->setChecked(true);
    m_hoverPreview->setChecked(false);
    m_hoverDelay->setValue(700);
    m_float->setChecked(false);
    m_dialogQuick->setChecked(true);
    m_dialogDelay->setValue(0);
    m_dialogSize->setCurrentIndex(1);
    m_folderSizes->setChecked(false);
    m_tiles->setChecked(true);
    m_sidebarFilters->setChecked(true);
    m_sidebarPinned->setChecked(true);
    m_folderContentDate->setChecked(true);
    m_cards->setCurrentIndex(qMax(0, m_cards->findData(QStringLiteral("b"))));
    m_cardsFit->setCurrentIndex(qMax(0, m_cardsFit->findData(QStringLiteral("stretch"))));
    m_cardsInfo->setCurrentIndex(0);
    m_cardsMark->setCurrentIndex(0);
    m_activitySize->setCurrentIndex(1);
    m_cardsBadge->setCurrentIndex(qMax(0, m_cardsBadge->findData(QStringLiteral("inside"))));
    m_quickView->setChecked(true);
    m_quickDelay->setValue(0);
    m_quickSide->setCurrentIndex(0);
}

void NgSettingsPage::applyPreset(const QString &name)
{
    restoreDefaults(); // Glass is the look the fork was designed around: the defaults
    if (name == QLatin1String("classic")) {
        // as close to stock Dolphin as the fork gets
        m_glassPanel->setChecked(false);
        m_devices->setCurrentIndex(1); // the classic section
        m_sidebarFilters->setChecked(false);
        m_twoLines->setChecked(false);
        m_rows->setCurrentIndex(3); // alternating stripes
        m_icons->setChecked(true);
        m_tiles->setChecked(false);
    } else if (name == QLatin1String("soft")) {
        m_glassPanel->setChecked(false);
        m_devices->setCurrentIndex(1);
    } else {
        m_cardsMark->setCurrentIndex(1); // the type as the background of the cards
    }
    updateDependencies();
}

#include "moc_ngsettingspage.cpp"
