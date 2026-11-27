/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ngrecentview.h"

#include "kitemviews/kitemlistwidget.h"

#include <KFileItem>
#include <KIO/JobUiDelegateFactory>
#include <KIO/ListJob>
#include <KIO/OpenUrlJob>
#include <KIO/PreviewJob>
#include <KIO/StatJob>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QUrlQuery>

namespace
{
const int s_gap = 6;
const int s_shownChildren = 6;

QColor hueColor(int hue, bool dark)
{
    switch (hue) {
    case 0:
        return dark ? QColor(0xE6, 0xB2, 0x5C) : QColor(0xA8, 0x6A, 0x12); // new: amber
    case 1:
        return dark ? QColor(0xB9, 0x9C, 0xF0) : QColor(0x7A, 0x4F, 0xC0); // added: violet
    case 2:
        return dark ? QColor(0x86, 0xB6, 0xF2) : QColor(0x2C, 0x6F, 0xC7); // modified: blue
    case 3:
        return dark ? QColor(0x6F, 0xD0, 0x8F) : QColor(0x2A, 0x8A, 0x4A); // downloaded: green
    case 4:
        return dark ? QColor(0xF0, 0x8A, 0x7C) : QColor(0xC0, 0x39, 0x2B); // made by an action: red
    case 7:
        return dark ? QColor(0x6F, 0xD0, 0xD0) : QColor(0x1A, 0x8A, 0x8A); // on its own: teal
    default:
        return dark ? QColor(0xA4, 0xA8, 0xB8) : QColor(0x66, 0x6B, 0x7C);
    }
}

QString homed(QString path)
{
    const QString home = QDir::homePath();
    if (path == home || path.startsWith(home + QLatin1Char('/'))) {
        path.replace(0, home.size(), QLatin1Char('~'));
    }
    return path;
}

QColor faded(QColor colour, qreal alpha)
{
    colour.setAlphaF(alpha);
    return colour;
}

// the sign of an operation: + new files, an arrow files added, a pencil files modified, a turning arrow on its own
void paintSign(QPainter &painter, const QRectF &box, int hue, const QColor &ink)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal radius = box.width() * 0.26;
    painter.setPen(QPen(faded(ink, 0.30), 1));
    painter.setBrush(faded(ink, 0.08));
    painter.drawRoundedRect(box.adjusted(3.5, 0.5, -0.5, -3.5), radius, radius);
    const QRectF front = box.adjusted(0.5, 3.5, -3.5, -0.5);
    painter.setPen(QPen(faded(ink, 0.55), 1));
    painter.setBrush(faded(ink, 0.18));
    painter.drawRoundedRect(front, radius, radius);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ink, qMax(1.6, box.width() / 18.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QPointF c = front.center();
    const qreal r = front.width() * 0.22;
    if (hue == 0) {
        painter.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
    } else if (hue == 1) {
        painter.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
        painter.drawPolyline(QPolygonF{QPointF(c.x() + r * 0.3, c.y() - r * 0.7), QPointF(c.x() + r, c.y()), QPointF(c.x() + r * 0.3, c.y() + r * 0.7)});
    } else if (hue == 7) {
        painter.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 60 * 16, 270 * 16);
        painter.drawPolyline(QPolygonF{QPointF(c.x() + r * 0.15, c.y() - r * 1.5), QPointF(c.x() + r * 0.62, c.y() - r * 0.86), QPointF(c.x() - r * 0.15, c.y() - r * 0.5)});
    } else {
        painter.drawLine(QPointF(c.x() - r * 0.75, c.y() + r * 0.75), QPointF(c.x() + r * 0.75, c.y() - r * 0.75));
        painter.drawLine(QPointF(c.x() - r, c.y() + r * 1.15), QPointF(c.x() + r, c.y() + r * 1.15));
    }
    painter.restore();
}
}

