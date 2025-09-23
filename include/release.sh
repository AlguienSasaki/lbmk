# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

eval "`setvars "" reldir reldest vdir rsrc relmode`"

release()
{
	export XBMK_RELEASE="y"

	reldir="release"

	while getopts m: option
	do
		if [ -z "$OPTARG" ]
		then
			err "empty argument not allowed" "release" "$@"
		fi

		case "$option" in
		m) relmode="$OPTARG" ;;
		*) err "invalid option '-$option'" "release" "$@" ;;
		esac
	done

	reldest="$reldir/$version"
	if [ -e "$reldest" ]
	then
		err "already exists: \"$reldest\"" "release" "$@"
	fi

	vdir="`mktemp -d || err "can't make vdir"`" || \
	    err "can't make tmp vdir" "release" "$@"
	vdir="$vdir/$version"

	rsrc="$vdir/${relname}_src"

	remkdir "$vdir"
	x_ git clone . "$rsrc"
	update_xbmkver "$rsrc"

	prep_release src
	prep_release tarball
	if [ "$relmode" != "src" ]
	then
		prep_release bin
	fi
	x_ rm -Rf "$rsrc"

	x_ mkdir -p "$reldir"
	x_ mv "$vdir" "$reldir"
	x_ rm -Rf "${vdir%"/$version"}"

	printf "\n\nDONE! Check release files under %s\n" "$reldest"
}

prep_release()
{
	(

		if [ "$1" != "tarball" ]
		then
			x_ cd "$rsrc"
		fi

		prep_release_$1

	) || err "can't prep release $1" "prep_release" "$@"
}

prep_release_src()
{
	x_ ./mk -f

	fx_ "x_ rm -Rf" x_ find . -name ".git"
	fx_ "x_ rm -Rf" x_ find . -name ".gitmodules"

	(

		fx_ nuke x_ find config -type f -name "nuke.list"

	) || err "can't prune project files" "prep_release_src" "$@"; :
}

nuke()
{
	r="$rsrc/src/${1#config/}"

	if [ -d "${r%/*}" ]
	then
		x_ cd "${r%/*}"

		dx_ "eval [ -L \"\$fx\" ] || x_ rm -Rf" "$rsrc/$1"
	fi
}

prep_release_tarball()
{
	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$rsrc/CHANGELOG" || \
	    err "can't create '$rsrc/CHANGELOG'" "prep_release_tarball" "$@" 

	x_ rm -f "$rsrc/lock"
	x_ rm -Rf "$rsrc/cache" "$rsrc/xbmkwd"

	(

		x_ cd "${rsrc%/*}"
		x_ mktarball "${rsrc##*/}" "${rsrc##*/}.tar.xz"

	) || err "can't create src tarball" "prep_release_tarball" "$@"; :
}

prep_release_bin()
{
	x_ ./mk -d coreboot

	fx_ "x_ ./mk -b" printf \
	    "coreboot\npico-serprog\nstm32-vserprog\npcsx-redux\n"

	fx_ mkrom_tarball x_ find bin -maxdepth 1 -type d -name "serprog_*"

	x_ mv bin ../roms
}
