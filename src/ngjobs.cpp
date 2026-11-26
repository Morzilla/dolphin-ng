/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ngjobs.h"

#include "dolphintabwidget.h"
#include "kitemviews/kitemlistwidget.h"

#include <QDateTime>
#include <QTimer>
#include <QCoreApplication>
#include <QActionEvent>
#include <QMouseEvent>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QScrollArea>
#include <QPainterPath>
#include <QLineEdit>
#include <QGridLayout>
#include <QFrame>
#include <QEvent>
#include <QDir>
#include <QComboBox>
#include <QStandardPaths>
#include <QFileInfo>
#include <cmath>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMimeDatabase>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyleOptionToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace
{
QString elided(const QString &text, const QWidget *widget, int width)
{
    return widget->fontMetrics().elidedText(text, Qt::ElideMiddle, width);
}

QString tookText(qint64 ms)
{
    const qint64 s = qMax<qint64>(1, ms / 1000);
    return s < 60 ? ngTr("%1 s", "%1 s").arg(s) : s < 3600 ? ngTr("%1 min", "%1 min").arg((s + 30) / 60) : ngTr("%1 h %2 min", "%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text.toUpper(), parent);
    QFont font = label->font();
    font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.5));
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::PercentageSpacing, 106);
    label->setFont(font);
    label->setEnabled(false);
    label->setContentsMargins(4, 6, 4, 0);
    return label;
}
}

NgJobsButton::NgJobsButton(QWidget *parent)
    : QToolButton(parent)
    , m_menu(new QMenu(this))
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setPopupMode(QToolButton::InstantPopup);
    setMenu(m_menu);
    setToolTip(ngTr("Work in progress and history (middle click: the jobs tab)", "Lavori in corso e storico (clic centrale: la scheda dei lavori)"));
    m_list = new QWidget(m_menu);
    m_list->setMinimumWidth(400);
    m_rows = new QVBoxLayout(m_list);
    m_rows->setContentsMargins(8, 8, 8, 8);
    m_rows->setSpacing(6);
    m_action = new QWidgetAction(m_menu);
    m_action->setDefaultWidget(m_list);
    m_menu->addAction(m_action);
    connect(m_menu, &QMenu::aboutToShow, this, [this] {
        m_shape.clear();
        rebuild();
    });
    connect(NgActivity::self(), &NgActivity::changed, this, &NgJobsButton::refresh);
    refresh();
}

static QString ngJobsCount(int count)
{
    return count == 1 ? ngTr("1 job", "1 lavoro") : ngTr("%1 jobs", "%1 lavori").arg(count);
}

QSize NgJobsButton::sizeHint() const
{
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    const int count = m_running + m_waiting;
    return QSize(count > 0 ? 30 + QFontMetrics(bold).horizontalAdvance(ngJobsCount(count)) + 12 : 32, 26);
}

void NgJobsButton::mousePressEvent(QMouseEvent *event)
{
    // the middle button: straight to the jobs tab, without the list
    if (event->button() == Qt::MiddleButton) {
        if (auto *tabs = window()->findChild<DolphinTabWidget *>()) {
            tabs->ngOpenHistoryTab();
        }
        return;
    }
    QToolButton::mousePressEvent(event);
}

void NgJobsButton::refresh()
{
    int running = 0, waiting = 0;
    const auto jobs = NgActivity::self()->jobs();
    for (const auto &job : jobs) {
        job.queued ? ++waiting : ++running;
    }
    if (running != m_running || waiting != m_waiting) {
        m_running = running;
        m_waiting = waiting;
        updateGeometry();
    }
    update(); // (the arc turns)
    if (m_menu->isVisible()) {
        rebuild();
    }
}

void NgJobsButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int count = m_running + m_waiting;
    const bool busy = count > 0;
    const QColor ink = palette().color(QPalette::WindowText);
    const QColor accent = palette().color(QPalette::Highlight);
    const QRectF pill = QRectF(rect()).adjusted(0.5, 1.5, -0.5, -1.5);
    painter.setPen(Qt::NoPen);
    if (busy) {
        // at work: a pill of the accent running to the hue next to it, white on it: seen at a glance in the title row
        const qreal hue = accent.hslHueF() < 0 ? 0.58 : accent.hslHueF();
        const bool grey = accent.hslSaturationF() < 0.15;
        QLinearGradient fill(pill.topLeft(), pill.topRight());
        fill.setColorAt(0.0, grey ? accent : QColor::fromHslF(hue, 0.78, 0.52));
        fill.setColorAt(1.0, grey ? accent : QColor::fromHslF(std::fmod(hue + 0.17, 1.0), 0.66, 0.58));
        painter.setBrush(fill);
        painter.drawRoundedRect(pill, 11.5, 11.5);
    } else {
        QColor rest = ink;
        rest.setAlphaF((underMouse() || m_menu->isVisible()) ? 0.16 : 0.08);
        painter.setBrush(rest);
        painter.drawRoundedRect(pill, 11.5, 11.5);
    }
    // the sign: a ring; at work an arc turns on it, at rest the hands of a clock (the history)
    const QRectF ring(busy ? 9 : (width() - 14) / 2.0, height() / 2.0 - 7, 14, 14);
    QColor track = busy ? QColor(255, 255, 255, 90) : ink;
    if (!busy) {
        track.setAlphaF(0.70);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(track, 2));
    painter.drawEllipse(ring);
    if (m_running > 0) {
        painter.setPen(QPen(QColor(255, 255, 255), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(ring, qRound(-NgActivity::self()->phase() * 360 * 16) + 90 * 16, -110 * 16);
    } else if (!busy) {
        painter.setPen(QPen(track, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(ring.center(), ring.center() + QPointF(0, -4));
        painter.drawLine(ring.center(), ring.center() + QPointF(3, 1.5));
    }
    if (busy) {
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        painter.setFont(bold);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(QRectF(30, 0, width() - 36, height()), Qt::AlignVCenter | Qt::AlignLeft, ngJobsCount(count));
    }
}

void NgJobsButton::rebuild()
{
    const auto jobs = NgActivity::self()->jobs();
    QList<NgActivity::Done> today;
    const QDate now = QDate::currentDate();
    for (const auto &done : NgActivity::history(40)) {
        if (QDateTime::fromMSecsSinceEpoch(done.endedAt).date() == now && today.count() < 6) {
            today.append(done);
        }
    }
    QString shape;
    for (const auto &job : jobs) {
        shape += job.url.toString() + (job.queued ? QLatin1Char('q') : QLatin1Char('r')) + QLatin1Char('\n');
    }
    shape += QString::number(today.count()) + (today.isEmpty() ? QString() : QString::number(today.constFirst().endedAt));
    if (shape == m_shape) {
        // the same rows: only how far they are
        for (const auto &job : jobs) {
            if (job.queued) {
                continue; // (no bar of its own; one waiting on a file at work has that file's address)
            }
            for (const auto &[key, bar] : std::as_const(m_bars)) {
                if (key == job.url.toString()) {
                    bar->setRange(0, job.percent < 0 ? 0 : 100);
                    bar->setValue(qMax(0, job.percent));
                }
            }
            for (const auto &[key, figure] : std::as_const(m_figures)) {
                if (key == job.url.toString()) {
                    figure->setText(job.percent < 0 ? QString() : QStringLiteral("%1%").arg(job.percent));
                }
            }
        }
        m_menu->update(); // (on a see-through menu a label painted over its old figure: the whole sheet again)
        return;
    }
    m_shape = shape;
    m_bars.clear();
    m_figures.clear();
    while (QLayoutItem *item = m_rows->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    auto row = [this](const QString &title, const QString &detail, const QString &tip) {
        auto *holder = new QWidget(m_list);
        auto *line = new QHBoxLayout(holder);
        line->setContentsMargins(4, 2, 4, 2);
        line->setSpacing(10);
        auto *text = new QVBoxLayout;
        text->setSpacing(1);
        auto *name = new QLabel(title, holder);
        QFont bold = name->font();
        bold.setWeight(QFont::DemiBold);
        name->setFont(bold);
        auto *under = new QLabel(elided(detail, this, 300), holder);
        under->setEnabled(false);
        under->setToolTip(tip);
        text->addWidget(name);
        text->addWidget(under);
        line->addLayout(text, 1);
        m_rows->addWidget(holder);
        return std::pair<QWidget *, QVBoxLayout *>(holder, text);
    };
    {
        // the way to the jobs tab first, where it is always in reach (at the bottom it was cut off when the list grew
        // while it was open)
        auto *holder = new QWidget(m_list);
        auto *line = new QHBoxLayout(holder);
        line->setContentsMargins(4, 0, 0, 0);
        auto *title = new QLabel(ngTr("Jobs", "Lavori"), holder);
        QFont bold = title->font();
        bold.setWeight(QFont::DemiBold);
        title->setFont(bold);
        line->addWidget(title, 1);
        auto *all = new QPushButton(QIcon::fromTheme(QStringLiteral("view-history")), ngTr("History and details", "Storico e dettagli"), holder);
        connect(all, &QPushButton::clicked, this, [this] {
            m_menu->close();
            if (auto *tabs = window()->findChild<DolphinTabWidget *>()) {
                tabs->ngOpenHistoryTab();
            }
        });
        line->addWidget(all);
        m_rows->addWidget(holder);
    }
    bool running = false, waiting = false;
    for (const auto &job : jobs) {
        if (!job.queued && !running) {
            running = true;
            m_rows->addWidget(caption(ngTr("At work", "Attivi"), m_list));
        }
        if (job.queued && !waiting) {
            waiting = true;
            m_rows->addWidget(caption(ngTr("Queued", "In coda"), m_list));
        }
        const auto [holder, text] = row(job.label, job.url.fileName(), job.url.toLocalFile());
        auto *line = static_cast<QHBoxLayout *>(holder->layout());
        if (!job.queued) {
            auto *bar = new QProgressBar(holder);
            bar->setTextVisible(false);
            bar->setFixedHeight(6);
            bar->setRange(0, job.percent < 0 ? 0 : 100);
            bar->setValue(qMax(0, job.percent));
            text->addWidget(bar);
            m_bars.append({job.url.toString(), bar});
            auto *figure = new QLabel(job.percent < 0 ? QString() : QStringLiteral("%1%").arg(job.percent), holder);
            figure->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("100%")));
            figure->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            line->addWidget(figure);
            m_figures.append({job.url.toString(), figure});
        }
        auto *stop = new QToolButton(holder);
        stop->setIcon(QIcon::fromTheme(QStringLiteral("dialog-cancel")));
        stop->setAutoRaise(true);
        stop->setToolTip(job.queued ? ngTr("Take off the queue", "Togli dalla coda") : ngTr("Stop", "Ferma"));
        const QUrl url = job.url;
        connect(stop, &QToolButton::clicked, this, [url, queued = job.queued] {
            NgActivity::self()->cancel(url, queued);
        });
        line->addWidget(stop);
    }
    if (jobs.isEmpty()) {
        auto *none = new QLabel(ngTr("Nothing at work", "Nessun lavoro in corso"), m_list);
        none->setEnabled(false);
        none->setContentsMargins(4, 4, 4, 4);
        m_rows->addWidget(none);
    }
    if (!today.isEmpty()) {
        m_rows->addWidget(caption(ngTr("Ended today", "Finiti oggi"), m_list));
        for (const auto &done : std::as_const(today)) {
            const QString when = QLocale().toString(QDateTime::fromMSecsSinceEpoch(done.endedAt).time(), QLocale::ShortFormat);
            const QString result = done.results.isEmpty() ? QString() : QStringLiteral(" → ") + done.results.join(QStringLiteral(", "));
            const auto [holder, text] = row(done.label, done.url.fileName() + result + QStringLiteral(" · ") + when, done.url.toLocalFile());
            Q_UNUSED(text)
            auto *outcome = new QLabel(done.stopped ? ngTr("Stopped", "Fermato") : QStringLiteral("✓ ") + tookText(done.endedAt - done.startedAt), holder);
            outcome->setEnabled(!done.stopped);
            static_cast<QHBoxLayout *>(holder->layout())->addWidget(outcome);
        }
    }
    // the menu measured its one entry when it opened: told that it changed, so that it grows or shrinks with the list
    // (a moment later: the rows just added are shown by their layout in the next turn of the event loop, and count for
    // nothing in the size until then)
    QTimer::singleShot(0, this, [this] {
        m_rows->activate();
        m_list->setMinimumHeight(0);
        const QSize wanted = m_list->sizeHint().expandedTo(QSize(400, 0));
        m_list->setMinimumSize(wanted);
        m_list->resize(wanted);
        QActionEvent changed(QEvent::ActionChanged, m_action);
        QCoreApplication::sendEvent(m_menu, &changed);
        if (m_menu->isVisible()) {
            m_menu->resize(m_menu->sizeHint());
        }
    });

}

namespace
{
// "Home/Downloads" for a folder under the user's home, the path as it is elsewhere
QString shownFolder(const QUrl &url)
{
    QString folder = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toLocalFile();
    const QString home = QDir::homePath();
    if (folder == home) {
        return QStringLiteral("Home");
    }
    if (folder.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("Home") + folder.mid(home.size());
    }
    return folder;
}

QString kindOf(const QUrl &url)
{
    const QString suffix = QFileInfo(url.fileName()).suffix().toUpper();
    return suffix.isEmpty() || suffix.size() > 4 ? QStringLiteral("FILE") : suffix;
}

QColor inkOf(const QWidget *widget, qreal alpha)
{
    QColor ink = widget->palette().color(QPalette::WindowText);
    ink.setAlphaF(alpha);
    return ink;
}

// the card's sheet, as the cards of Dolphin NG's panels: a veil on the glass with Aurora, a tone of the text elsewhere
QColor sheetOf(const QWidget *widget, bool faint = false)
{
    const bool dark = ngIsDark(widget->palette());
    QColor sheet = ngAurora() ? ngAuroraVeil(dark) : (dark ? QColor(255, 255, 255, 18) : QColor(255, 255, 255, 150));
    if (faint) {
        sheet.setAlphaF(sheet.alphaF() * 0.55);
    }
    return sheet;
}

void paintKind(QPainter *painter, const QRectF &rect, const QString &kind, const QFont &base)
{
    const QColor colour = ngTagColour(kind);
    QLinearGradient fill(rect.topLeft(), rect.bottomRight());
    fill.setColorAt(0.0, colour.lighter(118));
    fill.setColorAt(1.0, colour.darker(118));
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, rect.width() * 0.26, rect.width() * 0.26);
    QFont font = base;
    font.setPointSizeF(qMax(6.0, base.pointSizeF() * (rect.width() > 40 ? 0.85 : 0.70)));
    font.setWeight(QFont::Bold);
    painter->setFont(font);
    painter->setPen(QColor(255, 255, 255));
    painter->drawText(rect, Qt::AlignCenter, kind);
}

// The file's icon from the icon theme, as the file views show it (worked out once for each file: by its name, and one
// look at the disk to tell a folder); the coloured tile with the kind's letters only when the theme has no icon for it
void paintFile(QPainter *painter, const QRectF &rect, const QUrl &url, const QFont &base)
{
    static QHash<QString, QIcon> known;
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
    auto it = known.constFind(path);
    if (it == known.constEnd()) {
        if (known.size() > 4000) {
            known.clear();
        }
        QIcon icon;
        if (url.isLocalFile() && QFileInfo(path).isDir()) {
            icon = QIcon::fromTheme(QStringLiteral("folder"));
        } else {
            const QMimeType mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension);
            icon = QIcon::fromTheme(mime.iconName(), QIcon::fromTheme(mime.genericIconName()));
        }
        it = known.insert(path, icon);
    }
    if (it->isNull()) {
        paintKind(painter, rect, kindOf(url), base);
        return;
    }
    it->paint(painter, rect.toAlignedRect());
}

// "Action  Home/Folder/name" on one line: the action and the name in the text colour, the folder fainter; the name is
// what gives way when the line is too long
void paintTitle(QPainter *painter, const QWidget *widget, const QRectF &rect, const QString &action, const QUrl &url, const QFont &font)
{
    QFont bold = font;
    bold.setWeight(QFont::DemiBold);
    const QFontMetricsF boldFm(bold), plainFm(font);
    qreal x = rect.left();
    painter->setFont(bold);
    painter->setPen(widget->palette().color(QPalette::WindowText));
    painter->drawText(QRectF(x, rect.top(), rect.width(), rect.height()), Qt::AlignLeft | Qt::AlignVCenter, action);
    x += boldFm.horizontalAdvance(action) + boldFm.horizontalAdvance(QLatin1Char(' ')) * 1.5;
    const QString folder = shownFolder(url) + QLatin1Char('/');
    const qreal room = rect.right() - x;
    const qreal nameWidth = boldFm.horizontalAdvance(url.fileName());
    const QString shownPath = plainFm.elidedText(folder, Qt::ElideLeft, qMax(40.0, room - nameWidth));
    painter->setFont(font);
    painter->setPen(inkOf(widget, 0.60));
    painter->drawText(QRectF(x, rect.top(), room, rect.height()), Qt::AlignLeft | Qt::AlignVCenter, shownPath);
    x += plainFm.horizontalAdvance(shownPath);
    painter->setFont(bold);
    painter->setPen(widget->palette().color(QPalette::WindowText));
    painter->drawText(QRectF(x, rect.top(), rect.right() - x, rect.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      boldFm.elidedText(url.fileName(), Qt::ElideMiddle, qMax(30.0, rect.right() - x)));
}

QToolButton *roundButton(QWidget *parent, const QString &icon, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setIcon(QIcon::fromTheme(icon));
    button->setAutoRaise(true);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(30, 30);
    return button;
}

// a quick action at work, or waiting: a card that fills with how far it is
class RunCard : public QWidget
{
public:
    RunCard(const NgActivity::Job &job, QWidget *parent)
        : QWidget(parent)
        , m_url(job.url)
        , m_label(job.label)
        , m_queued(job.queued)
    {
        setMinimumHeight(84);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QUrl m_url;
    QString m_label;
    bool m_queued;
    int m_room = 0; // what the buttons take at the right

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const NgActivity::Entry entry = NgActivity::self()->entry(m_url);
        const int percent = m_queued ? -1 : entry.percent;
        const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape;
        shape.addRoundedRect(card, 14, 14);
        painter.fillPath(shape, sheetOf(this, m_queued));
        const QColor accent = palette().color(QPalette::Highlight);
        const qreal hue = accent.hslHueF() < 0 ? 0.58 : accent.hslHueF();
        const bool grey = accent.hslSaturationF() < 0.15;
        const QColor first = grey ? accent : QColor::fromHslF(hue, 0.85, ngIsDark(palette()) ? 0.66 : 0.50);
        const QColor second = grey ? accent : QColor::fromHslF(std::fmod(hue + 0.17, 1.0), 0.80, ngIsDark(palette()) ? 0.72 : 0.56);
        if (percent > 0) {
            // how far it is, as a wash of the accent over the card's left part
            painter.save();
            painter.setClipPath(shape);
            QLinearGradient wash(card.topLeft(), card.topRight());
            QColor a = first, b = second;
            a.setAlphaF(0.18);
            b.setAlphaF(0.10);
            wash.setColorAt(0.0, a);
            wash.setColorAt(1.0, b);
            painter.fillRect(QRectF(card.left(), card.top(), card.width() * percent / 100.0, card.height()), wash);
            painter.restore();
        }
        paintFile(&painter, QRectF(14, (height() - 54) / 2.0, 54, 54), m_url, font());
        const qreal left = 82, right = width() - m_room - 96;
        QFont large = font();
        large.setPointSizeF(font().pointSizeF() * 1.12);
        paintTitle(&painter, this, QRectF(left, 12, right - left, 24), m_label, m_url, large);
        // what it is making, or why it waits
        QStringList made;
        const auto links = NgActivity::self()->links();
        for (const auto &[child, parent] : links) {
            if (parent == m_url) {
                made.append(child.fileName());
            }
        }
        QFont small = font();
        small.setPointSizeF(qMax(7.0, font().pointSizeF() - 0.5));
        painter.setFont(small);
        painter.setPen(inkOf(this, 0.60));
        const QString under = m_queued ? ngTr("queued: it starts when the work before it has ended", "in coda: parte quando finisce il lavoro prima")
                                       : made.isEmpty() ? QString() : QStringLiteral("→ ") + made.join(QStringLiteral(", "));
        painter.drawText(QRectF(left, 36, right - left, 18), Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(small).elidedText(under, Qt::ElideRight, right - left));
        if (!m_queued) {
            const QRectF bar(left, 62, right - left, 6);
            painter.setPen(Qt::NoPen);
            painter.setBrush(inkOf(this, 0.13));
            painter.drawRoundedRect(bar, 3, 3);
            QLinearGradient fill(bar.topLeft(), bar.topRight());
            fill.setColorAt(0.0, first);
            fill.setColorAt(1.0, second);
            painter.setBrush(fill);
            if (percent >= 0) {
                painter.drawRoundedRect(QRectF(bar.left(), bar.top(), qMax(6.0, bar.width() * percent / 100.0), bar.height()), 3, 3);
            } else {
                // no figure: a piece that runs along the bar
                // (to the end and back, slowing at each end: started again from the left it jumped)
                const qreal phase = NgActivity::self()->phase();
                const qreal there = phase < 0.5 ? phase * 2 : 2 - phase * 2;
                const qreal at = there * there * (3 - 2 * there);
                painter.drawRoundedRect(QRectF(bar.left() + (bar.width() - 60) * at, bar.top(), 60, bar.height()), 3, 3);
            }
        }
        // the figure and the time left, from how long it took so far
        const QRectF figure(right + 8, 14, 84, 30);
        QFont big = font();
        big.setPointSizeF(font().pointSizeF() * 1.7);
        big.setWeight(QFont::Light);
        painter.setFont(m_queued ? font() : big);
        painter.setPen(m_queued ? inkOf(this, 0.60) : palette().color(QPalette::WindowText));
        painter.drawText(figure, Qt::AlignRight | Qt::AlignVCenter, m_queued ? ngTr("queued", "in coda") : percent >= 0 ? QStringLiteral("%1%").arg(percent) : QString());
        if (!m_queued && percent > 2 && entry.startedAt > 0) {
            const qint64 took = QDateTime::currentMSecsSinceEpoch() - entry.startedAt;
            const qint64 left = took * (100 - percent) / percent / 1000;
            painter.setFont(small);
            painter.setPen(inkOf(this, 0.60));
            painter.drawText(QRectF(right + 8, 46, 84, 18), Qt::AlignRight | Qt::AlignVCenter,
                             ngTr("%1:%2 left", "ancora %1:%2").arg(left / 60).arg(left % 60, 2, 10, QLatin1Char('0')));
        }
    }
};

// a quick action that ended: its time on the line, the action with the file and its path, what it made, how it went
class DoneRow : public QWidget
{
public:
    DoneRow(const NgActivity::Done &done, bool withDay, bool first, bool last, QWidget *parent)
        : QWidget(parent)
        , m_done(done)
        , m_withDay(withDay)
        , m_first(first)
        , m_last(last)
    {
        setFixedHeight(50);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_Hover, true);
    }
    NgActivity::Done m_done;
    bool m_withDay, m_first, m_last;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(sheetOf(this, true));
            painter.drawRoundedRect(QRectF(26, 2, width() - 28, height() - 4), 12, 12);
        }
        // the line of time and this entry's dot on it
        const qreal middle = height() / 2.0;
        painter.setPen(QPen(inkOf(this, 0.30), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(11, m_first ? middle : 0), QPointF(11, m_last ? middle : height()));
        // (a full dot with a light rim: on glass a small flat one was lost against the line and the background)
        painter.setPen(QPen(ngIsDark(palette()) ? QColor(255, 255, 255, 60) : QColor(255, 255, 255, 235), 2));
        // (the accent at full strength: Aurora's light scheme has a pale selection colour, a dot of it was a blank ring)
        QColor dot = palette().color(QPalette::Highlight);
        if (dot.hslSaturationF() >= 0.15) {
            dot = QColor::fromHslF(dot.hslHueF(), qMax(dot.hslSaturationF(), 0.75), ngIsDark(palette()) ? 0.64 : 0.50);
        }
        painter.setBrush(m_done.stopped ? QColor(0xf0, 0xa0, 0x4b) : dot);
        painter.drawEllipse(QPointF(11, middle), 6.5, 6.5);
        const QDateTime ended = QDateTime::fromMSecsSinceEpoch(m_done.endedAt);
        const QString when = m_withDay ? ngAgoText(ended) + QLatin1Char(' ') + QLocale().toString(ended.time(), QLocale::ShortFormat) : QLocale().toString(ended.time(), QLocale::ShortFormat);
        const qreal timeWidth = m_withDay ? 118 : 54;
        painter.setFont(font());
        painter.setPen(inkOf(this, 0.60));
        painter.drawText(QRectF(36, 0, timeWidth, height()), Qt::AlignLeft | Qt::AlignVCenter, when);
        const qreal chip = 36 + timeWidth + 8;
        paintFile(&painter, QRectF(chip, middle - 18, 36, 36), m_done.url, font());
        const QString outcome = m_done.stopped ? ngTr("Stopped", "Fermato") : QStringLiteral("✓ ") + tookText(m_done.endedAt - m_done.startedAt);
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        const qreal outcomeWidth = QFontMetricsF(bold).horizontalAdvance(outcome) + 8;
        const qreal left = chip + 46, right = width() - 44 - outcomeWidth;
        // what it made under the title; a quick action that left nothing says so; a deletion has no second line, and
        // its title stands level with the icon
        const bool fileWork = m_done.id.startsWith(QLatin1String("io-")) || m_done.id.startsWith(QLatin1String("dl-"));
        const QString made = !m_done.results.isEmpty() ? QStringLiteral("→ ") + m_done.results.join(QStringLiteral(", ")) : fileWork ? QString() : ngTr("nothing made", "nessun risultato");
        paintTitle(&painter, this, QRectF(left, made.isEmpty() ? middle - 10 : 6, right - left, 20), m_done.label, m_done.url, font());
        QFont small = font();
        small.setPointSizeF(qMax(7.0, font().pointSizeF() - 0.5));
        painter.setFont(small);
        painter.setPen(inkOf(this, 0.60));
        painter.drawText(QRectF(left, 26, right - left, 18), Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(small).elidedText(made, Qt::ElideRight, right - left));
        painter.setFont(bold);
        painter.setPen(m_done.stopped ? QColor(0xf0, 0xa0, 0x4b) : (ngIsDark(palette()) ? QColor(0x5a, 0xd1, 0x8f) : QColor(0x1f, 0x9a, 0x5c)));
        painter.drawText(QRectF(right, 0, outcomeWidth, height()), Qt::AlignRight | Qt::AlignVCenter, outcome);
    }
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
            update();
        }
        return QWidget::event(event);
    }
};