NgRecentView::NgRecentView(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_refresh(new QTimer(this))
{
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // asked every few seconds what the list holds (a few figures), and listed again only when that changed
    m_refresh->setInterval(10000);
    connect(m_refresh, &QTimer::timeout, this, [this] {
        if (!isVisible() || !window()->isActiveWindow() || m_loading || !m_url.isValid()) {
            return;
        }
        const QUrl url = m_url;
        KIO::StatJob *job = KIO::stat(url, KIO::HideProgressInfo);
        connect(job, &KJob::result, this, [this, job, url] {
            if (job->error() || url != m_url) {
                return;
            }
            const QString stamp = job->statResult().stringValue(KIO::UDSEntry::UDS_EXTRA + 15);
            if (!m_stamp.isEmpty() && stamp != m_stamp) {
                reload();
            }
            m_stamp = stamp;
        });
    });
    m_refresh->start();
}

QUrl NgRecentView::url() const
{
    return m_url;
}

void NgRecentView::setUrl(const QUrl &url)
{
    if (url == m_url && !m_entries.isEmpty()) {
        return;
    }
    m_url = url;
    m_stamp.clear();
    m_selected = -1;
    verticalScrollBar()->setValue(0);
    reload();
}

void NgRecentView::setFilters(int events, int kinds)
{
    if (events == m_events && kinds == m_kinds) {
        return;
    }
    const bool again = events != m_events; // (another list, or the same one with other lines: asked again)
    m_events = events;
    m_kinds = kinds;
    verticalScrollBar()->setValue(0);
    if (again) {
        m_stamp.clear();
        if (m_url.isValid()) {
            reload();
        }
    } else {
        applyKinds();
    }
}

int NgRecentView::eventFilter() const
{
    return m_events;
}

int NgRecentView::kindFilter() const
{
    return m_kinds;
}

int NgRecentView::kindsPresent() const
{
    return m_present;
}

// the kind of a line: a file's by its name; things done in one go count as a folder
static int kindBit(const QString &name, bool folder)
{
    return 1 << int(ngKindFor(name, folder).category);
}

void NgRecentView::applyKinds()
{
    m_entries.clear();
    for (const Entry &entry : std::as_const(m_listed)) {
        if (m_kinds == 0 || (m_kinds & kindBit(entry.name, entry.folder || entry.group))) {
            m_entries.append(entry);
        }
    }
    m_hover = -1;
    m_selected = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).group && qEnvironmentVariableIsSet("DOLPHIN_NG_RECENT_OPEN_ALL")) {
            m_expanded.insert(m_entries.at(i).id); // (to look at open operations without a pointer: tests, pictures)
        }
        if (m_entries.at(i).group && m_expanded.contains(m_entries.at(i).id)) {
            loadChildren(i);
        }
    }
    requestPreviews(m_entries);
    layoutRows();
}

static void readEntries(const KIO::UDSEntryList &list, QList<int> *hues, const std::function<void(const KIO::UDSEntry &, int)> &add)
{
    Q_UNUSED(hues)
    for (const KIO::UDSEntry &entry : list) {
        if (entry.stringValue(KIO::UDSEntry::UDS_NAME) == QLatin1String(".")) {
            continue;
        }
        int kind = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 8).toInt();
        if (kind == 6) {
            const int made = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 14).toInt();
            kind = made == 1 ? 0 : made == 2 ? 1 : 2;
        }
        add(entry, entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 11) == QLatin1String("automatic") ? 7 : kind);
    }
}

