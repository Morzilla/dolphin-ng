/*
 * SPDX-FileCopyrightText: 2012 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KSTANDARDITEMLISTWIDGET_H
#define KSTANDARDITEMLISTWIDGET_H

#include "dolphin_export.h"
#include "kitemviews/kitemlistwidget.h"

#include <QPixmap>
#include <QPointer>
#include <QPointF>
#include <QStaticText>

class KItemListRoleEditor;
class KItemListStyleOption;
class KItemListView;
class QVariantAnimation;

/**
 * @brief standard implementation of the ItemList widget informant for use with KStandardItemListView and KStandardItemModel.
 *
 * @see KItemListWidgetInformant
 */
class DOLPHIN_EXPORT KStandardItemListWidgetInformant : public KItemListWidgetInformant
{
public:
    KStandardItemListWidgetInformant();
    ~KStandardItemListWidgetInformant() override;

    void calculateItemSizeHints(QVector<std::pair<qreal /* height */, bool /* isElided */>> &logicalHeightHints,
                                qreal &logicalWidthHint,
                                const KItemListView *view) const override;

    qreal preferredRoleColumnWidth(const QByteArray &role, int index, const KItemListView *view) const override;

protected:
    /**
     * @return The value of the "text" role. The default implementation returns
     *         view->model()->data(index)["text"]. If a derived class can
     *         prevent the (possibly expensive) construction of the
     *         QHash<QByteArray, QVariant> returned by KItemModelBase::data(int),
     *         it can reimplement this function.
     */
    virtual QString itemText(int index, const KItemListView *view) const;

    /**
     * @return The value of the "isLink" role. The default implementation returns false.
     *         The derived class should reimplement this function, when information about
     *         links is available and in usage.
     */
    virtual bool itemIsLink(int index, const KItemListView *view) const;

    /** Configure whether the requested text should be optimized for viewing on a screen or for being read out aloud by a text-to-speech engine. */
    enum class ForUsageAs { DisplayedText, SpokenText };

    /**
     * @param role          The role the text is being requested for.
     * @param values        The data of the item. All the data is passed because the text might depend on multiple data points.
     * @param forUsageAs    Whether the roleText should be optimized for displaying (i.e. kept somewhat short) or optimized for speaking e.g. by screen readers
     *                      or text-to-speech in general (i.e. by preferring announcing a month as July instead of as the number 7).
     * @return String representation of the role \a role. The representation of
     *         a role might depend on other roles, so the values of all roles
     *         are passed as parameter.
     */
    virtual QString roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values, ForUsageAs forUsageAs = ForUsageAs::DisplayedText) const;

    /**
    * @return A font based on baseFont which is customized for symlinks.
    */
    virtual QFont customizedFontForLinks(const QFont &baseFont) const;

    void calculateIconsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const;
    void calculateCompactLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const;
    void calculateDetailsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const;
    void calculateCardsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const; // dolphin-ng

    friend class KStandardItemListWidget; // Accesses roleText()
};

/**
 * @brief standard implementation of an ItemList widget for KStandardItemListView and KStandardItemModel.
 *
 * @see KItemListWidget
 */
class DOLPHIN_EXPORT KStandardItemListWidget : public KItemListWidget
{
    Q_OBJECT

public:
    enum Layout { IconsLayout, CompactLayout, DetailsLayout, CardsLayout /* dolphin-ng */ };

    KStandardItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent);
    ~KStandardItemListWidget() override;

    void setLayout(Layout layout);

    void setHighlightEntireRow(bool highlightEntireRow);
    bool highlightEntireRow() const;

    void setSupportsItemExpanding(bool supportsItemExpanding);
    bool supportsItemExpanding() const;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;
    /** dolphin-ng: cards paint a little outside themselves (the bubble around files tied together) */
    QRectF boundingRect() const override;

    QRectF textRect() const override;
    QRectF textFocusRect() const override;
    QRectF selectionRectFull() const override;
    QRectF selectionRectCore() const override;
    QRectF expansionToggleRect() const override;
    QRectF selectionToggleRect() const override;
    int ngActionAt(const QPointF &pos) const override;
    void ngSettingsChanged() override;
    QPixmap createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;
    /** @see KItemListWidget::startActivateSoonAnimation() */
    void startActivateSoonAnimation(int timeUntilActivation) override;

    static KItemListWidgetInformant *createInformant();

