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

eval "`setvars "" _nogit board reinstall versiondate aur_notice configdir \
    datadir version xbmkpwd relname xbmkpwd xbtmp python pyver xbmklock \
    cvxbmk cvchk xbmkpath is_child basetmp`"

xbmk_init()
{
	xbmkpwd="`pwd || err "Cannot generate PWD"`" || err "!" xbmk_init "$@"
	xbmklock="$xbmkpwd/lock"
	basetmp="$xbmkpwd/xbmkwd"

	[ $# -gt 0 ] && [ "$1" = "dependencies" ] && x_ xbmkpkg "$@" && exit 0

	export PWD="$xbmkpwd"
	x_ mkdir -p "$basetmp"

	id -u 1>/dev/null 2>/dev/null || err "suid check failed" xbmk_init "$@"
	[ "$(id -u)" != "0" ] || \
	    err "this command as root is not permitted" xbmk_init "$@"

	for init_cmd in get_version set_env set_threads git_init child_exec; do
		xbmk_$init_cmd "$@" || break
	done
}

xbmkpkg()
{
	xchk xbmkpkg "$@"
	[ $# -gt 2 ] && reinstall="$3"

	eval "`setcfg "config/dependencies/$2"`"

	chkvars pkg_add pkglist
	x_ $pkg_add $pkglist

	[ -n "$aur_notice" ] && \
	    printf "You need AUR packages: %s\n" "$aur_notice" 1>&2; :
}

xbmk_get_version()
{
	[ ! -f ".version" ] || read -r version < ".version" || \
	    err "can't read version file" xbmk_get_version "$@"
	[ ! -f ".versiondate" ] || read -r versiondate < ".versiondate" || \
	    err "can't read versiondate" xbmk_get_version "$@"
	[ ! -f ".version" ] || chkvars version
	[ ! -f ".versiondate" ] || chkvars versiondate

	[ ! -e ".git" ] && [ ! -f ".version" ] && version="unknown"
	[ ! -e ".git" ] && [ ! -f ".versiondate" ] && versiondate="1716415872"

	xbmk_sanitize_version

	[ -n "$version" ] && relname="$projectname-$version"; :
}

xbmk_set_env()
{
	xbmkpath="$PATH"

	is_child="n"
	[ -f "$xbmklock" ] && is_child="y"

	if [ "$is_child" = "y" ]; then
		xbmk_child_set_tmp
		[ -z "${XBMK_CACHE+x}" ] && \
		    err "XBMK_CACHE unset on child" xbmk_set_env "$@"
		[ -z "${XBMK_THREADS+x}" ] && xbmk_set_threads; :
		return 1
	fi

	# parent instance of xbmk, so continue.
	
	xbmk_parent_check_tmp
	printf "%s\n" "$xbtmp" > "$xbmklock" || \
	    err "cannot create '$xbmklock'" xbmk_set_env "$@"; :
	x_ chmod -w "$xbmklock"

	xbmk_parent_set_export
	xbmk_set_version
	remkdir "$xbtmp" "$xbtmp/gnupath" "$xbtmp/xbmkpath"
	xbmk_set_pyver
}

xbmk_child_set_tmp()
{
	eval `setvars "" xbtmp badtmp xbtmpchk xbtmpname`

	[ -z "${TMPDIR+x}" ] && export TMPDIR="$basetmp"

	# extremely pedantic safety checks on TMPDIR
	xbtmpchk="`findpath "$TMPDIR" || err "!findpath $TMPDIR"`" || \
	    err "!findpath '$TMPDIR'" xbmk_child_set_tmp "$@"
	[ "$xbtmpchk" = "${xbtmpchk#"$basetmp/"}" ] && \
	    badtmp="not a subdirectory in $basetmp"
	[ -z "$badtmp" ] && xbtmpname="${xbtmpchk#"$basetmp/"}" && \
	    [ -z "$xbtmpchk" ] && badtmp="name after $basetmp is empty"
	[ -z "$badtmp" ] && [ "$xbtmpname" != "${xbtmpname#*/}" ] && \
	    badtmp="'$TMPDIR' is a subdirectory in a subdir of $basetmp"
	[ -z "$badtmp" ] && [ -L "$xbtmpchk" ] && badtmp="is a symlink"
	[ -z "$badtmp" ] && [ ! -d "$xbtmpchk" ] && \
	    badtmp="not a directory"
	if [ -z "$badtmp" ]; then
		# final check: check if TMPDIR changed
		locktmp=""
		read -r locktmp < "$xbmklock" || \
		    err "can't read '$xbmklock'" xbmk_child_set_tmp "$@"
		[ "$locktmp" = "$xbtmpchk" ] || \
		    badtmp="TMPDIR '$xbtmpchk' changed; was '$locktmp'"
	fi
	[ -n "$badtmp" ] && \
	    printf "bad TMPDIR initialisation, '%s': %s\n" \
	    "$TMPDIR" "$badtmp" 1>&2
	[ -n "$badtmp" ] && err \
	    "'$xbmklock' present with bad tmpdir. is a build running?"

	xbtmp="$xbtmpchk"
	export TMPDIR="$xbtmpchk"
}

xbmk_parent_check_tmp()
{
	export TMPDIR="$basetmp"

	xbmklist="`mktemp || err "can't make tmplist"`" || \
	    err "can't make tmplist" xbmk_parent_check_tmp "$@"
	x_ rm -f "$xbmklist"
	x_ touch "$xbmklist"
	for xtmpdir in "$basetmp"/xbmk_*; do
		[ -e "$xtmpdir" ] || continue
		printf "%s\n" "$xtmpdir" >> "$xbmklist" || \
		    err "can't write '$xtmpdir' to file: '$xbmklist'" \
		    xbmk_parent_check_tmp "$@"; :
	done

	# set up a unified temporary directory, for common deletion later:
	export TMPDIR="`x_ mktemp -d -t xbmk_XXXXXXXX`" || \
	    err "can't export TMPDIR" xbmk_parent_check_tmp "$@"
	xbtmp="$TMPDIR"

	while read -r xtmpdir; do
		[ "$xtmpdir" = "$xbtmp" ] && err \
		    "'$xbtmp' existed previously (possible race condition)" \
		    xbmk_parent_check_tmp "$@"; :
	done < "$xbmklist" || err "Couldn't read xbmklist: '$xbmklist'" \
	    xbmk_parent_check_tmp "$@"
	x_ rm -f "$xbmklist"
}

xbmk_parent_set_export()
{
	export XBMK_CACHE="$xbmkpwd/cache"
	[ -L "$XBMK_CACHE" ] && [ "$XBMK_CACHE" = "$xbmkpwd/cache" ] && \
	    err "cachedir '$xbmkpwd/cache' is a symlink" \
	    xbmk_parent_set_export "$@"
	[ ! -e "$XBMK_CACHE" ] || \
	    [ -d "$XBMK_CACHE" ] || err "cachedir '$XBMK_CACHE' is a file" \
	    xbmk_parent_set_export "$@"; :

	export PATH="$xbtmp/xbmkpath:$xbtmp/gnupath:$PATH"
	xbmkpath="$PATH"

	# if "y": a coreboot target won't be built if target.cfg says release="n"
	# (this is used to exclude certain build targets from releases)
	[ -z "${XBMK_RELEASE+x}" ] && export XBMK_RELEASE="n"
	[ "$XBMK_RELEASE" = "Y" ] && export XBMK_RELEASE="y"
	[ "$XBMK_RELEASE" = "y" ] || export XBMK_RELEASE="n"; :
}

xbmk_set_threads()
{
	[ -z "${XBMK_THREADS+x}" ] && export XBMK_THREADS=1
	expr "X$XBMK_THREADS" : "X-\{0,1\}[0123456789][0123456789]*$" \
	    1>/dev/null 2>/dev/null || export XBMK_THREADS=1
}

xbmk_set_version()
{
	version_="$version"
	[ ! -e ".git" ] || version="$(git describe --tags HEAD 2>&1)" || \
	    version="git-$(git rev-parse HEAD 2>&1)" || version="$version_"
	versiondate_="$versiondate"
	[ ! -e ".git" ] || versiondate="$(git show --no-patch --no-notes \
	    --pretty='%ct' HEAD)" || versiondate="$versiondate_"

	chkvars version versiondate
	update_xbmkver "."

	relname="$projectname-$version"
	export LOCALVERSION="-$projectname-${version%%-*}"
}

xbmk_set_pyver()
{
	pyv="import sys; print(sys.version_info[:])"
	python="python3"
	pybin python3 1>/dev/null || python="python"
	pyver="2" && [ "$python" = "python3" ] && pyver="3"
	pybin "$python" 1>/dev/null || pyver=""
	[ -z "$pyver" ] || "`x_ pybin "$python"`" -c "$pyv" 1>/dev/null \
	    2>/dev/null || err "Cannot detect host Python version." \
	    xbmk_set_pyver "$@"
	[ -n "$pyver" ] && \
	    pyver="$("$(pybin "$python")" -c "$pyv" | awk '{print $1}')" && \
	    pyver="${pyver#(}" && pyver="${pyver%,}"
	[ "${pyver%%.*}" = "3" ] || err "Bad python version (must by 3.x)" \
	    xbmk_set_pyver "$@"

	(
	# set up python v3.x in PATH, in case it's not set up correctly.
	# see code above that detected the correct python3 command.
	x_ cd "$xbtmp/xbmkpath"
	x_ ln -s "`x_ pybin "$python"`" python || err "can't make symlink" \
	    xbmk_set_pyver "$@"
	) || err "Can't set up python symlink in $xbtmp/xbmkpath" \
	    xbmk_set_pyver "$@"; :
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

xbmk_git_init()
{
	for gitarg in "--global user.name" "--global user.email"; do
		gitcmd="git config $gitarg"; $gitcmd 1>/dev/null 2>/dev/null \
		    || err "Run this first: $gitcmd \"your ${gitcmd##*.}\"" \
		    "xbmk_git_init" "$@"
	done

	[ -L ".git" ] && err "'$xbmkpwd/.git' is a symlink" \
	    "xbmk_git_init" "$@"
	[ -e ".git" ] && return 0

	x_ date --version | grep "GNU coreutils" 1>/dev/null 2>/dev/null || \
	    err "Non-GNU date implementation; current use relies on GNU date" \
	    "xbmk_git_init" "$@"

	eval "$(setvars "$(x_ date -Rud @$versiondate)" cdate _nogit || err)" \
	    || err "can't get date" "xbmk_git_init" "$@"

	x_ git init 1>/dev/null 2>/dev/null
	x_ git add -A . 1>/dev/null 2>/dev/null
	x_ git commit -m "$projectname $version" --date "$cdate" \
	    --author="xbmk <xbmk@example.com>" 1>/dev/null 2>/dev/null
	x_ git tag -a "$version" -m "$projectname $version" 1>/dev/null \
	    2>/dev/null; :
}

xbmk_child_exec()
{
	xbmk_rval=0
	( x_ ./mk "$@" ) || xbmk_rval=1
	( x_ rm -Rf "$xbtmp" ) || xbmk_rval=1
	( x_ rm -f "$xbmklock" ) || xbmk_rval=1
	exit $xbmk_rval
}

xbmk_init "$@"
