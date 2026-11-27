/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ngactionspage.h"
#include "kitemviews/kitemlistwidget.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <KShell>
#include <KIconButton>
#include <KOpenWithDialog>
#include <KService>
#include <KSharedConfig>

#include <QCheckBox>
#include <QDateTime>
#include <QFontDatabase>
#include <climits>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
const char *const Group = "DolphinNG QuickActions";

QIcon actionIcon(const QString &name)
{
    if (name == QLatin1String("ng-pin")) {
        return QIcon::fromTheme(QStringLiteral("pin"), QIcon::fromTheme(QStringLiteral("window-pin")));
    }
    return QIcon::fromTheme(name, QIcon::fromTheme(QStringLiteral("system-run")));
}

QColor kindColour(const QString &kind)
{
    if (kind == QLatin1String("image")) {
        return ngCategoryTint(NgKind::Image);
    } else if (kind == QLatin1String("pdf")) {
        return QColor(238, 108, 98);
    } else if (kind == QLatin1String("audio")) {
        return QColor(214, 140, 214);
    } else if (kind == QLatin1String("video")) {
        return ngCategoryTint(NgKind::Archive); // the warm orange the video cards have
    } else if (kind == QLatin1String("archive")) {
        return QColor(214, 178, 96);
    } else if (kind == QLatin1String("all")) {
        return ngCategoryTint(NgKind::Other);
    }
    return ngCategoryTint(NgKind::Media);
}

QIcon dot(const QColor &colour)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(QRectF(4, 4, 8, 8), 2.5, 2.5);
    return QIcon(pixmap);
}

// The field codes of a desktop file's Exec line (the Desktop Entry specification, "The Exec key"), for a quick
// action: it runs on one file, the card's, so %f and %u are that file and %F and %U a list of one. %i is the
// icon's option (--icon NAME, nothing without an icon), %c the name, %k the desktop file's location, %% a percent
// sign; the deprecated codes (%d %D %n %N %v %m) and unknown ones are removed, as the specification asks
QString commandFromExec(const QString &exec, const QString &icon, const QString &name, const QString &location)
{
    QString command;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        if (exec.at(i) != QLatin1Char('%') || i + 1 >= exec.size()) {
            command += exec.at(i);
            continue;
        }
        switch (exec.at(++i).toLatin1()) {
        case 'f':
        case 'F':
        case 'u':
        case 'U':
            command += QStringLiteral("{file}");
            break;
        case 'i':
            if (!icon.isEmpty()) {
                command += QStringLiteral("--icon ") + KShell::quoteArg(icon);
            }
            break;
        case 'c':
            command += KShell::quoteArg(name);
            break;
        case 'k':
            command += KShell::quoteArg(location);
            break;
        case '%':
            command += QLatin1Char('%');
            break;
        default:
            break;
        }
    }
    return command.trimmed();
}
} // namespace

/** The pill a card will show, drawn from the editor's lists. */
class NgPillPreview : public QWidget
{
public:
    struct Item {
        QIcon icon;
        QString label; // only the main one shows it
        bool separator = false;
    };
    QList<Item> items;