protected:
    /**
     * Invalidates the cache which results in calling KStandardItemListWidget::refreshCache() as
     * soon as the item need to gets repainted.
     */
    void invalidateCache();

    /**
     * Invalidates the icon cache which results in calling KStandardItemListWidget::refreshCache() as
     * soon as the item needs to get repainted.
     */
    void invalidateIconCache();

    /**
     * Is called if the cache got invalidated by KStandardItemListWidget::invalidateCache().
     * The default implementation is empty.
     */
    virtual void refreshCache();

    /**
     * @return True if the give role should be right aligned when showing it inside a column.
     *         Per default false is returned.
     */
    virtual bool isRoleRightAligned(const QByteArray &role) const;

    /**
     * @return True if the item should be visually marked as hidden item. Per default
     *         false is returned.
     */
    virtual bool isHidden() const;

    /**
     * @return A font based on baseFont which is customized according to the data shown in the widget.
     */
    virtual QFont customizedFont(const QFont &baseFont) const;

    virtual QPalette::ColorRole normalTextColorRole() const;

    void setTextColor(const QColor &color);
    QColor textColor(const QWidget &widget) const;

    void setOverlays(QHash<Qt::Corner, QString> &overlay);
    QHash<Qt::Corner, QString> overlays() const;

    /**
     * @see KStandardItemListWidgetInformant::roleText().
     */
    QString roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values) const;

    static int numberOfUnicodeCharactersIn(const QString &text);

    /**
     * @return Selection length (with or without MIME-type extension) in number of unicode characters, which might be different from number of QChars.
     */
    virtual int selectionLength(const QString &text) const;

    void dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles = QSet<QByteArray>()) override;
    void visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous) override;
    void columnWidthChanged(const QByteArray &role, qreal current, qreal previous) override;
    void sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth) override;
    void styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous) override;
    void hoveredChanged(bool hovered) override;
    void selectedChanged(bool selected) override;
    void pressedChanged(bool pressed) override;
    void siblingsInformationChanged(const QBitArray &current, const QBitArray &previous) override;
    void editedRoleChanged(const QByteArray &current, const QByteArray &previous) override;
    void iconSizeChanged(int current, int previous) override;
    void resizeEvent(QGraphicsSceneResizeEvent *event) override;
    void moveEvent(QGraphicsSceneMoveEvent *event) override; // dolphin-ng: the bubble depends on where the others are
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool event(QEvent *event) override;

    struct TextInfo {
        QPointF pos;
        QStaticText staticText;
    };
    void updateAdditionalInfoTextColor();

public Q_SLOTS:
    void finishRoleEditing();
    void cancelRoleEditing();
    void updateRoleEditorGeometry();

private Q_SLOTS:
    void slotCutItemsChanged();
    void slotRoleEditingCanceled(const QByteArray &role, const QVariant &value);
    void slotRoleEditingFinished(const QByteArray &role, const QVariant &value);

private:
    void triggerCacheRefreshing();
    void updateExpansionArea();
    qreal ngExpansionIndent() const; // dolphin-ng: room for the expansion arrow before a drawn row
    void updatePixmapCache();

    void updateTextsCache();
    void updateIconsLayoutTextCache();
    void updateCompactLayoutTextCache();
    void updateDetailsLayoutTextCache();
    // dolphin-ng: the cards view
    void updateCardsLayoutTextCache();
    void ngPlaceCardPixmap();
    void paintNgCardSheet(QPainter *painter);
    bool ngOwnSelection() const override;
    qreal ngSelectionRadius() const override;
    bool ngRingSelection() const override;
    qreal ngBlockInset() const override;
    bool ngNoHover() const override;
    void paintNgCard(QPainter *painter, QWidget *widget);
    void paintNgActivity(QPainter *painter); // work in progress on this file (NgActivity): a ring on cards, a fill on rows
    void paintNgFamily(QPainter *painter); // files tied together (NgActivity families): one bubble around them
    void paintNgFold(QPainter *painter);
    /** dolphin-ng: a pushpin on an item pinned to the top of the folder */
    bool ngDemoHovered() const;
    bool ngSortedByUse() const; // dolphin-ng: the view is sorted by when files were used (Recent Files)
    void paintNgPin(QPainter *painter); // CARDS_BADGE=fold: the folded glass corner with the type and size
    void paintNgPreviewObject(QPainter *painter, const QRectF &band, const QPixmap &pixmap, bool whole = false);
    void paintNgCardTags(QPainter *painter);
    qreal ngLayoutActionPill(const QList<NgQuickAction> &quickActions, qreal x, qreal y, qreal right, qreal actionHeight, const QFontMetricsF &chipFm, bool alignRight,
                             const QList<int> &origin = {});
    void paintNgActionPill(QPainter *painter, const QColor &text, bool dark);

    /** Draw the icon overlays at a fixed size in the corners of the on-screen
     *  icon. Must be called after the icon itself has been drawn. */
    void drawOverlays(QPainter *painter) const;

    void drawPixmap(QPainter *painter, const QPixmap &pixmap);
    /** Draw the lines and arrows that visualize the expanded state and level of this row. */
    void drawSiblingsInformation(QPainter *painter);

    QRectF roleEditingRect(const QByteArray &role) const;

    QString elideText(QString text, qreal elidingWidth) const;

    /**
     * Escapes text for display purposes.
     *
     * Replaces '\n' with Unicode line break (U+21B5).
     */
    QString escapeString(const QString &text) const;

    /**
     * Closes the role editor and returns the focus back
     * to the KItemListContainer.
     */
    void closeRoleEditor();

    QPixmap pixmapForIcon(const QString &name, const QSize &size, QIcon::Mode mode) const;

    /**
     * @return Preferred size of the rating-image based on the given
     *         style-option. The height of the font is taken as
     *         reference.
     */
    static QSizeF preferredRatingSize(const KItemListStyleOption &option);

    /**
     * @return Horizontal padding in pixels that is added to the required width of
     *         a column to display the content.
     */
    static qreal columnPadding(const KItemListStyleOption &option);

    /** @returns whether the usual icon should be shown or not. */
    bool isIconControlledByActivateSoonAnimation() const;

