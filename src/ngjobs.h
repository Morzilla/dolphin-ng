/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NGJOBS_H
#define NGJOBS_H

#include <QToolButton>

class QMenu;
class QVBoxLayout;
class QProgressBar;
class QLabel;

/**
 * dolphin-ng: the button of the window's title row for the work of quick actions, of every Dolphin NG: how many are
 * at work (with a turning arc), and on a click the list: what runs (how far, a stop button), what waits, what ended
 * today, and the way to the whole history. The same for every tab, and out of the tool bar's way.
 */
class NgJobsButton : public QToolButton
{
    Q_OBJECT
public:
    explicit NgJobsButton(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void refresh();
    void rebuild();
    QMenu *m_menu;
    class QWidgetAction *m_action; // the list, as the menu's one entry
    QWidget *m_list;
    QVBoxLayout *m_rows;
    QString m_shape; // what the list shows now: built again only when it changes
    QList<std::pair<QString, QProgressBar *>> m_bars;
    QList<std::pair<QString, QLabel *>> m_figures;
    int m_running = 0;
    int m_waiting = 0;
};

/**
 * dolphin-ng: the content of the jobs tab (DolphinTabWidget::ngOpenHistoryTab): what is at work and what waits, as
 * large cards at the top (how far, the time left, stop, show the file in Dolphin NG), then every quick action that
 * ended as a line of time, the last first, grouped by day or by action. Each entry names the action and the file
 * with its path, and under it what it made.
 */
class NgHistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit NgHistoryPage(QWidget *parent = nullptr);
    /** only the jobs whose action, file or result contains \a text (the window's search field, next to the location) */
    void setSearch(const QString &text);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool m_narrow = false; // the cards of what is at work in one column
    void rebuild(bool force = false);
    void showInDolphin(const QUrl &url);
    QLabel *m_status;
    QString m_wanted;
    class QComboBox *m_grouping;
    QWidget *m_content;
    QVBoxLayout *m_rows;
    QString m_shape;
    QList<QWidget *> m_cards; // the cards of what is at work: painted again as it goes on
};

#endif