    explicit NgPillPreview(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(fontMetrics().height() + 26);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int height = fontMetrics().height() + 18;
        const qreal side = height - 8;
        // its width first
        qreal width = 4;
        for (const Item &item : std::as_const(items)) {
            width += item.separator ? 7 : (item.label.isEmpty() ? side + 2 : side + fontMetrics().horizontalAdvance(item.label) + 16);
        }
        width += 2;
        const QRectF pill(1, (this->height() - height) / 2.0, qMin<qreal>(width, this->width() - 2), height);
        // as the pill over a card's picture (KStandardItemListWidget::paintNgActionPill): dark on a dark scheme, near
        // white on a light one. Always dark, it held the light scheme's dark icons: black on black
        const bool dark = ngIsDark(palette());
        const QColor text = palette().color(QPalette::WindowText);
        auto ink = [&text](qreal alpha) {
            QColor c = text;
            c.setAlphaF(alpha);
            return c;
        };
        painter.setPen(QPen(ink(0.22), 1));
        painter.setBrush(dark ? QColor(10, 11, 13, 240) : QColor(252, 252, 252, 240));
        painter.drawRoundedRect(pill, height / 2.0, height / 2.0);
        painter.setClipRect(pill.adjusted(2, 0, -2, 0));
        qreal x = pill.left() + 4;
        const qreal y = pill.top() + 4;
        for (const Item &item : std::as_const(items)) {
            if (item.separator) {
                painter.setPen(QPen(ink(0.18), 1));
                painter.drawLine(QPointF(x + 3, y + side * 0.2), QPointF(x + 3, y + side * 0.8));
                x += 7;
                continue;
            }
            const qreal itemWidth = item.label.isEmpty() ? side : side + fontMetrics().horizontalAdvance(item.label) + 14;
            if (!item.label.isEmpty()) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink(0.10));
                painter.drawRoundedRect(QRectF(x, y, itemWidth, side), side / 2.0, side / 2.0);
            }
            const int iconSide = 16;
            item.icon.paint(&painter, QRect(qRound(x + (side - iconSide) / 2.0), qRound(y + (side - iconSide) / 2.0), iconSide, iconSide));
            if (!item.label.isEmpty()) {
                painter.setPen(text);
                painter.drawText(QRectF(x + side - 2, y, itemWidth - side, side), Qt::AlignVCenter | Qt::AlignLeft, item.label);
            }
            x += itemWidth + 2;
        }
    }
};

