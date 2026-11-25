#!/bin/sh
# SPDX-FileCopyrightText: 2026 Phosphor Works Team
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the FreeBSD packages of the bots from this checkout, on the FreeBSD
# release they are for:
#
#   sh dist/freebsd/build.sh [OUT]      OUT: ./packages when not given
#
# Writes OUT/wisp-<VERSION>.pkg and OUT/glim-<VERSION>.pkg. Needs cmake-core, ninja, pkgconf, curl,
# googletest and nlohmann-json installed (pkg install cmake-core ninja pkgconf
# curl googletest nlohmann-json). The tests run before anything is packaged.
# A package depends on the curl and ca_root_nss this machine has.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)

if [ "$(uname -s)" != FreeBSD ]; then
	echo "build.sh makes FreeBSD packages: run it on FreeBSD, not on $(uname -s)" >&2
	exit 1
fi
for tool in cmake ninja pkg; do
	command -v "$tool" > /dev/null || { echo "$tool is not installed: pkg install cmake-core ninja" >&2; exit 1; }
done
version=$(sed -n 's/^project(phosphor-bots VERSION \([0-9.]*\).*/\1/p' "$top/CMakeLists.txt")
[ -n "$version" ] || { echo "no version in $top/CMakeLists.txt" >&2; exit 1; }
mkdir -p "${1:-packages}"
out=$(cd "${1:-packages}" && pwd)
work=$(mktemp -d -t phosphor-bots-pkg)
trap 'rm -rf "$work"' EXIT

cmake -S "$top" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
	-DCMAKE_COMPILE_WARNING_AS_ERROR=ON > "$work/configure.log" 2>&1 ||
	{ tail -30 "$work/configure.log" >&2; exit 1; }
cmake --build "$work/build" -j "$(sysctl -n hw.ncpu)"
ctest --test-dir "$work/build" --output-on-failure -j "$(sysctl -n hw.ncpu)"

# The packages <name>... this machine has, as a manifest's deps entries
deps() {
	for d in "$@"; do
		pkg query '"%n": { origin: "%o", version: "%v" },' "$d" 2> /dev/null | grep . ||
			{ echo "$d is not installed here, and the package would depend on it: pkg install $d" >&2; exit 1; }
	done
}

# package NAME COMMENT DEPS: the manifest of NAME from its .desc and
# .post-install, its files from its .plist under $work/stage/NAME
package() {
	{
		printf 'name: "%s"\nversion: "%s"\norigin: "local/%s"\n' "$1" "$version" "$1"
		printf 'comment: "%s"\nwww: "https://github.com/phosphor-works/phosphor-bots"\n' "$2"
		printf 'maintainer: "noreply@phosphor-works.org"\nprefix: "/usr/local"\n'
		printf 'licenselogic: "single"\nlicenses: ["GPLv3+"]\n'
		printf 'desc: <<EOD\n'
		cat "$here/$1.desc"
		printf 'EOD\ndeps: {\n%s\n}\n' "$3"
		printf 'scripts: {\npost-install: <<EOD\n'
		cat "$here/$1.post-install"
		printf 'EOD\n}\n'
	} > "$work/$1.manifest"
	pkg create -M "$work/$1.manifest" -r "$work/stage/$1" -p "$here/$1.plist" -o "$out"
	echo "$out/$1-$version.pkg"
}

s=$work/stage/wisp/usr/local
install -d "$s/libexec" "$s/etc/rc.d"
install -s -m 755 "$work/build/wisp/wisp" "$s/libexec/wisp"
install -m 755 "$here/wisp.rc" "$s/etc/rc.d/wisp"
install -m 644 "$here/wisp.env.sample" "$s/etc/wisp.env.sample"
package wisp "Wisp, phosphor-works' Discord bot" "$(deps curl ca_root_nss)"

s=$work/stage/glim/usr/local
install -d "$s/libexec" "$s/etc/rc.d"
install -s -m 755 "$work/build/glim/glim" "$s/libexec/glim"
install -m 755 "$here/glim.rc" "$s/etc/rc.d/glim"
install -m 644 "$here/glim.env.sample" "$s/etc/glim.env.sample"
package glim "Glim, files help posts of a Discord forum as GitHub issues" "$(deps curl ca_root_nss)"
