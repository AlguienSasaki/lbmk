#!/bin/sh

# SPDX-License-Identifier: MIT

# Copyright (c) 2026 Leah Rowe

set -u -e

urlmain="https://www.mirrorservice.org/sites/libreboot.org/release/misc/grub"
urlbkup="https://mirror.math.princeton.edu/pub/libreboot/misc/grub"

# script to grab GNU gettext po files from translationproject.org -
# i noticed that the grub bootstrap script grabs these at build time,
# without actually checking signatures, and they could change on the
# server upstream at any time

# this means that the GRUB build process is currently non-deterministic,
# which is a violation of libreboot policy.

tmpdir="`mktemp -d`"
tmpmod="`mktemp -d`"

mkdir -p "$tmpdir" "$tmpmod" || exit 1

(
cd "$tmpdir" || exit 1
wget --mirror --level=1 -nd -nv -A.po -P 'po/.reference' \
    https://translationproject.org/latest/grub/ || \
    exit 1
find -type f > "$tmpmod/tmplist" || exit 1
while read -r f; do
	printf "%s\n" "${f#./}" >> "$tmpmod/module.list"

	# now make the actual config files, but don't use
	# the main upstream, because those files can change
	# at any time. we will, over time, manually update
	# our mirrors

	pkgname="${f##*/}"
	[ -z "$pkgname" ] && printf "ERR\n" && exit 1

	pkgsum="`sha512sum "$f" | awk '{print $1}'`"

	mkdir -p "$tmpmod/$pkgname" || exit 1

	printf "# SPDX-License-Identifier: GPL-3.0-or-later\n\n" >> \
	    "$tmpmod/$pkgname/module.cfg" || exit 1

	printf "subcurl=\"%s/%s\"\n" "$urlmain" "$pkgname" >> \
	    "$tmpmod/$pkgname/module.cfg" || exit 1
	printf "subcurl_bkup=\"%s/%s\"\n" "$urlbkup" "$pkgname" >> \
	    "$tmpmod/$pkgname/module.cfg" || exit 1
	printf "subhash=\"%s\"\n" "$pkgsum" >> "$tmpmod/$pkgname/module.cfg"

done < "$tmpmod/tmplist" || exit 1; :
mv "$tmpmod/tmplist" "$tmpdir" || exit 1
)

printf "tmpdir for modules: '%s'\n" "$tmpmod"

rm -f "module.list" || exit 1

printf "Check directory for lbmk files: '%s'\n" "$tmpmod"
printf "This directory has the PO files: '%s'\n" "$tmpdir"

exit 0