NgActionsPage::NgActionsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 4, 0, 0);
    outer->setSpacing(12);

    // ---- the pill, as a card will show it
    auto *top = new QHBoxLayout();
    auto *pillCaption = new QLabel(ngTr("On a card:", "Sulla card:"), this);
    pillCaption->setEnabled(false);
    top->addWidget(pillCaption);
    m_pill = new NgPillPreview(this);
    top->addWidget(m_pill, 1);
    outer->addLayout(top);
    auto *intro = new QLabel(ngTr("The pill shows the first action with its name, then the applications and “Open with”, then the other actions of the "
                                  "kind, then cut, copy and pin. Drag to change the order; the check switches one off.",
                                  "La pill mostra la prima azione col nome, poi le applicazioni e “Apri con”, poi le altre azioni del tipo, poi taglia, "
                                  "copia e fissa. Trascina per cambiare l'ordine; la spunta spegne un'azione."),
                             this);
    intro->setWordWrap(true);
    intro->setEnabled(false);
    outer->addWidget(intro);

    // how the work of quick actions runs: all together (as it starts), or one after the other
    m_oneAtATime = new QCheckBox(ngTr("Run quick actions one at a time (the others wait in the queue)", "Esegui le azioni rapide una alla volta (le altre aspettano in coda)"), this);
    m_oneAtATime->setToolTip(ngTr("Off: every action starts at once, side by side. On: a new action waits until the one at work has ended; the title bar's jobs button shows the queue.",
                                  "Spento: ogni azione parte subito, in parallelo. Acceso: una nuova azione aspetta che quella al lavoro sia finita; il pulsante dei lavori nella barra del titolo mostra la coda."));
    m_oneAtATime->setChecked(ngSetting("JOBS_SERIES") == QLatin1String("1"));
    connect(m_oneAtATime, &QCheckBox::toggled, this, [this] {
        Q_EMIT changed();
    });
    outer->addWidget(m_oneAtATime);

    auto *panes = new QHBoxLayout();
    panes->setSpacing(14);
    outer->addLayout(panes, 1);

    // ---- the kinds
    auto *kindsColumn = new QVBoxLayout();
    auto *kindsTitle = new QLabel(ngTr("Kinds", "Tipi"), this);
    kindsTitle->setEnabled(false);
    kindsColumn->addWidget(kindsTitle);
    m_kindList = new QListWidget(this);
    m_kindList->setFixedWidth(150);
    kindsColumn->addWidget(m_kindList, 1);
    auto *addKindButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), ngTr("Custom kind…", "Tipo personalizzato…"), this);
    addKindButton->setToolTip(ngTr("By extension or type: *.md, text/markdown", "Per estensione o tipo: *.md, text/markdown"));
    kindsColumn->addWidget(addKindButton);
    panes->addLayout(kindsColumn);

    // ---- the actions of the kind
    auto *listColumn = new QVBoxLayout();
    m_listTitle = new QLabel(this);
    m_listTitle->setEnabled(false);
    listColumn->addWidget(m_listTitle);
    m_list = new QListWidget(this);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setIconSize(QSize(20, 20));
    m_list->setSpacing(1);
    listColumn->addWidget(m_list, 1);
    auto *adds = new QHBoxLayout();
    auto *addButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), ngTr("Add", "Aggiungi"), this);
    auto *addMenu = new QMenu(addButton);
    QAction *newButton = addMenu->addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), ngTr("A command…", "Un comando…"));
    QAction *fromMenu = addMenu->addAction(QIcon::fromTheme(QStringLiteral("application-menu")), ngTr("From a service menu…", "Da un menu di servizio…"));
    QAction *fromApp = addMenu->addAction(QIcon::fromTheme(QStringLiteral("applications-other")), ngTr("An application…", "Un'applicazione…"));
    addButton->setMenu(addMenu);
    adds->addWidget(addButton);
    adds->addStretch();
    listColumn->addLayout(adds);
    panes->addLayout(listColumn, 1);

    // ---- the chosen action
    m_editor = new QWidget(this);
    auto *editorColumn = new QVBoxLayout(m_editor);
    editorColumn->setContentsMargins(0, 0, 10, 0);
    m_editorTitle = new QLabel(m_editor);
    m_editorTitle->setEnabled(false);
    editorColumn->addWidget(m_editorTitle);
    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_name = new QLineEdit(m_editor);
    form->addRow(ngTr("Name:", "Nome:"), m_name);
    m_icon = new KIconButton(m_editor);
    m_icon->setIconSize(22);
    m_icon->setIconType(KIconLoader::Small, KIconLoader::Action);
    form->addRow(ngTr("Icon:", "Icona:"), m_icon);
    m_command = new QPlainTextEdit(m_editor);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_command->setFont(mono);
    m_command->setFixedHeight(QFontMetrics(mono).height() * 4 + 12);
    m_command->setTabChangesFocus(true);
    form->addRow(ngTr("Command:", "Comando:"), m_command);
    m_placeholders = new QWidget(m_editor);
    auto *placeholderRow = new QHBoxLayout(m_placeholders);
    placeholderRow->setContentsMargins(0, 0, 0, 0);
    placeholderRow->setSpacing(4);
    const QList<std::pair<QString, QString>> placeholders{{QStringLiteral("{file}"), ngTr("the file", "il file")},
                                                          {QStringLiteral("{name}"), ngTr("its name without the extension", "il nome senza estensione")},
                                                          {QStringLiteral("{ext}"), ngTr("the extension", "l'estensione")},
                                                          {QStringLiteral("{dir}"), ngTr("its folder", "la sua cartella")},
                                                          {QStringLiteral("{choice}"), ngTr("what was picked in the menu below", "la voce scelta nel menu qui sotto")}};
    for (const auto &[text, tip] : placeholders) {
        auto *button = new QToolButton(m_placeholders);
        button->setText(text);
        button->setFont(mono);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this, [this, text] {
            m_command->insertPlainText(text);
            m_command->setFocus();
        });
        placeholderRow->addWidget(button);
    }
    placeholderRow->addStretch();
    form->addRow(QString(), m_placeholders);
    m_choices = new QLineEdit(m_editor);
    m_choices->setToolTip(ngTr("Optional: a small menu on the pill; what is picked goes in {choice}", "Facoltativo: un piccolo menu sulla pill; la voce scelta va in {choice}"));
    m_choices->setPlaceholderText(QStringLiteral("75%, 50%, 25%"));
    form->addRow(ngTr("Menu:", "Menu:"), m_choices);
    editorColumn->addLayout(form);
    m_isMain = new QCheckBox(ngTr("Main (first, with its name)", "Principale (prima, col nome)"), m_editor);
    editorColumn->addWidget(m_isMain);
    m_confirm = new QCheckBox(ngTr("Ask before running it", "Chiedi prima di eseguirla"), m_editor);
    editorColumn->addWidget(m_confirm);
    auto *note = new QLabel(ngTr("Runs with sh, without a terminal, in the folder of the file. What it makes next to the file, named after it, "
                                 "appears beside it with the work's progress, as for the built-in actions.",
                                 "Gira con sh, senza terminale, nella cartella del file. Quello che crea accanto al file, col suo nome, "
                                 "compare vicino con l'avanzamento del lavoro, come per le azioni integrate."),
                              m_editor);
    note->setWordWrap(true);
    note->setEnabled(false);
    editorColumn->addWidget(note);
    m_builtinNote = new QLabel(ngTr("A built-in action: it can be moved and switched off. To change it, make a copy of your own.",
                                    "Un'azione integrata: si può spostare e spegnere. Per cambiarla, fanne una copia tua."),
                               m_editor);
    m_builtinNote->setWordWrap(true);
    editorColumn->addWidget(m_builtinNote);
    auto *editorButtons = new QHBoxLayout();
    m_copy = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")), ngTr("Make a copy to change", "Fanne una copia da modificare"), m_editor);
    m_delete = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), ngTr("Delete", "Elimina"), m_editor);
    editorButtons->addWidget(m_copy);
    editorButtons->addStretch();
    editorButtons->addWidget(m_delete);
    editorColumn->addLayout(editorButtons);
    editorColumn->addStretch();
    auto *editorScroll = new QScrollArea(this);
    editorScroll->setWidget(m_editor);
    editorScroll->setWidgetResizable(true);
    editorScroll->setFrameShape(QFrame::NoFrame);
    editorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editorScroll->viewport()->setAutoFillBackground(false);
    m_editor->setAutoFillBackground(false);
    editorScroll->setMinimumWidth(280);
    panes->addWidget(editorScroll, 1);

    // ---- behaviour
    connect(m_kindList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_filling || row < 0 || row >= m_kinds.count()) {
            return;
        }
        m_kind = m_kinds.at(row);
        m_row = m_lists.value(m_kind).isEmpty() ? -1 : 0;
        showList();
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_filling) {
            return;
        }
        m_row = row;
        showEditor();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_filling) {
            return;
        }
        auto &list = m_lists[m_kind];
        const int row = m_list->row(item);
        if (row >= 0 && row < list.count() && list[row].off != (item->checkState() != Qt::Checked)) {
            list[row].off = item->checkState() != Qt::Checked;
            showKinds();
            updatePill();
            Q_EMIT changed();
        }
    });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        takeListOrder();
    });
    connect(addKindButton, &QPushButton::clicked, this, &NgActionsPage::addKind);
    connect(newButton, &QAction::triggered, this, [this] {
        NgActionInfo info;
        info.label = ngTr("New action", "Nuova azione");
        info.icon = QStringLiteral("system-run");
        addCustom(info);
        m_name->setFocus();
        m_name->selectAll();
    });
    connect(fromMenu, &QAction::triggered, this, &NgActionsPage::addFromServiceMenu);
    connect(fromApp, &QAction::triggered, this, &NgActionsPage::addFromApplication);

    auto edited = [this] {
        if (!m_filling) {
            updatePill();
            Q_EMIT changed();
        }
    };
    connect(m_name, &QLineEdit::textEdited, this, [this, edited](const QString &text) {
        if (Entry *entry = current()) {
            entry->info.label = text;
            if (QListWidgetItem *item = m_list->item(m_row)) {
                m_filling = true;
                item->setText(text);
                m_filling = false;
            }
            edited();
        }
    });
    connect(m_icon, &KIconButton::iconChanged, this, [this, edited](const QString &icon) {
        if (Entry *entry = current(); entry && !m_filling) {
            entry->info.icon = icon;
            if (QListWidgetItem *item = m_list->item(m_row)) {
                m_filling = true;
                item->setIcon(actionIcon(icon));
                m_filling = false;
            }
            edited();
        }
    });
    connect(m_command, &QPlainTextEdit::textChanged, this, [this, edited] {
        if (Entry *entry = current(); entry && !m_filling && entry->info.custom) {
            entry->info.command = m_command->toPlainText().trimmed();
            if (QListWidgetItem *item = m_list->item(m_row)) {
                item->setToolTip(entry->info.command);
            }
            edited();
        }
    });
    connect(m_choices, &QLineEdit::textEdited, this, [this, edited](const QString &text) {
        if (Entry *entry = current()) {
            QStringList choices;
            for (const QString &choice : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                if (!choice.trimmed().isEmpty()) {
                    choices << choice.trimmed();
                }
            }
            entry->info.choices = choices;
            edited();
        }
    });
    connect(m_isMain, &QCheckBox::toggled, this, [this, edited](bool on) {
        if (Entry *entry = current(); entry && !m_filling) {
            if (on) {
                m_main[m_kind] = entry->info.id;
            } else if (m_main.value(m_kind) == entry->info.id) {
                m_main.remove(m_kind);
            }
            showList();
            edited();
        }
    });
    connect(m_confirm, &QCheckBox::toggled, this, [this, edited](bool on) {
        if (Entry *entry = current(); entry && !m_filling) {
            entry->info.confirm = on;
            edited();
        }
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        Entry *entry = current();
        if (!entry || entry->info.custom) {
            return;
        }
        NgActionInfo info = entry->info;
        entry->off = true; // the copy takes its place
        info.custom = true;
        addCustom(info);
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        Entry *entry = current();
        if (!entry || !entry->info.custom) {
            return;
        }
        m_deleted.append(entry->info.id);
        m_lists[m_kind].removeAt(m_row);
        m_row = qMin(m_row, int(m_lists.value(m_kind).count()) - 1);
        showKinds();
        showList();
        Q_EMIT changed();
    });

    load(true);
    m_kind = m_kinds.value(qMax(0, int(m_kinds.indexOf(QStringLiteral("video")))));
    m_row = 0;
    showKinds();
    showList();
}

