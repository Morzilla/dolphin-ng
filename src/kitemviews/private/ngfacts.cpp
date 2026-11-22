/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ngfacts.h"

#include "kitemviews/kitemlistwidget.h" // ngTr, ngKindFor

#include <KFileMetaData/Extractor>
#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/Properties>
#include <KFileMetaData/SimpleExtractionResult>
#include <KFileMetaData/UserMetaData>
#include <KIO/Global>

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
using KFileMetaData::Property::Property;

QString number(qint64 value)
{
    return QLocale().toString(value);
}

// "word" or "words" after a count
QString plural(qint64 count, const char *oneEn, const char *manyEn, const char *oneIt, const char *manyIt)
{
    return count == 1 ? ngTr(oneEn, oneIt) : ngTr(manyEn, manyIt);
}

QString duration(double seconds)
{
    const qint64 total = qRound64(seconds);
    const qint64 h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

// the name of a text file's language, for the editor's status line in the preview
QString ngLanguageName(const QString &name, const QString &mime)
{
    static const QHash<QString, QString> bySuffix{
        {QStringLiteral("py"), QStringLiteral("Python")},      {QStringLiteral("cpp"), QStringLiteral("C++")},       {QStringLiteral("cc"), QStringLiteral("C++")},
        {QStringLiteral("cxx"), QStringLiteral("C++")},        {QStringLiteral("h"), QStringLiteral("C/C++")},       {QStringLiteral("hpp"), QStringLiteral("C++")},
        {QStringLiteral("c"), QStringLiteral("C")},            {QStringLiteral("rs"), QStringLiteral("Rust")},       {QStringLiteral("go"), QStringLiteral("Go")},
        {QStringLiteral("js"), QStringLiteral("JavaScript")},  {QStringLiteral("mjs"), QStringLiteral("JavaScript")}, {QStringLiteral("ts"), QStringLiteral("TypeScript")},
        {QStringLiteral("tsx"), QStringLiteral("TypeScript")}, {QStringLiteral("jsx"), QStringLiteral("JavaScript")}, {QStringLiteral("java"), QStringLiteral("Java")},
        {QStringLiteral("kt"), QStringLiteral("Kotlin")},      {QStringLiteral("rb"), QStringLiteral("Ruby")},       {QStringLiteral("php"), QStringLiteral("PHP")},
        {QStringLiteral("sh"), QStringLiteral("Shell")},       {QStringLiteral("bash"), QStringLiteral("Shell")},    {QStringLiteral("zsh"), QStringLiteral("Shell")},
        {QStringLiteral("qml"), QStringLiteral("QML")},        {QStringLiteral("cmake"), QStringLiteral("CMake")},   {QStringLiteral("md"), QStringLiteral("Markdown")},
        {QStringLiteral("json"), QStringLiteral("JSON")},      {QStringLiteral("yaml"), QStringLiteral("YAML")},     {QStringLiteral("yml"), QStringLiteral("YAML")},
        {QStringLiteral("toml"), QStringLiteral("TOML")},      {QStringLiteral("xml"), QStringLiteral("XML")},       {QStringLiteral("html"), QStringLiteral("HTML")},
        {QStringLiteral("css"), QStringLiteral("CSS")},        {QStringLiteral("sql"), QStringLiteral("SQL")},       {QStringLiteral("lua"), QStringLiteral("Lua")},
        {QStringLiteral("ini"), QStringLiteral("INI")},        {QStringLiteral("conf"), QStringLiteral("Config")},   {QStringLiteral("txt"), QStringLiteral("Text")},
        {QStringLiteral("log"), QStringLiteral("Log")},        {QStringLiteral("csv"), QStringLiteral("CSV")},       {QStringLiteral("spec"), QStringLiteral("RPM spec")},
    };
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (const auto it = bySuffix.constFind(suffix); it != bySuffix.constEnd()) {
        return *it;
    }
    if (name == QLatin1String("CMakeLists.txt")) {
        return QStringLiteral("CMake");
    }
    if (name == QLatin1String("Makefile")) {
        return QStringLiteral("Make");
    }
    const QString comment = QMimeDatabase().mimeTypeForName(mime).comment();
    return comment.isEmpty() ? QStringLiteral("Text") : comment;
}

QString aspect(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return QString();
    }
    static const int known[][2] = {{1, 1}, {4, 3}, {3, 2}, {16, 10}, {16, 9}, {21, 9}, {5, 4}, {3, 4}, {2, 3}, {10, 16}, {9, 16}};
    const double ratio = double(width) / height;
    for (const auto &k : known) {
        if (std::abs(ratio - double(k[0]) / k[1]) < 0.012 * ratio) {
            return QStringLiteral("%1:%2").arg(k[0]).arg(k[1]);
        }
    }
    return QStringLiteral("%1:1").arg(QLocale().toString(ratio, 'f', 2));
}

