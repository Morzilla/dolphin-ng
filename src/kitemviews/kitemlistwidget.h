/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * Based on the Itemviews NG project from Trolltech Labs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KITEMLISTWIDGET_H
#define KITEMLISTWIDGET_H

#include "dolphin_export.h"
#include "kitemviews/kitemliststyleoption.h"

#include <QBitArray>
#include <QColor>
#include <QPalette>
#include <QGraphicsWidget>
#include <QStyle>
#include <QTimer>
#include <functional>
#include <QUrl>
#include <QHash>

class KItemListSelectionToggle;

/**
 * dolphin-ng settings. A value comes from the environment variable DOLPHIN_NG_<NAME> if it is set,
 * else from dolphinrc, group DolphinNG, key <NAME> (written by the settings page), else it is the fallback.
 */
/**
 * dolphin-ng: emits changed() after the NG settings page applied new values (ngSettingsInvalidate()).
 * Everything that reads a setting at construction or caches it listens here and re-applies it.
 */
class DOLPHIN_EXPORT NgSettings : public QObject
{
    Q_OBJECT
public:
    static NgSettings *instance();
Q_SIGNALS:
    void changed();
};
/** dolphin-ng: forget the cached values, re-read dolphinrc and tell every listener. */
DOLPHIN_EXPORT void ngSettingsInvalidate();
/** dolphin-ng: grows with every ngSettingsInvalidate(), for callers that keep a derived value. */
DOLPHIN_EXPORT int ngSettingsGeneration();
class QDateTime;
/** dolphin-ng: "oggi", "ieri", "7 giorni fa", "2 anni fa". */
DOLPHIN_EXPORT QString ngAgoText(const QDateTime &when);
// dolphin-ng: a tag's colour, the same wherever the tag is shown (cards, panel): picked from its name, from a set of
// colours that read on light and dark grounds
DOLPHIN_EXPORT QColor ngTagColour(const QString &tag);
// dolphin-ng: the tags of a local file, read from its extended attributes (as KDE writes them, without Baloo)
DOLPHIN_EXPORT QStringList ngFileTags(const QString &path);
// dolphin-ng: where a file made by a quick action came from, kept in the file itself (extended attributes
// user.dolphin-ng.made-from, the source's path, and user.dolphin-ng.made-by, the action's name), so that it travels with
// the file when it is moved or renamed. Empty strings when the file says nothing
struct NgMadeFrom {
    QString source; // the path of the file it was made from
    QString action; // "Transcribe", "Extract"
    bool isEmpty() const
    {
        return source.isEmpty();
    }
};
DOLPHIN_EXPORT NgMadeFrom ngFileMadeFrom(const QString &path);
DOLPHIN_EXPORT void ngSetFileMadeFrom(const QString &path, const QString &source, const QString &action);
DOLPHIN_EXPORT QString ngSetting(const char *name, const QString &fallback = QString());
DOLPHIN_EXPORT int ngSettingInt(const char *name, int fallback);
/** dolphin-ng: the interface text, in Italian when the desktop language is Italian and in English otherwise. */
DOLPHIN_EXPORT QString ngTr(const char *english, const char *italian);
/** dolphin-ng: the pushpin in the NeoLine style (24 grid, 1.7 round stroke), tilted, fitted in `box`; `filled`
 *  for a pinned item. Used by the quick action and by the mark on pinned items. */
DOLPHIN_EXPORT void ngDrawPin(QPainter *painter, const QRectF &box, const QColor &ink, bool filled, qreal stroke = 1.7);
/** dolphin-ng: sidebar sections folded by a click on their title. `key` names a section ("places", "recent", "pinned",
 *  "filters"...); the folded ones are kept in SIDEBAR_COLLAPSED, comma separated (Dialog NG reads it too). */
DOLPHIN_EXPORT bool ngSectionCollapsed(const QString &key);
DOLPHIN_EXPORT void ngToggleSection(const QString &key);
/** dolphin-ng: the small arrow of a section title, pointing down when open and right when folded, centred in `box`. */
DOLPHIN_EXPORT void ngDrawChevron(QPainter *painter, const QRectF &box, bool collapsed, const QColor &ink);