NgActionsPage::~NgActionsPage() = default;

void NgActionsPage::load(bool withSettings)
{
    const KConfigGroup group = KSharedConfig::openConfig()->group(QString::fromLatin1(Group));
    const QList<NgActionInfo> custom = ngCustomActions();
    m_lists.clear();
    m_main.clear();
    m_kinds = ngActionKinds();
    for (const NgActionInfo &info : custom) {
        if (!m_kinds.contains(info.kind)) {
            m_kinds.append(info.kind);
        }
    }
    for (const QString &kind : std::as_const(m_kinds)) {
        QList<Entry> list;
        for (const NgActionInfo &info : ngBuiltinActions(kind)) {
            list.append({info, false});
        }
        for (const NgActionInfo &info : custom) {
            if (info.kind == kind) {
                list.append({info, false});
            }
        }
        if (withSettings) {
            const QStringList off = group.readEntry(QStringLiteral("Off_") + kind, QStringList());
            const QStringList order = group.readEntry(QStringLiteral("Order_") + kind, QStringList());
            for (Entry &entry : list) {
                entry.off = off.contains(entry.info.id);
            }
            if (!order.isEmpty()) {
                std::stable_sort(list.begin(), list.end(), [&order](const Entry &a, const Entry &b) {
                    const int ia = order.indexOf(a.info.id), ib = order.indexOf(b.info.id);
                    return (ia < 0 ? INT_MAX : ia) < (ib < 0 ? INT_MAX : ib);
                });
            }
            const QString main = group.readEntry(QStringLiteral("Main_") + kind, QString());
            if (!main.isEmpty()) {
                m_main.insert(kind, main);
            }
        }
        m_lists.insert(kind, list);
    }
    m_deleted.clear();
    if (!m_kinds.contains(m_kind)) {
        m_kind = m_kinds.value(0);
    }
}