QString shortDate(const QDate &date)
{
    const QString format = date.year() == QDate::currentDate().year() ? QStringLiteral("d MMM") : QStringLiteral("d MMM yy");
    return QLocale().toString(date, format);
}

qint64 countWords(QStringView text)
{
    qint64 words = 0;
    bool inWord = false;
    for (const QChar c : text) {
        const bool letter = !c.isSpace();
        if (letter && !inWord) {
            ++words;
        }
        inWord = letter;
    }
    return words;
}

// the entries of a ZIP archive, from its central directory (ZIP64 included): no decompression, a few reads
bool zipContents(const QString &path, qint64 fileSize, qint64 *files, qint64 *folders, qint64 *unpacked, const QString &prefix = QString(), int *matching = nullptr,
                 QList<NgFileFacts::Entry> *top = nullptr)
{
    QFile file(path);
    if (fileSize < 22 || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    auto u16 = [](const char *p) {
        return quint64(uchar(p[0])) | quint64(uchar(p[1])) << 8;
    };
    auto u32 = [&u16](const char *p) {
        return u16(p) | u16(p + 2) << 16;
    };
    auto u64 = [&u32](const char *p) {
        return u32(p) | u32(p + 4) << 32;
    };
    const qint64 tail = qMin<qint64>(fileSize, 22 + 65535 + 20);
    file.seek(fileSize - tail);
    const QByteArray end = file.read(tail);
    const int eocd = end.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (eocd < 0 || eocd + 22 > end.size()) {
        return false;
    }
    const char *d = end.constData() + eocd;
    quint64 count = u16(d + 10), cdSize = u32(d + 12), cdOffset = u32(d + 16);
    if ((count == 0xFFFF || cdSize == 0xFFFFFFFF || cdOffset == 0xFFFFFFFF) && eocd >= 20 && end.mid(eocd - 20, 4) == QByteArray("PK\x06\x07", 4)) {
        file.seek(qint64(u64(end.constData() + eocd - 20 + 8)));
        const QByteArray z = file.read(56);
        if (z.size() == 56 && z.startsWith(QByteArray("PK\x06\x06", 4))) {
            count = u64(z.constData() + 32);
            cdSize = u64(z.constData() + 40);
            cdOffset = u64(z.constData() + 48);
        }
    }
    if (cdSize > 64 * 1024 * 1024 || qint64(cdOffset + cdSize) > fileSize) {
        return false;
    }
    file.seek(qint64(cdOffset));
    const QByteArray cd = file.read(qint64(cdSize));
    if (quint64(cd.size()) != cdSize) {
        return false;
    }
    *files = *folders = *unpacked = 0;
    qsizetype pos = 0;
    while (pos + 46 <= cd.size() && cd.mid(pos, 4) == QByteArray("PK\x01\x02", 4)) {
        const char *p = cd.constData() + pos;
        quint64 size = u32(p + 24);
        const qsizetype nameLength = qsizetype(u16(p + 28)), extraLength = qsizetype(u16(p + 30)), commentLength = qsizetype(u16(p + 32));
        if (pos + 46 + nameLength + extraLength > cd.size()) {
            break;
        }
        if (size == 0xFFFFFFFF) {
            // the real size is in the ZIP64 extra field
            qsizetype e = pos + 46 + nameLength;
            const qsizetype extraEnd = e + extraLength;
            while (e + 4 <= extraEnd) {
                const quint64 id = u16(cd.constData() + e), length = u16(cd.constData() + e + 2);
                if (id == 1 && length >= 8) {
                    size = u64(cd.constData() + e + 4);
                    break;
                }
                e += 4 + qsizetype(length);
            }
        }
        const bool folder = nameLength > 0 && cd.at(pos + 46 + nameLength - 1) == '/';
        if (top) {
            // the first part of the path: a folder at the top (with what is under it), or a file there
            const QString name = QString::fromUtf8(cd.constData() + pos + 46, nameLength);
            const int slash = name.indexOf(QLatin1Char('/'));
            const QString head = slash < 0 ? name : name.left(slash + 1);
            auto it = std::find_if(top->begin(), top->end(), [&head](const NgFileFacts::Entry &e) {
                return e.name == head;
            });
            if (it == top->end() && top->size() < 400) {
                top->append({head, 0, 0});
                it = top->end() - 1;
            }
            if (it != top->end() && !folder) {
                it->size += qint64(size);
                if (slash >= 0) {
                    ++it->count;
                }
            }
        }
        if (matching && !folder && QLatin1String(cd.constData() + pos + 46, nameLength).startsWith(prefix)) {
            ++*matching;
        }
        if (folder) {
            ++*folders;
        } else {
            ++*files;
            *unpacked += qint64(size);
        }
        pos += 46 + nameLength + extraLength + commentLength;
    }
    Q_UNUSED(count)
    return *files + *folders > 0;
}

// how many entries of a ZIP archive (an office file) start with @p prefix; 0 if it is not one
int zipEntries(const QString &path, qint64 fileSize, const QString &prefix)
{
    qint64 files = 0, folders = 0, unpacked = 0;
    int matching = 0;
    return zipContents(path, fileSize, &files, &folders, &unpacked, prefix, &matching) ? matching : 0;
}

QString categoryLabel(NgKind::Category category, int count, bool audio)
{
    switch (category) {
    case NgKind::Folder:
        return plural(count, "folder", "folders", "cartella", "cartelle");
    case NgKind::Image:
        return plural(count, "image", "images", "immagine", "immagini");
    case NgKind::Document:
        return plural(count, "document", "documents", "documento", "documenti");
    case NgKind::Sheet:
        return plural(count, "sheet", "sheets", "foglio", "fogli");
    case NgKind::Archive:
        return plural(count, "archive", "archives", "archivio", "archivi");
    case NgKind::Media:
        return audio ? QStringLiteral("audio") : QStringLiteral("video");
    case NgKind::Data:
        return plural(count, "code file", "code files", "file di codice", "file di codice");
    case NgKind::Package:
        return plural(count, "package", "packages", "pacchetto", "pacchetti");
    default:
        return plural(count, "other", "others", "altro", "altri");
    }
}

void folderFacts(const QString &path, NgFileFacts *out)
{
    // the kinds of what it holds and what changed last, from the folder itself (not its subfolders); a huge
    // folder is not read to the end
    static const QStringList audioSuffixes{QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"),
                                           QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("mka"),
                                           QStringLiteral("ape"), QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("mid"), QStringLiteral("midi")};
    struct Tally {
        int count = 0;
        NgKind::Category category = NgKind::Other;
        bool audio = false;
        QString sigla; // set: shown by its own type ("8 PDF")
        QColor colour;
    };
    QHash<QString, Tally> parts;
    QHash<QString, int> innerTags;
    QStringList subfolders;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
    int seen = 0;
    while (it.hasNext() && seen < 5000) {
        it.next();
        ++seen;
        const QFileInfo info = it.fileInfo();
        const QString name = info.fileName();
        const QDateTime when = info.lastModified();
        if (when.isValid() && (!out->lastWhen.isValid() || when > out->lastWhen)) {
            out->lastWhen = when;
            out->lastName = name;
        }
        const NgKind kind = ngKindFor(name, info.isDir());
        const bool audio = kind.category == NgKind::Media && audioSuffixes.contains(info.suffix().toLower());
        // documents by their own type when it has a colour of its own (PDF, slides); the rest by category
        const bool own = kind.category == NgKind::Document && kind.tint != ngCategoryTint(NgKind::Document);
        const QString key = own ? QString(QLatin1Char('#') + kind.sigla) : QString(QString::number(int(kind.category)) + (audio ? QStringLiteral("a") : QString()));
        Tally &tally = parts[key];
        if (tally.count == 0) {
            tally.category = kind.category;
            tally.audio = audio;
            tally.sigla = own ? kind.sigla : QString();
            tally.colour = own ? kind.tint : audio ? QColor(214, 140, 214) : ngCategoryTint(kind.category);
        }
        ++tally.count;
        // the tags of what is inside (a subfolder: its own, and those of what it holds, below)
        for (const QString &tag : KFileMetaData::UserMetaData(info.filePath()).tags()) {
            ++innerTags[tag];
        }
        if (info.isDir() && !info.isSymLink() && subfolders.size() < 60) {
            subfolders.append(info.filePath());
        }
    }
    // one level down, a few thousand entries at most: a tagged file in a subfolder shows on the folder too
    int below = 0;
    for (const QString &sub : std::as_const(subfolders)) {
        QDirIterator inner(sub, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        while (inner.hasNext() && below < 3000) {
            inner.next();
            ++below;
            for (const QString &tag : KFileMetaData::UserMetaData(inner.filePath()).tags()) {
                ++innerTags[tag];
            }
        }
    }
    for (auto it = innerTags.cbegin(); it != innerTags.cend(); ++it) {
        out->innerTags.append({it.key(), it.value()});
    }
    std::sort(out->innerTags.begin(), out->innerTags.end(), [](const std::pair<QString, int> &a, const std::pair<QString, int> &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    for (const Tally &tally : std::as_const(parts)) {
        NgMixPart part;
        part.colour = tally.colour;
        part.count = tally.count;
        part.label = number(tally.count) + QLatin1Char(' ') + (tally.sigla.isEmpty() ? categoryLabel(tally.category, tally.count, tally.audio) : tally.sigla);
        out->mix.append(part);
    }
    std::sort(out->mix.begin(), out->mix.end(), [](const NgMixPart &a, const NgMixPart &b) {
        return a.count > b.count;
    });
}

NgFileFacts readFacts(const QString &path, const QString &mime, bool isDir, qint64 size)
{
    NgFileFacts out;
    const KFileMetaData::UserMetaData user(path);
    const QUrl origin = user.originUrl();
    if (origin.isValid() && !origin.host().isEmpty()) {
        out.origin = origin.host().startsWith(QLatin1String("www.")) ? origin.host().mid(4) : origin.host();
    }
    out.tags = user.tags();
    out.rating = user.rating();
    if (isDir) {
        folderFacts(path, &out);
        return out;
    }

    const QString name = QFileInfo(path).fileName();
    const NgKind kind = ngKindFor(name, false);
    const bool image = mime.startsWith(QLatin1String("image/"));
    const bool video = mime.startsWith(QLatin1String("video/"));
    const bool audio = mime.startsWith(QLatin1String("audio/"));
    const bool document = !image && !video && !audio && (kind.category == NgKind::Document || kind.category == NgKind::Sheet);
    const bool text = !document && (mime.startsWith(QLatin1String("text/")) || kind.category == NgKind::Data);
    auto add = [&out](const QString &value, const QString &word) {
        if (!value.isEmpty() && out.facts.count() < 3) {
            out.facts.append({value, word});
        }
    };

    KFileMetaData::PropertyMultiMap props;
    QString plain;
    if (image || video || audio || document) {
        thread_local KFileMetaData::ExtractorCollection collection;
        // the text only of documents of a reasonable size: it is what the words are counted on
        const bool wantText = document && size < 24 * 1024 * 1024;
        KFileMetaData::SimpleExtractionResult result(path, mime,
                                                     wantText ? KFileMetaData::ExtractionResult::Flags(KFileMetaData::ExtractionResult::ExtractMetaData
                                                                                                      | KFileMetaData::ExtractionResult::ExtractPlainText)
                                                              : KFileMetaData::ExtractionResult::Flags(KFileMetaData::ExtractionResult::ExtractMetaData));
        const QList<KFileMetaData::Extractor *> extractors = collection.fetchExtractors(mime);
        for (KFileMetaData::Extractor *extractor : extractors) {
            extractor->extract(&result);
            if (!result.properties().isEmpty()) {
                break; // the first that knows the type
            }
        }
        props = result.properties();
        plain = result.text();
    }
    auto prop = [&props](Property p) {
        return props.value(p);
    };

    if (image) {
        int width = prop(Property::Width).toInt(), height = prop(Property::Height).toInt();
        if (width <= 0 || height <= 0) {
            const QSize pixels = QImageReader(path).size();
            width = pixels.width();
            height = pixels.height();
        }
        if (width > 0 && height > 0) {
            const double mp = double(width) * height / 1e6;
            add(QStringLiteral("%1×%2").arg(width).arg(height),
                mp >= 1 ? QStringLiteral("%1 megapixel").arg(QLocale().toString(mp, 'f', mp < 10 ? 1 : 0)) : ngTr("pixels", "pixel"));
        }
        QDateTime taken = prop(Property::PhotoDateTimeOriginal).toDateTime();
        if (!taken.isValid()) {
            taken = prop(Property::ImageDateTime).toDateTime();
        }
        if (taken.isValid()) {
            add(shortDate(taken.date()), ngTr("taken", "scattata"));
        }
        const double fNumber = prop(Property::PhotoFNumber).toDouble();
        if (fNumber > 0) {
            QStringList exposure;
            const double time = prop(Property::PhotoExposureTime).toDouble();
            if (time > 0) {
                exposure << (time < 1 ? QStringLiteral("1/%1 s").arg(qRound(1 / time)) : QStringLiteral("%1 s").arg(QLocale().toString(time, 'g', 3)));
            }
            if (prop(Property::PhotoISOSpeedRatings).toInt() > 0) {
                exposure << QStringLiteral("ISO %1").arg(prop(Property::PhotoISOSpeedRatings).toInt());
            }
            add(QStringLiteral("f/%1").arg(QLocale().toString(fNumber, 'g', 2)), exposure.isEmpty() ? ngTr("aperture", "apertura") : exposure.join(QStringLiteral(" · ")));
        }
        add(aspect(width, height), ngTr("aspect ratio", "proporzioni"));
        out.bar = NgFileFacts::Photo;
        out.detail.insert(QStringLiteral("width"), width);
        out.detail.insert(QStringLiteral("height"), height);
        if (taken.isValid()) {
            out.detail.insert(QStringLiteral("taken"), taken);
        }
        if (fNumber > 0) {
            out.detail.insert(QStringLiteral("fnumber"), QStringLiteral("f/%1").arg(QLocale().toString(fNumber, 'g', 2)));
            const double time = prop(Property::PhotoExposureTime).toDouble();
            if (time > 0) {
                out.detail.insert(QStringLiteral("shutter"), time < 1 ? QStringLiteral("1/%1").arg(qRound(1 / time)) : QStringLiteral("%1\"").arg(QLocale().toString(time, 'g', 3)));
            }
            if (prop(Property::PhotoISOSpeedRatings).toInt() > 0) {
                out.detail.insert(QStringLiteral("iso"), prop(Property::PhotoISOSpeedRatings).toInt());
            }
        }
        if (const double focal = prop(Property::PhotoFocalLength).toDouble(); focal > 0) {
            out.detail.insert(QStringLiteral("focal"), QStringLiteral("%1 mm").arg(QLocale().toString(focal, 'g', 3)));
        }
        const QString make = prop(Property::Manufacturer).toString().trimmed();
        QString model = prop(Property::Model).toString().trimmed();
        if (!make.isEmpty() && !model.startsWith(make.section(QLatin1Char(' '), 0, 0), Qt::CaseInsensitive)) {
            model = make.section(QLatin1Char(' '), 0, 0) + QLatin1Char(' ') + model;
        }
        if (!model.trimmed().isEmpty()) {
            out.whoKind = NgFileFacts::Camera;
            out.who = model.trimmed();
        }
    } else if (video || audio) {
        const double seconds = prop(Property::Duration).toDouble();
        if (seconds > 0) {
            add(duration(seconds), ngTr("duration", "durata"));
        }
        out.bar = video ? NgFileFacts::Video : NgFileFacts::Audio;
        if (seconds > 0) {
            out.detail.insert(QStringLiteral("duration"), seconds);
        }
        if (video) {
            const int width = prop(Property::Width).toInt(), height = prop(Property::Height).toInt();
            const double fps = prop(Property::FrameRate).toDouble();
            out.detail.insert(QStringLiteral("width"), width);
            out.detail.insert(QStringLiteral("height"), height);
            if (fps > 0) {
                out.detail.insert(QStringLiteral("fps"), fps);
            }
            if (prop(Property::BitRate).toLongLong() > 0) {
                out.detail.insert(QStringLiteral("bitrate"), prop(Property::BitRate).toLongLong());
            }
            if (height > 0) {
                const bool common = width * 9 == height * 16 && (height == 480 || height == 720 || height == 1080 || height == 1440 || height == 2160);
                add(common ? (height == 2160 ? QStringLiteral("4K") : QStringLiteral("%1p").arg(height)) : QStringLiteral("%1×%2").arg(width).arg(height),
                    fps > 0 ? QStringLiteral("%1 fps").arg(QLocale().toString(fps, 'g', 3)) : aspect(width, height));
            }
            const qint64 bitRate = prop(Property::BitRate).toLongLong();
            if (bitRate > 0) {
                add(QStringLiteral("%1 Mbit/s").arg(QLocale().toString(bitRate / 1e6, 'f', 1)), ngTr("bit rate", "bit rate"));
            }
            add(aspect(width, height), ngTr("aspect ratio", "proporzioni"));
        } else {
            const int sampleRate = prop(Property::SampleRate).toInt();
            const int bitRate = prop(Property::BitRate).toInt();
            const int channels = prop(Property::Channels).toInt();
            out.detail.insert(QStringLiteral("sampleRate"), sampleRate);
            out.detail.insert(QStringLiteral("bitrate"), bitRate);
            out.detail.insert(QStringLiteral("channels"), channels);
            out.detail.insert(QStringLiteral("title"), prop(Property::Title).toString().trimmed());
            const QString channelWord = channels == 1 ? ngTr("mono", "mono") : channels == 2 ? ngTr("stereo", "stereo") : channels > 2 ? ngTr("%1 channels", "%1 canali").arg(channels) : QString();
            if (sampleRate > 0) {
                const QString kHz = QStringLiteral("%1 kHz").arg(QLocale().toString(sampleRate / 1000.0, 'g', 3));
                add(kHz, bitRate > 0 ? QStringLiteral("%1 kbit/s").arg(qRound(bitRate / (bitRate > 10000 ? 1000.0 : 1.0))) : channelWord);
            }
            const int track = prop(Property::TrackNumber).toInt();
            if (track > 0) {
                add(QString::number(track), ngTr("track", "traccia"));
            }
            const int year = prop(Property::ReleaseYear).toInt();
            if (year > 0) {
                add(QString::number(year), ngTr("year", "anno"));
            }
                QString artist = prop(Property::Artist).toString();
            if (artist.isEmpty()) {
                artist = prop(Property::AlbumArtist).toString();
            }
            const QString album = prop(Property::Album).toString();
            out.detail.insert(QStringLiteral("artist"), artist.trimmed());
            out.detail.insert(QStringLiteral("album"), album.trimmed());
            QStringList who;
            if (!artist.isEmpty()) {
                who << artist;
            }
            if (!album.isEmpty()) {
                who << album;
            }
            if (!who.isEmpty()) {
                out.whoKind = NgFileFacts::Artist;
                out.who = who.join(QStringLiteral(" · "));
            }
        }
    } else if (document) {
        if (kind.category == NgKind::Sheet) {
            const int sheets = zipEntries(path, size, QStringLiteral("xl/worksheets/sheet"));
            if (sheets > 0) {
                add(number(sheets), plural(sheets, "sheet", "sheets", "foglio", "fogli"));
            }
        }
        const int pages = prop(Property::PageCount).toInt();
        if (pages > 0) {
            add(number(pages), plural(pages, "page", "pages", "pagina", "pagine"));
        }
        out.bar = NgFileFacts::Pages;
        out.detail.insert(QStringLiteral("pages"), pages);
        qint64 words = prop(Property::WordCount).toLongLong();
        if (words <= 0 && !plain.isEmpty()) {
            words = countWords(plain);
        }
        if (words > 0 && kind.category == NgKind::Document) { // the words of a spreadsheet say little
            add(number(words), plural(words, "word", "words", "parola", "parole"));
            if (kind.category == NgKind::Document) {
                add(QStringLiteral("%1 min").arg(qMax<qint64>(1, qRound64(words / 230.0))), ngTr("to read", "di lettura"));
            }
            out.detail.insert(QStringLiteral("words"), words);
            out.detail.insert(QStringLiteral("minutes"), qMax<qint64>(1, qRound64(words / 230.0)));
        }
        const qint64 lines = prop(Property::LineCount).toLongLong();
        if (lines > 0) {
            add(number(lines), plural(lines, "line", "lines", "riga", "righe"));
        }
        const QString author = prop(Property::Author).toString().trimmed();
        // "LibreOffice/26.2.7.3$Linux_X86_64 LibreOffice_project/..." -> "LibreOffice 26.2.7.3"
        QString generator = prop(Property::Generator).toString().section(QLatin1Char('$'), 0, 0);
        generator.remove(QRegularExpression(QStringLiteral("\\s*\\(.*$")));
        generator = generator.replace(QLatin1Char('/'), QLatin1Char(' ')).trimmed();
        if (!author.isEmpty()) {
            out.whoKind = NgFileFacts::Author;
            out.who = author;
        } else if (!generator.isEmpty()) {
            out.whoKind = NgFileFacts::Program;
            out.who = generator;
        }
    } else if (text && size < 4 * 1024 * 1024) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            const bool binary = data.contains('\0');
            if (!binary) {
                const qint64 lines = data.count('\n') + ((!data.isEmpty() && !data.endsWith('\n')) ? 1 : 0);
                add(number(lines), plural(lines, "line", "lines", "riga", "righe"));
                QStringDecoder decoder(QStringDecoder::Utf8);
                const QString decoded = decoder(data);
                add(number(countWords(decoded)), ngTr("words", "parole"));
                bool ascii = true;
                for (const char c : data) {
                    if (uchar(c) >= 0x80) {
                        ascii = false;
                        break;
                    }
                }
                add(decoder.hasError() ? QStringLiteral("8-bit") : ascii ? QStringLiteral("ASCII") : QStringLiteral("UTF-8"), ngTr("encoding", "codifica"));
                // the editor's status line: language, lines, encoding, line ends, indentation
                out.bar = NgFileFacts::Code;
                out.detail.insert(QStringLiteral("lines"), lines);
                out.detail.insert(QStringLiteral("encoding"), decoder.hasError() ? QStringLiteral("8-bit") : QStringLiteral("UTF-8"));
                out.detail.insert(QStringLiteral("eol"), data.contains("\r\n") ? QStringLiteral("CRLF") : QStringLiteral("LF"));
                int tabs = 0, two = 0, four = 0;
                for (const QByteArray &line : data.left(64 * 1024).split('\n')) {
                    if (line.startsWith('\t')) {
                        ++tabs;
                    } else if (line.startsWith("    ")) {
                        ++four;
                    } else if (line.startsWith("  ") && !line.startsWith("   ")) {
                        ++two;
                    }
                }
                if (tabs + two + four > 0) {
                    out.detail.insert(QStringLiteral("indent"), tabs >= two && tabs >= four ? ngTr("tabs", "tabulazioni") : four >= two ? ngTr("4 spaces", "4 spazi") : ngTr("2 spaces", "2 spazi"));
                }
                out.detail.insert(QStringLiteral("language"), ngLanguageName(name, mime));
            }
        }
    } else if (kind.category == NgKind::Archive || kind.category == NgKind::Package) {
        qint64 files = 0, folders = 0, unpacked = 0;
        QList<NgFileFacts::Entry> top;
        if (zipContents(path, size, &files, &folders, &unpacked, QString(), nullptr, &top)) {
            out.bar = NgFileFacts::Archive;
            out.detail.insert(QStringLiteral("files"), files);
            out.detail.insert(QStringLiteral("folders"), folders);
            out.detail.insert(QStringLiteral("unpacked"), unpacked);
            out.detail.insert(QStringLiteral("packed"), size);
            std::sort(top.begin(), top.end(), [](const NgFileFacts::Entry &a, const NgFileFacts::Entry &b) {
                const bool fa = a.name.endsWith(QLatin1Char('/')), fb = b.name.endsWith(QLatin1Char('/'));
                return fa != fb ? fa : a.size > b.size;
            });
            out.entries = top.mid(0, 8);
            add(number(files), plural(files, "file", "files", "file", "file"));
            if (unpacked > 0) {
                add(KIO::convertSize(KIO::filesize_t(unpacked)), ngTr("unpacked", "scompattato"));
                if (unpacked > size) {
                    add(QStringLiteral("%1 %").arg(qRound(100.0 * (unpacked - size) / unpacked)), ngTr("saved", "risparmiato"));
                }
            }
            if (folders > 0) {
                add(number(folders), plural(folders, "folder", "folders", "cartella", "cartelle"));
            }
        }
    }
    // room left: what the file system knows
    out.ownFacts = out.facts.count();
    add(KIO::convertSize(KIO::filesize_t(qMax<qint64>(0, size))), ngTr("size", "dimensione"));
    add(kind.sigla, ngTr("format", "formato"));
    return out;
}
} // namespace

QString ngContentText(const NgFileFacts &facts, QString *sortKey, QString *group)
{
    auto heading = [group](const QString &text) {
        if (group) {
            *group = text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1);
        }
    };
    auto pad = [](double amount) {
        return QStringLiteral("%1").arg(qint64(std::llround(qMax(0.0, amount))), 15, 10, QLatin1Char('0'));
    };
    if (!facts.mix.isEmpty()) {
        int total = 0;
        QStringList parts;
        for (const NgMixPart &part : facts.mix) {
            total += part.count;
            if (parts.count() < 2) {
                parts << part.label;
            }
        }
        if (sortKey) {
            *sortKey = QStringLiteral("0|") + pad(total);
        }
        heading(ngTr("Folders", "Cartelle"));
        return parts.join(QStringLiteral(" · "));
    }
    if (facts.ownFacts <= 0 || facts.facts.isEmpty()) {
        if (sortKey) {
            sortKey->clear();
        }
        heading(ngTr("Other", "Altro"));
        return QString();
    }
    const NgFact &fact = facts.facts.constFirst();
    const QString &value = fact.value;
    QString unit;
    double amount = 0;
    QString text;
    static const QRegularExpression digitsOnly(QStringLiteral("^[\\d\\s.,'\\x{00A0}\\x{202F}]+$"));
    if (value.contains(QLatin1Char(':'))) {
        // a duration: h:mm:ss or m:ss
        unit = QStringLiteral("2time");
        for (const QString &part : value.split(QLatin1Char(':'))) {
            amount = amount * 60 + part.toDouble();
        }
        text = value;
        heading(ngTr("Duration", "Durata"));
    } else if (value.contains(QChar(0x00D7))) {
        // pixels: width×height
        unit = QStringLiteral("3pixels");
        const QStringList sides = value.split(QChar(0x00D7));
        amount = sides.value(0).toDouble() * sides.value(1).toDouble();
        text = value;
        heading(ngTr("Pixels", "Pixel"));
    } else if (value == QLatin1String("4K") || value.endsWith(QLatin1Char('p'))) {
        unit = QStringLiteral("3pixels");
        const double height = value == QLatin1String("4K") ? 2160 : value.chopped(1).toDouble();
        amount = height * height * 16 / 9;
        text = value;
        heading(ngTr("Pixels", "Pixel"));
    } else if (digitsOnly.match(value).hasMatch()) {
        // a count: its word says of what, in the singular or the plural ("1 page", "24 pages")
        struct Counted {
            const char *stems; // the first letters of the word, in English and Italian, singular and plural
            const char *english, *italian;
        };
        static const Counted counted[] = {{"page|pagi", "pages", "pagine"},
                                          {"word|paro", "words", "parole"},
                                          {"shee|fogl", "sheets", "fogli"},
                                          {"line|riga|righ", "lines", "righe"},
                                          {"file", "files", "file"}};
        const QString stem = fact.word.left(4).toLower();
        unit = QStringLiteral("1") + stem;
        QString plural = fact.word;
        for (const Counted &c : counted) {
            if (QString::fromLatin1(c.stems).split(QLatin1Char('|')).contains(stem)) {
                unit = QStringLiteral("1") + QString::fromLatin1(c.english);
                plural = ngTr(c.english, c.italian);
                break;
            }
        }
        QString digits = value;
        digits.remove(QRegularExpression(QStringLiteral("\\D")));
        amount = digits.toDouble();
        text = value + QLatin1Char(' ') + fact.word;
        heading(plural);
    } else {
        unit = QStringLiteral("4") + fact.word.left(4).toLower();
        text = value + QLatin1Char(' ') + fact.word;
        heading(fact.word);
    }
    if (sortKey) {
        *sortKey = unit + QLatin1Char('|') + pad(amount);
    }
    return text;
}