/**
 * dolphin-ng: work in progress on files, shown on their cards and rows. Copies, moves, extractions,
 * conversions: whoever starts the work tells which files it concerns and how far it is; the item widgets
 * read it here when they paint. A percent below 0 means "busy, no figure" (drawn as a turning arc).
 */
/**
 * dolphin-ng: the list of what is pinned to the top of its folder, for the "Pinned" section of the sidebar. The
 * pin itself is the extended attribute user.ng.pinned on the file; this list (in dolphinrc) remembers where they
 * are, filled when something is pinned or unpinned and whenever a folder shows a pinned item.
 */
/**
 * dolphin-ng: whether Ctrl or Shift is held (a selection being made). While it is, the quick actions that come with the
 * pointer stay hidden and cannot be clicked. changed() is emitted when one of the two keys goes down or up.
 */
class DOLPHIN_EXPORT NgModifiers : public QObject
{
    Q_OBJECT
public:
    static NgModifiers *self();
    static bool selecting();

Q_SIGNALS:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    NgModifiers();
    bool m_selecting = false;
};

class DOLPHIN_EXPORT NgPins : public QObject
{
    Q_OBJECT
public:
    static NgPins *self();
    QStringList paths() const;
    bool contains(const QString &path) const;
    /** records (or forgets) a path; saved and announced a moment later, together with the others */
    void set(const QString &path, bool pinned);
    /** unpins for good: takes the attribute off the file and forgets it */
    void unpin(const QString &path);
    /** forgets what no longer exists or is no longer pinned */
    void prune();

Q_SIGNALS:
    void changed();

private:
    NgPins();
    QStringList m_paths;
    QTimer m_save;
};

class DOLPHIN_EXPORT NgActivity : public QObject
{
    Q_OBJECT
public:
    struct Entry {
        int percent = -1;
        QString text;
        qint64 startedAt = 0; // ms since epoch: with the percentage, it gives the time left
    };
    static NgActivity *self();
    void set(const QUrl &url, int percent, const QString &text);
    void clear(const QUrl &url);
    bool contains(const QUrl &url) const;
    Entry entry(const QUrl &url) const;
    bool isEmpty() const;
    /** busy until the process \a pid ends (checked twice a second; it may outlive Dolphin). If the process
     *  writes a percentage (one number) to \a progressFile, that is shown; the file is removed at the end. */
    void setUntilProcessEnds(const QUrl &url, qint64 pid, const QString &text, const QString &progressFile = QString());
    /** 0..1, turns while anything is busy without a figure: the angle of the turning arc */
    qreal phase() const;

    /* Families: a file made from another one (a transcription, an extracted folder, a copy in the same
     * folder) is tied to it while the work runs and for a while after; the view keeps it right after its
     * parent and draws both in one bubble. */
    /** files that appear in \a parent's folder, named like it, while it is busy are tied to it */
    void watchForChildren(const QUrl &parent);
    /** told by the model about every file that appears: ties it to a watched parent, if it is one of its children */
    void offerNewFile(const QUrl &url);
    void link(const QUrl &parent, const QUrl &child);
    /** the parent of \a child, or an empty URL */
    QUrl parentOf(const QUrl &child) const;
    /** the family \a url belongs to (its parent's key, or its own if it has children), or empty */
    QString familyOf(const QUrl &url) const;
    /** every tie, as (child, parent) */
    QList<std::pair<QUrl, QUrl>> links() const;
    bool hasLinks() const;
    /** 0..1: how far the file has come in since it was tied to its family (1 when it is settled or not tied) */
    qreal growth(const QUrl &url) const;
    /** 1..0: how much of the family's bubble is left while it fades out after the work (1 when not fading) */
    qreal fading(const QUrl &url) const;