NgActionsPage::Entry *NgActionsPage::current()
{
    auto it = m_lists.find(m_kind);
    if (it == m_lists.end() || m_row < 0 || m_row >= it->count()) {
        return nullptr;
    }
    return &(*it)[m_row];
}

void NgActionsPage::showKinds()
{
    m_filling = true;
    m_kindList->clear();
    for (const QString &kind : std::as_const(m_kinds)) {
        int on = 0;
        for (const Entry &entry : m_lists.value(kind)) {
            on += entry.off ? 0 : 1;
        }
        auto *item = new QListWidgetItem(dot(kindColour(kind)), QStringLiteral("%1  (%2)").arg(ngActionKindName(kind)).arg(on), m_kindList);
        Q_UNUSED(item)
    }
    m_kindList->setCurrentRow(m_kinds.indexOf(m_kind));
    m_filling = false;
}

void NgActionsPage::showList()
{
    m_filling = true;
    m_list->clear();
    m_listTitle->setText(ngTr("Actions: %1", "Azioni: %1").arg(ngActionKindName(m_kind)));
    const QList<Entry> list = m_lists.value(m_kind);
    // the main one: the marked one when it is on, otherwise the first that is on
    QString main = m_main.value(m_kind);
    bool mainOn = false;
    for (const Entry &entry : list) {
        mainOn = mainOn || (entry.info.id == main && !entry.off);
    }
    if (!mainOn) {
        main.clear();
        for (const Entry &entry : list) {
            if (!entry.off) {
                main = entry.info.id;
                break;
            }
        }
    }
    for (const Entry &entry : list) {
        QString text = entry.info.label;
        if (m_kind != QLatin1String("all") && entry.info.id == main) {
            text += QStringLiteral("   · ") + ngTr("main", "principale");
        }
        if (entry.info.custom) {
            text += QStringLiteral("   · ") + ngTr("yours", "tua");
        }
        auto *item = new QListWidgetItem(actionIcon(entry.info.icon), text, m_list);
        item->setData(Qt::UserRole, entry.info.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setCheckState(entry.off ? Qt::Unchecked : Qt::Checked);
        item->setToolTip(entry.info.command);
        if (entry.info.id == main && m_kind != QLatin1String("all")) {
            QFont bold = item->font();
            bold.setWeight(QFont::DemiBold);
            item->setFont(bold);
        }
    }
    if (m_row >= list.count()) {
        m_row = list.count() - 1;
    }
    m_list->setCurrentRow(m_row);
    m_filling = false;
    showEditor();
    updatePill();
}

void NgActionsPage::showEditor()
{
    m_filling = true;
    Entry *entry = current();
    m_editor->setEnabled(entry);
    const bool custom = entry && entry->info.custom;
    m_editorTitle->setText(!entry ? QString() : custom ? ngTr("Your action", "La tua azione") : ngTr("Built-in action", "Azione integrata"));
    m_name->setText(entry ? entry->info.label : QString());
    m_name->setReadOnly(!custom);
    m_icon->setIcon(entry ? entry->info.icon : QString());
    m_icon->setEnabled(custom);
    m_command->setPlainText(entry ? entry->info.command : QString());
    m_command->setReadOnly(!custom);
    m_placeholders->setVisible(custom);
    m_choices->setText(entry ? entry->info.choices.join(QStringLiteral(", ")) : QString());
    m_choices->setEnabled(custom);
    m_isMain->setVisible(m_kind != QLatin1String("all"));
    // the main one: the marked one when it is on, otherwise the first that is on
    QString main = m_main.value(m_kind);
    const QList<Entry> list = m_lists.value(m_kind);
    if (std::none_of(list.cbegin(), list.cend(), [&main](const Entry &e) {
            return e.info.id == main && !e.off;
        })) {
        main.clear();
        for (const Entry &e : list) {
            if (!e.off) {
                main = e.info.id;
                break;
            }
        }
    }
    m_isMain->setChecked(entry && main == entry->info.id);
    m_confirm->setChecked(entry && entry->info.confirm);
    m_confirm->setEnabled(custom);
    m_builtinNote->setVisible(entry && !custom);
    m_copy->setVisible(entry && !custom && !entry->info.command.isEmpty());
    m_delete->setVisible(custom);
    m_filling = false;
}

void NgActionsPage::updatePill()
{
    // a sample file of the kind shown; for "every file", a video
    const QString kind = m_kind == QLatin1String("all") ? QStringLiteral("video") : m_kind;
    QList<Entry> own;
    for (const Entry &entry : m_lists.value(kind)) {
        if (!entry.off) {
            own.append(entry);
        }
    }
    QList<Entry> general, tail;
    for (const Entry &entry : m_lists.value(QStringLiteral("all"))) {
        if (entry.off) {
            continue;
        }
        const QString &id = entry.info.id;
        (id == QLatin1String("cut") || id == QLatin1String("copy") || id == QLatin1String("pin") ? tail : general).append(entry);
    }
    int mainIndex = 0;
    for (int i = 0; i < own.count(); ++i) {
        if (own.at(i).info.id == m_main.value(kind)) {
            mainIndex = i;
        }
    }
    QList<NgPillPreview::Item> items;
    if (!own.isEmpty()) {
        items.append({actionIcon(own.at(mainIndex).info.icon), own.at(mainIndex).info.label, false});
        own.removeAt(mainIndex);
    }
    auto section = [&items](const QList<Entry> &entries, int most) {
        if (entries.isEmpty()) {
            return;
        }
        if (!items.isEmpty()) {
            items.append({QIcon(), QString(), true});
        }
        for (int i = 0; i < entries.count() && i < most; ++i) {
            items.append({actionIcon(entries.at(i).info.icon), QString(), false});
        }
    };
    section(general, 4);
    section(own, 4);
    section(tail, 3);
    m_pill->items = items;
    m_pill->update();
}

void NgActionsPage::takeListOrder()
{
    QList<Entry> &list = m_lists[m_kind];
    QList<Entry> ordered;
    for (int row = 0; row < m_list->count(); ++row) {
        const QString id = m_list->item(row)->data(Qt::UserRole).toString();
        for (const Entry &entry : std::as_const(list)) {
            if (entry.info.id == id) {
                ordered.append(entry);
                break;
            }
        }
    }
    if (ordered.count() == list.count()) {
        list = ordered;
    }
    m_row = m_list->currentRow();
    // the marks (main, yours) follow the new order
    QMetaObject::invokeMethod(this, &NgActionsPage::showList, Qt::QueuedConnection);
    Q_EMIT changed();
}

QString NgActionsPage::newCustomId(const QString &label) const
{
    QString slug = label.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug = slug.mid(0, 32).remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (slug.isEmpty()) {
        slug = QStringLiteral("action");
    }
    return QStringLiteral("custom:%1-%2").arg(slug, QString::number(QDateTime::currentMSecsSinceEpoch() % 0xFFFFFF, 36));
}

void NgActionsPage::addCustom(const NgActionInfo &source)
{
    NgActionInfo info = source;
    info.custom = true;
    info.kind = m_kind;
    info.id = newCustomId(info.label);
    QList<Entry> &list = m_lists[m_kind];
    const int at = m_row >= 0 && m_row < list.count() ? m_row + 1 : list.count();
    list.insert(at, {info, false});
    m_row = at;
    showKinds();
    showList();
    Q_EMIT changed();
}

void NgActionsPage::addFromServiceMenu()
{
    // the service menus of the right-click menu (kio/servicemenus/*.desktop), each of their actions
    QMenu menu(this);
    QStringList seen;
    const QStringList folders = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("kio/servicemenus"), QStandardPaths::LocateDirectory);
    QList<std::pair<QString, NgActionInfo>> found;
    for (const QString &folder : folders) {
        const QStringList files = QDir(folder).entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            if (seen.contains(file)) {
                continue; // the user's copy hides the system's
            }
            seen.append(file);
            const KDesktopFile desktop(QDir(folder).filePath(file));
            for (const QString &actionName : desktop.readActions()) {
                const KConfigGroup action = desktop.actionGroup(actionName);
                NgActionInfo info;
                info.label = action.readEntry("Name");
                info.icon = action.readEntry("Icon", desktop.readIcon());
                info.command = commandFromExec(action.readEntry("Exec"), info.icon, desktop.readName(), desktop.fileName());
                if (!info.label.isEmpty() && !info.command.isEmpty()) {
                    found.append({desktop.readName().isEmpty() ? file : desktop.readName(), info});
                }
            }
        }
    }
    if (found.isEmpty()) {
        menu.addAction(ngTr("No service menu is installed", "Nessun menu di servizio installato"))->setEnabled(false);
    }
    for (const auto &[menuName, info] : std::as_const(found)) {
        QAction *action = menu.addAction(actionIcon(info.icon), QStringLiteral("%1 — %2").arg(info.label, menuName));
        connect(action, &QAction::triggered, this, [this, info = info] {
            addCustom(info);
        });
    }
    menu.exec(QCursor::pos());
}