protected:
    QHash<QByteArray, TextInfo *> m_textInfo; // PlacesItemListWidget needs to access this

private:
    bool m_isCut;
    bool m_isHidden;
    QFont m_customizedFont;
    QFontMetrics m_customizedFontMetrics;
    bool m_isExpandable;
    bool m_highlightEntireRow;
    bool m_supportsItemExpanding;

    bool m_dirtyLayout;
    bool m_dirtyContent;
    QSet<QByteArray> m_dirtyContentRoles;

    Layout m_layout;
    QPointF m_pixmapPos;
    QPixmap m_pixmap;
    QSize m_scaledPixmapSize; //Size of the pixmap in device independent pixels

    qreal m_columnWidthSum;
    QRectF m_iconRect; // Cache for KItemListWidget::iconRect()

    QRectF m_textRect;

    // dolphin-ng: icon-less row (see ngNoIcons())
    QString m_ngSigla; // file type abbreviation drawn as a watermark
    int m_ngCategory = 0; // NgKind::Category of the file, for the line glyph of the type mark
    QString m_ngIconName; // type icon drawn, faint, before the abbreviation
    QPixmap m_ngIcon;
    bool m_ngIconFaded = false;
    QColor m_ngTint; // colour of the file's category
    QByteArray m_ngDateRole; // the date role shown at the right edge, if any
    QString m_ngAgo; // "3 giorni fa", under the date
    QString m_ngCount; // folders: "20 elementi" under the size of the contents
    struct NgChip {
        QString text;
        QRectF rect;
        bool tinted = false; // the type chip takes the colour of the row
        QColor colour; // a tag: its own colour
    };
    QList<NgChip> m_ngChips; // second line under the name: type and other details as small pills
    struct NgTimeLine {
        QString letter; // "C" / "M"
        QString text; // "12/10/26 · 29 giorni fa"
        QPointF letterPos;
        QPointF textPos;
    };
    QList<NgTimeLine> m_ngTime; // the time column: creation and modification
    QPointF m_ngCountPos;
    QPointF m_ngAgoPos;
    QRectF m_ngBarRect; // size bar
    qreal m_ngBarFraction = -1;
    QRectF m_ngActionsRect; // where the hover actions go
    QGraphicsWidget *m_ngActions = nullptr;
    // dolphin-ng: the cards view
    struct NgCardText {
        QString text;
        QPointF pos; // baseline origin
        QFont font;
        int tone = 0; // 0 additional-info colour, 1 text colour, 2 dimmer
    };
    QList<NgCardText> m_ngCardTexts;
    QRectF m_ngCardThumb; // the thumbnail band (Tessera) or the icon area
    QRectF m_ngCardHeader; // Dossier: the header in the colour of the type
    // Dossier: the folder's bar and its legend (filled shapes), the hairlines between the facts and above the dates,
    // the small sign before the origin line; the file whose facts the card waits for
    QList<std::pair<QRectF, QColor>> m_ngDossierShapes;
    QList<QLineF> m_ngDossierRules;
    QRectF m_ngDossierSignRect;
    QString m_ngDossierSign;
    QString m_ngFactsPath;
    bool m_ngFactsHooked = false;
    QString m_ngTagsPath; // the card's file, for the notice that its tags changed
    bool m_ngTagsHooked = false;
    qreal m_ngCardIconSide = 0; // side of the icon when the pixmap is not a preview
    bool m_ngCardPreview = false; // the pixmap is a preview, not the icon of the type
    bool m_ngCardWatermark = false; // the type is drawn as the background instead of the icon
    bool m_ngShowedActivity = false; // painted with an activity mark last time: repaint once more when it ends
    bool m_ngShowedFamily = false; // painted inside a bubble last time
    bool m_ngCutPass = false; // painting a cut item into its grey image (paint() calls itself once)
    QRectF m_ngCancelRect; // the stop button on a file at work, where it was painted last
    QRectF m_ngCardEye; // hover actions: preview and menu
    QRectF m_ngCardMenu;
    QList<QPixmap> m_ngCardActionIcons; // one per entry of m_ngCardActions
    QList<qreal> m_ngCardActionSeps; // x of the hairlines between the groups
    QRectF m_ngCardActionsPill;
    QPixmap m_ngBadgeGlass; // the blurred piece of the picture under a glass badge
    qint64 m_ngBadgeGlassKey = 0;
    int m_ngBadgeGlassShade = 70; // how dark the veil over it is: more over a light picture, so the white text reads
    // CARDS_BADGE=fold: type and size written on the back of the band's corner, folded over like a page in glass
    // CARDS_BADGE=inside: the type and the size written into the drawing of the file
    bool m_ngInfoInside = false;
    QString m_ngInfoSize;
    QString m_ngFoldType;
    QString m_ngFoldSize;
    qreal m_ngFoldFill = 0; // 0..1, how heavy the file is (log scale): the larger, the larger the fold
    QPixmap m_ngFoldGlass; // the blurred piece of the picture under the fold
    qint64 m_ngFoldGlassKey = 0;
    QRect m_ngFoldGlassSource;
    QRectF m_ngCardFab; // Tessera: the main action on its own, astride the bottom edge of the picture band
    qreal m_ngReveal = 0.0; // Tessera: how far the hover pill has faded in (0..1)
    QPointer<QVariantAnimation> m_ngRevealAnimation;
    QStringList m_ngCardFolderPictures;
    bool m_ngCardFolderCover = false; // m_ngCardFolderPictures is the folder's own cover (folder.jpg, cover.png...)
    QString m_ngCardFolderPath; // Tessera, folders: the folder whose pictures are shown in the band
    QStringList m_ngCardTags; // the file's tags, drawn over its picture (paintNgCardTags)
    QRectF m_ngCardTagTab; // a folder: where its drawn tab is, the first tag written on it
    QStringList m_ngCardPeek; // Tessera, folders: the first entries of the folder; a trailing "/" marks a folder
    int m_ngCardPeekMore = 0; // how many more there are (-1: many, not counted)
    bool m_ngCardEyeInPill = false; // Tessera: the preview eye is the last button of the pill
    bool m_ngCardHoverPill = false; // Tessera: the pill (everything but the main action) shows only while hovered
    QList<int> m_ngCardActionIndex; // one per entry of m_ngCardActions: which quick action of the file it is
    QRectF m_ngRowThumb; // two-line rows: where the small preview is drawn (empty: the type mark is shown)
    bool m_ngObjectRow = false; // two-line rows drawn like the cards (MARK=object, the default)
    QRectF m_ngRowObject; // ... where the drawing of the file is
    int m_ngRowCount = -1; // ... the item count of a folder, for its drawing
    QPixmap m_ngRowThumbPixmap;
    qint64 m_ngRowThumbKey = 0;
    QList<NgChip> m_ngCardActions; // quick actions of the file's kind, see ngQuickActionsFor(); clicked through ngActionAt() (10 + n)

    QList<QByteArray> m_sortedVisibleRoles;

    QRectF m_expansionArea;

    QColor m_customTextColor;
    QColor m_additionalInfoTextColor;

    QHash<Qt::Corner, QString> m_overlays;
    QPixmap m_rating;

    KItemListRoleEditor *m_roleEditor;
    KItemListRoleEditor *m_oldRoleEditor;

    /** @see startActivateSoonAnimation() */
    QPointer<QVariantAnimation> m_activateSoonAnimation;

    friend class KStandardItemListWidgetInformant; // Accesses private static methods to be able to
                                                   // share a common layout calculation
};

#endif
