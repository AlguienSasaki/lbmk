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
	nuke "$project/$tree" "$project/$tree"
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

	for x in config/git/*; do
		[ -d "$x" ] && nuke "${x##*/}" "src/${x##*/}" 2>/dev/null; :
	done; :
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

	for url in "$2" "$3"; do
		[ -n "$url" ] && try_file "$url" "$@" && return 0
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

	e "$cached" $echk || try_$2 "$cached" "$@" || return 1
	if e "$5" $echk && [ "$2" != "git" ]; then
		bad_checksum "$6" "$5" 2>/dev/null && x_ cp "$cached" "$5"
	fi
	e "$cached" $echk missing && return 1

	if [ "$2" = "git" ]; then
		tmpclone "$cached" "$5" "$6" "$7" || return 1
	else
		bad_checksum "$6" "$cached" && x_ rm -f "$cached" && return 1
		[ "$cached" != "$5" ] && x_ cp "$cached" "$5"
		bad_checksum "$6" "$5" && x_ rm -f "$5" && return 1; :
	fi
}

try_curl()
{
	_ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0"
	curl --location --retry 3 -A "$_ua" "$2" -o "$1" || \
	    wget --tries 3 -U "$_ua" "$2" -O "$1" || return 1; :
}

try_copy()
{
	[ -L "$2" ] && printf "symlink %s (try_cp %s)\n" "$2" "$*" && return 1
	[ ! -f "$2" ] && "no such file %s (try_cp %s)\n" "$2" "$*" && return 1
	cp "$2" "$1" || return 1; :
}

try_git()
{
	gitdest="`findpath "$1"`" || err "Can't get readpath for '$1'"
	x_ rm -Rf "$tmpgitcache"

	[ -d "$gitdest" ] || git clone "$2" "$tmpgitcache" || return 1
	[ -d "$gitdest" ] || x_ mkdir -p "${gitdest##*/}"
	[ -d "$gitdest" ] || x_ mv "$tmpgitcache" "$gitdest"

	git -C "$gitdest" remote add main "$4" 2>/dev/null || :
	git -C "$gitdest" remote add backup "$5" 2>/dev/null || :
	git -C "$gitdest" fetch --all || :
	git -C "$gitdest" pull --all || :
}

bad_checksum()
{
	[ ! -f "$2" ] || [ "$(sha512sum "$2" | awk '{print $1}')" != "$1" ] \
	    || return 1
	printf "Bad checksum for file: %s\n" "$2" 1>&2
	x_ rm -f "$2"
}

tmpclone()
{
	[ -d "$2" ] && return 0
	printf "Creating git clone '%s' from '%s'\n" "$2" "$1"
	( x_ git clone "$1" "$2" ) || return 1
	( x_ git -C "$2" reset --hard "$3" ) || return 1
	( fx_ "eval x_ git -C \"$2\" am" find "$4" -type f ) || return 1; :
}

nuke()
{
	e "config/${1%/}/nuke.list" f missing || while read -r nukefile; do
		rmf="src/${2%/}/$nukefile" && [ -L "$rmf" ] && continue
		e "$rmf" e missing || x_ rm -Rf "$rmf"
	done < "config/${1%/}/nuke.list"; :
}