void NgActionsPage::addFromApplication()
{
    KOpenWithDialog dialog(QList<QUrl>(), this);
    dialog.hideNoCloseOnExit();
    dialog.hideRunInTerminal();
    if (dialog.exec() != QDialog::Accepted || !dialog.service()) {
        return;
    }
    const KService::Ptr service = dialog.service();
    NgActionInfo info;
    info.label = ngTr("Open with %1", "Apri con %1").arg(service->name());
    info.icon = service->icon();
    info.command = commandFromExec(service->exec(), service->icon(), service->name(), service->entryPath());
    if (!info.command.contains(QLatin1String("{file}"))) {
        info.command += QStringLiteral(" {file}");
    }
    addCustom(info);
}

void NgActionsPage::addKind()
{
    bool ok = false;
    const QString pattern = QInputDialog::getText(this,
                                                  ngTr("Custom kind", "Tipo personalizzato"),
                                                  ngTr("Files by extension or by type, several separated by “;”:\n*.md; *.markdown   or   text/markdown",
                                                       "File per estensione o per tipo, più d'uno separati da “;”:\n*.md; *.markdown   oppure   text/markdown"),
                                                  QLineEdit::Normal,
                                                  QString(),
                                                  &ok)
                                  .trimmed();
    if (!ok || pattern.isEmpty()) {
        return;
    }
    if (!m_kinds.contains(pattern)) {
        m_kinds.append(pattern);
        m_lists.insert(pattern, {});
    }
    m_kind = pattern;
    m_row = -1;
    showKinds();
    showList();
}

