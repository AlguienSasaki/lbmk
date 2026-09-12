# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2021,2023-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

eval "`newvar url bkup_url depend loc subcurl subcurl_bkup subgit subgit_bkup \
    subhash`"

tmpgit="$xbtmp/gitclone"
tmpgitcache="$xbtmp/tmpgit"

fetch_targets()
{
	[ -d "src/$project/$tree" ] && \
		return 0

	git_prep "$url" "$bkup_url" "$xbmkpwd/$configdir/$tree/patches" \
	    "src/$project/$tree" "submod"
}

fetch_project()
{
	xgcctree=""

	. "config/git/$project/pkg.cfg" || \
	    err "Can't read config 'config/git/$project/pkg.cfg'" \
	    "fetch_project" "@"

	if [ -z "$url" ] || [ -z "$bkup_url" ]; then
		err "url/bkup_url not both set 'config/git/$project/pkg.cfg'" \
		    "fetch_project" "$@"
	fi

	[ -n "$xgcctree" ] && \
		x_ ./mk -f coreboot "$xgcctree"

	[ -n "$depend" ] && \
		for d in $depend ; do
			x_ ./mk -f $d
		done

	clone_project
}

clone_project()
{
	singletree "$project" || \
		return 0

	loc="src/$project"
	[ -d "$loc" ] && \
		return 0

	remkdir "${tmpgit%/*}"
	git_prep "$url" "$bkup_url" "$xbmkpwd/config/$project/patches" "$loc"
}

git_prep()
{
	printf "Creating code directory, src/%s/%s\n" "$project" "$tree"

	_patchdir="$3"
	_loc="$4" # $1 and $2 are gitrepo and gitrepo_backup

	[ -z "$rev" ] && \
		err "$project/$tree: rev not set" "git_prep" "$@"

	xbget git "$1" "$2" "$tmpgit" "$rev" "$_patchdir"
	if singletree "$project" || [ $# -gt 4 ]; then
		dx_ fetch_submodule "$mdir/module.list"
	fi

	[ "$_loc" != "${_loc%/*}" ] && \
		x_ xbmkdir "${_loc%/*}"

	x_ mv "$tmpgit" "$_loc"
}

fetch_submodule()
{
	mcfgdir="$mdir/${1##*/}"
	eval "`newvar st subcurl subcurl_bkup subgit subgit_bkup subhash`"

	if e "$mcfgdir/module.cfg" f missing; then
		return 0
	fi
	. "$mcfgdir/module.cfg" || \
	    err "Can't read '$mcfgdir/module.cfg'" "fetch_submodules" "$@"

	[ -n "$subgit" ] && [ -n "$subgit_bkup" ] && \
		st="git"
	[ -n "$subcurl" ] && [ -n "$subcurl_bkup" ] && \
		st="curl$st"
	[ "$st" = "curlgit" ] && \
		err "$mdir: git+curl defined" "fetch_submodule" "$@"
	[ -z "$st" ] && \
		return 0
	[ -z "$subhash" ] && \
		err "subhash not set" "fetch_submodule" "$@"

	[ "$st" = "git" ] && \
		x_ rm -Rf "$tmpgit/$1"

	eval "xbget \"\$st\" \"\$sub$st\" \"\$sub${st}_bkup\" \"\$tmpgit/\$1\" \
	    \"\$subhash\" \"\$mdir/\${1##*/}/patches\""
}

# TODO: in the following functions, argument numbers are used
#       which is hard to understand. the code should be modified
#       so that variable names are used instead, for easy reading

xbget()
{
	[ "$1" != "curl" ] && [ "$1" != "copy" ] && [ "$1" != "git" ] && \
		err "Bad dlop (arg 1)" "xbget" "$@"

	for url in "$2" "$3"; do
		[ -z "$url" ] && \
			err "empty URL given in" "xbget" "$@"
		try_fetch_$1 "$url" "$@" || \
			continue
		[ -e "$4" ] && \
			return 0; : # successful download/copy
	done

	err "failed to download file/repository" "xbget" "$@"; :
}

try_fetch_git()
{
	# 1st argument $1 is the current git remote being tried,
	# let's say it was https://foo.example.com/repo, then cached
	# directories becomes cache/mirror/foo.example.com/repo

	cached="clone"
	[ "$XBMK_CACHE_MIRROR" = "y" ] && \
		cached="mirror"
	cached="$cached/${1#*://}"
	cached="$XBMK_CACHE/$cached"

	x_ xbmkdir "${5%/*}" "${cached%/*}"

	if ! try_$2 "$cached" "$@"; then
		return 1
	elif [ ! -d "$cached" ]; then
		return 1
	fi

	if [ ! -d "$5" ]; then
		tmpclone "$cached" "$5" "$6" "$7" || \
		    err "Can't clone final repo" "try_fetch" "$@"; :
	fi

	[ ! -d "$5" ] && \
		return 1; :
}

try_fetch_curl()
{
	cached="file/$6"
	cached="$XBMK_CACHE/$cached"

	x_ xbmkdir "${5%/*}" "${cached%/*}"

	if bad_checksum "$6" "$cached" 2>/dev/null; then
		x_ rm -f "$cached"
	fi

	[ ! -f "$cached" ] && \
		if ! try_$2 "$cached" "$@"; then
			return 1
		fi

	[ -f "$5" ] && \
		if bad_checksum "$6" "$5" 2>/dev/null; then
			x_ cp "$cached" "$5"
		fi

	if [ ! -f "$cached" ]; then
		return 1
	elif bad_checksum "$6" "$cached"; then
		x_ rm -f "$cached"

		return 1
	fi

	[ "$cached" != "$5" ] && \
		x_ cp "$cached" "$5"

	if bad_checksum "$6" "$5"; then
		x_ rm -f "$5"
		return 1
	elif [ ! -f "$5" ]; then
		return 1
	fi
}

try_curl()
{
	_ua=""

	case "$2" in
	https://www.supermicro.com/*)
		_ua="curl/8.6.0";;
	*)
		_ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0";;
	esac

	( x_ curl --location --retry 3 -A "$_ua" "$2" -o "$1" ) \
	    || ( x_ wget --tries 3 -U "$_ua" "$2" -O "$1" ) \
	    || return 1; :
}

