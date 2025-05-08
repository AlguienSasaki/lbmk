# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

eval "`setvars "" vdir src_dirname relsrcdir relmode`"

release()
{
	export XBMK_RELEASE="y"

	vdir="release"
	while getopts d:m: option; do
		[ -z "$OPTARG" ] && err "empty argument not allowed"
		case "$option" in
		d) vdir="$OPTARG" ;;
		m) relmode="$OPTARG" ;;
		*) err "invalid option '-$option'" ;;
		esac
	done

	vdir="$vdir/$version"
	src_dirname="${relname}_src"
	relsrcdir="$vdir/$src_dirname"

	[ -e "$vdir" ] && err "already exists: \"$vdir\""
	mkdir -p "$vdir" || err "mkvdir: !mkdir -p \"$vdir\""
	git clone . "$relsrcdir" || err "mkdir: !gitclone \"$relsrcdir\""
	touch "$relsrcdir/lock" || err "can't make lock file in $relsrcdir/"

	build_release

	printf "\n\nDONE! Check release files under %s\n" "$vdir"
}

build_release()
{
	(
	cd "$relsrcdir" || err "$vdir: !cd \"$relsrcdir\""

	x_ ./mk -f
	rmgit .
	x_ mv src/docs docs
	) || err "can't create release files"

	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$relsrcdir/CHANGELOG" || err "!log $relsrcdir"
	rm -f "$relsrcdir/lock" || err "can't remove lock file in $relsrcdir"

	(
	cd "${relsrcdir%/*}" || err "$vdir: mktarball \"$relsrcdir\""
	x_ mktarball "${relsrcdir##*/}" "${relsrcdir##*/}.tar.xz"
	) || err "can't create src tarball"
	[ "$relmode" = "src" ] && return 0

	touch "$relsrcdir/lock" || err "can't make lock file in $relsrcdir/"
	(
	cd "$relsrcdir" || err "$vdir: 2 !cd \"$relsrcdir\""
	x_ ./mk -d coreboot
	mk -b coreboot pico-serprog stm32-vserprog pcsx-redux

	fx_ mkrom_tarball x_ find bin -maxdepth 1 -type d -name "serprog_*"
	x_ mv bin ../roms
	) || err "can't build rom images"

	rm -Rf "$relsrcdir" || err "!rm -Rf $relsrcdir"
}