    /** the work on \a url can be stopped: a process of a quick action, or a job with a canceller */
    bool canCancel(const QUrl &url) const;
    void setCanceller(const QUrl &url, const std::function<void()> &cancel);
    void cancel(const QUrl &url);
    /** the colour of a family's bubble: the colour of its parent's card, told by the parent when it paints */
    void setFamilyColour(const QString &family, const QColor &colour);
    QColor familyColour(const QString &family) const;
    /** the stop button under the pointer (drawn red), or none */
    void setHoveredStop(const QUrl &url);
    bool isStopHovered(const QUrl &url) const;

Q_SIGNALS:
    void changed();
    void linksChanged();

private:
    NgActivity();
    static QString key(const QUrl &url);
    void tick();
    QHash<QString, Entry> m_entries;
    QHash<QString, qint64> m_pids;
    QHash<QString, QString> m_progressFiles;
    void parentDone(const QString &parentKey);
    QHash<QString, std::pair<QUrl, QUrl>> m_links; // child key -> (child, parent)
    QHash<QString, QUrl> m_watched; // parent key -> parent
    QHash<QString, qint64> m_linkedAt; // child key -> when it was tied (ms since epoch): it grows in
    QHash<QString, std::function<void()>> m_cancellers;
    QString m_hoveredStop;
    QHash<QString, QColor> m_familyColours;
    QHash<QString, qint64> m_fadingSince; // family (parent key) -> when its bubble began to fade out
    QHash<QString, qint64> m_idleSince; // family (parent key) -> since when none of its files is at work
    QTimer m_sweep; // once a second while there are ties: ends the ties of families idle for a while
    void sweepFamilies();
    /* processes of quick actions outlive Dolphin: they are written down, so a Dolphin started later shows them */
    void saveProcesses();
    void loadProcesses();
    bool growing() const;
    QTimer m_timer;
    qreal m_phase = 0;
};
/** Store a value in dolphinrc; an empty value removes it. */
DOLPHIN_EXPORT void setNgSetting(const char *name, const QString &value);
/** True if the environment decides this setting, so the stored value is ignored. */
DOLPHIN_EXPORT bool ngSettingFromEnvironment(const char *name);

/**
 * dolphin-ng: how the row background is drawn in views that use row
 * backgrounds (the details view). Chosen with DOLPHIN_NG_ROWS:
 * card (default), line, stripe (stock Dolphin) or none.
 */
enum class NgRowStyle { Stripe, Card, Line, None, Open };
DOLPHIN_EXPORT NgRowStyle ngRowStyle();
/** dolphin-ng: change the row style now and remember it (dolphinrc, group DolphinNG). */
DOLPHIN_EXPORT void setNgRowStyle(NgRowStyle style);

/**
 * dolphin-ng: the details view shows each item on two lines (name; then the other columns joined,
 * with the date at the right edge) instead of in columns. DOLPHIN_NG_LIST=columns restores the columns.
 */
DOLPHIN_EXPORT bool ngTwoLineList();
// dolphin-ng: the same, for one view: false in a view that asked to stay plain ("_ng_plain" on it or a parent: the
// tree of the Folders panel keeps Dolphin's own rows)
DOLPHIN_EXPORT bool ngTwoLineList(const QGraphicsItem *item);
DOLPHIN_EXPORT bool ngPlainItem(const QGraphicsItem *item);

/**
 * dolphin-ng: the two-line list has no icons; the file type is a large, faint abbreviation in the row
 * (a watermark), the size has a bar and hovering a row shows its actions. DOLPHIN_NG_ICONS=show keeps the icons.
 */
DOLPHIN_EXPORT bool ngNoIcons();
DOLPHIN_EXPORT bool ngNoIcons(const QGraphicsItem *item);
// dolphin-ng: the drawn rows with columns of attributes after the name (ROW_COLUMNS=on), under Dolphin's own header
DOLPHIN_EXPORT bool ngRowColumns();

/** dolphin-ng: is this a dark colour scheme? The fork's own painting picks its shades from the answer. */
DOLPHIN_EXPORT bool ngIsDark(const QPalette &palette);
/** dolphin-ng: \a colour made readable as an accent on the scheme: lightened on dark, darkened on light. */
DOLPHIN_EXPORT QColor ngAccent(const QColor &colour, bool dark);