try_copy()
{
	( x_ cp "$2" "$1" ) || return 1; :
}

try_git()
{
	gitdest="`findpath "$1" || err "Can't get findpath for '$1'"`" || \
	    err "failed findpath for '$1'" try_get "$@"

	x_ rm -Rf "$tmpgitcache"

	if [ ! -d "$gitdest" ]; then
		if [ "$XBMK_CACHE_MIRROR" = "y" ]; then
			( x_ git clone --mirror "$2" "$tmpgitcache" ) || \
			    return 1
		else
			( x_ git clone "$2" "$tmpgitcache" ) || return 1
		fi

		x_ xbmkdir "${gitdest%/*}"
		x_ mv "$tmpgitcache" "$gitdest"
	fi

	if git -C "$gitdest" show "$7" 1>/dev/null 2>/dev/null && \
	    [ "$forcepull" != "y" ]; then
		# don't try to pull the latest changes if the given target
		# revision already exists locally. this saves a lot of time
		# during release builds, and reduces the chance that we will
		# interact with grub.git or gnulib.git overall during runtime

		return 0
	fi

	if [ "$XBMK_CACHE_MIRROR" = "y" ]; then
		( x_ git -C "$gitdest" fetch ) || :; :
		( x_ git -C "$gitdest" update-server-info ) || :; :
	else
		( x_ git -C "$gitdest" pull --all ) || :; :
	fi
}

bad_checksum()
{
	if e "$2" f missing; then
		return 0
	fi

	build_sbase
	csum="$(x_ "$sha512sum" "$2" | awk '{print $1}')" || \
	    err "!sha512 '$2' $1" bad_checksum "$@"

	[ "$csum" = "$1" ] && \
		return 1

	x_ rm -f "$2"
	printf "BAD SHA512 %s, '%s'; need %s\n" "$csum" "$2" "$1" 1>&2
}

tmpclone()
{
	( x_ git clone "$1" "$2" ) || return 1
	( x_ git -C "$2" reset --hard "$3" ) || return 1

	[ ! -d "$4" ] && \
		return 0

	tmpclone_patchlist="`mktemp || err "Can't create tmp patch list"`" || \
	    err "Can't create tmp patch list" "tmpclone" "$@"

	x_ find "$4" -type f | sort > "$tmpclone_patchlist" || \
	    err "Can't write patch names to '$tmpclone_patchlist'" \
	    "tmpclone" "$@"

	while read -r tmpclone_patch; do

		( x_ git -C "$2" am -3 --keep-cr "$tmpclone_patch" ) || \
		    err "Can't apply '$tmpclone_patch'" "tmpclone" "$@"; :

	done < "$tmpclone_patchlist" || \
	    err "Can't read '$tmpclone_patchlist'" "tmpclone" "$@"

	x_ rm -f "$tmpclone_patchlist"
}
