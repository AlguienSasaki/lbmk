#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

set -u -e

if [ "./${0##*/}" != "${0}" ] || [ ! -f "mk" ] || [ -L "mk" ]; then
	printf "You must run this in the proper work directory.\n" 1>&2
	exit 1
fi

. "include/lib.sh"
. "include/vendor.sh"
. "include/mrc.sh"

eval "`setvars "" vdir src_dirname srcdir mode xp ser`"
err="fail"

main()
{
	[ $# -lt 1 ] && $err "bad command"

	spath="script/$1"
	shcmd="shift 1"
	if [ "${1#-*}" != "$1" ]; then
		spath="script/trees"
		shcmd=":"
	fi

	for g in "command -v git" "git config --global user.name" \
	    "git config --global user.email" "git_init"; do
		eval "$g 1>/dev/null 2>/dev/null || $err \"Unconfigured: $g\""
	done

	case "${spath#script/}" in
	version) printf "%s\nWebsite: %s\n" "$relname" "$projectsite" ;;
	release)
		shift 1
		mkrelease "$@" ;;
	inject)
		shift 1
		vendor_inject "$@" ;;
	download)
		shift 1
		vendor_download "$@" ;;
	*)
		[ -f "$spath" ] || $err "bad command"
		$shcmd
		"$spath" "$@" || $err "excmd: $spath $(echo "$@")" ;;
	esac
	set -u -e # some commands disable them. turn them on!
}

git_init()
{
	[ -L ".git" ] && return 1
	[ -e ".git" ] && return 0
	eval "`setvars "$(date -Rud @$versiondate)" cdate _nogit`"

	git init || return 1
	git add -A . || return 1
	git commit -m "$projectname $version" --date "$cdate" \
	    --author="xbmk <xbmk@example.com>" || return 1
	git tag -a "$version" -m "$projectname $version" || return 1
}

mkrelease()
{
	export XBMK_RELEASE="y"

	vdir="release"
	while getopts d:m: option; do
		[ -z "$OPTARG" ] && $err "empty argument not allowed"
		case "$option" in
		d) vdir="$OPTARG" ;;
		m) mode="$OPTARG" ;;
		*) $err "invalid option '-$option'" ;;
		esac
	done

	vdir="$vdir/$version"
	src_dirname="${relname}_src"
	srcdir="$vdir/$src_dirname"

	[ -e "$vdir" ] && $err "already exists: \"$vdir\""
	mkdir -p "$vdir" || $err "mkvdir: !mkdir -p \"$vdir\""
	git clone . "$srcdir" || $err "mkdir: !gitclone \"$srcdir\""
	touch "$srcdir/lock" || $err "can't make lock file in $srcdir/"

	build_release

	printf "\n\nDONE! Check release files under %s\n" "$vdir"
}

build_release()
{
	(
	cd "$srcdir" || $err "$vdir: !cd \"$srcdir\""
	./mk -f
	x_ rm -Rf tmp
	rmgit .
	x_ mv src/docs docs
	) || $err "can't create release files"

	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$srcdir/CHANGELOG" || $err "!gitlog $srcdir"
	rm -f "$srcdir/lock" || $err "can't remove lock file in $srcdir"

	(
	cd "${srcdir%/*}" || $err "$vdir: mktarball \"$srcdir\""
	mktarball "${srcdir##*/}" "${srcdir##*/}.tar.xz" || $err "$vdir: mksrc"
	) || $err "can't create src tarball"
	[ "$mode" = "src" ] && return 0

	touch "$srcdir/lock" || $err "can't make lock file in $srcdir/"
	(
	cd "$srcdir" || $err "$vdir: 2 !cd \"$srcdir\""
	mk -b coreboot pico-serprog stm32-vserprog pcsx-redux
	x_ mv bin ../roms
	) || $err "can't build rom images"

	rm -Rf "$srcdir" || $err "!rm -Rf $srcdir"
}

fail()
{
	tmp_cleanup || printf "WARNING: can't rm tmpfiles: %s\n" "$TMPDIR" 1>&2
	err_ "${1}"
}

tmp_cleanup()
{
	[ "$xbmk_parent" = "y" ] || return 0
	[ "$TMPDIR" = "/tmp" ] || rm -Rf "$TMPDIR" || return 1
	rm -f lock || return 1
}

main "$@"
tmp_cleanup || err_ "can't rm TMPDIR upon non-zero exit: $TMPDIR"