void NgRecentView::reload()
{
    if (m_loading || !m_url.isValid()) {
        return;
    }
    m_loading = true;
    auto fresh = std::make_shared<QList<Entry>>();
    // (what was only opened is not in the list unless asked for: ng-recent adds it with "opened=1")
    QUrl asked = m_url;
    if (m_events & 4) {
        QUrlQuery query(asked);
        query.addQueryItem(QStringLiteral("opened"), QStringLiteral("1"));
        asked.setQuery(query);
    }
    const int filter = m_events;
    KIO::ListJob *job = KIO::listDir(asked, KIO::HideProgressInfo);
    const QUrl url = m_url;
    connect(job, &KIO::ListJob::entries, this, [fresh](KIO::Job *, const KIO::UDSEntryList &list) {
        readEntries(list, nullptr, [&fresh](const KIO::UDSEntry &entry, int hue) {
            Entry e;
            e.group = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 8) == QLatin1String("6");
            e.name = entry.stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
            e.url = QUrl(entry.stringValue(KIO::UDSEntry::UDS_URL));
            e.path = e.group ? entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 9) : entry.stringValue(KIO::UDSEntry::UDS_LOCAL_PATH);
            e.what = entry.stringValue(KIO::UDSEntry::UDS_COMMENT);
            e.mime = entry.stringValue(KIO::UDSEntry::UDS_MIME_TYPE);
            e.folder = entry.isDir();
            e.size = e.folder ? -1 : entry.numberValue(KIO::UDSEntry::UDS_SIZE, -1);
            e.when = entry.numberValue(KIO::UDSEntry::UDS_ACCESS_TIME, 0);
            e.hue = hue;
            e.count = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 10).toInt();
            e.made = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 14).toInt();
            e.automatic = hue == 7;
            e.id = e.group ? e.url.path().section(QLatin1Char('/'), -1) : QString();
            fresh->append(e);
        });
    });
    connect(job, &KJob::result, this, [this, job, fresh, url, filter] {
        m_loading = false;
        if (url != m_url || filter != m_events) {
            reload();
            return;
        }
        if (job->error()) {
            return;
        }
        // the hues: 0 new, 1 added, 2 modified, 3 downloaded, 4 made by an action, 5 opened, 7 on its own
        fresh->removeIf([filter](const Entry &entry) {
            const bool made = entry.hue == 0 || entry.hue == 1 || entry.hue == 3 || entry.hue == 4;
            if (filter == 0) {
                return entry.hue == 5; // (everything that was made or changed: not what was only opened)
            }
            // (what was done on its own, 7, is in a list of its own and goes with any choice there)
            return !(((filter & 1) && made) || ((filter & 2) && entry.hue == 2) || ((filter & 4) && entry.hue == 5) || entry.hue == 7);
        });
        std::stable_sort(fresh->begin(), fresh->end(), [](const Entry &a, const Entry &b) {
            return a.when > b.when;
        });
        m_listed = *fresh;
        m_present = 0;
        for (const Entry &entry : std::as_const(m_listed)) {
            m_present |= kindBit(entry.name, entry.folder || entry.group);
        }
        applyKinds();
        Q_EMIT contentChanged();
    });
}

void NgRecentView::loadChildren(int index)
{
    const QString id = m_entries.at(index).id;
    auto fresh = std::make_shared<QList<Entry>>();
    KIO::ListJob *job = KIO::listDir(m_entries.at(index).url, KIO::HideProgressInfo);
    connect(job, &KIO::ListJob::entries, this, [fresh](KIO::Job *, const KIO::UDSEntryList &list) {
        readEntries(list, nullptr, [&fresh](const KIO::UDSEntry &entry, int hue) {
            if (entry.isDir()) {
                return; // (the folders an operation made are not what one opens it for)
            }
            Entry e;
            e.name = entry.stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
            e.url = QUrl(entry.stringValue(KIO::UDSEntry::UDS_URL));
            e.path = entry.stringValue(KIO::UDSEntry::UDS_LOCAL_PATH);
            e.what = entry.stringValue(KIO::UDSEntry::UDS_COMMENT);
            e.mime = entry.stringValue(KIO::UDSEntry::UDS_MIME_TYPE);
            e.size = entry.numberValue(KIO::UDSEntry::UDS_SIZE, -1);
            e.when = entry.numberValue(KIO::UDSEntry::UDS_ACCESS_TIME, 0);
            e.hue = hue;
            fresh->append(e);
        });
    });
    connect(job, &KJob::result, this, [this, fresh, id] {
        for (Entry &entry : m_entries) {
            if (entry.group && entry.id == id) {
                std::stable_sort(fresh->begin(), fresh->end(), [](const Entry &a, const Entry &b) {
                    return a.name.localeAwareCompare(b.name) < 0;
                });
                entry.children = *fresh;
                entry.loaded = true;
                requestPreviews(entry.children.mid(0, m_showAll.contains(id) ? 300 : s_shownChildren));
                break;
            }
        }
        layoutRows();
    });
}

void NgRecentView::requestPreviews(const QList<Entry> &entries)
{
    KFileItemList items;
    for (const Entry &entry : entries) {
        const bool pictured = entry.mime.startsWith(QLatin1String("image/")) || entry.mime.startsWith(QLatin1String("video/")) || entry.mime == QLatin1String("application/pdf");
        if (!entry.group && !entry.folder && pictured && !entry.path.isEmpty() && !m_previewAsked.contains(entry.path) && items.size() < 80) {
            m_previewAsked.insert(entry.path);
            items.append(KFileItem(QUrl::fromLocalFile(entry.path), entry.mime));
        }
    }
    if (items.isEmpty()) {
        return;
    }
    if (m_previews.size() > 600) {
        m_previews.clear();
        m_previewAsked.clear();
    }
    auto *job = new KIO::PreviewJob(items, QSize(128, 128));
    job->setScaleType(KIO::PreviewJob::Scaled);
    connect(job, &KIO::PreviewJob::gotPreview, this, [this](const KFileItem &item, const QPixmap &pixmap) {
        m_previews.insert(item.localPath(), pixmap);
        viewport()->update();
    });
}

