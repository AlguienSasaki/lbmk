# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2025 Alper Nebi Yasak <alpernebiyasak@gmail.com>

export LC_COLLATE=C
export LC_ALL=C

projectname="libreboot"
projectsite="https://libreboot.org/"

[ -z "${PATH+x}" ] && \
    export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/games:/usr/games"
xbmkpath="$PATH"

eval "`setvars "" _nogit board reinstall versiondate aur_notice configdir \
    datadir version xbmkpwd relname xbmkpwd xbmktmp python pyver xbmklocal \
    xbmklock`"

xbmk_init()
{
	xbmkpwd="`pwd`" || err "Cannot generate PWD"
	xbmklocal="$xbmkpwd/tmp"
	xbmklock="$xbmkpwd/lock"

	export PWD="$xbmkpwd"

	[ $# -gt 0 ] && [ "$1" = "dependencies" ] && x_ xbmkpkg "$@" && exit 0

	id -u 1>/dev/null 2>/dev/null || err "suid check failed (id -u)"
	[ "$(id -u)" != "0" ] || err "this command as root is not permitted"

	for init_cmd in set_pyver set_version set_env git_init create_tmpdir \
	    lock create_pathdirs child_exec; do
		xbmk_$init_cmd "$@" || break
	done
}

xbmkpkg()
{
	[ $# -lt 2 ] && err "fewer than two arguments"
	[ $# -gt 2 ] && reinstall="$3"

	eval "`setcfg "config/dependencies/$2"`"

	chkvars pkg_add pkglist
	$pkg_add $pkglist || err "Cannot install packages"

	[ -n "$aur_notice" ] && \
	    printf "You need AUR packages: %s\n" "$aur_notice" 1>&2; :
}

xbmk_set_pyver()
{
	pyv="import sys; print(sys.version_info[:])"
	python="python3"
	pybin python3 1>/dev/null || python="python"
	pyver="2" && [ "$python" = "python3" ] && pyver="3"
	pybin "$python" 1>/dev/null || pyver=""
	[ -z "$pyver" ] || "`pybin "$python"`" -c "$pyv" 1>/dev/null \
	    2>/dev/null || err "Cannot detect host Python version."
	[ -n "$pyver" ] && \
	    pyver="$("$(pybin "$python")" -c "$pyv" | awk '{print $1}')" && \
	    pyver="${pyver#(}" && pyver="${pyver%,}"
	[ "${pyver%%.*}" = "3" ] || err "Bad python version (must by 3.x)"; :
}

# Use direct path, to prevent a hang if Python is using a virtual environment,
# not command -v, to prevent a hang when checking python's version
# See: https://docs.python.org/3/library/venv.html#how-venvs-work
pybin()
{
	py="import sys; quit(1) if sys.prefix == sys.base_prefix else quit(0)"

	venv=1
	command -v "$1" 1>/dev/null 2>/dev/null || venv=0
	[ $venv -lt 1 ] || "$1" -c "$py" 1>/dev/null 2>/dev/null || venv=0

	# ideally, don't rely on PATH or hardcoded paths if python venv.
	# use the *real*, direct executable linked to by the venv symlink
	if [ $venv -gt 0 ] && [ -L "`command -v "$1" 2>/dev/null`" ]; then
		pypath="$(findpath \
		    "$(command -v "$1" 2>/dev/null)" 2>/dev/null || :)"
		[ -e "$pypath" ] && [ ! -d "$pypath" ] && \
		    [ -x "$pypath" ] && printf "%s\n" "$pypath" && return 0; :
	fi

	# if python venv: fall back to common PATH directories for checking
	[ $venv -gt 0 ] && for pypath in "/usr/local/bin" "/usr/bin"; do
		[ -e "$pypath/$1" ] && [ ! -d "$pypath/$1" ] && \
		    [ -x "$pypath/$1" ] && printf "%s/%s\n" "$pypath" "$1" && \
		    return 0
	done && return 1

	# Defer to normal command -v if not a venv
	command -v "$1" 2>/dev/null || return 1
}

xbmk_set_version()
{
	[ ! -f ".version" ] || read -r version < ".version" || :; :
	[ ! -f ".versiondate" ] || read -r versiondate < ".versiondate" || :; :

	[ -e ".git" ] || [ -f ".version" ] || printf "unknown\n" > ".version" \
	    || err "Cannot generate unknown .version file"
	[ -e ".git" ] || [ -f ".versiondate" ] || printf "1716415872\n" > \
	    ".versiondate" || err "Can't generate unknown versiondate file"; :

	version_="$version"
	[ ! -e ".git" ] || version="$(git describe --tags HEAD 2>&1)" || \
	    version="git-$(git rev-parse HEAD 2>&1)" || version="$version_"
	versiondate_="$versiondate"
	[ ! -e ".git" ] || versiondate="$(git show --no-patch --no-notes \
	    --pretty='%ct' HEAD)" || versiondate="$versiondate_"

	chkvars version versiondate
	printf "%s\n" "$version" > ".version" || err "can't save version"
	printf "%s\n" "$versiondate" > ".versiondate" || err "can't save date"

	relname="$projectname-$version"
}

xbmk_set_env()
{
	export LOCALVERSION="-$projectname-${version%%-*}"

	# XBMK_CACHE is a directory, for caching downloads and git repon
	[ -z "${XBMK_CACHE+x}" ] && export XBMK_CACHE="$xbmkpwd/cache"
	[ -z "$XBMK_CACHE" ] && export XBMK_CACHE="$xbmkpwd/cache"
	[ -L "$XBMK_CACHE" ] && [ "$XBMK_CACHE" = "$xbmkpwd/cache" ] && \
	    err "cachedir '$xbmkpwd/cache' is a symlink"
	[ -L "$XBMK_CACHE" ] && export XBMK_CACHE="$xbmkpwd/cache"
	xbmkcache="`findpath "$XBMK_CACHE"`" || \
	    err "Can't resolve cachedir: '$XBMK_CACHE'"
	export XBMK_CACHE="$xbmkcache"
	[ -d "$XBMK_CACHE" ] || err "cachedir '$XBMK_CACHE' is a file"; :

	# if "y": a coreboot target won't be built if target.cfg says release="n"
	# (this is used to exclude certain build targets from releases)
	[ -z "${XBMK_RELEASE+x}" ] && export XBMK_RELEASE="n"
	[ "$XBMK_RELEASE" = "N" ] && export XBMK_RELEASE="n"
	[ "$XBMK_RELEASE" = "Y" ] && export XBMK_RELEASE="y"
	[ "$XBMK_RELEASE" = "y" ] || export XBMK_RELEASE="n"

	[ -z "${XBMK_THREADS+x}" ] && export XBMK_THREADS=1
	expr "X$XBMK_THREADS" : "X-\{0,1\}[0123456789][0123456789]*$" \
	    1>/dev/null 2>/dev/null || export XBMK_THREADS=1; :
}

xbmk_git_init()
{
	for gitarg in "--global user.name" "--global user.email"; do
		gitcmd="git config $gitarg"; $gitcmd 1>/dev/null 2>/dev/null \
		    || err "Run this first: $gitcmd \"your ${gitcmd##*.}\""
	done

	[ -L ".git" ] && return 1
	[ -e ".git" ] && return 0
	eval "`setvars "$(date -Rud @$versiondate)" cdate _nogit`"

	x_ git init 1>/dev/null 2>/dev/null
	x_ git add -A . 1>/dev/null 2>/dev/null
	x_ git commit -m "$projectname $version" --date "$cdate" \
	    --author="xbmk <xbmk@example.com>" 1>/dev/null 2>/dev/null
	x_ git tag -a "$version" -m "$projectname $version" 1>/dev/null \
	    2>/dev/null; :
}

xbmk_create_tmpdir()
{
	x_ mkdir -p "$xbmklocal"

	# unify all temporary files/directories in a single TMPDIR
	[ -z "${TMPDIR+x}" ] || [ "${TMPDIR%_*}" = "/tmp/xbmk" ] || \
	    unset TMPDIR
	[ -n "${TMPDIR+x}" ] && export TMPDIR="$TMPDIR" && xbmktmp="$TMPDIR"
	[ -z "${TMPDIR+x}" ] || return 1 # child instance, so return

	# parent instance of xbmk, so don't return. set up TMPDIR
	export TMPDIR="/tmp"
	export TMPDIR="$(mktemp -d -t xbmk_XXXXXXXX)"
	xbmktmp="$TMPDIR"

	# /tmp might be a tmpfs, so for large files we use ./tmp,
	# not to be confused with xbmktmp (xbmktmp points to /tmp)
	remkdir "$xbmktmp" "$xbmklocal"
}

xbmk_lock()
{
	[ -f "$xbmklock" ] && err "'$xbmklock' exists. Is a build running?"
	touch "$xbmklock" || err "cannot create '$xbmklock'"; :
}

xbmk_create_pathdirs()
{
	remkdir "$XBMK_CACHE/gnupath" "$XBMK_CACHE/xbmkpath"
	export PATH="$XBMK_CACHE/xbmkpath:$XBMK_CACHE/gnupath:$PATH"
	(
	# set up python v3.x in PATH, in case it's not set up correctly.
	# see code above that detected the correct python3 command.
	cd "$XBMK_CACHE/xbmkpath" || err "can't cd $XBMK_CACHE/xbmkpath"
	x_ ln -s "`pybin "$python"`" python
	) || err "Can't set up python symlink in $XBMK_CACHE/xbmkpath"; :
}

xbmk_child_exec()
{
	xbmk_rval=0
	( x_ ./mk "$@" ) || xbmk_rval=1
	( x_ rm -Rf "$xbmklocal" "$xbmktmp" ) || xbmk_rval=1
	( x_ rm -f "$xbmklock" ) || xbmk_rval=1
	exit $xbmk_rval
}

xbmk_init "$@"
