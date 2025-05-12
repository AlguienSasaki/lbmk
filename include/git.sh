# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020-2021,2023-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

eval "`setvars "" loc url bkup_url subfile subhash subrepo subrepo_bkup \
    depend subfile_bkup repofail`"

tmpgit="$xbmklocal/gitclone"

fetch_targets()
{
	e "src/$project/$tree" d && return 0

	printf "Creating %s tree %s\n" "$project" "$tree"
	git_prep "$loc" "$loc" "$xbmkpwd/$configdir/$tree/patches" \
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
	loc="$XBMK_CACHE/repo/$project" && singletree "$project" && \
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
	tmpclone "$1" "$2" "$tmpgit" "$rev" "$_patchdir"
	if singletree "$project" || [ $# -gt 4 ]; then
		dx_ fetch_submodule "$mdir/module.list"
	fi

	[ "$_loc" = "${_loc%/*}" ] || x_ mkdir -p "${_loc%/*}"
	x_ mv "$tmpgit" "$_loc"
}

fetch_submodule()
{
	mcfgdir="$mdir/${1##*/}"; eval \
	    "`setvars "" subhash subrepo subrepo_bkup subfile subfile_bkup st`"
	[ ! -f "$mcfgdir/module.cfg" ] || . "$mcfgdir/module.cfg" || \
	    err "! . $mcfgdir/module.cfg"

	for xt in repo file; do
		_seval="if [ -n \"\$sub$xt\" ] || [ -n \"\$sub${xt}_bkup\" ]"
		eval "$_seval; then st=\"\$st \$xt\"; fi"
	done
	st="${st# }" && [ "$st" = "repo file" ] && err "$mdir: repo+file"

	[ -z "$st" ] && return 0 # subrepo/subfile not defined
	chkvars "sub${st}" "sub${st}_bkup" "subhash"

	[ "$st" = "file" ] && xbmkget "$subfile" "$subfile_bkup" \
	    "$tmpgit/$1" "$subhash" && return 0
	x_ rm -Rf "$tmpgit/$1"
	tmpclone "$subrepo" "$subrepo_bkup" "$tmpgit/$1" "$subhash" \
	    "$mdir/${1##*/}/patches"
}

# can grab from the internet, or copy locally.
# if copying locally, it can only copy a file.
xbmkget()
{
	_dlop="curl" && [ $# -gt 4 ] && _dlop="$5"
	x_ mkdir -p "${3%/*}" "$XBMK_CACHE/file"
	for url in "$1" "$2"; do
		[ -n "$url" ] && try_file "$url" "$_dlop" "$@" && return 0
	done && err "$1 $2 $3 $4: not downloaded"; :
}

try_file()
{
	cached="$XBMK_CACHE/file/$6"
	dl_fail="n" # 1 url, 2 url backup, 3 destination, 4 checksum
	bad_checksum "$6" "$cached" 2>/dev/null && dl_fail="y"
	[ "$dl_fail" = "n" ] && e "$5" f && return 0

	x_ rm -f "$cached"
	[ "$2" = "curl" ] || [ "$2" = "copy" ] || \
	    err "$3 $4 $5 $6: Unsupported dlop type: '$2'"

	try_$2 "$cached" "$@" || return 1

	bad_checksum "$6" "$cached" && return 1
	[ "$cached" = "$5" ] || x_ cp "$cached" "$5"; :
}

try_curl()
{
	_ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0"
	curl --location --retry 3 -A "$_ua" "$2" -o "$1" || \
	    wget --tries 3 -U "$_ua" "$2" -O "$1" || return 1; :
}

try_copy()
{
	[ -L "$2" ] && printf "dl %s %s %s %s: '%s' is a symlink\n" \
	    "$4" "$5" "$6" "$7" "$2" 1>&2 && return 1
	[ ! -f "$2" ] && printf "dl %s %s %s %s: '%s' not a file\n" \
	    "$4" "$5" "$6" "$7" "$2" 1>&2 && return 1
	cp "$2" "$1" || return 1; :
}

bad_checksum()
{
	[ "$(sha512sum "$2" | awk '{print $1}')" != "$1" ] || return 1
	printf "Bad checksum for file: %s\n" "$2" 1>&2; rm -f "$2" || :; :
}

tmpclone()
{
	[ -d "$3" ] && return 0
	printf "Creating git clone '%s' from '%s', '%s'\n" "$3" "$1" "$2"
	git clone "$1" "$3" || x_ rm -Rf "$3"
	[ -d "$3" ] || x_ git clone "$2" "$3"
	x_ git -C "$3" reset --hard "$4"
	fx_ "eval x_ git -C \"$3\" am" find "$5" -type f
}

nuke()
{
	e "config/${1%/}/nuke.list" f missing || while read -r nukefile; do
		rmf="src/${2%/}/$nukefile" && [ -L "$rmf" ] && continue
		e "$rmf" e missing || x_ rm -Rf "$rmf"
	done < "config/${1%/}/nuke.list"; :
}
