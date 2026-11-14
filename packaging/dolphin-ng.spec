# SPDX-License-Identifier: GPL-2.0-or-later
%global commit      407dfc92c0d55a49ef04e49b7ce04bbf78ac5602
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global snapdate    202611141458
# installed apart from the system, next to Fedora's own Dolphin
%global ngprefix    /opt/ng/dolphin-ng
# its programs find its libraries through an RPATH to /opt/ng/dolphin-ng: on purpose, since they have the same names
# as Fedora's Dolphin libraries and must not be put on the system's library path
%global __brp_check_rpaths %{nil}

Name:           dolphin-ng
Version:        26.08.1^%{snapdate}git%{shortcommit}
Release:        1%{?dist}
Summary:        A fork of Dolphin that rethinks how you work with your files
License:        GPL-2.0-or-later
URL:            https://github.com/Morzilla/dolphin-ng
Source0:        %{url}/archive/%{commit}/dolphin-ng-%{shortcommit}.tar.gz

BuildRequires:  cmake
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Concurrent)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Multimedia)
BuildRequires:  cmake(Qt6MultimediaWidgets)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(KF6Baloo)
BuildRequires:  cmake(KF6BalooWidgets)
BuildRequires:  cmake(KF6Bookmarks)
BuildRequires:  cmake(KF6Codecs)
BuildRequires:  cmake(KF6ColorScheme)
BuildRequires:  cmake(KF6Completion)
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6Crash)
BuildRequires:  cmake(KF6DBusAddons)
BuildRequires:  cmake(KF6DocTools)
BuildRequires:  cmake(KF6FileMetaData)
BuildRequires:  cmake(KF6GuiAddons)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(KF6IconThemes)
BuildRequires:  cmake(KF6KCMUtils)
BuildRequires:  cmake(KF6KIO)
BuildRequires:  cmake(KF6NewStuff)
BuildRequires:  cmake(KF6Notifications)
BuildRequires:  cmake(KF6Parts)
BuildRequires:  cmake(KF6Solid)
BuildRequires:  cmake(KF6TextWidgets)
BuildRequires:  cmake(KF6UserFeedback)
BuildRequires:  cmake(KF6WidgetsAddons)
BuildRequires:  cmake(KF6WindowSystem)
BuildRequires:  cmake(packagekitqt6)

# what the quick actions call (resize and convert images, audio and video, PDF pages and text)
Recommends:     ImageMagick
Recommends:     ffmpeg-free
Recommends:     poppler-utils
# ng-job shows quick actions in Plasma's notifications
Recommends:     python3-gobject

%description
Dolphin NG is a fork of KDE's Dolphin that rethinks how you work with your
files: cards and drawn rows that tell what a file is at a glance, quick filters
by kind of file, groups that follow the sorting, quick actions one click away,
and work in progress shown on the files themselves.

It installs under %{ngprefix}, next to Fedora's Dolphin, which stays the
default file manager: start it as "Dolphin NG" from the menu, or with the
dolphin-ng command.

%prep
%autosetup -n dolphin-ng-%{commit}

%build
%set_build_flags
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=%{ngprefix} \
      -DBUILD_TESTING=OFF -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF
cmake --build build -j%{?_smp_build_ncpus}

%install
DESTDIR=%{buildroot} cmake --install build
# the launcher sets the paths of the private prefix; the command, the helpers of the quick actions, the menu entry
install -Dm755 ng/dolphin-ng %{buildroot}%{ngprefix}/bin/dolphin-ng-launch
install -Dm755 ng/ng-job %{buildroot}%{ngprefix}/bin/ng-job
install -Dm755 ng/ng-transcribe %{buildroot}%{ngprefix}/bin/ng-transcribe
mkdir -p %{buildroot}%{_bindir}
printf '#!/bin/sh\nDOLPHIN_NG_PREFIX=%s exec %s/bin/dolphin-ng-launch "$@"\n' %{ngprefix} %{ngprefix} > %{buildroot}%{_bindir}/dolphin-ng
chmod 755 %{buildroot}%{_bindir}/dolphin-ng
ln -s %{ngprefix}/bin/ng-job %{buildroot}%{_bindir}/ng-job
ln -s %{ngprefix}/bin/ng-transcribe %{buildroot}%{_bindir}/ng-transcribe
# the NG toolbar (New Folder, Up, Root): found in the prefix's data folder, which the launcher puts first
install -Dm644 ng/dolphinui.rc.example %{buildroot}%{ngprefix}/share/kxmlgui5/dolphin/dolphinui.rc
mkdir -p %{buildroot}%{_datadir}/applications
cat > %{buildroot}%{_datadir}/applications/dolphin-ng.desktop <<EOT
[Desktop Entry]
Type=Application
Name=Dolphin NG
GenericName=File Manager
Comment=A fork of Dolphin with cards, quick filters and quick actions
Exec=%{_bindir}/dolphin-ng %u
Icon=system-file-manager
Terminal=false
Categories=Qt;KDE;System;FileTools;FileManager;
MimeType=inode/directory;
StartupWMClass=dolphin
EOT

%post
# on the first install only: say how to open it (Fedora's Dolphin stays the default file manager)
if [ "$1" -eq 1 ]; then
    echo
    echo "Dolphin NG is installed next to Dolphin. Open it with:  dolphin-ng"
    echo "or from the application menu: Dolphin NG."
    echo
fi

%files
%license LICENSE COPYING
%doc NG.md FEATURES.md
%dir /opt/ng
%{ngprefix}
%{_bindir}/dolphin-ng
%{_bindir}/ng-job
%{_bindir}/ng-transcribe
%{_datadir}/applications/dolphin-ng.desktop

%changelog
* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141326git8c34db9-1
- Breeze NG Aurora: the whole window, file views and sidebar are glass

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141256gite56cd00-1
- Under Breeze NG Aurora the whole window is a sheet of glass

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141227git2a92cd3-1
- List, Icons and Classic icons in the menu of the cards button

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141225git17878f7-1
- No stray line where selected cards meet; smaller eject glyph in the disk boxes

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141217git78ab3b1-1
- The bar follows a change of theme while Dolphin is open

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141153git543c404-1
- Live blur sidebar less see-through under a light colour scheme

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141104git7bc41d1-2
- At the first install, say how to open it

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141104git7bc41d1-1
- The same room around every menu arrow in the bar

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141020gitba4fa3f-1
- Settings dialog no longer makes the main window's children native

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611141004gitdd933e3-1
- No native siblings: the sidebar stays in the window that carries its blur

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611140955git18500d5-1
- The window is translucent from the start, never switched on an open window

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^202611140945git3900dd7-1
- Live blur asked again on a new native window
- Snapshot versions carry the time of the commit, so builds of the same day sort right

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^20261114git5957c2b-1
- Default look: Cards view (Scheda), sidebar over the desktop blur

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 26.08.1^20261114gitfb17636-1
- First package