void NgRecentView::layoutRows()
{
    m_rows.clear();
    const QFontMetrics metrics(font());
    const int fileHeight = qMax(58, 2 * metrics.height() + 24);
    const int width = viewport()->width() - 12;
    const QDate today = QDate::currentDate();
    QDate day;
    int y = 4;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &entry = m_entries.at(i);
        const QDate date = QDateTime::fromSecsSinceEpoch(entry.when).date();
        if (date != day) {
            day = date;
            Row title;
            title.type = Row::Day;
            const int ago = date.daysTo(today);
            title.text = ago == 0 ? ngTr("Today", "Oggi") : ago == 1 ? ngTr("Yesterday", "Ieri") : QLocale().toString(date, QStringLiteral("dddd d MMMM"));
            title.rect = QRect(6, y + 6, width, metrics.height() + 8);
            m_rows.append(title);
            y = title.rect.bottom() + 1;
        }
        Row row;
        row.type = entry.group ? Row::Group : Row::File;
        row.entry = i;
        row.rect = QRect(6, y, width, fileHeight);
        m_rows.append(row);
        y += fileHeight + s_gap;
        if (entry.group && m_expanded.contains(entry.id)) {
            const int shown = m_showAll.contains(entry.id) ? entry.children.size() : qMin(s_shownChildren, int(entry.children.size()));
            for (int c = 0; c < shown; ++c) {
                Row child;
                child.entry = i;
                child.child = c;
                child.rect = QRect(6 + 44, y, width - 44, fileHeight);
                m_rows.append(child);
                y += fileHeight + s_gap;
            }
            Row foot;
            foot.type = Row::Foot;
            foot.entry = i;
            foot.rect = QRect(6 + 44, y, width - 44, metrics.height() + 22);
            m_rows.append(foot);
            y += foot.rect.height() + s_gap + 4;
        }
    }
    m_contentHeight = y + 8;
    verticalScrollBar()->setRange(0, qMax(0, m_contentHeight - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(fileHeight / 2);
    viewport()->update();
}

const NgRecentView::Entry *NgRecentView::entryOf(const Row &row) const
{
    if (row.entry < 0 || row.entry >= m_entries.size()) {
        return nullptr;
    }
    const Entry &entry = m_entries.at(row.entry);
    if (row.child >= 0) {
        return row.child < entry.children.size() ? &entry.children.at(row.child) : nullptr;
    }
    return &entry;
}

int NgRecentView::rowAt(const QPoint &pos) const
{
    const QPoint at = pos + QPoint(0, verticalScrollBar()->value());
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).type != Row::Day && m_rows.at(i).rect.contains(at)) {
            return i;
        }
    }
    return -1;
}

