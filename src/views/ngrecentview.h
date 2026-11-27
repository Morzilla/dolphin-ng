/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "dolphin_export.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QUrl>

class QTimer;

/**
 * dolphin-ng: the list of what happened lately (ng-recent, ngrecent:/) as a story of the days: one card per file
 * with what happened to it, which application did it, its size and the time; things done in one go (an archive
 * unpacked, a synchronisation) are one card that opens in place on its files, with what can be done about it
 * underneath. It stands in for the item view while that list is on show (NG setting RECENT_LOOK, "classic" for
 * the item view).
 */
class DOLPHIN_EXPORT NgRecentView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit NgRecentView(QWidget *parent = nullptr);

    void setUrl(const QUrl &url);
    /**
     * What to see. @p events: a sum of 1 what is new (made, downloaded, added), 2 what was modified, 4 what was only
     * opened; 0: everything that was made or changed. @p kinds: a sum of 1 << NgKind::Category (pictures, documents…);
     * 0: every kind. Within each the choices add up (new or modified); the two together narrow (… that are pictures).
     */
    void setFilters(int events, int kinds);
    int eventFilter() const;
    int kindFilter() const;
    /** the kinds of file the list holds (before the filter by kind), as a sum of 1 << NgKind::Category */
    int kindsPresent() const;
    QUrl url() const;

Q_SIGNALS:
    /** the list was read again: other kinds of file may be in it */
    void contentChanged();

    /** a folder is to be shown (the folder of an operation, the one a file is in) */
    void folderRequested(const QUrl &url);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    struct Entry {
        QString name;
        QString path; // the real file or, for an operation, the folder its files are in
        QUrl url; // what to list for an operation, what to open for a file
        QString what; // "Modified with GIMP"
        QString mime;
        qint64 size = -1;
        qint64 when = 0; // seconds
        int hue = -1;
        bool folder = false;
        bool group = false;
        int count = 0;
        int made = 0;
        bool automatic = false;
        QString id; // for an operation: its number
        QList<Entry> children;
        bool loaded = false;
    };
    struct Row {
        enum Type { Day, File, Group, Foot } type = File;
        QRect rect;
        int entry = -1; // index into m_entries
        int child = -1; // index into its children, or -1
        QString text; // Day: its title
    };
    struct Button {
        QRect rect;
        int entry = -1;
        int action = 0; // 1 show all, 2 open the folder, 3 hide this operation, 4 always leave this folder out
    };

    void reload();
    void loadChildren(int entry);
    void layoutRows();
    void applyKinds();
    void requestPreviews(const QList<Entry> &entries);
    int rowAt(const QPoint &pos) const;
    const Entry *entryOf(const Row &row) const;
    void open(const Entry &entry);
    void act(const Button &button);
    void paintEntry(QPainter &painter, const Row &row, const Entry &entry, bool hovered);
    void paintFoot(QPainter &painter, const Row &row, const Entry &entry);

    QUrl m_url;
    QList<Entry> m_entries;
    QList<Row> m_rows;
    QList<Button> m_buttons;
    QSet<QString> m_expanded; // operations open at the moment
    QSet<QString> m_showAll;
    QHash<QString, QPixmap> m_previews; // path -> picture
    QSet<QString> m_previewAsked;
    int m_hover = -1;
    int m_hoverButton = -1;
    int m_selected = -1;
    int m_contentHeight = 0;
    bool m_loading = false;
    int m_events = 0;
    int m_kinds = 0;
    int m_present = 0;
    QList<Entry> m_listed; // what the list holds, before the filter by kind
    QString m_stamp;
    QTimer *m_refresh;
};
