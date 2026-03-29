# SPDX-License-Identifier: GPL-3.0-only

# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2025 Alper Nebi Yasak <alpernebiyasak@gmail.com>

export LANG=C.UTF-8
export LC_COLLATE=C.UTF-8
export LC_ALL=C.UTF-8

projectname="libreboot"
projectsite="https://libreboot.org/"

if [ -z "${PATH+x}" ]; then
	export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/games:/usr/games"
fi

sha512sum="util/sbase/sha512sum"

aur_notice=""
basetmp=""
board=""
checkvarschk=""
checkvarsxbmk=""
configdir=""
datadir=""
is_child=""
python=""
pyver=""
reinstall=""
relname=""
version=""
versiondate=""
xbmklock=""
xbmkpath=""
xbmkpwd=""
xbmkpwd=""
xbtmp=""

xbmk_init()
{
	xbmkpwd="`pwd || err "Cannot generate PWD"`" || err "!" xbmk_init "$@"
	xbmklock="$xbmkpwd/lock"
	basetmp="$xbmkpwd/xbmkwd"
	sha512sum="$xbmkpwd/util/sbase/sha512sum"

	if [ $# -gt 0 ] && [ "$1" = "dependencies" ]; then
		x_ xbmkpkg "$@"

		exit 0
	fi

	id -u 1>/dev/null 2>/dev/null || \
	    err "suid check failed" "xbmk_init" "$@"

	if [ "$(id -u)" = "0" ]; then
		err "this command as root is not permitted" "xbmk_init" "$@"
	fi

	export PWD="$xbmkpwd"
	x_ xbmkdir "$basetmp"

	if [ ! -e "cache" ]; then
		x_ xbmkdir "cache"
	fi

	for init_cmd in get_version set_env set_threads git_init child_exec; do
		if ! xbmk_$init_cmd "$@"; then
			break
		fi
	done
}

xbmkpkg()
{
	xchk xbmkpkg "$@"

	if [ $# -gt 2 ]; then
		reinstall="$3"
	fi

	. "config/dependencies/$2" || \
	    err "Can't read 'config/dependencies/$2'" "xbmkpkg" "$@"

	if [ -z "$pkg_add" ] || [ -z "$pkglist" ]; then
		err "pkg_add/pkglist not both set" "xbmkpkg" "$@"
	fi

	x_ $pkg_add $pkglist

	if [ -n "$aur_notice" ]; then
		printf "You need AUR packages: %s\n" "$aur_notice" 1>&2
	fi
}

xbmk_get_version()
{
	if [ -f ".version" ]; then
		read -r version < ".version" || \
		    err "can't read version file" "xbmk_get_version" "$@"
	fi
	if [ -f ".versiondate" ]; then
		read -r versiondate < ".versiondate" || \
		    err "can't read versiondate" xbmk_get_version "$@"
	fi

	if [ -f ".version" ] && [ -z "$version" ]; then
		err "version not set" "xbmk_get_version" "$@"
	fi
	if [ -f ".versiondate" ] && [ -z "$versiondate" ]; then
		err "versiondate not set" "xbmk_get_version" "$@"
	fi

	if [ ! -e ".git" ] && [ ! -f ".version" ]; then
		version="unknown"
	fi
	if [ ! -e ".git" ] && [ ! -f ".versiondate" ]; then
		versiondate="1716415872"
	fi

	xbmk_sanitize_version

	if [ -n "$version" ]; then
		relname="$projectname-$version"
	fi
}

# a parent instance will cause this function to return 0.
# a child instance will return 1, skipping further initialisation
# after this function is called.
xbmk_set_env()
{
	is_child="n"

	xbmkpath="$PATH"

	# unify all temporary files/directories in a single TMPDIR
	if [ -n "${TMPDIR+x}" ] && [ "${TMPDIR%_*}" != "$basetmp/xbmk" ]; then
		unset TMPDIR
	fi
	if [ -n "${TMPDIR+x}" ]; then
		export TMPDIR="$TMPDIR"
		xbtmp="$TMPDIR"
	fi
	if [ -n "${TMPDIR+x}" ]; then
		is_child="y"
	fi

	if [ "$is_child" = "y" ]
	then
		# child instance of xbmk, so we stop init after this point
		# and execute the given user command upon return:

		xbmk_child_set_env

		return 1
	else
		# parent instance of xbmk, so we continue initialising.
		# a parent instance of xbmk never processes its own
		# command directly; instead, it calls a child instance
		# of xbmk, and exits with the corresponding return status.
		
		xbmk_parent_set_env

		return 0
	fi
}

xbmk_child_set_env()
{
	xbmk_child_set_tmp

	if [ -z "${XBMK_CACHE+x}" ]; then
		err "XBMK_CACHE unset on child" "xbmk_set_env" "$@"
	fi
	if [ -z "${XBMK_THREADS+x}" ]; then
		xbmk_set_threads; :
	fi
	if [ -z "${XBMK_CACHE_MIRROR+x}" ]; then
		xbmk_set_mirror
	fi
}

xbmk_child_set_tmp()
{
	badtmp=""
	locktmp=""
	xbtmpchk=""

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
	    err "cannot create '$xbmklock'" xbmk_set_env "$@"; :

	# not really critical for security, but it's a barrier
	# against the user to make them think twice before deleting it
	# in case an actual instance of xbmk is already running:

	x_ chmod -w "$xbmklock"

	xbmk_parent_set_export
	xbmk_set_version

	remkdir "$xbtmp" "$xbtmp/gnupath" "$xbtmp/xbmkpath"

	xbmk_set_pyver
	xbmk_set_mirror
}

xbmk_parent_check_tmp()
{
	export TMPDIR="$basetmp"

	xbmklist="`mktemp || err "can't make tmplist"`" || \
	    err "can't make tmplist" xbmk_parent_check_tmp "$@"

	x_ rm -f "$xbmklist"
	x_ touch "$xbmklist"

	for xtmpdir in "$basetmp"/xbmk_*; do
		if [ -e "$xtmpdir" ]; then
			printf "%s\n" "$xtmpdir" >> "$xbmklist" || \
			    err "can't write '$xtmpdir' to '$xbmklist'" \
			    "xbmk_parent_check_tmp" "$@"; :
		fi
	done

	# set up a unified temporary directory, for common deletion later:
	export TMPDIR="`x_ mktemp -d -t xbmk_XXXXXXXX`" || \
	    err "can't export TMPDIR" "xbmk_parent_check_tmp" "$@"
	xbtmp="$TMPDIR"

	while read -r xtmpdir; do
		if [ "$xtmpdir" = "$xbtmp" ]; then
			err "pre-existing '$xbtmp'" "xbmk_parent_check_tmp" "$@"
		fi
	done < "$xbmklist" || \
	    err "Can't read xbmklist: '$xbmklist'" "xbmk_parent_check_tmp" "$@"

	x_ rm -f "$xbmklist"
}

xbmk_parent_set_export()
{
	export XBMK_CACHE="$xbmkpwd/cache"

	if [ -e "$XBMK_CACHE" ] && [ ! -d "$XBMK_CACHE" ]; then
		err "cachedir '$XBMK_CACHE' is a file" \
		    "xbmk_parent_set_export" "$@"
	fi

	export PATH="$xbtmp/xbmkpath:$xbtmp/gnupath:$PATH"
	xbmkpath="$PATH"

	# if "y": a coreboot target won't be built if target.cfg says release=n
	# (this is used to exclude certain build targets from releases)

	if [ -z "${XBMK_RELEASE+x}" ]; then
		export XBMK_RELEASE="n"
	fi
	if [ "$XBMK_RELEASE" = "Y" ]; then
		export XBMK_RELEASE="y"
	fi
	if [ "$XBMK_RELEASE" != "y" ]; then
		export XBMK_RELEASE="n"
	fi
}

xbmk_set_threads()
{
	if [ -z "${XBMK_THREADS+x}" ]; then
		export XBMK_THREADS=1
	fi
	if ! expr "X$XBMK_THREADS" : "X-\{0,1\}[0123456789][0123456789]*$" \
	    1>/dev/null 2>/dev/null; then
		export XBMK_THREADS=1
	fi
}

xbmk_set_version()
{
	version_="$version"
	if [ -e ".git" ]; then
		version="$(git describe --tags HEAD 2>&1)" || \
		    version="git-$(git rev-parse HEAD 2>&1)" || \
		    version="$version_"
	fi

	versiondate_="$versiondate"
	if [ -e ".git" ]; then
		versiondate="$(git show --no-patch --no-notes \
		    --pretty='%ct' HEAD)" || versiondate="$versiondate_"
	fi

	if [ -z "$version" ] || [ -z "$versiondate" ]; then
		err "version and/or versiondate unset" "xbmk_set_version" "$@"
	fi

	update_xbmkver "."

	relname="$projectname-$version"
	export LOCALVERSION="-$projectname-${version%%-*}"
}

xbmk_set_pyver()
{
	python="python3"
	pyver="2"
	pyv="import sys; print(sys.version_info[:])"

	if ! pybin python3 1>/dev/null; then
		python="python"
	fi
	if [ "$python" = "python3" ]; then
		pyver="3"
	fi
	if ! pybin "$python" 1>/dev/null; then
		pyver=""
	fi
	if [ -n "$pyver" ]; then
		"`x_ pybin "$python"`" -c "$pyv" 1>/dev/null \
		    2>/dev/null || \
		    err "Can't detect Python version." "xbmk_set_pyver" "$@"
	fi
	if [ -n "$pyver" ]; then
		pyver="$("$(pybin "$python")" -c "$pyv" | awk '{print $1}')"
		pyver="${pyver#(}"
		pyver="${pyver%,}"
	fi
	if [ "${pyver%%.*}" != "3" ]; then
		err "Bad python version (must by 3.x)" "xbmk_set_pyver" "$@"
	fi

	# set up python in PATH (environmental variable):

	(
		x_ cd "$xbtmp/xbmkpath"

		x_ ln -s "`x_ pybin "$python"`" python || \
		    err "can't make symlink" "xbmk_set_pyver" "$@"

	) || \
	    err "Can't link Python in $xbtmp/xbmkpath" "xbmk_set_pyver" "$@"; :
}

# Use direct path, to prevent a hang if Python is using a virtual environment,
# not command -v, to prevent a hang when checking python's version
# See: https://docs.python.org/3/library/venv.html#how-venvs-work
pybin()
{
	py="import sys; quit(1) if sys.prefix == sys.base_prefix else quit(0)"

	venv=1
	if ! command -v "$1" 1>/dev/null 2>/dev/null; then
		venv=0
	fi
	if [ $venv -gt 0 ]; then
		if ! "$1" -c "$py" 1>/dev/null 2>/dev/null; then
			venv=0
		fi
	fi

	# ideally, don't rely on PATH or hardcoded paths if python venv.
	# use the *real*, direct executable linked to by the venv symlink:

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
	if ! command -v "$1" 2>/dev/null; then
		return 1
	fi
}

xbmk_set_mirror()
{
	# defines whether cache/clone/ (regular clones)
	# or cache/mirror (--mirror clones) are used, per project

	# to use cache/mirror/ do: export XBMK_CACHE_MIRROR="y"
	# mirror/ stores a separate directory per repository, even per backup.
	# it's slower, and uses more disk space, and some upstreams might not
	# appreciate it, so it should only be used for development or archival

	if [ -z "${XBMK_CACHE_MIRROR+x}" ]; then
		export XBMK_CACHE_MIRROR="n"
	fi
	if [ "$XBMK_CACHE_MIRROR" != "y" ]; then
		export XBMK_CACHE_MIRROR="n"
	fi
}

xbmk_git_init()
{
	for gitarg in "--global user.name" "--global user.email"; do
		gitcmd="git config $gitarg"
		if ! $gitcmd 1>/dev/null 2>/dev/null; then
			err "Run this first: $gitcmd \"your ${gitcmd##*.}\"" \
			    "xbmk_git_init" "$@"
		fi
	done

	if [ -L ".git" ]; then
		err "'$xbmkpwd/.git' is a symlink" "xbmk_git_init" "$@"
	fi
	if [ -e ".git" ]; then
		return 0
	fi

	# GNU-specific extensions of date are used.
	# TODO: that is a bug. fix it!

	x_ date --version | grep "GNU coreutils" 1>/dev/null 2>/dev/null || \
	    err "Non-GNU date implementation" "xbmk_git_init" "$@"

	cdate="`x_ date -Rud @$versiondate || err "can't get date"`" || \
	    err "can't get date" "xbmk_git_init" "$@"

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
