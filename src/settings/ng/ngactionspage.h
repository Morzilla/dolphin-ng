/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NGACTIONSPAGE_H
#define NGACTIONSPAGE_H

#include "kitemviews/kitemlistwidget.h"
#include "settings/settingspagebase.h"

#include <QHash>
#include <QStringList>

class KIconButton;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class NgPillPreview;

/**
 * dolphin-ng: the quick actions editor (Settings > NG quick actions). The kinds of files on the left, the actions of
 * the chosen kind in the middle (their order is the order in the pill; a check switches one off), the chosen action
 * on the right, and the pill as a card will show it above. Built-in actions can be reordered and switched off; the
 * user's own are .desktop files in ngActionsFolder() (see kitemlistwidget.h), written when the dialog applies.
 */
class NgActionsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit NgActionsPage(QWidget *parent);
    ~NgActionsPage() override;

    void applySettings() override;
    void restoreDefaults() override;

private:
    struct Entry {
        NgActionInfo info;
        bool off = false;
    };

    void load(bool withSettings);
    void showKinds();
    void showList();
    void showEditor();
    void updatePill();
    void takeListOrder(); // the list widget was reordered by dragging: the model follows
    Entry *current();
    QString newCustomId(const QString &label) const;
    void addCustom(const NgActionInfo &info);
    void addFromServiceMenu();
    void addFromApplication();
    void addKind();

    QHash<QString, QList<Entry>> m_lists; // kind -> its actions, in order
    QHash<QString, QString> m_main; // kind -> the id of the main action
    QStringList m_kinds; // the built-in kinds, then the user's patterns
    QStringList m_deleted; // the user's actions removed since the last apply
    QString m_kind;
    int m_row = -1;
    bool m_filling = false;

    NgPillPreview *m_pill;
    QListWidget *m_kindList;
    QListWidget *m_list;
    QLabel *m_listTitle;
    QWidget *m_editor;
    QLabel *m_editorTitle;
    QLineEdit *m_name;
    KIconButton *m_icon;
    QPlainTextEdit *m_command;
    QWidget *m_placeholders;
    QLineEdit *m_choices;
    QCheckBox *m_isMain;
    QCheckBox *m_confirm;
    QCheckBox *m_oneAtATime = nullptr; // quick actions run one after the other (the group DolphinNG's JOBS_SERIES)
    QLabel *m_builtinNote;
    QPushButton *m_copy;
    QPushButton *m_delete;
};

#endif