void NgRecentView::paintEntry(QPainter &painter, const Row &row, const Entry &entry, bool hovered)
{
    const QPalette pal = palette();
    const bool dark = ngIsDark(pal);
    const QColor ink = pal.color(QPalette::Text);
    const QColor dim = faded(ink, 0.62);
    const QColor hue = hueColor(entry.hue, dark);
    const QRectF card = row.rect;
    const bool child = row.child >= 0;
    const bool selected = m_selected >= 0 && &m_rows.at(m_selected) == &row;
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(selected ? QPen(faded(pal.color(QPalette::Highlight), 0.9), 1.2) : Qt::NoPen);
    // a card: a veil of the text's colour on the window (lighter for what is inside an operation)
    const qreal veil = (dark ? 0.075 : 0.055) * (child ? 0.6 : 1) + (hovered ? 0.05 : 0) + (selected ? 0.04 : 0);
    painter.setBrush(dark ? faded(QColor(255, 255, 255), veil) : faded(QColor(255, 255, 255), 0.42 + veil * 4));
    painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    QFont plain = font();
    QFont bold = plain;
    bold.setWeight(QFont::DemiBold);
    QFont small = plain;
    small.setPointSizeF(plain.pointSizeF() * 0.92);
    const QFontMetrics metrics(plain), smallMetrics(small), boldMetrics(bold);
    const int x0 = row.rect.left();
    const int midY = row.rect.center().y();
    int x = x0 + 12;
    if (entry.group) {
        // the twisty, then the sign of what was done
        const bool open = m_expanded.contains(entry.id);
        QPainterPath twisty;
        const QPointF c(x + 8, midY);
        if (open) {
            twisty.moveTo(c.x() - 5, c.y() - 3);
            twisty.lineTo(c.x() + 5, c.y() - 3);
            twisty.lineTo(c.x(), c.y() + 4);
        } else {
            twisty.moveTo(c.x() - 3, c.y() - 5);
            twisty.lineTo(c.x() - 3, c.y() + 5);
            twisty.lineTo(c.x() + 4, c.y());
        }
        twisty.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(faded(ink, 0.7));
        painter.drawPath(twisty);
        x += 30;
        const int side = qMin(row.rect.height() - 14, 46);
        paintSign(painter, QRectF(x, midY - side / 2.0, side, side), entry.hue, hue);
        x += side + 12;
    } else {
        if (!child) {
            x += 30; // (in line with the names of the operations)
        }
        // the file: its picture when it has one, else a small sheet with its extension in the colour of what happened
        const int tileH = row.rect.height() - 12, tileW = qRound(tileH * 0.86);
        const QRectF tile(x, midY - tileH / 2.0, tileW, tileH);
        const QPixmap preview = m_previews.value(entry.path);
        QPainterPath clip;
        clip.addRoundedRect(tile, 6, 6);
        if (!preview.isNull()) {
            painter.save();
            painter.setClipPath(clip);
            const QPixmap scaled = preview.scaled((tile.size() * devicePixelRatioF()).toSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const QSizeF shown = scaled.size() / devicePixelRatioF();
            painter.drawPixmap(QRectF(tile.center().x() - shown.width() / 2, tile.center().y() - shown.height() / 2, shown.width(), shown.height()), scaled, scaled.rect());
            painter.restore();
            painter.setPen(QPen(faded(ink, 0.18), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        } else {
            painter.setPen(QPen(faded(ink, 0.14), 1));
            painter.setBrush(dark ? faded(QColor(0, 0, 0), 0.28) : faded(QColor(255, 255, 255), 0.75));
            painter.drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            QFont tiny = bold;
            tiny.setPointSizeF(plain.pointSizeF() * 0.72);
            painter.setFont(tiny);
            painter.setPen(hue);
            const QString extension = entry.folder ? ngTr("DIR", "DIR") : QFileInfo(entry.name).suffix().left(4).toUpper();
            painter.drawText(tile.adjusted(5, 0, -3, -4), Qt::AlignLeft | Qt::AlignBottom, extension.isEmpty() ? QStringLiteral("·") : extension);
        }
        x += tileW + 12;
    }

    // from the right: the time, the size, for an operation how many files; what happened takes the middle
    const int right = row.rect.right() - 14;
    const int timeW = 64, sizeW = 84;
    const QDateTime when = QDateTime::fromSecsSinceEpoch(entry.when);
    painter.setFont(small);
    painter.setPen(dim);
    painter.drawText(QRect(right - timeW, row.rect.top(), timeW, row.rect.height()), Qt::AlignRight | Qt::AlignVCenter, QLocale().toString(when.time(), QLocale::ShortFormat));
    if (entry.size >= 0) {
        painter.drawText(QRect(right - timeW - sizeW, row.rect.top(), sizeW, row.rect.height()), Qt::AlignRight | Qt::AlignVCenter, QLocale().formattedDataSize(entry.size, 1));
    }
    const int infoRight = right - timeW - sizeW - 16;
    const int whatLeft = x0 + qRound(row.rect.width() * (child ? 0.50 : 0.52));
    const bool room = infoRight - whatLeft > 120;
    int nameRight = room ? whatLeft - 16 : infoRight;
    if (entry.group) {
        // how many files, on a pill
        painter.setFont(bold);
        const QString count = QLocale().toString(entry.count) + QLatin1String(" file");
        const int pillW = boldMetrics.horizontalAdvance(count) + 22, pillH = boldMetrics.height() + 8;
        const QRectF pill(infoRight - pillW, midY - pillH / 2.0, pillW, pillH);
        painter.setPen(QPen(faded(ink, 0.16), 1));
        painter.setBrush(faded(ink, dark ? 0.10 : 0.06));
        painter.drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
        painter.setPen(ink);
        painter.drawText(pill, Qt::AlignCenter, count);
        nameRight = int(pill.left()) - 16;
    } else if (room) {
        // what happened; the application that did it stands out
        static const QRegularExpression byWhom(QStringLiteral("^(.*? (?:con|with) |.*?\\()(.+?)(\\)?)$"));
        const QString what = smallMetrics.elidedText(entry.what, Qt::ElideRight, infoRight - whatLeft);
        const auto match = byWhom.match(what);
        const int baseline = midY + (smallMetrics.ascent() - smallMetrics.descent()) / 2;
        painter.setFont(small);
        if (match.hasMatch() && what == entry.what) {
            painter.setPen(dim);
            painter.drawText(whatLeft, baseline, match.captured(1));
            int at = whatLeft + smallMetrics.horizontalAdvance(match.captured(1));
            QFont strong = small;
            strong.setWeight(QFont::DemiBold);
            painter.setFont(strong);
            painter.setPen(hue);
            painter.drawText(at, baseline, match.captured(2));
            at += QFontMetrics(strong).horizontalAdvance(match.captured(2));
            painter.setFont(small);
            painter.setPen(dim);
            painter.drawText(at, baseline, match.captured(3));
        } else {
            painter.setPen(entry.hue >= 0 && entry.hue != 5 ? hue : dim);
            painter.drawText(whatLeft, baseline, what);
        }
    }

    // the name, and under it where it is (for an operation: where, and who did it)
    QString title = entry.name;
    QString under = homed(entry.group ? entry.path : QFileInfo(entry.path).path());
    if (entry.group) {
        title = (entry.made == 1 ? ngTr("New files in ", "Nuovi file in ") : entry.made == 2 ? ngTr("Files added to ", "File aggiunti in ") : ngTr("Files modified in ", "File modificati in "))
            + entry.name;
        static const QRegularExpression who(QStringLiteral(" (?:con|with) (.+)$|, (?:automatico|automatic)(?: \\((.+)\\))?$"));
        const auto match = who.match(entry.what);
        if (match.hasMatch()) {
            under += QStringLiteral(" · ")
                + (match.captured(1).isEmpty() ? ngTr("automatic", "automatico") + (match.captured(2).isEmpty() ? QString() : QStringLiteral(" (") + match.captured(2) + QLatin1Char(')'))
                                               : ngTr("with ", "con ") + match.captured(1));
        }
    } else if (!room) {
        under = entry.what + QStringLiteral(" · ") + under;
    }
    const int textW = qMax(40, nameRight - x);
    const int lineGap = 2;
    const int top = midY - (boldMetrics.height() + lineGap + smallMetrics.height()) / 2;
    painter.setFont(bold);
    painter.setPen(ink);
    painter.drawText(x, top + boldMetrics.ascent(), boldMetrics.elidedText(title, Qt::ElideMiddle, textW));
    painter.setFont(small);
    painter.setPen(dim);
    painter.drawText(x, top + boldMetrics.height() + lineGap + smallMetrics.ascent(), smallMetrics.elidedText(under, Qt::ElideMiddle, textW));
}

void NgRecentView::paintFoot(QPainter &painter, const Row &row, const Entry &entry)
{
    const QPalette pal = palette();
    const bool dark = ngIsDark(pal);
    const QColor ink = pal.color(QPalette::Text);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.92);
    const QFontMetrics metrics(small);
    painter.setFont(small);
    int x = row.rect.left() + 12;
    const int midY = row.rect.center().y();
    const bool all = m_showAll.contains(entry.id);
    const int shown = all ? entry.children.size() : qMin(s_shownChildren, int(entry.children.size()));
    const int more = qMax(0, entry.count - shown);
    QString lead;
    if (!entry.loaded) {
        lead = ngTr("Reading…", "Lettura…");
    } else if (more > 0) {
        lead = ngTr("and %1 more files", "e altri %1 file").arg(QLocale().toString(more));
    }
    if (!lead.isEmpty()) {
        painter.setPen(faded(ink, 0.62));
        painter.drawText(x, midY + (metrics.ascent() - metrics.descent()) / 2, lead);
        x += metrics.horizontalAdvance(lead) + 12;
    }
    struct Choice {
        int action;
        QString text;
    };
    QList<Choice> choices;
    if (entry.loaded && !all && entry.children.size() > shown) {
        // (the first files of a long operation are kept one by one; the rest are only counted)
        choices.append({1, entry.children.size() < entry.count ? ngTr("Show the first %1", "Mostra i primi %1").arg(entry.children.size()) : ngTr("Show all", "Mostra tutti")});
    }
    choices.append({2, ngTr("Open the folder", "Apri la cartella")});
    choices.append({3, ngTr("Hide this operation", "Nascondi questa operazione")});
    choices.append({4, ngTr("Always leave this folder out", "Escludi sempre questa cartella")});
    for (const Choice &choice : std::as_const(choices)) {
        const int w = metrics.horizontalAdvance(choice.text) + 24, h = metrics.height() + 10;
        if (x + w > row.rect.right() - 6) {
            break;
        }
        const QRect rect(x, midY - h / 2, w, h);
        const bool hovered = m_hoverButton >= 0 && m_hoverButton < m_buttons.size() && m_buttons.at(m_hoverButton).entry == row.entry && m_buttons.at(m_hoverButton).action == choice.action;
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(faded(ink, hovered ? 0.34 : 0.18), 1));
        painter.setBrush(faded(ink, (dark ? 0.09 : 0.05) + (hovered ? 0.07 : 0)));
        painter.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), h / 2.0, h / 2.0);
        painter.setPen(ink);
        painter.drawText(rect, Qt::AlignCenter, choice.text);
        m_buttons.append({rect, row.entry, choice.action});
        x += w + 8;
    }
}

