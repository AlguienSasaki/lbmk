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
	x_ mkdir -p "$vdir"
	x_ git clone . "$relsrcdir"
	x_ touch "$relsrcdir/lock"

	build_release

	printf "\n\nDONE! Check release files under %s\n" "$vdir"
}

build_release()
{
	(
	x_ cd "$relsrcdir"

	x_ ./mk -f
	rmgit .
	x_ mv src/docs docs
	) || err "can't create release files"

	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$relsrcdir/CHANGELOG" || err "!log $relsrcdir"
	x_ rm -f "$relsrcdir/lock"

	(
	x_ cd "${relsrcdir%/*}"
	x_ mktarball "${relsrcdir##*/}" "${relsrcdir##*/}.tar.xz"
	) || err "can't create src tarball"
	[ "$relmode" = "src" ] && return 0

	x_ touch "$relsrcdir/lock"
	(
	x_ cd "$relsrcdir"
	x_ ./mk -d coreboot
	fx_ "x_ ./mk -b" printf \
	    "coreboot\npico-serprog\nstm32-vserprog\npcsx-redux\n"

	fx_ mkrom_tarball x_ find bin -maxdepth 1 -type d -name "serprog_*"
	x_ mv bin ../roms
	) || err "can't build rom images"

	x_ rm -Rf "$relsrcdir"
}
