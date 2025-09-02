# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

eval "`setvars "" reldir reldest vdir rsrc relmode`"

release()
{
	export XBMK_RELEASE="y"

	reldir="release"

	while getopts m: option; do
		[ -z "$OPTARG" ] && err "empty argument not allowed"
		case "$option" in
		m) relmode="$OPTARG" ;;
		*) err "invalid option '-$option'" ;;
		esac
	done

	reldest="$reldir/$version"
	[ -e "$reldest" ] && \
	    err "already exists: \"$reldest\""

	vdir="$XBMK_CACHE/relpwd/${xbtmp##*/}/$version"
	rsrc="$vdir/${relname}_src"

	remkdir "$vdir"
	x_ git clone . "$rsrc"
	update_xbmkver "$rsrc"

	prep_release src
	prep_release tarball
	[ "$relmode" = "src" ] || prep_release bin
	x_ rm -Rf "$rsrc"

	x_ mkdir -p "$reldir"
	x_ mv "$vdir" "$reldir"
	x_ rm -Rf "${vdir%"/$version"}"

	printf "\n\nDONE! Check release files under %s\n" "$reldest"
}

prep_release()
{
	x_ cp "$xbmklock" "$rsrc/lock"
	(
	[ "$1" = "tarball" ] || x_ cd "$rsrc"
	prep_release_$1
	) || err "can't prep release $1"
}

prep_release_src()
{
	x_ ./mk -f
	fx_ "x_ rm -Rf" x_ find . -name ".git"
	fx_ "x_ rm -Rf" x_ find . -name ".gitmodules"
	( fx_ nuke x_ find config -type f -name "nuke.list" ) || err; :
}

nuke()
{
	r="$rsrc/src/${1#config/}"
	[ -d "${r%/*}" ] && x_ cd "${r%/*}" && \
	    dx_ "eval [ -L \"\$fx\" ] || x_ rm -Rf" "$rsrc/$1"; :
}

prep_release_tarball()
{
	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$rsrc/CHANGELOG" || err "!log $rsrc"
	x_ rm -f "$rsrc/lock"
	x_ rm -Rf "$rsrc/cache" "$rsrc/tmp"
	(
	x_ cd "${rsrc%/*}"
	x_ mktarball "${rsrc##*/}" "${rsrc##*/}.tar.xz"
	) || err "can't create src tarball"; :
}

prep_release_bin()
{
	x_ ./mk -d coreboot
	fx_ "x_ ./mk -b" printf \
	    "coreboot\npico-serprog\nstm32-vserprog\npcsx-redux\n"

	fx_ mkrom_tarball x_ find bin -maxdepth 1 -type d -name "serprog_*"
	x_ mv bin ../roms
}
