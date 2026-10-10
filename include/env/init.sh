# SPDX-License-Identifier: GPL-3.0-only

# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2025 Alper Nebi Yasak <alpernebiyasak@gmail.com>

projectname="libreboot"
projectsite="https://libreboot.org/"

eval "`newvar aur_notice basetmp board checkvarschk checkvarsxbmk configdir \
    datadir is_child python pyver reinstall relname version versiondate \
    xbmklock xbmkpath xbtmp sha512sum`"

xbmk_init()
{
	xbmklock="$xbmkpwd/lock"
	basetmp="$xbmkpwd/xbmkwd"
	sha512sum="$xbmkpwd/util/sbase/sha512sum"

	export PATH="$PATH"

	export PWD="$xbmkpwd"
	export XBMK_CACHE="$xbmkpwd/cache"
	export TMPDIR="${TMPDIR-}"

	if [ $# -gt 0 ] && [ "$1" = "dependencies" ]; then
		x_ xbmkpkg "$@"
		exit 0
	fi

	id -u 1>/dev/null 2>/dev/null || \
	    err "suid check failed" "xbmk_init" "$@"

	[ "$(id -u)" = "0" ] && \
		err "this command as root is not permitted" "xbmk_init" "$@"

	x_ mkdir -p "$basetmp" "cache"

	eval "$(printf "get_ver set_env set_threads git_init xchild\n" | awk \
	    '{ for (i=1; i<=NF; i++) $i = $i " \"$@\" || return 0;"; print }')"
}