QWidget *sectionCaption(const QString &text, QWidget *parent)
{
    auto *holder = new QWidget(parent);
    auto *line = new QHBoxLayout(holder);
    line->setContentsMargins(0, 8, 0, 2);
    line->setSpacing(10);
    line->addWidget(caption(text, holder));
    auto *rule = new QFrame(holder);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QStringLiteral("background: rgba(128, 128, 128, 0.30); border: none;"));
    line->addWidget(rule, 1);
    return holder;
}
}

NgHistoryPage::NgHistoryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 10, 8, 8);
    layout->setSpacing(6);
    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    auto *title = new QLabel(ngTr("Jobs", "Lavori"), this);
    QFont large = title->font();
    large.setPointSizeF(large.pointSizeF() * 1.6);
    title->setFont(large);
    top->addWidget(title);
    m_status = new QLabel(this);
    m_status->setEnabled(false);
    top->addWidget(m_status);
    top->addStretch(1);
    m_grouping = new QComboBox(this);
    m_grouping->addItem(ngTr("By day", "Per giorno"));
    m_grouping->addItem(ngTr("By action", "Per azione"));
    m_grouping->setCurrentIndex(ngSetting("JOBS_GROUPING") == QLatin1String("action") ? 1 : 0);
    top->addWidget(m_grouping);
    auto *clear = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-history")), ngTr("Clear history", "Svuota storico"), this);
    top->addWidget(clear);
    layout->addLayout(top);
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    m_content = new QWidget(scroll);
    m_content->setAutoFillBackground(false);
    m_rows = new QVBoxLayout(m_content);
    m_rows->setContentsMargins(0, 0, 10, 12);
    m_rows->setSpacing(2);
    scroll->setWidget(m_content);
    layout->addWidget(scroll, 1);
    connect(clear, &QPushButton::clicked, this, [this] {
        NgActivity::clearHistory();
        rebuild(true);
    });
    connect(m_grouping, &QComboBox::currentIndexChanged, this, [this](int index) {
        KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
        group.writeEntry("JOBS_GROUPING", index == 1 ? QStringLiteral("action") : QStringLiteral("day"));
        group.sync();
        ngSettingsInvalidate();
        rebuild(true);
    });
    connect(NgActivity::self(), &NgActivity::changed, this, [this] {
        if (isVisible()) {
            rebuild();
        }
    });
    rebuild(true);
}