void NgActionsPage::applySettings()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QString::fromLatin1(Group));
    group.deleteGroup();
    group = KSharedConfig::openConfig()->group(QString::fromLatin1(Group));
    for (const QString &kind : std::as_const(m_kinds)) {
        const QList<Entry> list = m_lists.value(kind);
        QStringList order, off;
        for (const Entry &entry : list) {
            order << entry.info.id;
            if (entry.off) {
                off << entry.info.id;
            }
            if (entry.info.custom) {
                NgActionInfo info = entry.info;
                info.kind = kind;
                ngWriteCustomAction(info);
            }
        }
        if (list.isEmpty()) {
            continue;
        }
        group.writeEntry(QStringLiteral("Order_") + kind, order);
        if (!off.isEmpty()) {
            group.writeEntry(QStringLiteral("Off_") + kind, off);
        }
        if (!m_main.value(kind).isEmpty()) {
            group.writeEntry(QStringLiteral("Main_") + kind, m_main.value(kind));
        }
    }
    for (const QString &id : std::as_const(m_deleted)) {
        QFile::remove(ngActionsFolder() + QLatin1Char('/') + id.mid(7) + QStringLiteral(".desktop"));
    }
    m_deleted.clear();
    group.sync();
    KConfigGroup general = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
    general.writeEntry("JOBS_SERIES", m_oneAtATime->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    general.sync();
    ngQuickActionsChanged();
    ngSettingsInvalidate();
}

void NgActionsPage::restoreDefaults()
{
    // the built-in order and every action on; the user's own actions stay
    const QStringList deleted = m_deleted;
    QHash<QString, QList<Entry>> previous = m_lists;
    load(false);
    for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
        for (const Entry &entry : it.value()) {
            if (entry.info.custom && !m_lists.value(it.key()).isEmpty()) {
                bool present = false;
                for (const Entry &e : m_lists.value(it.key())) {
                    present = present || e.info.id == entry.info.id;
                }
                if (!present) {
                    m_lists[it.key()].append({entry.info, false});
                }
            } else if (entry.info.custom) {
                if (!m_kinds.contains(it.key())) {
                    m_kinds.append(it.key());
                }
                m_lists[it.key()].append({entry.info, false});
            }
        }
    }
    m_deleted = deleted;
    m_oneAtATime->setChecked(false); // (in parallel)
    m_row = 0;
    showKinds();
    showList();
    Q_EMIT changed();
}

#include "moc_ngactionspage.cpp"
