/*
 * SPDX-FileCopyrightText: 2026 Morzilla
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NGINFOVIEW_H
#define NGINFOVIEW_H

#include <KFileItem>

#include <QPointer>
#include <QScrollArea>

#include <functional>

class QProcess;
class MediaWidget;
class NgInfoHero;
class NgInfoFacts;
class NgInfoDates;
class NgInfoMix;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QProgressBar;
class QToolButton;
class QVBoxLayout;

namespace KIO
{
class PreviewJob;
class DirectorySizeJob;
}

/**
 * dolphin-ng: the Information panel, drawn again. One place for what the cards, the quick view, the preview card and
 * the lightbox show about a file, and for what can be done with it:
 *   - a large, live preview: the picture (its eye opens the lightbox), the first page, the first lines of a text,
 *     audio and video played on the spot, a folder's own pictures;
 *   - the name, the kind in its colour, the size, pinned or not;
 *   - the quick actions of its kind in one pill of icons, and the work in progress on the file;
 *   - the three facts of the Dossier (pages, pixels, duration... read from the file, without Baloo), where it came
 *     from, its rating and tags (both changed on the spot), and its dates on one line.
 * With several items: a stack of their previews, how many and how large, the kinds they are, their tags.
 * It asks the window for what only a view can do (run a quick action, pin, open the lightbox) through its signals.
 */
class NgInfoView : public QScrollArea
{
    Q_OBJECT

public:
    explicit NgInfoView(QWidget *parent = nullptr);
    ~NgInfoView() override;

    void showItem(const KFileItem &item);
    void showItems(const KFileItemList &items);
    void refresh();
    QSize sizeHint() const override
    {
        return QSize(340, 600); // (the panel opens this wide: the preview, the facts and the pill need it)
    }

    /** whether these items are pinned (the window answers from its view) */
    void setPinnedQuery(std::function<bool(const KFileItemList &)> query);

Q_SIGNALS:
    void quickActionRequested(const KFileItem &item, int action);
    void pinRequested(const KFileItemList &items, bool pinned);
    void lightboxRequested(const KFileItem &item);
    void urlActivated(const QUrl &url);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void build();
    void showOne();
    void showMany();
    void askPreview(const KFileItemList &items, int sharper = 1);
    void showFacts();
    void showActivity();
    void showRating(int rating);
    void showTags();
    void editTags();
    void writeTags(const QStringList &tags);
    void restyle();
    void layoutActions();
    void updateOverlay();
    void updatePinTip();
    void renderSharper(int side);

    KFileItem m_item;
    KFileItemList m_items;
    std::function<bool(const KFileItemList &)> m_pinnedQuery;
    QPointer<KIO::PreviewJob> m_previewJob;
    int m_sharpSide = 0; // the size the zoomed preview was last drawn again at
    QPointer<QProcess> m_sharpProcess;
    QPointer<KIO::DirectorySizeJob> m_sizeJob; // a folder's size, counted in the background

    QWidget *m_body = nullptr;
    QVBoxLayout *m_layout = nullptr;
    NgInfoHero *m_hero = nullptr;
    MediaWidget *m_media = nullptr;
    QLabel *m_name = nullptr;
    QToolButton *m_copyName = nullptr;
    QLabel *m_kind = nullptr;
    QLabel *m_size = nullptr;
    QToolButton *m_pin = nullptr;
    QWidget *m_actions = nullptr;
    QHBoxLayout *m_actionsLayout = nullptr;
    QLabel *m_actionCaption = nullptr;
    QWidget *m_activity = nullptr;
    QLabel *m_activityText = nullptr;
    QProgressBar *m_activityBar = nullptr;
    NgInfoFacts *m_facts = nullptr;
    NgInfoMix *m_mix = nullptr;
    QWidget *m_lines = nullptr;
    QGridLayout *m_linesLayout = nullptr;
    QLabel *m_origin = nullptr;
    QLabel *m_originLabel = nullptr;
    QLabel *m_who = nullptr;
    QLabel *m_whoLabel = nullptr;
    QWidget *m_stars = nullptr;
    QList<QToolButton *> m_starButtons;
    QWidget *m_tags = nullptr;
    QLayout *m_tagsLayout = nullptr;
    NgInfoDates *m_dates = nullptr;
    QStringList m_tagList;
    QColor m_overlayTint;
    QList<std::pair<QString, QString>> m_overlayFacts;
    struct Action {
        QString label;
        QString icon;
        int index;
    };
    QList<Action> m_actionList; // the quick actions of the item shown: as many as fit in the pill, the rest in its menu
    int m_actionsWidth = -1;
    int m_rating = 0;
    bool m_hookedFacts = false;
};

#endif
