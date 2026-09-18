# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2021,2023-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

eval "`newvar url bkup_url fetch_depend loc subcurl subcurl_bkup subgit \
    subgit_bkup subhash badhash badtghash`"

tmpgit="$xbtmp/gitclone"
tmpgitcache="$xbtmp/tmpgit"

# delete a source tree and corresponding builds if metadata changed
# inside config/ - this is done by hashing a concatenation of hashes
# from all metadata, checked against a previously calculated hash

delete_old_project_files()
{
	# delete an entire source tree along with its builds:
	same_pjhash mhash "$tree" badhash "$datadir" \
	    "$configdir/$tree" "$mdir" || \
		x_ rm -Rf "src/$project/$tree" "elf/$project/$tree"

	x_ cp "$xbtmp/new.hash" "$XBMK_CACHE/mhash/$project$tree"

	if [ -n "$target" ] && [ "$target" != "$tree" ]; then
		# delete only target builds, if only the target changed
		same_pjhash mtghash "$target" badtghash "$configdir/$target" \
		    || x_ rm -Rf "elf/$project/$tree/$target"

		x_ cp "$xbtmp/new.hash" "$XBMK_CACHE/mtghash/$project$target"
	fi
}

same_pjhash()
{
	eval "`newvar hash old_hash`"

	hashdir="$1"
	hashname="$2"
	badhashvar="$3"

	shift 3

	x_ mkdir -p "$XBMK_CACHE/$hashdir"

	if [ -f "$XBMK_CACHE/$hashdir/$project$hashname" ]; then
		read -r old_hash < "$XBMK_CACHE/$hashdir/$project$hashname" \
		    || err \
		    "$hashdir: err '$XBMK_CACHE/$hashdir/$project$hashname'" \
		    same_pjhash "$hashdir" "$hashname" "$badhashvar" "$@"
	fi

	eval "$build_sbase"
	fx_ "x_ util/sbase/sha512sum" find "$@" -type f -not -path \
	    "*/.git*/*" | awk '{print $1}' > "$xbtmp/tmp.hash" || \
	    err "!h $project $hashdir" \
	    "same_pjhash" "$hashdir" "$hashname" "$badhashvar" "$@"

	hash="$(x_ "$sha512sum" "$xbtmp/tmp.hash" | awk '{print $1}' || \
	    err)" || err "$hashname: Can't read sha512 of '$xbtmp/tmp.hash'" \
	    "same_pjhash" "$hashdir" "$hashname" "$badhashvar" "$@"

	if [ "$hash" != "$old_hash" ] || \
	    [ ! -f "$XBMK_CACHE/$hashdir/$project$hashname" ]; then
		eval "$badhashvar=\"y\""
	fi

	printf "%s\n" "$hash" > "$xbtmp/new.hash" || \
	    err "!mkhash $xbtmp/new.hash ($hashdir $hashname $badhashvar)" \
	    "same_pjhash" "$hashdir" "$hashname" "$badhashvar" "$@"

	eval "[ \"\$$badhashvar\" = \"y\" ] && return 1"; :
}

fetch_trees()
{
	[ -d "src/$project/$tree" ] && return 0

	mk_dependencies -f $fetch_depend

	git_prep "$url" "$bkup_url" "$xbmkpwd/$configdir/$tree/patches" \
	    "src/$project/$tree"
}

git_prep()
{
	printf "Creating code directory, src/%s/%s\n" "$project" "$tree"

	_patchdir="$3"
	_loc="$4" # $1 and $2 are gitrepo and gitrepo_backup

	[ -z "$rev" ] && \
		err "$project/$tree: rev unset" "git_prep" "$@"

	xbget git "$1" "$2" "$tmpgit" "$rev" "$_patchdir"
	dx_ fetch_submodule "$mdir/module.list"

	[ "$_loc" != "${_loc%/*}" ] && \
		x_ mkdir -p "${_loc%/*}"

	x_ mv "$tmpgit" "$_loc"
}

fetch_submodule()
{
	mcfgdir="$mdir/${1##*/}"
	eval "`newvar st subcurl subcurl_bkup subgit subgit_bkup subhash`"

	e "$mcfgdir/module.cfg" f missing && return 0
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
		[ -e "$4" ] && return 0; : # successful download/copy
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

	x_ mkdir -p "${5%/*}" "${cached%/*}"

	try_$2 "$cached" "$@" || \
		return 1
	[ ! -d "$cached" ] && \
		return 1

	if [ ! -d "$5" ]; then
		tmpclone "$cached" "$5" "$6" "$7" || \
		    err "Can't clone final repo" "try_fetch" "$@"; :
	fi

	[ ! -d "$5" ] && \
		return 1; :
}

try_fetch_copy()
{
	try_fetch_curl "$@" || return 1; :
}

try_fetch_curl()
{
	cached="file/$6"
	cached="$XBMK_CACHE/$cached"

	x_ mkdir -p "${5%/*}" "${cached%/*}"

	if bad_checksum "$6" "$cached" 2>/dev/null; then
		x_ rm -f "$cached"
	fi

	[ -f "$cached" ] || \
		try_$2 "$cached" "$@" || \
			return 1

	[ -f "$5" ] && bad_checksum "$6" "$5" 2>/dev/null && \
		x_ cp "$cached" "$5"

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
	ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0"

	( x_ curl --location --retry 3 -A "$ua" "$2" -o "$1" ) \
	    || ( x_ wget --tries 3 -U "$ua" "$2" -O "$1" ) || return 1; :
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

		x_ mkdir -p "${gitdest%/*}"
		x_ mv "$tmpgitcache" "$gitdest"
	fi

	git -C "$gitdest" show "$7" 1>/dev/null 2>/dev/null && \
	    [ "$forcepull" != "y" ] && \
		return 0 # don't pull changes if target rev exists locally

	if [ "$XBMK_CACHE_MIRROR" = "y" ]; then
		( x_ git -C "$gitdest" fetch ) || :; :
		( x_ git -C "$gitdest" update-server-info ) || :; :
	else
		( x_ git -C "$gitdest" pull --all ) || :; :
	fi
}

bad_checksum()
{
	e "$2" f missing && \
		return 0

	eval "$build_sbase"
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