/** dolphin-ng: in the icons view every item is a dark rounded tile and previews have rounded corners. DOLPHIN_NG_TILES=off disables. */
DOLPHIN_EXPORT bool ngTiles();
/** dolphin-ng: the Aurora look of Breeze NG is in use (glass without rims): set by the main window. */
DOLPHIN_EXPORT bool ngAurora();
DOLPHIN_EXPORT void ngSetAurora(bool aurora);
// Breeze NG Aurora: the veil every surface wears at rest (ink over the window's glass), one intensity for the sidebar,
// the cards and the lists, one for buttons and fields; both from Breeze NG's settings (breezengrc), the same by default
DOLPHIN_EXPORT void ngLoadAuroraSettings();
DOLPHIN_EXPORT qreal ngAuroraVeilAlpha(bool dark, bool button = false);
// Aurora on a light scheme: the frost of its surfaces, white with a tinge of the accent (and as "r, g, b" for style sheets)
DOLPHIN_EXPORT QColor ngAuroraFrost(bool button = true);
DOLPHIN_EXPORT QString ngAuroraFrostRgb();
// a light surface of Aurora on a light scheme with the accent's tinge (amount: how much of it), as the frost
DOLPHIN_EXPORT QColor ngAuroraTinted(const QColor &colour, qreal amount = 0.15);
DOLPHIN_EXPORT QColor ngAuroraVeil(bool dark, bool button = false);
// the accent as a light: its hue, always luminous (as the style's Helper::auroraAccent)
DOLPHIN_EXPORT QColor ngAuroraAccent(const QPalette &palette, qreal alpha);
// Aurora: what is selected or switched on: a light sheet of the accent, or the full accent with white text (Breeze
// NG's setting AuroraFullSelection); \a more adds to it (hover)
DOLPHIN_EXPORT bool ngAuroraFull();
DOLPHIN_EXPORT QColor ngAuroraSelection(const QPalette &palette, qreal more = 0.0);
DOLPHIN_EXPORT QColor ngAuroraSelectionText(const QPalette &palette);
// Breeze NG's "Shadow around text on glass" (breezengrc), and whether the text Dolphin NG draws in its views gets it
// now (Aurora: the views stand on the window's glass; not where the window is solid for want of blur)
DOLPHIN_EXPORT int ngTextShadowMode(); // 0 never, 1 where the contrast may be low, 2 always
DOLPHIN_EXPORT bool ngTextShadowSetting(); // not "never"
// on glass of this opacity (0..1), could a white (or black) window behind leave the window's text below 4.5:1?
DOLPHIN_EXPORT bool ngLowContrastOnGlass(qreal opacity);
DOLPHIN_EXPORT bool ngTextHalo();
// the shadow around a text in @p textColour: @p draw paints the text at the offset it is given, in the pen set here
// dolphin-ng: a dimmed text colour for glass with the text shadow on: solid and nearly as strong as the text's, instead of
// see-through (a see-through grey took the colour of whatever lay behind the window, and over a white window light
// grey text vanished); the colour itself otherwise
DOLPHIN_EXPORT QColor ngHaloInk(const QColor &ink, const QPalette &palette);
// dolphin-ng: the text colour on the accent: the colour scheme's own selection text (as the style shows a selection),
// unless it reads badly on the accent (then black or white, whichever reads)
DOLPHIN_EXPORT QColor ngSelectionText(const QPalette &palette);
// dolphin-ng: the application's widget style is Breeze NG (else: stock Breeze or another style, which Dolphin NG dresses
// itself where it must)
DOLPHIN_EXPORT bool ngWidgetStyleIsNg();
// dolphin-ng: with another widget style than Breeze NG the window is one grey gradient, top to bottom, and the sidebar
// and the Information panel stand on it without a card of their own. @p windowRect is the whole window's, in the
// painter's coordinates
DOLPHIN_EXPORT void ngPaintWindowGradient(QPainter *painter, const QRectF &windowRect, const QPalette &palette);
DOLPHIN_EXPORT void ngPaintTextHalo(QPainter *painter, const QColor &textColour, const std::function<void(const QPointF &)> &draw, qreal strength = 1.0);
// the sidebar's text gets the shadow now (decided by the main window, see DolphinMainWindow::ngApplyTextHalo)
DOLPHIN_EXPORT bool ngSidebarHalo();
// the shadow of a shape (an icon, a painted row) in the tone opposite to @p textColour, under it at @p topLeft
DOLPHIN_EXPORT void ngPaintShapeHalo(QPainter *painter, const QImage &shape, const QPointF &topLeft, const QColor &textColour, qreal strength = 1.0, bool keep = true);
// dolphin-ng: the colour of text and icons on a solid fill of \a fill (an accent): white on a dark one, black on a
// light one
DOLPHIN_EXPORT QColor ngOnAccent(const QColor &fill);