void NgRecentView::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    const int offset = verticalScrollBar()->value();
    painter.translate(0, -offset);
    const QRect visible(0, offset, viewport()->width(), viewport()->height());
    const QPalette pal = palette();
    const bool dark = ngIsDark(pal);
    m_buttons.clear();
    if (m_entries.isEmpty()) {
        painter.setPen(faded(pal.color(QPalette::Text), 0.6));
        painter.drawText(QRect(0, offset, viewport()->width(), viewport()->height()), Qt::AlignCenter,
                         m_loading ? ngTr("Reading what happened lately…", "Lettura di ciò che è successo di recente…")
                             : m_events == 0 && m_kinds == 0 ? ngTr("Nothing happened lately", "Non è successo nulla di recente")
                                             : ngTr("Nothing of this kind lately", "Niente di questo tipo di recente"));
        return;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        if (!row.rect.intersects(visible)) {
            continue;
        }
        if (row.type == Row::Day) {
            QFont title = font();
            title.setWeight(QFont::Bold);
            title.setPointSizeF(title.pointSizeF() * 0.94);
            painter.setFont(title);
            painter.setPen(faded(pal.color(QPalette::Text), 0.66));
            painter.drawText(row.rect.adjusted(8, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, row.text);
            continue;
        }
        const Entry &owner = m_entries.at(row.entry);
        if (row.type == Row::Foot) {
            paintFoot(painter, row, owner);
            continue;
        }
        if (const Entry *entry = entryOf(row)) {
            paintEntry(painter, row, *entry, i == m_hover);
        }
    }
    // what is inside an open operation hangs from a line in its colour
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        if (row.type != Row::Group || !m_expanded.contains(m_entries.at(row.entry).id)) {
            continue;
        }
        int bottom = row.rect.bottom();
        for (int j = i + 1; j < m_rows.size() && m_rows.at(j).entry == row.entry && m_rows.at(j).type != Row::Group; ++j) {
            bottom = m_rows.at(j).rect.bottom();
        }
        if (bottom > row.rect.bottom() && QRect(0, row.rect.bottom(), 10, bottom - row.rect.bottom()).intersects(visible)) {
            painter.setPen(QPen(faded(hueColor(m_entries.at(row.entry).hue, dark), 0.55), 2, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(row.rect.left() + 32, row.rect.bottom() + 6), QPointF(row.rect.left() + 32, bottom - 2));
        }
    }
}

