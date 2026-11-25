/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ngjobs.h"

#include "kitemviews/kitemlistwidget.h"

#include <QDateTime>
#include <cmath>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyleOptionToolButton>
#include <QTreeWidget>
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
    setToolTip(ngTr("Work in progress and history", "Lavori in corso e storico"));
    m_list = new QWidget(m_menu);
    m_list->setMinimumWidth(400);
    m_rows = new QVBoxLayout(m_list);
    m_rows->setContentsMargins(8, 8, 8, 8);
    m_rows->setSpacing(6);
    auto *action = new QWidgetAction(m_menu);
    action->setDefaultWidget(m_list);
    m_menu->addAction(action);
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
        connect(stop, &QToolButton::clicked, this, [url] {
            NgActivity::self()->cancel(url);
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
    auto *all = new QPushButton(ngTr("Whole history…", "Storico completo…"), m_list);
    all->setFlat(true);
    connect(all, &QPushButton::clicked, this, [this] {
        m_menu->close();
        auto *dialog = new NgHistoryDialog(window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    m_rows->addWidget(all, 0, Qt::AlignLeft);
    m_list->adjustSize();
}

NgHistoryDialog::NgHistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(ngTr("History of quick actions", "Storico delle azioni rapide"));
    resize(820, 480);
    auto *layout = new QVBoxLayout(this);
    auto *tree = new QTreeWidget(this);
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setHeaderLabels({ngTr("When", "Quando"), ngTr("Action", "Azione"), ngTr("File", "File"), ngTr("Made", "Risultato"), ngTr("Took", "Durata"), ngTr("Folder", "Cartella")});
    auto fill = [tree] {
        tree->clear();
        const auto all = NgActivity::history(1000);
        for (const auto &done : all) {
            const QDateTime ended = QDateTime::fromMSecsSinceEpoch(done.endedAt);
            auto *item = new QTreeWidgetItem(tree);
            item->setText(0, QLocale().toString(ended, QLocale::ShortFormat));
            item->setText(1, done.label);
            item->setText(2, done.url.fileName());
            item->setText(3, done.results.join(QStringLiteral(", ")));
            item->setText(4, done.stopped ? ngTr("Stopped", "Fermato") : tookText(done.endedAt - done.startedAt));
            item->setText(5, done.url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toLocalFile());
            item->setToolTip(2, done.url.toLocalFile());
        }
        for (int column = 0; column < 5; ++column) {
            tree->resizeColumnToContents(column);
        }
    };
    fill();
    layout->addWidget(tree);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *clear = buttons->addButton(ngTr("Clear history", "Svuota storico"), QDialogButtonBox::ResetRole);
    connect(clear, &QPushButton::clicked, this, [fill] {
        NgActivity::clearHistory();
        fill();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}

#include "moc_ngjobs.cpp"
