/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NGJOBS_H
#define NGJOBS_H

#include <QDialog>
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

private:
    void refresh();
    void rebuild();
    QMenu *m_menu;
    QWidget *m_list;
    QVBoxLayout *m_rows;
    QString m_shape; // what the list shows now: built again only when it changes
    QList<std::pair<QString, QProgressBar *>> m_bars;
    QList<std::pair<QString, QLabel *>> m_figures;
    int m_running = 0;
    int m_waiting = 0;
};

/** dolphin-ng: every quick action that ended, the last first */
class NgHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NgHistoryDialog(QWidget *parent = nullptr);
};

#endif