/**
 * dolphin-ng: the look of the cards view (DOLPHIN_NG_CARDS = a | b | c):
 *   Tessera  a thumbnail band on top, then the name, chips and a label/value list
 *   Scheda   the icon in a column on the left, three lines of text on the right
 *   Dossier  the list row folded: icon and name, chips, the dates on the left, the big "ago" on the right
 */
enum class NgCardStyle { Tessera, Scheda, Dossier };
class QGraphicsItem;
class QUrl;
DOLPHIN_EXPORT NgCardStyle ngCardStyle(); // the default look (NG settings)
// the look of the cards of the view this item belongs to: the folder's own when it has one, else the default
DOLPHIN_EXPORT NgCardStyle ngCardStyle(const QGraphicsItem *item);
// a folder's own look of cards: "a", "b", "c", or empty for the default
// PANELS_EDGE=flush: the sidebar and the Information panel against the window's edges (no margin on the outer sides)
DOLPHIN_EXPORT bool ngPanelsFlush();
DOLPHIN_EXPORT QString ngFolderCardLook(const QUrl &url);
// dolphin-ng: the Details view (the drawn rows with columns): a folder's own choice ("on", "off", or empty for the
// default, ROW_COLUMNS), kept beside its look of cards
DOLPHIN_EXPORT QString ngFolderColumns(const QUrl &url);
DOLPHIN_EXPORT void ngSetFolderColumns(const QUrl &url, const QString &columns);
// the view the item is in shows columns: its folder's choice (property _ng_row_columns of the view), else ROW_COLUMNS
DOLPHIN_EXPORT bool ngRowColumns(const QGraphicsItem *item);
// DETAILS_DENSITY=compact: the rows with columns in one line, as low as the text
DOLPHIN_EXPORT bool ngDetailsCompact();
// DETAILS_LOOK: the Details view plain (the default: the file's own icon, no card or colour wash on the rows) or "tiles"
DOLPHIN_EXPORT bool ngDetailsPlain();
DOLPHIN_EXPORT void ngSetFolderCardLook(const QUrl &url, const QString &look);
/** dolphin-ng: cards grow to fill the row (DOLPHIN_NG_CARDS_FIT=stretch) instead of keeping their size. */
DOLPHIN_EXPORT bool ngCardsStretch();

/** dolphin-ng: a command that makes sense for a file of its kind (resize or convert a picture, extract an archive...). */
struct NgQuickAction {
    QString id; // stable: "to-mp3", "cut"... or "custom:<file>" for the user's own (see ngActionsFolder())
    QString label;
    QString program;
    QStringList arguments; // used when there are no choices
    QString icon; // theme icon name (for "@app": the icon of the application)
    bool main = false; // the typical action of this kind of file: shown first, with its label
    QList<std::pair<QString, QStringList>> choices; // a menu: label and arguments, all run with the program
    bool confirm = false; // ask before running it
};

/**
 * dolphin-ng: the quick actions as the editor (Settings > NG quick actions) sees them. Every file belongs to one
 * kind ("image", "pdf", "audio", "video", "archive"), or to none; "all" holds what every file has (applications,
 * open with, cut, copy, pin). The user's own actions are .desktop files in ngActionsFolder() and belong to a kind
 * or to a pattern of their own ("*.md", "text/markdown"). Per list, dolphinrc [DolphinNG QuickActions] keeps the
 * order (Order_<kind>), what is switched off (Off_<kind>) and the main action (Main_<kind>).
 */
