# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020-2021,2023-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

eval "`setvars "" loc url bkup_url subcurl subhash subgit subgit_bkup \
    depend subcurl_bkup repofail`"

tmpgit="$xbmklocal/gitclone"
tmpgitcache="$XBMK_CACHE/tmpgit"

fetch_targets()
{
	e "src/$project/$tree" d && return 0

	printf "Creating %s tree %s\n" "$project" "$tree"
	git_prep "$url" "$bkup_url" "$xbmkpwd/$configdir/$tree/patches" \
	    "src/$project/$tree" with_submodules
	( fx_ nuke find config -type f -name "nuke.list" ) || err; :
}

fetch_project()
{
	eval "`setvars "" xtree`"
	eval "`setcfg "config/git/$project/pkg.cfg"`"

	chkvars url

	[ -n "$xtree" ] && x_ ./mk -f coreboot "$xtree"
	[ -z "$depend" ] || for d in $depend ; do
		printf "'%s' needs '%s'; grabbing '%s'\n" "$project" "$d" "$d"
		x_ ./mk -f $d
	done
	clone_project
	( fx_ nuke find config -type f -name "nuke.list" ) || err; :
}

clone_project()
{
	loc="$XBMK_CACHE/clone/$project" && singletree "$project" && \
	    loc="src/$project"
	printf "Downloading project '%s' to '%s'\n" "$project" "$loc"

	e "$loc" d missing && remkdir "${tmpgit%/*}" && git_prep \
	    "$url" "$bkup_url" "$xbmkpwd/config/$project/patches" "$loc"; :
}

git_prep()
{
	_patchdir="$3"
	_loc="$4" # $1 and $2 are gitrepo and gitrepo_backup

	chkvars rev
	xbmkget git "$1" "$2" "$tmpgit" "$rev" "$_patchdir"
	if singletree "$project" || [ $# -gt 4 ]; then
		dx_ fetch_submodule "$mdir/module.list"
	fi

	[ "$_loc" = "${_loc%/*}" ] || x_ mkdir -p "${_loc%/*}"
	x_ mv "$tmpgit" "$_loc"
}

fetch_submodule()
{
	mcfgdir="$mdir/${1##*/}"; eval \
	    "`setvars "" subhash subgit subgit_bkup subcurl subcurl_bkup st`"
	eval "`setcfg "$mcfgdir/module.cfg" 0`"

	for xt in git curl; do
		_seval="if [ -n \"\$sub$xt\" ] || [ -n \"\$sub${xt}_bkup\" ]"
		eval "$_seval; then st=\"\$st \$xt\"; fi"
	done

	st="${st# }" && [ "$st" = "git curl" ] && err "$mdir: git+curl defined"
	[ -z "$st" ] && return 0 # subgit/subcurl not defined
	chkvars "sub${st}" "sub${st}_bkup" "subhash"

	[ "$st" = "git" ] && x_ rm -Rf "$tmpgit/$1"
	eval xbmkget "$st" "\$sub$st" "\$sub${st}_bkup" "$tmpgit/$1" \
	    "$subhash" "$mdir/${1##*/}/patches"
}

xbmkget()
{
	[ "$1" = "curl" ] || [ "$1" = "copy" ] || [ "$1" = "git" ] || \
	    err "Bad dlop (arg 1): xbmkget $*"

	echk="f" && [ "$1" = "git" ] && echk="d"

	for url in "$2" "$3"; do
		[ -n "$url" ] && try_file "$url" "$@" && \
		    eval "[ -$echk \"$4\" ] && return 0"
	done && err "$1 $2 $3 $4: not downloaded"; :
}

try_file()
{
	cached="file/$6" && [ "$2" = "git" ] && cached="clone/${3##*/}" && \
	    cached="${cached%.git}" # always the main repo as basis for naming,
					# in case the backup has another name
	cached="$XBMK_CACHE/$cached"
	x_ mkdir -p "${5%/*}" "${cached%/*}"

	echk="d" && [ "$2" != "git" ] && echk="f" && \
	    bad_checksum "$6" "$cached" 2>/dev/null && x_ rm -f "$cached"

	eval "[ -$echk \"$cached\" ] || try_$2 \"\$cached\" \"\$@\" || return 1"
	[ "$2" != "git" ] && [ -f "$5" ] && \
	    bad_checksum "$6" "$5" 2>/dev/null && x_ cp "$cached" "$5"
	eval "[ -$echk \"$cached\" ] || return 1"

	if [ "$2" = "git" ]; then
		[ -d "$5" ] || tmpclone "$cached" "$5" "$6" "$7" || return 1
	else
		bad_checksum "$6" "$cached" && x_ rm -f "$cached" && return 1
		[ "$cached" != "$5" ] && x_ cp "$cached" "$5"
		bad_checksum "$6" "$5" && x_ rm -f "$5" && return 1; :
	fi

	eval "[ -$echk \"$5\" ] || return 1"
}

try_curl()
{
	_ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0"
	( x_ curl --location --retry 3 -A "$_ua" "$2" -o "$1" ) || \
	    ( x_ wget --tries 3 -U "$_ua" "$2" -O "$1" ) || return 1; :
}

try_copy()
{
	[ -L "$2" ] && printf "symlink %s (trycp %s)\n" "$2" "$*" && return 1
	[ ! -f "$2" ] && printf "%s missing (trycp %s)\n" "$2" "$*" && return 1
	( x_ cp "$2" "$1" ) || return 1; :
}

try_git()
{
	gitdest="`findpath "$1"`" || err "Can't get readpath for '$1'"
	x_ rm -Rf "$tmpgitcache"

	[ -d "$gitdest" ] || ( x_ git clone "$2" "$tmpgitcache" ) || return 1
	[ -d "$gitdest" ] || x_ mkdir -p "${gitdest%/*}"
	[ -d "$gitdest" ] || x_ mv "$tmpgitcache" "$gitdest"

	( x_ git -C "$gitdest" remote add main "$4" 2>/dev/null ) || :
	( x_ git -C "$gitdest" remote add backup "$5" 2>/dev/null ) || :
	( x_ git -C "$gitdest" fetch --all ) || :
	( x_ git -C "$gitdest" pull --all ) || :; :
}

bad_checksum()
{
	[ ! -f "$2" ] && printf "File '%s' missing (sha512sum '%s')\n" \
	    "$2" "$1" 1>&2 && return 0
	fchksum="$(x_ sha512sum "$2" | awk '{print $1}')" || \
	    err "Can't get sha512sum on '$2' (checking for sha512sum '$1')"
	[ "$fchksum" != "$1" ] || return 1
	printf "WARNING: BAD checksum for '%s' - expected '%s', got '%s'\n" \
	    "$2" "$1" "$fchksum" 1>&2
	x_ rm -f "$2"
}

tmpclone()
{
	printf "Creating git clone '%s' from '%s'\n" "$2" "$1"
	( x_ git clone "$1" "$2" ) || return 1
	( x_ git -C "$2" reset --hard "$3" ) || return 1
	( fx_ "eval x_ git -C \"$2\" am" find "$4" -type f ) || return 1; :
}

nuke()
{
	r="${1#config/}" && r="$xbmkpwd/src/${r%/*}" && [ -d "$r" ] && x_ cd \
	    "$r" && dx_ "eval [ -L \"\$fx\" ] || x_ rm -Rf" "$xbmkpwd/$1"; :
}