void NgRecentView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    layoutRows();
}

void NgRecentView::scrollContentsBy(int, int)
{
    viewport()->update();
}

void NgRecentView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint at = event->pos() + QPoint(0, verticalScrollBar()->value());
    int button = -1;
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i).rect.contains(at)) {
            button = i;
        }
    }
    const int row = rowAt(event->pos());
    const bool clickable = button >= 0 || (row >= 0 && m_rows.at(row).type != Row::Foot);
    viewport()->setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (row != m_hover || button != m_hoverButton) {
        m_hover = row;
        m_hoverButton = button;
        viewport()->update();
    }
}

void NgRecentView::leaveEvent(QEvent *event)
{
    m_hover = -1;
    m_hoverButton = -1;
    viewport()->update();
    QAbstractScrollArea::leaveEvent(event);
}

void NgRecentView::open(const Entry &entry)
{
    if (entry.path.isEmpty()) {
        return;
    }
    if (entry.folder || entry.group) {
        Q_EMIT folderRequested(QUrl::fromLocalFile(entry.path));
        return;
    }
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(entry.path), entry.mime);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
    job->start();
}

void NgRecentView::act(const Button &button)
{
    if (button.entry < 0 || button.entry >= m_entries.size()) {
        return;
    }
    const Entry entry = m_entries.at(button.entry);
    switch (button.action) {
    case 1:
        m_showAll.insert(entry.id);
        requestPreviews(entry.children);
        layoutRows();
        break;
    case 2:
        Q_EMIT folderRequested(QUrl::fromLocalFile(entry.path));
        break;
    case 3: {
        // ng-recent leaves out the operations listed here (its own settings: Dialog NG reads the same list)
        QSettings settings(QStringLiteral("ng-recent"), QStringLiteral("list"));
        QStringList hidden = settings.value(QStringLiteral("HiddenOperations")).toStringList();
        hidden.append(entry.id);
        settings.setValue(QStringLiteral("HiddenOperations"), hidden.mid(qMax(0, int(hidden.size()) - 500)));
        settings.sync();
        m_expanded.remove(entry.id);
        reload();
        break;
    }
    case 4: {
        // a line more in what the file change service leaves out for this user: it reads the file again at once
        const QString home = QDir::homePath();
        if (!entry.path.startsWith(home + QLatin1Char('/'))) {
            break;
        }
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ng-fsevents");
        QDir().mkpath(folder);
        QFile file(folder + QStringLiteral("/exclude"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            file.write((QLatin1Char('\n') + entry.path.mid(home.size() + 1) + QStringLiteral("/\n")).toUtf8());
            file.close();
        }
        m_expanded.remove(entry.id);
        QTimer::singleShot(1500, this, &NgRecentView::reload);
        break;
    }
    }
}

void NgRecentView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPoint at = event->pos() + QPoint(0, verticalScrollBar()->value());
    for (const Button &button : std::as_const(m_buttons)) {
        if (button.rect.contains(at)) {
            act(button);
            return;
        }
    }
    const int index = rowAt(event->pos());
    if (index < 0) {
        m_selected = -1;
        viewport()->update();
        return;
    }
    const Row row = m_rows.at(index);
    if (row.type == Row::Group) {
        // an operation opens in place, and closes
        const QString id = m_entries.at(row.entry).id;
        if (m_expanded.contains(id)) {
            m_expanded.remove(id);
            m_showAll.remove(id);
        } else {
            m_expanded.insert(id);
            if (!m_entries.at(row.entry).loaded) {
                loadChildren(row.entry);
            }
        }
        layoutRows();
        return;
    }
    if (row.type == Row::File) {
        m_selected = index;
        viewport()->update();
        if (style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, this)) {
            if (const Entry *entry = entryOf(row)) {
                open(*entry);
            }
        }
    }
}