struct NgActionInfo {
    QString id;
    QString label;
    QString icon;
    QString command; // the user's: with {file} {name} {ext} {dir} {choice}; built-in: a readable sketch
    QString kind; // a kind above or a pattern
    QStringList choices; // the menu {choice} takes its value from
    bool confirm = false;
    bool custom = false;
};
DOLPHIN_EXPORT QStringList ngActionKinds(); // the built-in kinds, then "all"
DOLPHIN_EXPORT QString ngActionKindName(const QString &kind); // "Videos", "*.md"
DOLPHIN_EXPORT QString ngActionKindOf(const QString &mime); // "" when the type has no kind
DOLPHIN_EXPORT QList<NgActionInfo> ngBuiltinActions(const QString &kind);
DOLPHIN_EXPORT QList<NgActionInfo> ngCustomActions(); // all of them, read from ngActionsFolder()
DOLPHIN_EXPORT QString ngActionsFolder(); // ~/.config/dolphin-ng/actions
DOLPHIN_EXPORT bool ngWriteCustomAction(const NgActionInfo &action); // to <folder>/<id without "custom:">.desktop
DOLPHIN_EXPORT void ngQuickActionsChanged(); // after the editor applied: the cached lists are built again
/** dolphin-ng: the quick actions of a local file, from its path and MIME type; empty for folders and unknown kinds. */
DOLPHIN_EXPORT QList<NgQuickAction> ngQuickActionsFor(const QString &localPath, const QString &mime);

/** dolphin-ng: what replaces the icon — a short type abbreviation, the colour and the category of the file. */
struct NgKind {
    enum Category { Folder, Image, Document, Sheet, Archive, Media, Data, Package, Other, CategoryCount };
    QString sigla;
    QColor tint;
    Category category;
};
DOLPHIN_EXPORT NgKind ngKindFor(const QString &name, bool isDir);
DOLPHIN_EXPORT QColor ngCategoryTint(NgKind::Category category);
class KItemListView;
class QPropertyAnimation;

/**
 * @brief Provides generic information for all KItemListWidgets
 * for which the construction of any specific KItemListWidget isn't required.
 *
 * KItemListView only creates KItemListWidget instances for the visible
 * area. For calculating the required size of all items the expected
 * size for the invisible items must be accessible. KItemListWidgetInformant
 * provides this information.
 */
class DOLPHIN_EXPORT KItemListWidgetInformant
{
public:
    KItemListWidgetInformant();
    virtual ~KItemListWidgetInformant();

    virtual void calculateItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const = 0;

    virtual qreal preferredRoleColumnWidth(const QByteArray &role, int index, const KItemListView *view) const = 0;
};

/**
 * @brief Widget that shows a visible item from the model.
 *
 * For showing an item from a custom model it is required to at least overwrite KItemListWidget::paint().
 * All properties are set by KItemListView, for each property there is a corresponding
 * virtual protected method that allows to react on property changes.
 */
