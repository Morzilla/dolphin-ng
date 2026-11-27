/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NGSETTINGSPAGE_H
#define NGSETTINGSPAGE_H

#include "settings/settingspagebase.h"

#include <QHash>
#include <QList>
#include <functional>

class KColorButton;
class KUrlRequester;
class QCheckBox;
class QComboBox;
class QListWidget;
class QFormLayout;
class QSlider;
class QSpinBox;
class QWidget;
class QStackedWidget;
class NgSettingsPreview;
class NgTileRow;

/**
 * dolphin-ng: settings of the fork (sidebar glass, list look). They are stored in dolphinrc,
 * group DolphinNG; a DOLPHIN_NG_* environment variable overrides the matching entry.
 * Applying them takes effect at once: ngSettingsInvalidate() tells every part of the window.
 */
class NgSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit NgSettingsPage(QWidget *parent);
    ~NgSettingsPage() override;

    void applySettings() override;
    void restoreDefaults() override;

private:
    void loadSettings();
    void applyPreset(const QString &name); // classic | soft | glass: fills the page, applied with the dialog
    QComboBox *m_preset;
    void updateDependencies(); // show only the rows the chosen backdrop uses
    void showOption(QWidget *option, bool shown); // an option with everything that belongs to it (its heading, its form row)
    void updatePreview(); // the miniature follows the page
    NgSettingsPreview *m_preview;
    QStackedWidget *m_pages;
    NgTileRow *m_sideRow; // plain panel, or glass over one of three backdrops
    QHash<QWidget *, QList<QWidget *>> m_parts;
    QHash<QWidget *, QFormLayout *> m_formOf;
    QList<std::pair<QWidget *, QWidget *>> m_proxies; // a hidden value and the tiles that drive it
    QList<std::function<void()>> m_syncs;

    // sidebar
    QCheckBox *m_glassPanel;
    QComboBox *m_backdrop;
    QComboBox *m_rim;
    QComboBox *m_devices;
    QComboBox *m_disksPos;
    QComboBox *m_startAt = nullptr; // Dolphin's "Show on startup": the folders and tabs of last time, or the home folder
    QCheckBox *m_macButtons = nullptr; // the window's buttons at the left, as on macOS (TITLE_BUTTONS=left)
    QCheckBox *m_barNames = nullptr; // the sort bar's new folder and split buttons with their names (BAR_NAMES, off: icons)
    QCheckBox *m_restoreSelection = nullptr; // with them, the files that were selected (RESTORE_SELECTION, off by default)
    QComboBox *m_tabsPlace = nullptr; // where the tabs are (TABS): in the title bar, above the location bar, above the files
    QListWidget *m_sectionOrder; // the sidebar's sections, dragged into their order; the checks show or hide them
    void fillSectionOrder(const QStringList &order, bool pinned, bool filters, bool disks); // the disk boxes at the foot of the panel, or above the pinned items and the filters // disks and partitions: NG boxes at the foot of the panel | a classic section | hidden
    QWidget *m_deviceList; // one box per disk or partition: unticked, it is hidden (the places' own hiding)
    QList<std::pair<QString, QCheckBox *>> m_deviceBoxes; // device udi, its box
    void fillDevices();
    QCheckBox *m_sidebarFilters;
    QCheckBox *m_panelsFlush = nullptr; // the panels against the window's edges (PANELS_EDGE=flush)
    QCheckBox *m_sidebarPinned;
    QCheckBox *m_recentLocations = nullptr; // "Recent Locations" among the places (RECENT_LOCATIONS)
    QCheckBox *m_recentClassic = nullptr; // "Recent Files" in the item view, not in its own (RECENT_LOOK=classic)
    QCheckBox *m_recentLegacy = nullptr; // "Recent Files" stays the desktop's own list (RECENT_LEGACY)
    QCheckBox *m_sectionsFollow = nullptr; // the sections below the places right after the list (SIDEBAR_FOLLOW)
    QCheckBox *m_folderContentDate;
    // sidebar: picture
    KUrlRequester *m_image;
    QSlider *m_glass;
    QSlider *m_blur;
    QSlider *m_liveBlur; // the compositor's own blur strength, for the live backdrop
    QCheckBox *m_motion;
    // sidebar: animated gradient
    QComboBox *m_lavaStyle;
    QSlider *m_lavaSize;
    QComboBox *m_lavaColours;
    QWidget *m_lavaColourRow;
    KColorButton *m_lavaColour[3];
    // sidebar: shared by the gradient and the moving picture
    QSlider *m_lavaSpeed;
    // list
    QCheckBox *m_twoLines;
    QComboBox *m_rows;
    QComboBox *m_mark;
    QComboBox *m_sigla;
    QCheckBox *m_icons;
    QCheckBox *m_ago;
    QCheckBox *m_rowDates;
    QCheckBox *m_rowWash; // the colour of the type fading into the row
    QCheckBox *m_hoverPreview;
    QCheckBox *m_folderSizes;
    QSpinBox *m_hoverDelay;
    QCheckBox *m_float;
    QCheckBox *m_dialogQuick; // Dialog NG (the open/save window of every application): its own quick preview
    QSpinBox *m_dialogDelay;
    QComboBox *m_dialogSize; // small | medium | large
    // icons view
    QCheckBox *m_tiles;
    QCheckBox *m_perFolder = nullptr; // every folder remembers its own view (Dolphin's GlobalViewProps, the other way round)
    // cards view
    QComboBox *m_cards;
    QComboBox *m_cardsFit;
    QComboBox *m_cardsInfo;
    QComboBox *m_cardsMark;
    QComboBox *m_cardsBadge; // the type and size badge on the band of a Tessera: solid or glass
    QComboBox *m_activitySize = nullptr; // ACTIVITY_SIZE: the capsule of work in progress on cards
    QCheckBox *m_quickView;
    QSpinBox *m_quickDelay;
    QComboBox *m_quickSide;
};

#endif