void NgRecentView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int index = rowAt(event->pos());
    if (event->button() != Qt::LeftButton || index < 0 || m_rows.at(index).type != Row::File || style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, this)) {
        return;
    }
    if (const Entry *entry = entryOf(m_rows.at(index))) {
        open(*entry);
    }
}

void NgRecentView::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = rowAt(event->pos());
    if (index < 0 || m_rows.at(index).type == Row::Foot) {
        return;
    }
    const Entry *found = entryOf(m_rows.at(index));
    if (!found || found->path.isEmpty()) {
        return;
    }
    const Entry entry = *found;
    QMenu menu(this);
    if (!entry.group) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), ngTr("Open", "Apri"), this, [this, entry] {
            open(entry);
        });
    }
    menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), entry.group || entry.folder ? ngTr("Open the folder", "Apri la cartella") : ngTr("Open the folder it is in", "Apri la cartella che lo contiene"),
                   this, [this, entry] {
                       Q_EMIT folderRequested(QUrl::fromLocalFile(entry.group || entry.folder ? entry.path : QFileInfo(entry.path).path()));
                   });
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), ngTr("Copy the path", "Copia il percorso"), this, [entry] {
        QApplication::clipboard()->setText(entry.path);
    });
    menu.exec(event->globalPos());
}

#include "moc_ngrecentview.cpp"
