# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2021,2023-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

eval "`setvars "" loc url bkup_url subcurl subhash subgit subgit_bkup \
    depend subcurl_bkup`"

tmpgit="$xbtmp/gitclone"
tmpgitcache="$xbtmp/tmpgit"

fetch_targets()
{
	if [ ! -d "src/$project/$tree" ]; then
		git_prep "$url" "$bkup_url" \
		    "$xbmkpwd/$configdir/$tree/patches" \
		    "src/$project/$tree" "submod"
	fi
}

fetch_project()
{
	eval "`setvars "" xtree`"
	eval "`setcfg "config/git/$project/pkg.cfg"`"

	chkvars url bkup_url

	if [ -n "$xtree" ]; then
		x_ ./mk -f coreboot "$xtree"
	fi
	if [ -n "$depend" ]; then
		for d in $depend ; do
			x_ ./mk -f $d
		done
	fi

	clone_project
}

clone_project()
{
	loc="$XBMK_CACHE/clone/$project"
	if singletree "$project"; then
		loc="src/$project"
	fi

	if e "$loc" d missing; then
		remkdir "${tmpgit%/*}"
		git_prep "$url" "$bkup_url" \
		    "$xbmkpwd/config/$project/patches" "$loc"
	fi
}

git_prep()
{
	printf "Creating code directory, src/%s/%s\n" "$project" "$tree"

	_patchdir="$3"
	_loc="$4" # $1 and $2 are gitrepo and gitrepo_backup

	chkvars rev

	xbget git "$1" "$2" "$tmpgit" "$rev" "$_patchdir"
	if singletree "$project" || [ $# -gt 4 ]; then
		dx_ fetch_submodule "$mdir/module.list"
	fi

	if [ "$_loc" != "${_loc%/*}" ]; then
		x_ mkdir -p "${_loc%/*}"
	fi
	x_ mv "$tmpgit" "$_loc"
}

fetch_submodule()
{
	mcfgdir="$mdir/${1##*/}"

	eval \
	    "`setvars "" subhash subgit subgit_bkup subcurl subcurl_bkup st`"

	eval "`setcfg "$mcfgdir/module.cfg" 0`"

	for xt in git curl; do
		_seval="if [ -n \"\$sub$xt\" ] || [ -n \"\$sub${xt}_bkup\" ]"
		eval "$_seval; then st=\"\$st \$xt\"; fi"
	done

	st="${st# }"
	if [ "$st" = "git curl" ]; then
		err "$mdir: git+curl defined" "fetch_submodule" "$@"
	fi

	if [ -n "$st" ]
	then
		chkvars "sub${st}" "sub${st}_bkup" "subhash"

		if [ "$st" = "git" ]; then
			x_ rm -Rf "$tmpgit/$1"
		fi

		eval xbget "$st" "\$sub$st" "\$sub${st}_bkup" "$tmpgit/$1" \
		    "$subhash" "$mdir/${1##*/}/patches"
	fi
}

# TODO: in the following functions, argument numbers are used
#       which is hard to understand. the code should be modified
#       so that variable names are used instead, for easy reading

xbget()
{
	if [ "$1" != "curl" ] && [ "$1" != "copy" ] && [ "$1" != "git" ]; then
		err "Bad dlop (arg 1)" "xbget" "$@"
	fi

	for url in "$2" "$3"
	do
		if [ -z "$url" ]; then
			err "empty URL given in" "xbget" "$@"
		elif ! try_fetch "$url" "$@"; then
			continue
		fi

		case "$1" in
		git)
			if [ ! -d "$4" ]; then
				continue
			fi
			;;
		*)
			if [ ! -f "$4" ]; then
				continue
			fi
			;;
		esac
		return 0 # successful download/copy
	done

	err "failed to download file/repository" "xbget" "$@"; :
}

# TODO: try_fetch is also a bit messy. those eval statements can
#       be tidied up, or eval can be dropped entirely.
# (it works much better than the old code, but it's over-engineered)

try_fetch()
{
	cached="file/$6"
	if [ "$2" = "git" ]
	then
		# always the main repo as basis for naming,
		# in case the backup has another name

		cached="clone/${3##*/}"
		cached="${cached%.git}"
	fi
	cached="$XBMK_CACHE/$cached"

	x_ mkdir -p "${5%/*}" "${cached%/*}"

	echk="d"
	if [ "$2" != "git" ]; then
		echk="f"
		if bad_checksum "$6" "$cached" 2>/dev/null; then
			x_ rm -f "$cached"
		fi
	fi

	evalchk="[ -$echk \"$cached\" ] || "
	if [ "$2" = "git" ]; then
		evalchk=""
	fi

	eval "${evalchk}try_$2 \"\$cached\" \"\$@\" || return 1"

	if [ "$2" != "git" ] && [ -f "$5" ]; then
		if bad_checksum "$6" "$5" 2>/dev/null; then
			x_ cp "$cached" "$5"
		fi
	fi

	eval "[ -$echk \"$cached\" ] || return 1"

	if [ "$2" = "git" ]
	then
		if [ ! -d "$5" ]; then
			tmpclone "$cached" "$5" "$6" "$7" || \
			    err "Can't clone final repo" "try_fetch" "$@"; :
		fi
	else
		if bad_checksum "$6" "$cached"; then
			x_ rm -f "$cached"
			return 1
		fi
		if [ "$cached" != "$5" ]; then
			x_ cp "$cached" "$5"
		fi
		if bad_checksum "$6" "$5"; then
			x_ rm -f "$5"
			return 1
		fi
	fi

	eval "[ -$echk \"$5\" ] || return 1"
}

try_curl()
{
	_ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0"

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
		( x_ git clone "$2" "$tmpgitcache" ) || return 1

		x_ mkdir -p "${gitdest%/*}"
		x_ mv "$tmpgitcache" "$gitdest"
	fi

	if git -C "$gitdest" whatchanged "$7" 1>/dev/null 2>/dev/null && \
	    [ "$forcepull" != "y" ]; then
		# don't try to pull the latest changes if the given target
		# revision already exists locally. this saves a lot of time
		# during release builds, and reduces the chance that we will
		# interact with grub.git or gnulib.git overall during runtime

		return 0
	fi

	( x_ git -C "$gitdest" remote remove main ) || :
	( x_ git -C "$gitdest" remote remove backup ) || :

	x_ git -C "$gitdest" remote add main "$4"
	x_ git -C "$gitdest" remote add backup "$5"

	( x_ git -C "$gitdest" pull --all ) || :; :
}

bad_checksum()
{
	if e "$2" f missing; then
		return 0
	fi

	csum="$(x_ sha512sum "$2" | awk '{print $1}')" || \
	    err "!sha512 '$2' $1" bad_checksum "$@"

	if [ "$csum" = "$1" ]; then
		return 1
	else
		x_ rm -f "$2"
		printf "BAD SHA512 %s, '%s'; need %s\n" "$csum" "$2" "$1" 1>&2
	fi
}

tmpclone()
{
	( x_ git clone "$1" "$2" ) || return 1
	( x_ git -C "$2" reset --hard "$3" ) || return 1
	( fx_ "eval x_ git -C \"$2\" am" find "$4" -type f ) || return 1; :
}