class DOLPHIN_EXPORT KItemListWidget : public QGraphicsWidget
{
    Q_OBJECT

public:
    KItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent);
    ~KItemListWidget() override;

    void setIndex(int index);
    int index() const;

    void setData(const QHash<QByteArray, QVariant> &data, const QSet<QByteArray> &roles = QSet<QByteArray>());
    QHash<QByteArray, QVariant> data() const;
    QVariant value(const QByteArray &key) const;

    /**
     * Draws the hover-rectangle if the item is hovered. Overwrite this method
     * to show the data of the custom model provided by KItemListWidget::data().
     * @reimp
     */
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;

    void setVisibleRoles(const QList<QByteArray> &roles);
    QList<QByteArray> visibleRoles() const;

    /**
     * Sets the width of a role that should be used if the alignment of the content
     * should be done in columns.
     */
    void setColumnWidth(const QByteArray &role, qreal width);
    qreal columnWidth(const QByteArray &role) const;

    void setSidePadding(qreal leftPaddingWidth, qreal rightPaddingWidth);
    qreal leftPadding() const;
    qreal rightPadding() const;

    void setStyleOption(const KItemListStyleOption &option);
    const KItemListStyleOption &styleOption() const;

    // TODO: Hides QGraphicsItem::setSelected()/isSelected(). Replace
    // this by using the default mechanism.
    void setSelected(bool selected);
    bool isSelected() const;

    void setCurrent(bool current);
    bool isCurrent() const;

    void setHovered(bool hovered);
    bool isHovered() const;

    /** Sets a purely visual pressed highlight effect. */
    void setPressed(bool enabled);
    bool isPressed() const;

    void setExpansionAreaHovered(bool hover);
    bool expansionAreaHovered() const;

    void setHoverPosition(const QPointF &pos);

    void setAlternateBackground(bool enable);
    bool alternateBackground() const;

    void setEnabledSelectionToggle(bool enabled);
    bool enabledSelectionToggle() const;

    /**
     * Sets the sibling information for the item and all of its parents.
     * The sibling information of the upper most parent is represented by
     * the first bit, the sibling information of the item by the last bit.
     * The sibling information is useful for drawing the branches in
     * tree views.
     */
    void setSiblingsInformation(const QBitArray &siblings);
    QBitArray siblingsInformation() const;

    /**
     * Allows the user to edit the role \a role. The signals
     * roleEditingCanceled() or roleEditingFinished() will be
     * emitted after editing. An ongoing editing gets canceled if
     * the role is empty. Derived classes must implement
     * editedRoleChanged().
     */
    void setEditedRole(const QByteArray &role);
    QByteArray editedRole() const;

    /**
     * Contains the actual icon size used to draw the icon.
     * Also used during icon resizing animation.
     */
    void setIconSize(int iconSize);
    int iconSize() const;

    /**
     * @return True if \a point is inside KItemListWidget::selectionRectFull(),
     *         KItemListWidget::selectionToggleRect()
     *         or KItemListWidget::expansionToggleRect().
     * @reimp
     */
    bool contains(const QPointF &point) const override;

    /**
     * @return Rectangle for the area that contains the text-properties.
     */
    virtual QRectF textRect() const = 0;

    /**
     * @return Focus rectangle for indicating the current item. Per default
     *         textRect() will be returned. Overwrite this method if textRect()
     *         provides a larger rectangle than the actual text (e.g. to
     *         be aligned with the iconRect()). The textFocusRect() may not be
     *         outside the boundaries of textRect().
     */
    virtual QRectF textFocusRect() const;

    /**
     * Used for drawing the visuals, and situations where we want the behavior of the
     * selection to match the visuals.
     *
     * @return The rectangle around selection.
     */
    virtual QRectF selectionRectFull() const = 0;

    /**
     * @return The core area of the item. All of it reacts exactly the same way to mouse clicks.
     */
    virtual QRectF selectionRectCore() const = 0;

    /**
     * @return Rectangle for the selection-toggle that is used to select or deselect an item.
     *         Per default an empty rectangle is returned which means that no selection-toggle
     *         is available.
     */
    virtual QRectF selectionToggleRect() const;

    /**
     * dolphin-ng: which inline action of the row is at \a pos (in widget coordinates):
     * 0 none, 1 open, 2 menu. The controller handles the click, like for the selection toggle.
     */
    virtual int ngActionAt(const QPointF &pos) const;
    /** dolphin-ng: the NG settings changed; drop whatever layout was derived from them. */
    virtual void ngSettingsChanged();

    /**
     * @return Rectangle for the expansion-toggle that is used to open a sub-tree of the model.
     *         Per default an empty rectangle is returned which means that no opening of sub-trees
     *         is supported.
     */
    virtual QRectF expansionToggleRect() const;

    /**
     * @return Pixmap that is used when dragging an item. Per default the current state of the
     *         widget is returned as pixmap.
     */
    virtual QPixmap createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr);

    /**
     * Starts an animation that makes clear that the item will be activated soon.
     * @param timeUntilActivation time in milliseconds until the item will activate
     */
    virtual void startActivateSoonAnimation(int timeUntilActivation);

Q_SIGNALS:
    void roleEditingCanceled(int index, const QByteArray &role, const QVariant &value);
    void roleEditingFinished(int index, const QByteArray &role, const QVariant &value);

