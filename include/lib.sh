# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>

export LC_COLLATE=C
export LC_ALL=C

projectname="libreboot"
projectsite="https://libreboot.org/"

[ -z "${PATH+x}" ] && \
    export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/games:/usr/games"
xbmkpath="$PATH"

_ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0"

ifdtool="elf/ifdtool/default/ifdtool"
cbfstool="elf/cbfstool/default/cbfstool"
rmodtool="elf/cbfstool/default/rmodtool"
grubdata="config/data/grub"
err="err_"

err_()
{
	printf "ERROR %s: %s\n" "$0" "$1" 1>&2
	exit 1
}
x_() {
	[ $# -lt 1 ] || eval "$@" || $err "Unhandled error on: $(echo "$@")"; :
}

xbmkpwd="`pwd`" || $err "Cannot generate PWD"
export PWD="$xbmkpwd"

setvars()
{
	_setvars="" && [ $# -lt 2 ] && $err "setvars: too few arguments"
	val="$1" && shift 1 && for var in "$@"; do
		_setvars="$var=\"$val\"; $_setvars"
	done
	printf "%s\n" "${_setvars% }"
}
chkvars()
{
	for var in "$@"; do
		eval "[ -n \"\${$var+x}\" ] || \$err \"$var unset\""
		eval "[ -n \"\$$var\" ] || \$err \"$var unset\""
	done; :
}

eval "`setvars "" _nogit board reinstall versiondate aur_notice configdir \
    datadir version relname`"

for fv in version versiondate; do
	eval "[ ! -f \".$fv\" ] || read -r $fv < \".$fv\" || :"
done

setcfg()
{
	[ $# -gt 1 ] && printf "e \"%s\" f missing && return %s;\n" "$1" "$2"
	[ $# -gt 1 ] || \
		printf "e \"%s\" f not && %s \"Missing config\";\n" "$1" "$err"
	printf ". \"%s\" || %s \"Could not read config\";\n" "$1" "$err"
}

e()
{
	es_t="e" && [ $# -gt 1 ] && es_t="$2"
	es2="already exists"
	estr="[ -$es_t \"\$1\" ] || return 1"
	[ $# -gt 2 ] && estr="[ -$es_t \"\$1\" ] && return 1" && es2="missing"

	eval "$estr"
	printf "%s %s\n" "$1" "$es2" 1>&2
}

install_packages()
{
	[ $# -lt 2 ] && $err "fewer than two arguments"
	[ $# -gt 2 ] && reinstall="$3"

	eval "`setcfg "config/dependencies/$2"`"

	chkvars pkg_add pkglist
	$pkg_add $pkglist || $err "Cannot install packages"

	[ -n "$aur_notice" ] && \
	    printf "You need AUR packages: %s\n" "$aur_notice" 1>&2; :
}
if [ $# -gt 0 ] && [ "$1" = "dependencies" ]; then
	install_packages "$@" || exit 1
	exit 0
fi

pyver="2"
python="python3"
command -v python3 1>/dev/null || python="python"
command -v $python 1>/dev/null || pyver=""
[ -z "$pyver" ] || \
	python -c 'import sys; print(sys.version_info[:])' 1>/dev/null \
	    2>/dev/null || $err "Cannot determine which Python version."
[ -n "$pyver" ] && \
	pyver="`python -c 'import sys; print(sys.version_info[:])' | \
	    awk '{print $1}'`" && \
	pyver="${pyver#(}" && pyver="${pyver%,}"
if [ "${pyver%%.*}" != "3" ]; then
	printf "Wrong python version, or python missing. Must be v 3.x.\n" 1>&2
	exit 1
fi

id -u 1>/dev/null 2>/dev/null || $err "suid check failed (id -u)"
[ "$(id -u)" != "0" ] || $err "this command as root is not permitted"

# XBMK_CACHE is a directory, for caching downloads and git repositories
[ -z "${XBMK_CACHE+x}" ] && export XBMK_CACHE="$xbmkpwd/cache"
[ -z "$XBMK_CACHE" ] && export XBMK_CACHE="$xbmkpwd/cache"
[ -L "$XBMK_CACHE" ] && [ "$XBMK_CACHE" = "$xbmkpwd/cache" ] && \
    $err "cachedir is default, $xbmkpwd/cache, but it exists and is a symlink"
[ -L "$XBMK_CACHE" ] && export XBMK_CACHE="$xbmkpwd/cache"
[ -f "$XBMK_CACHE" ] && $err "cachedir '$XBMK_CACHE' exists but it's a file"

# unify all temporary files/directories in a single TMPDIR
[ -z "${TMPDIR+x}" ] || [ "${TMPDIR%_*}" = "/tmp/xbmk" ] || unset TMPDIR
[ -n "${TMPDIR+x}" ] && export TMPDIR="$TMPDIR"
if [ -z "${TMPDIR+x}" ]; then
	[ -f "lock" ] && $err "$xbmkpwd/lock exists. Is a build running?"
	export TMPDIR="/tmp"
	export TMPDIR="$(mktemp -d -t xbmk_XXXXXXXX)"
	touch lock || $err "cannot create 'lock' file"
	x_ rm -Rf "$XBMK_CACHE/xbmkpath" "$XBMK_CACHE/gnupath"
	x_ mkdir -p "$XBMK_CACHE/gnupath" "$XBMK_CACHE/xbmkpath"
	export PATH="$XBMK_CACHE/xbmkpath:$XBMK_CACHE/gnupath:$PATH"
	(
	# set up python v3.x in PATH, in case it's not set up correctly.
	# see code above that detected the correct python3 command.
	cd "$XBMK_CACHE/xbmkpath" || $err "can't cd $XBMK_CACHE/xbmkpath"
	x_ ln -s "`command -v "$python"`" python
	) || $err "Can't set up python symlink in $XBMK_CACHE/xbmkpath"

	xbmk_rval=0
	./mk "$@" || xbmk_rval=1
	rm -Rf "$TMPDIR" || xbmk_rval=1
	rm -f lock || xbmk_rval=1
	exit $xbmk_rval
fi

# if "y": a coreboot target won't be built if target.cfg says release="n"
# (this is used to exclude certain build targets from releases)
[ -z "${XBMK_RELEASE+x}" ] && export XBMK_RELEASE="n"
[ "$XBMK_RELEASE" = "y" ] || export XBMK_RELEASE="n"

[ -z "${XBMK_THREADS+x}" ] && export XBMK_THREADS=1
expr "X$XBMK_THREADS" : "X-\{0,1\}[0123456789][0123456789]*$" \
    1>/dev/null 2>/dev/null || export XBMK_THREADS=1 # user gave a non-integer

[ -e ".git" ] || [ -f ".version" ] || printf "unknown\n" > .version || \
    $err "Cannot generate unknown .version file"
[ -e ".git" ] || [ -f ".versiondate" ] || printf "1716415872\n" > \
    .versiondate || $err "Cannot generate unknown .versiondate file"

version_="$version"
[ ! -e ".git" ] || version="$(git describe --tags HEAD 2>&1)" || \
    version="git-$(git rev-parse HEAD 2>&1)" || version="$version_"
versiondate_="$versiondate"
[ ! -e ".git" ] || versiondate="$(git show --no-patch --no-notes \
    --pretty='%ct' HEAD)" || versiondate="$versiondate_"
for p in version versiondate; do
	chkvars "$p"
	eval "x_ printf \"%s\\n\" \"\$$p\" > .$p"
done
relname="$projectname-$version"
export LOCALVERSION="-$projectname-${version%%-*}"

check_defconfig()
{
	[ -d "$1" ] || $err "Target '$1' not defined."
	for x in "$1"/config/*; do
		[ -f "$x" ] && printf "%s\n" "$x" && return 1
	done; :
}

remkdir()
{
	rm -Rf "$1" || $err "remkdir: !rm -Rf \"$1\""
	mkdir -p "$1" || $err "remkdir: !mkdir -p \"$1\""
}

mkrom_tarball()
{
	printf "%s\n" "$version" > "$1/.version" || $err "$1 !version"
	printf "%s\n" "$versiondate" > "$1/.versiondate" || $err "$1 !vdate"

	mktarball "$1" "${1%/*}/${relname}_${1##*/}.tar.xz"
	x_ rm -Rf "$1"
}

mktarball()
{
	if [ "${2%/*}" != "$2" ]; then
		mkdir -p "${2%/*}" || $err "mk, !mkdir -p \"${2%/*}\""
	fi
	tar -c "$1" | xz -T$XBMK_THREADS -9e > "$2" || $err "mktarball 2, $1"
}

mksha512sum()
{
	(
	[ "${1%/*}" != "$1" ] && x_ cd "${1%/*}"
	sha512sum ./"${1##*/}" >> "$2" || $err "!sha512sum \"$1\" > \"$2\""
	) || $err "failed to create tarball checksum"
}

rmgit()
{
	(
	cd "$1" || $err "!cd gitrepo $1"
	find . -name ".git" -exec rm -Rf {} + || $err "!rm .git $1"
	find . -name ".gitmodules" -exec rm -Rf {} + || $err "!rm .gitmod $1"
	) || $err "Cannot remove .git/.gitmodules in $1"
}

# return 0 if project is single-tree, otherwise 1
# e.g. coreboot is multi-tree, so 1
singletree()
{
	for targetfile in "config/${1}/"*/target.cfg; do
		[ -e "$targetfile" ] && [ -f "$targetfile" ] && return 1; :
	done; :
}

# can grab from the internet, or copy locally.
# if copying locally, it can only copy a file.
download()
{
	_dlop="curl" && [ $# -gt 4 ] && _dlop="$5"
	cached="$XBMK_CACHE/file/$4"
	dl_fail="n" # 1 url, 2 url backup, 3 destination, 4 checksum
	vendor_checksum "$4" "$cached" 2>/dev/null && dl_fail="y"
	[ "$dl_fail" = "n" ] && e "$3" f && return 0
	x_ mkdir -p "${3%/*}" "$XBMK_CACHE/file"
	for url in "$1" "$2"; do
		[ "$dl_fail" = "n" ] && break
		[ -z "$url" ] && continue
		rm -f "$cached" || $err "!rm -f '$cached'"
		if [ "$_dlop" = "curl" ]; then
			curl --location --retry 3 -A "$_ua" "$url" \
			    -o "$cached" || wget --tries 3 -U "$_ua" "$url" \
			    -O "$cached" || continue
		elif [ "$_dlop" = "copy" ]; then
			[ -L "$url" ] && \
				printf "dl %s %s %s %s: '%s' is a symlink\n" \
				    "$1" "$2" "$3" "$4" "$url" 1>&2 && continue
			[ ! -f "$url" ] && \
				printf "dl %s %s %s %s: '%s' not a file\n" \
				    "$1" "$2" "$3" "$4" "$url" 1>&2 && continue
			cp "$url" "$cached" || continue
		else
			$err "$1 $2 $3 $4: Unsupported dlop type: '$_dlop'"
		fi
		vendor_checksum "$4" "$cached" || dl_fail="n"
	done
	[ "$dl_fail" = "y" ] && $err "$1 $2 $3 $4: not downloaded"
	[ "$cached" = "$3" ] || x_ cp "$cached" "$3"; :
}

vendor_checksum()
{
	[ "$(sha512sum "$2" | awk '{print $1}')" != "$1" ] || return 1
	printf "Bad checksum for file: %s\n" "$2" 1>&2; rm -f "$2" || :; :
}

cbfs()
{
	fRom="$1" # image to operate on
	fAdd="$2" # file to add
	fName="$3" # filename when added in CBFS

	ccmd="add-payload" && [ $# -gt 3 ] && [ $# -lt 5 ] && ccmd="add"
	lzma="-c lzma" && [ $# -gt 3 ] && [ $# -lt 5 ] && lzma="-t $4"

	# hack. TODO: do it better. this whole function is cursed
	if [ $# -gt 4 ]; then
		# add flat binary for U-Boot (u-boot.bin) on x86
		if [ "$5" = "0x1110000" ]; then
			ccmd="add-flat-binary"
			lzma="-c lzma -l 0x1110000 -e 0x1110000"
		fi
	fi

	x_ "$cbfstool" "$fRom" $ccmd -f "$fAdd" -n "$fName" $lzma
}

mk()
{
	mk_flag="$1" || $err "No argument given"
	shift 1 && for mk_arg in "$@"; do
		./mk $mk_flag $mk_arg || $err "./mk $mk_flag $mk_arg"; :
	done; :
}