xbmkpkg()
{
	xchk xbmkpkg "$@"

	[ $# -gt 2 ] && \
		reinstall="$3"

	. "config/dependencies/$2" || \
	    err "Can't read 'config/dependencies/$2'" "xbmkpkg" "$@"

	if [ -z "$pkg_add" ] || [ -z "$pkglist" ]; then
		err "pkg_add/pkglist not both set" "xbmkpkg" "$@"
	fi

	if [ "$2" == "gentoo" ]; then

		if [ -z "$pkg_use" ] || [ -z "$pkg_use_file" ]; then
			err "pkg_use/pkg_use_file not both set" "xbmkpkg" "$@"
		fi

		printf "Updating package use...\n"
		printf "Writing into %s" "$pkg_use_file..."

		x_ mkdir -p "`dirname $pkg_use_file`"

		printf "%s\n" "$pkg_use" >> "$pkg_use_file"
	fi

	x_ $pkg_add $pkglist

	[ -n "$aur_notice" ] && \
		printf "You need AUR packages: %s\n" "$aur_notice" 1>&2; :
}

get_ver()
{
	[ ! -f ".version" ] || \
		read -r version < ".version" || \
		    err "can't read version file" "get_ver" "$@"
	[ ! -f ".versiondate" ] || \
		read -r versiondate < ".versiondate" || \
		    err "can't read versiondate" get_ver "$@"

	[ -f ".version" ] && [ -z "$version" ] && \
		err "version not set" "get_ver" "$@"
	[ -f ".versiondate" ] && [ -z "$versiondate" ] && \
		err "versiondate not set" "get_ver" "$@"

	[ ! -e ".git" ] && [ ! -f ".version" ] && \
		version="unknown"
	[ ! -e ".git" ] && [ ! -f ".versiondate" ] && \
		versiondate="1716415872"

	xbmk_sanitize_version

	[ -n "$version" ] && \
		relname="$projectname-$version"; :
}

# a parent instance will cause this function to return 0.
# a child instance will return 1, skipping further initialisation
# after this function is called.
set_env()
{
	is_child="n"
	xbmkpath="$PATH"

	# unify all temporary files/directories in a single TMPDIR
	[ -n "${TMPDIR+x}" ] && [ "${TMPDIR%_*}" != "$basetmp/xbmk" ] && \
		unset TMPDIR
	if [ -n "${TMPDIR+x}" ]; then
		export TMPDIR="$TMPDIR"
		xbtmp="$TMPDIR"
	fi
	[ -n "${TMPDIR+x}" ] && \
		is_child="y"

	if [ "$is_child" = "y" ]; then
		xbmk_child_set_env
		return 1 # child instance. discontinue initialisation.
	fi
	xbmk_parent_set_env
}

xbmk_child_set_env()
{
	xbmk_child_set_tmp

	[ -z "${XBMK_CACHE+x}" ] && \
		err "XBMK_CACHE unset on child" "set_env" "$@"
	[ -z "${XBMK_THREADS+x}" ] && \
		set_threads; :
}

xbmk_child_set_tmp()
{
	eval "`newvar badtmp locktmp xbtmpchk`"

	xbtmpchk="`findpath "$TMPDIR" || err "!findpath $TMPDIR"`" || \
	    err "!findpath '$TMPDIR'" "xbmk_child_set_tmp" "$@"

	read -r locktmp < "$xbmklock" || \
	    err "can't read '$xbmklock'" "xbmk_child_set_tmp" "$@"

	if [ "$locktmp" != "$xbtmpchk" ]; then
		badtmp="TMPDIR '$xbtmpchk' changed; was '$locktmp'"
		printf "bad TMPDIR init, '%s': %s\n" "$TMPDIR" "$badtmp" 1>&2
		err "'$xbmklock' present with bad tmpdir. is a build running?"
	fi

	xbtmp="$xbtmpchk"
	export TMPDIR="$xbtmpchk"
}

xbmk_parent_set_env()
{
	xbmk_parent_check_tmp

	printf "%s\n" "$xbtmp" > "$xbmklock" || \
	    err "cannot create '$xbmklock'" set_env "$@"; :

	# not really critical for security, but it's a barrier
	# against the user to make them think twice before deleting it
	# in case an actual instance of xbmk is already running:

	x_ chmod -w "$xbmklock"

	forx 'xbmk_%s' parent_set_export set_version

	remkdir "$xbtmp" "$xbtmp/xbmkpath"

	xbmk_set_pyver
}

xbmk_parent_check_tmp()
{
	export TMPDIR="$basetmp"

	xbmklist="`mktemp || err "can't make tmplist"`" || \
	    err "can't make tmplist" xbmk_parent_check_tmp "$@"

	x_ rm -f "$xbmklist"
	x_ touch "$xbmklist"

	for xtmpdir in "$basetmp"/xbmk_*; do
		[ ! -e "$xtmpdir" ] || \
			printf "%s\n" "$xtmpdir" >> "$xbmklist" || \
			    err "can't write '$xtmpdir' to '$xbmklist'" \
			    "xbmk_parent_check_tmp" "$@"; :
	done

	# set up a unified temporary directory, for common deletion later:
	export TMPDIR="`x_ mktemp -d -t xbmk_XXXXXXXX`" || \
	    err "can't export TMPDIR" "xbmk_parent_check_tmp" "$@"
	xbtmp="$TMPDIR"

	while read -r xtmpdir; do
		[ "$xtmpdir" = "$xbtmp" ] && \
			err "pre-existing $xbtmp" xbmk_parent_check_tmp "$@"; :
	done < "$xbmklist" || \
	    err "Can't read xbmklist: '$xbmklist'" "xbmk_parent_check_tmp" "$@"

	x_ rm -f "$xbmklist"
}

xbmk_parent_set_export()
{
	[ -e "$XBMK_CACHE" ] && [ ! -d "$XBMK_CACHE" ] && \
		err "cachedir '$XBMK_CACHE' is a file" \
		    "xbmk_parent_set_export" "$@"

	export PATH="$xbtmp/xbmkpath:$PATH"
	xbmkpath="$PATH"

	# if "y": a coreboot target won't be built if target.cfg says release=n
	# (this is used to exclude certain build targets from releases)

	[ -z "${XBMK_RELEASE+x}" ] && \
		export XBMK_RELEASE="n"
	[ "$XBMK_RELEASE" = "Y" ] && \
		export XBMK_RELEASE="y"
	[ "$XBMK_RELEASE" != "y" ] && \
		export XBMK_RELEASE="n"; :

	# change IFD MAC address at build time (GbE NVM)
	[ -z "${XBMK_MAC+x}" ] && \
		export XBMK_MAC="xx:xx:xx:xx:xx:xx"; :
}

set_threads()
{
	[ -z "${XBMK_THREADS+x}" ] && \
		export XBMK_THREADS=1
	expr "X$XBMK_THREADS" : "X-\{0,1\}[0123456789][0123456789]*$" \
	    1>/dev/null 2>/dev/null || \
		export XBMK_THREADS=1; :
}

xbmk_set_version()
{
	version_="$version"
	[ ! -e ".git" ] || \
		version="$(git describe --tags HEAD 2>&1)" || \
		    version="git-$(git rev-parse HEAD 2>&1)" || \
		    version="$version_"

	versiondate_="$versiondate"
	[ ! -e ".git" ] || \
		versiondate="$(git show --no-patch --no-notes \
		    --pretty='%ct' HEAD)" || versiondate="$versiondate_"

	update_xbmkver "."

	relname="$projectname-$version"
	export LOCALVERSION="-$projectname-${version%%-*}"
}

xbmk_set_pyver()
{
	python="python3"
	pyver="2"
	pyv="import sys; print(sys.version_info[:])"

	pybin python3 1>/dev/null || \
		python="python"
	[ "$python" = "python3" ] && \
		pyver="3"
	pybin "$python" 1>/dev/null || \
		pyver=""

	[ -z "$pyver" ] || \
		"`x_ pybin "$python"`" -c "$pyv" 1>/dev/null \
		    2>/dev/null || \
		    err "Can't detect Python version." "xbmk_set_pyver" "$@"
	if [ -n "$pyver" ]; then
		pyver="$("$(pybin "$python")" -c "$pyv" | awk '{print $1}')"
		pyver="${pyver#(}"
		pyver="${pyver%,}"
	fi
	[ "${pyver%%.*}" != "3" ] && \
		err "Bad python version (must by 3.x)" "xbmk_set_pyver" "$@"

	# set up python in PATH (environmental variable):
	(
		x_ cd "$xbtmp/xbmkpath"
		x_ ln -s "`x_ pybin "$python"`" python || \
		    err "can't make symlink" "xbmk_set_pyver" "$@"

	) || err "Can't link Python in $xbtmp/xbmkpath" xbmk_set_pyver "$@"; :
}

# Use direct path, to prevent a hang if Python is using a virtual environment,
# not command -v, to prevent a hang when checking python's version
# See: https://docs.python.org/3/library/venv.html#how-venvs-work
pybin()
{
	py="import sys; quit(1) if sys.prefix == sys.base_prefix else quit(0)"

	venv=1
	command -v "$1" 1>/dev/null 2>/dev/null || \
		venv=0

	[ $venv -lt 1 ] || \
		"$1" -c "$py" 1>/dev/null 2>/dev/null || \
			venv=0

	# try the direct path from PATH first. the binary specified by venv
	# can be called directly, which then removes use of the venv
	if [ $venv -gt 0 ] && [ -L "`command -v "$1" 2>/dev/null`" ]; then
		pypath="$(findpath \
		    "$(command -v "$1" 2>/dev/null)" 2>/dev/null || :)"
		if [ -e "$pypath" ] && [ ! -d "$pypath" ] && \
		    [ -x "$pypath" ]; then
			printf "%s\n" "$pypath"
			return 0
		fi
	fi

	# if python venv: fall back to common PATH directories for checking:
	[ $venv -gt 0 ] && for pypath in "/usr/local/bin" "/usr/bin"; do
		if [ -e "$pypath/$1" ] && [ ! -d "$pypath/$1" ] && \
		    [ -x "$pypath/$1" ]; then
			printf "%s/%s\n" "$pypath" "$1"
			return 0
		fi
	done && return 1

	# Defer to normal command -v if not a venv
	command -v "$1" 2>/dev/null || \
		return 1; :
}

git_init()
{
	# the git identity check is only needed for: "$mk" -b coreboot [args]
	[ "${2-}" = "-b" ] && [ "${3-}" = "coreboot" ] && \
	    for gitarg in "user.name" "user.email"; do
		git config --includes $gitarg 1>/dev/null 2>/dev/null || \
			err "Run this first: $gitcmd \"your ${gitcmd##*.}\"" \
			    "git_init" "$@"; :
	done

	[ -L ".git" ] && \
		err "'$xbmkpwd/.git' is a symlink" "git_init" "$@"
	[ -e ".git" ] && \
		return 0

	(
	export GIT_COMMITTER_DATE="@$versiondate +0000"
	export GIT_AUTHOR_DATE="@$versiondate +0000"

	x_ git init 1>/dev/null 2>/dev/null
	x_ git add -A . 1>/dev/null 2>/dev/null
	x_ git -c user.name="xbmk" -c user.email="xbmk@example.com" \
	    commit -m "$projectname $version" \
	    --author="xbmk <xbmk@example.com>" 1>/dev/null 2>/dev/null
	x_ git -c user.name="xbmk" -c user.email="xbmk@example.com" \
	    tag -a "$version" -m "$projectname $version" \
	    1>/dev/null 2>/dev/null; :

	) || err "Can't initialise workdir" git_init "$@"; :
}

xchild()
{
	xbmk_rval=0

	( x_ "$mk" "$@" ) || xbmk_rval=1

	( x_ rm -Rf "$xbtmp" ) || xbmk_rval=1
	( x_ rm -f "$xbmklock" ) || xbmk_rval=1

	exit $xbmk_rval
}
