#!/bin/bash
# packaging/ppa.sh SERIES [binary|source] [PPA_REVISION]: a Debian package of Dolphin NG for an Ubuntu release (resolute,
# stonking...), from the last pushed commit of this checkout (the same snapshot as the Fedora spec). Run inside an
# Ubuntu container or machine with the build tools (devscripts, debhelper, equivs).
#   binary  builds the .deb here, after installing the build dependencies (to test the packaging)
#   source  builds the source package for Launchpad, unsigned: then debsign -k KEY *_source.changes and
#           dput ppa:mori-daniele/desktop-ng *_source.changes. DEBEMAIL: the address of the signing key (the changelog's)
set -euo pipefail
SERIES=${1:?series}; MODE=${2:-binary}; REV=${3:-1}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
COMMIT=$(git -C "$HERE" rev-parse HEAD); SHORT=${COMMIT:0:7}
DATE=$(git -C "$HERE" log -1 --format=%cd --date=format:%Y%m%d%H%M)
UPSTREAM="26.08.1+${DATE}git${SHORT}"
VERSION="${UPSTREAM}-0ppa${REV}~${SERIES}1"
WORK=${WORK:-$(mktemp -d)}
cd "$WORK"
git -C "$HERE" archive --format=tar.gz --prefix="dolphin-ng-${UPSTREAM}/" "$COMMIT" > "dolphin-ng_${UPSTREAM}.orig.tar.gz"
tar xzf "dolphin-ng_${UPSTREAM}.orig.tar.gz"
cd "dolphin-ng-${UPSTREAM}"
cp -r "$HERE/packaging/debian" debian # (the packaging of the checkout, committed or not)
cat > debian/changelog <<EOT
dolphin-ng (${VERSION}) ${SERIES}; urgency=medium

  * Snapshot ${SHORT} of the ng branch (https://github.com/Morzilla/dolphin-ng/commit/${COMMIT}).

 -- ${DEBFULLNAME:-Morzilla} <${DEBEMAIL:-3956663+Morzilla@users.noreply.github.com}>  $(LC_ALL=C date -R)
EOT
if [ "$MODE" = source ]; then
    dpkg-buildpackage -S -sa -d
else
    mk-build-deps -i -r $([ "$(id -u)" = 0 ] || echo "-s sudo") -t "apt-get -y --no-install-recommends" debian/control >/dev/null
    dpkg-buildpackage -b -us -uc -j"$(nproc)"
fi
echo "built in $WORK"