NgFacts *NgFacts::instance()
{
    static NgFacts *facts = new NgFacts();
    return facts;
}

void NgFacts::attributesChanged(const QString &path)
{
    auto it = m_cache.find(path);
    if (it != m_cache.end() && !it->pending) {
        m_cache.erase(it);
    }
    Q_EMIT changed(path);
    // (and its folder's, which tells the tags inside it; the folder's own date does not change with them)
    const QString folder = QFileInfo(path).absolutePath();
    if (auto up = m_cache.find(folder); up != m_cache.end() && !up->pending) {
        m_cache.erase(up);
    }
    Q_EMIT changed(folder);
}

NgFacts::NgFacts(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(2);
    m_pool.setThreadPriority(QThread::LowPriority);
    m_pool.setExpiryTimeout(20000);
}

const NgFileFacts *NgFacts::facts(const QString &path, const QString &mime, bool isDir, qint64 modified, qint64 size)
{
    if (path.isEmpty()) {
        return nullptr;
    }
    auto it = m_cache.find(path);
    // (facts read at a time as recent as the one asked for, or more, still hold: a folder's card asks with the newest
    // date of what it holds, the Information panel with the folder's own, and with an exact match each asking threw the
    // other's facts away and read the folder again, in a loop: the panel's "Changed last" flickered)
    if (it != m_cache.end() && it->modified >= modified && it->size == size) {
        return it->pending ? nullptr : &it->facts;
    }
    if (m_cache.size() > 3000) {
        // forget what is done; what is being read stays, its result is awaited
        for (auto entry = m_cache.begin(); entry != m_cache.end();) {
            entry = entry->pending ? std::next(entry) : m_cache.erase(entry);
        }
    }
    Entry &entry = m_cache[path];
    entry.modified = modified;
    entry.size = size;
    entry.pending = true;
    m_pool.start([this, path, mime, isDir, modified, size]() {
        const NgFileFacts facts = readFacts(path, mime, isDir, size);
        QMetaObject::invokeMethod(
            this,
            [this, path, modified, size, facts]() {
                store(path, modified, size, facts);
            },
            Qt::QueuedConnection);
    });
    return nullptr;
}

void NgFacts::store(const QString &path, qint64 modified, qint64 size, const NgFileFacts &facts)
{
    auto it = m_cache.find(path);
    if (it == m_cache.end() || it->modified != modified || it->size != size) {
        return; // changed meanwhile: a newer read is on its way
    }
    it->pending = false;
    it->facts = facts;
    Q_EMIT ready(path);
}

#include "moc_ngfacts.cpp"
