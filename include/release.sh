# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

eval "`setvars "" reldir reldest vdir relsrcdir relmode`"

release()
{
	export XBMK_RELEASE="y"

	reldir="release"

	while getopts d:m: option; do
		[ -z "$OPTARG" ] && err "empty argument not allowed"
		case "$option" in
		d) reldir="$OPTARG" ;;
		m) relmode="$OPTARG" ;;
		*) err "invalid option '-$option'" ;;
		esac
	done

	reldest="$reldir/$version"
	[ -e "$reldest" ] && \
	    err "already exists: \"$reldest\""

	vdir="$XBMK_CACHE/relpwd/${xbmktmp##*/}/$version"
	relsrcdir="$vdir/${relname}_src"

	remkdir "$vdir"
	x_ git clone . "$relsrcdir"

	prep_release src
	prep_release_tarball
	[ "$relmode" = "src" ] || prep_release bin
	x_ rm -Rf "$relsrcdir"

	x_ mkdir -p "$reldir"
	x_ mv "$vdir" "$reldir"
	x_ rm -Rf "${vdir%"/$version"}"

	printf "\n\nDONE! Check release files under %s\n" "$reldest"
}

prep_release()
{
	x_ touch "$relsrcdir/lock"
	(
	x_ cd "$relsrcdir"
	prep_release_$1
	) || err "can't prep release $1"
}

prep_release_src()
{
	x_ ./mk -f
	fx_ "x_ rm -Rf" x_ find . -name ".git"
	fx_ "x_ rm -Rf" x_ find . -name ".gitmodules"
	x_ mv src/docs docs
}

prep_release_tarball()
{
	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$relsrcdir/CHANGELOG" || err "!log $relsrcdir"
	x_ rm -f "$relsrcdir/lock"
	x_ rm -Rf "$relsrcdir/cache" "$relsrcdir/tmp"
	(
	x_ cd "${relsrcdir%/*}"
	x_ mktarball "${relsrcdir##*/}" "${relsrcdir##*/}.tar.xz"
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