protected:
    virtual void dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles = QSet<QByteArray>());
    virtual void visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous);
    virtual void columnWidthChanged(const QByteArray &role, qreal current, qreal previous);
    virtual void sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth);
    virtual void styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous);
    virtual void currentChanged(bool current);
    virtual void selectedChanged(bool selected);
    virtual void hoveredChanged(bool hovered);
    virtual void pressedChanged(bool pressed);
    virtual void alternateBackgroundChanged(bool enabled);
    virtual void siblingsInformationChanged(const QBitArray &current, const QBitArray &previous);
    virtual void editedRoleChanged(const QByteArray &current, const QByteArray &previous);
    virtual void iconSizeChanged(int current, int previous);
    void resizeEvent(QGraphicsSceneResizeEvent *event) override;
    void clearHoverCache();

    /**
     * Called when the user starts hovering this item.
     */
    virtual void hoverSequenceStarted();

    /**
     * Called in regular intervals while the user is hovering this item.
     *
     * @param sequenceIndex An index that increases over time while the user hovers.
     */
    virtual void hoverSequenceIndexChanged(int sequenceIndex);

    /**
     * Called when the user stops hovering this item.
     */
    virtual void hoverSequenceEnded();

    /**
     * @return The current opacity of the hover-animation. When implementing a custom painting-code for a hover-state
     *         this opacity value should be respected.
     */
    qreal hoverOpacity() const;

    int hoverSequenceIndex() const;

    const KItemListWidgetInformant *informant() const;

private Q_SLOTS:
    void slotHoverSequenceTimerTimeout();

private:
    void initializeSelectionToggle();
    void setHoverOpacity(qreal opacity);
    void drawItemStyleOption(QPainter *painter, QWidget *widget, QStyle::State styleState);

protected:
    /** dolphin-ng: true if this item paints its own hover and selection (two-line rows, cards). */
    virtual bool ngOwnSelection() const;
    /** dolphin-ng: the corner radius of that shape; above 10 the shape is the whole item (a card). */
    virtual qreal ngSelectionRadius() const;
    /** dolphin-ng: the selection is a ring set off around the item, with a check (cards), not a tinted fill */
    virtual bool ngRingSelection() const;
    /** dolphin-ng: how far inside the item its selection ring sits (rows: their card is inset) */
    virtual qreal ngBlockInset() const { return 0; }
    /** dolphin-ng: no hover look (a file at work, or one of a family: the bubble and the progress say enough) */
    virtual bool ngNoHover() const;
    /** dolphin-ng: the check of a ring selection, drawn by the item over its content */
    void ngPaintCheck(QPainter *painter, QWidget *widget);
    /** dolphin-ng: the outline of the block of selected cards this one is part of (item coordinates); \a reach
     *  receives the part this item draws */
    QPainterPath ngSelectionBlock(qreal grow, QRectF *reach, qreal *alpha = nullptr) const;

private:

private:
    KItemListWidgetInformant *m_informant;
    int m_index;
    bool m_selected;
    // dolphin-ng: 0..1, how far the ring selection of a card has come out (animated on select and deselect)
    qreal m_ngSelProgress = 0;
    class QVariantAnimation *m_ngSelAnim = nullptr;
public:
    qreal ngSelectionProgress() const { return m_ngSelProgress; }
private:
    bool m_current;
    bool m_hovered;
    bool m_expansionAreaHovered;
    bool m_alternateBackground;
    bool m_enabledSelectionToggle;
    bool m_clickHighlighted;
    QHash<QByteArray, QVariant> m_data;
    QList<QByteArray> m_visibleRoles;
    QHash<QByteArray, qreal> m_columnWidths;
    qreal m_leftPadding;
    qreal m_rightPadding;
    KItemListStyleOption m_styleOption;
    QBitArray m_siblingsInfo;

    qreal m_hoverOpacity;
    mutable QPixmap *m_hoverCache;

    int m_hoverSequenceIndex;
    QTimer m_hoverSequenceTimer;

    KItemListSelectionToggle *m_selectionToggle;

    QByteArray m_editedRole;
    int m_iconSize;
};

inline const KItemListWidgetInformant *KItemListWidget::informant() const
{
    return m_informant;
}

#endif