void NgHistoryPage::setSearch(const QString &text)
{
    if (m_wanted != text.trimmed()) {
        m_wanted = text.trimmed();
        rebuild(true);
    }
}

void NgHistoryPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    rebuild(true);
}

void NgHistoryPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // (two cards side by side need room for the action, the path and the name)
    if ((width() < 980) != m_narrow) {
        m_narrow = width() < 980;
        rebuild(true);
    }
}

void NgHistoryPage::showInDolphin(const QUrl &url)
{
    // its folder in a tab of this window, the file selected (the tab that shows the folder already, or a new one)
    if (auto *tabs = window()->findChild<DolphinTabWidget *>()) {
        tabs->openFiles({url}, false);
    }
}

void NgHistoryPage::rebuild(bool force)
{
    const auto jobs = NgActivity::self()->jobs();
    const QFileInfo file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/ng-history.tsv"));
    QString shape = QString::number(file.exists() ? file.lastModified().toMSecsSinceEpoch() + file.size() : 0);
    int running = 0, waiting = 0;
    for (const auto &job : jobs) {
        shape += QLatin1Char('\n') + job.url.toString() + (job.queued ? QLatin1Char('q') : QLatin1Char('r'));
        job.queued ? ++waiting : ++running;
    }
    m_status->setText(running + waiting == 0 ? ngTr("nothing at work", "nessun lavoro in corso")
                                             : waiting == 0 ? ngTr("%1 at work", "%1 attivi").arg(running) : ngTr("%1 at work · %2 queued", "%1 attivi · %2 in coda").arg(running).arg(waiting));
    if (!force && shape == m_shape) {
        for (QWidget *card : std::as_const(m_cards)) {
            card->update(); // (how far it is)
        }
        return;
    }
    m_shape = shape;
    m_cards.clear();
    while (QLayoutItem *item = m_rows->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    if (!jobs.isEmpty()) {
        m_rows->addWidget(sectionCaption(ngTr("At work", "In corso"), m_content));
        auto *holder = new QWidget(m_content);
        auto *grid = new QGridLayout(holder);
        grid->setContentsMargins(0, 4, 0, 4);
        grid->setSpacing(12);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        int at = 0;
        for (const auto &job : jobs) {
            auto *card = new RunCard(job, holder);
            auto *buttons = new QHBoxLayout(card);
            buttons->setContentsMargins(0, 0, 12, 0);
            buttons->setSpacing(6);
            buttons->addStretch(1);
            const QUrl url = job.url;
            auto *show = roundButton(card, QStringLiteral("document-open-folder"), ngTr("Show in Dolphin NG", "Mostra in Dolphin NG"));
            connect(show, &QToolButton::clicked, this, [this, url] {
                showInDolphin(url);
            });
            buttons->addWidget(show);
            auto *stop = roundButton(card, QStringLiteral("dialog-cancel"), job.queued ? ngTr("Take off the queue", "Togli dalla coda") : ngTr("Stop", "Ferma"));
            connect(stop, &QToolButton::clicked, this, [url, queued = job.queued] {
                NgActivity::self()->cancel(url, queued);
            });
            buttons->addWidget(stop);
            card->m_room = 30 * 2 + 6 + 12;
            m_narrow ? grid->addWidget(card, at, 0, 1, 2) : grid->addWidget(card, at / 2, at % 2);
            m_cards.append(card);
            ++at;
        }
        m_rows->addWidget(holder);
    }
    // the history: the last first, in groups (a day, or an action); within a group the last first as well
    const QString wanted = m_wanted;
    QList<NgActivity::Done> all;
    for (const auto &done : NgActivity::history(2000)) {
        if (wanted.isEmpty() || done.label.contains(wanted, Qt::CaseInsensitive) || done.url.toLocalFile().contains(wanted, Qt::CaseInsensitive)
            || done.results.join(QLatin1Char(' ')).contains(wanted, Qt::CaseInsensitive)) {
            all.append(done);
        }
    }
    const bool byAction = m_grouping->currentIndex() == 1;
    QStringList order;
    QHash<QString, QList<NgActivity::Done>> groups;
    const QDate today = QDate::currentDate();
    for (const auto &done : std::as_const(all)) {
        const QDate day = QDateTime::fromMSecsSinceEpoch(done.endedAt).date();
        QString name = byAction ? done.label
            : day == today ? ngTr("Today", "Oggi") : day == today.addDays(-1) ? ngTr("Yesterday", "Ieri") : QLocale().toString(day, QLocale::LongFormat);
        if (!groups.contains(name)) {
            order.append(name);
        }
        groups[name].append(done);
    }
    for (const QString &name : std::as_const(order)) {
        const auto entries = groups.value(name);
        m_rows->addWidget(sectionCaption(byAction ? QStringLiteral("%1 · %2").arg(name).arg(entries.count()) : name, m_content));
        for (int i = 0; i < entries.count(); ++i) {
            auto *row = new DoneRow(entries.at(i), byAction, i == 0, i == entries.count() - 1, m_content);
            auto *buttons = new QHBoxLayout(row);
            buttons->setContentsMargins(0, 0, 8, 0);
            buttons->addStretch(1);
            const QUrl url = entries.at(i).url;
            const QStringList results = entries.at(i).results;
            const QUrl where = entries.at(i).where;
            auto *show = roundButton(row, QStringLiteral("document-open-folder"), ngTr("Show in Dolphin NG", "Mostra in Dolphin NG"));
            connect(show, &QToolButton::clicked, this, [this, url, results, where] {
                // a copy or a move: the item where it went (in that folder under its own name, or the place itself)
                if (where.isLocalFile()) {
                    const QString inside = where.toLocalFile() + QLatin1Char('/') + url.fileName();
                    if (QFileInfo::exists(inside) || QFileInfo::exists(where.toLocalFile())) {
                        showInDolphin(QFileInfo::exists(inside) ? QUrl::fromLocalFile(inside) : where);
                        return;
                    }
                }
                // what a quick action made, when it is still there (the first result, beside its source); otherwise
                // the source
                const QUrl made = (results.isEmpty() || where.isValid()) ? QUrl() : QUrl::fromLocalFile(url.adjusted(QUrl::RemoveFilename).toLocalFile() + results.constFirst());
                showInDolphin(made.isValid() && QFileInfo::exists(made.toLocalFile()) ? made : url);
            });
            buttons->addWidget(show);
            m_rows->addWidget(row);
        }
    }
    if (all.isEmpty()) {
        auto *none = new QLabel(wanted.isEmpty() ? ngTr("No quick action has ended yet: they will be listed here.", "Nessuna azione rapida è ancora finita: compariranno qui.")
                                                 : ngTr("Nothing in the history matches.", "Niente nello storico corrisponde."), m_content);
        none->setEnabled(false);
        none->setContentsMargins(4, 16, 4, 4);
        m_rows->addWidget(none);
    }
    m_rows->addStretch(1);
}

#include "moc_ngjobs.cpp"
