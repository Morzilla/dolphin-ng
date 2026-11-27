/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NGFACTS_H
#define NGFACTS_H

#include "dolphin_export.h"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>

/**
 * dolphin-ng: what the Dossier card tells about a file beyond its name and dates — what is inside it (pages and
 * words, size in pixels and when it was taken, duration and quality...), where it came from and how it is labelled.
 * Read in the background with KFileMetaData (the extractors the information panel uses, without Baloo), only for
 * the cards that ask, and kept until the file changes.
 */
struct NgFact {
    QString value; // large: "24", "4032×3024", "2:47"
    QString word; // below it: "pages", "12 megapixel", "duration"
};

struct NgMixPart {
    QColor colour;
    int count = 0;
    QString label; // "8 PDF", "3 sheets"
};

struct NgFileFacts {
    QList<NgFact> facts; // at most three
    int ownFacts = 0; // how many of them the file itself told (the rest: its size and format, from the file system)
    QList<NgMixPart> mix; // folders: what they hold, the most first
    QString lastName; // folders: the entry changed last
    QDateTime lastWhen;
    QString origin; // the site it was downloaded from
    QString madeFrom; // the path of the file a quick action made it from (ngFileMadeFrom)
    QString madeBy; // that action's name
    enum Who { Nobody, Camera, Artist, Author, Program };
    Who whoKind = Nobody;
    QString who; // "Pixel 8", "Artist · Album (2025)", "Finance team"
    QStringList tags;
    int rating = 0; // 0..10, as in Baloo
    // what the Information panel's preview draws in its bar, by kind: the values as read, not yet worded (keys in
    // ngfacts.cpp: fnumber, shutter, iso, focal, width, height, taken; pages, words, minutes; duration, fps, bitrate;
    // sampleRate, channels, artist, title; lines, encoding, eol, indent, language; files, folders, unpacked, packed)
    enum Bar { NoBar, Photo, Pages, Video, Audio, Code, Archive };
    Bar bar = NoBar;
    QVariantMap detail;
    struct Entry {
        QString name;
        qint64 size = 0;
        int count = 0; // a folder: the files under it (0: a file)
    };
    QList<Entry> entries; // archives: the top level, folders first, at most eight
    // folders: the tags of what is inside (its files and those of its subfolders, one level down), the most used first,
    // with how many carry each
    QList<std::pair<QString, int>> innerTags;
};

/**
 * dolphin-ng: the "Content" column: the first thing the file itself tells, with its unit ("24 pages", "2:47",
 * "4032×3024"), or what a folder holds ("8 PDF · 3 images"); empty when the file tells nothing. @p sortKey: for
 * sorting, the unit first and then the amount, so pages go with pages and seconds with seconds. @p group: the
 * heading of the unit when the view is grouped ("Pages", "Duration", "Pixels").
 */
DOLPHIN_EXPORT QString ngContentText(const NgFileFacts &facts, QString *sortKey = nullptr, QString *group = nullptr);

class DOLPHIN_EXPORT NgFacts : public QObject
{
    Q_OBJECT

public:
    static NgFacts *instance();

    /**
     * The facts of the local file or folder at @p path, or nullptr while they are being read: ready(path) follows.
     * @p modified and @p size tell when the cached facts are stale.
     */
    const NgFileFacts *facts(const QString &path, const QString &mime, bool isDir, qint64 modified, qint64 size);

    /**
     * What is told by the file's attributes changed (its tags, its rating: the file's dates and size stay the same):
     * the facts are read again, and the cards of the file are laid out again.
     */
    void attributesChanged(const QString &path);

Q_SIGNALS:
    void ready(const QString &path);
    void changed(const QString &path);

private:
    explicit NgFacts(QObject *parent = nullptr);
    void store(const QString &path, qint64 modified, qint64 size, const NgFileFacts &facts);

    struct Entry {
        qint64 modified = 0;
        qint64 size = 0;
        bool pending = true;
        NgFileFacts facts;
    };
    QHash<QString, Entry> m_cache;
    QThreadPool m_pool;
};

#endif
