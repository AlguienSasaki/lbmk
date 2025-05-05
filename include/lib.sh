# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2025 Alper Nebi Yasak <alpernebiyasak@gmail.com>

cbfstool="elf/cbfstool/default/cbfstool"
rmodtool="elf/cbfstool/default/rmodtool"

remkdir()
{
	x_ rm -Rf "$@"
	x_ mkdir -p "$@"
}

mkrom_tarball()
{
	printf "%s\n" "$version" > "$1/.version" || err "$1 !version"
	printf "%s\n" "$versiondate" > "$1/.versiondate" || err "$1 !vdate"

	mktarball "$1" "${1%/*}/${relname}_${1##*/}.tar.xz"
	x_ rm -Rf "$1"
}

mktarball()
{
	printf "Creating tar archive '%s' from directory '%s'\n" "$2" "$1"
	[ "${2%/*}" = "$2" ] || x_ mkdir -p "${2%/*}"
	x_ tar -c "$1" | xz -T$XBMK_THREADS -9e > "$2" || err "mktarball2, $1"
}

mksha512sum()
{
	(
	[ "${1%/*}" != "$1" ] && x_ cd "${1%/*}"
	sha512sum ./"${1##*/}" >> "$2" || err "!sha512sum \"$1\" > \"$2\""
	) || err "failed to create tarball checksum"
}

rmgit()
{
	x_ find "$1" -name ".git" -exec rm -Rf {} +
	x_ find "$1" -name ".gitmodules" -exec rm -Rf {} +
}

# can grab from the internet, or copy locally.
# if copying locally, it can only copy a file.
xbmkget()
{
	_ua="Mozilla/5.0 (Windows NT 10.0; rv:91.0) Gecko/20100101 Firefox/91.0"

	_dlop="curl" && [ $# -gt 4 ] && _dlop="$5"
	cached="$XBMK_CACHE/file/$4"
	dl_fail="n" # 1 url, 2 url backup, 3 destination, 4 checksum
	bad_checksum "$4" "$cached" 2>/dev/null && dl_fail="y"
	[ "$dl_fail" = "n" ] && e "$3" f && return 0
	x_ mkdir -p "${3%/*}" "$XBMK_CACHE/file"
	for url in "$1" "$2"; do
		[ "$dl_fail" = "n" ] && break
		[ -z "$url" ] && continue
		rm -f "$cached" || err "!rm -f '$cached'"
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
			err "$1 $2 $3 $4: Unsupported dlop type: '$_dlop'"
		fi
		bad_checksum "$4" "$cached" || dl_fail="n"
	done
	[ "$dl_fail" = "y" ] && err "$1 $2 $3 $4: not downloaded"
	[ "$cached" = "$3" ] || x_ cp "$cached" "$3"; :
}

bad_checksum()
{
	[ "$(sha512sum "$2" | awk '{print $1}')" != "$1" ] || return 1
	printf "Bad checksum for file: %s\n" "$2" 1>&2; rm -f "$2" || :; :
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

mk()
{
	mk_flag="$1" || err "No argument given"
	shift 1 && for mk_arg in "$@"; do
		x_ ./mk $mk_flag $mk_arg
	done; :
}

check_defconfig()
{
	[ -d "$1" ] || err "Target '$1' not defined."
	for x in "$1"/config/*; do
		[ -f "$x" ] && printf "%s\n" "$x" && return 1
	done; :
}

setvars()
{
	_setvars=""
	if [ $# -lt 2 ]; then
		printf "err \"setvars: too few args\\n\""
		return 0
	fi
	val="$1"
	shift 1
	for var in "$@"; do
		_setvars="$var=\"$val\"; $_setvars"
	done
	printf "%s\n" "${_setvars% }"
}

setcfg()
{
	[ $# -gt 1 ] && printf "e \"%s\" f missing && return %s;\n" "$1" "$2"
	[ $# -gt 1 ] || \
		printf "e \"%s\" f not && err \"Missing config\";\n" "$1"
	printf ". \"%s\" || err \"Could not read config\";\n" "$1"
}

chkvars()
{
	for var in "$@"; do
		eval "[ -n \"\${$var+x}\" ] || err \"$var unset\""
		eval "[ -n \"\$$var\" ] || err \"$var unset\""
	done; :
}

fe_()
{
	find_ex "x_" "$@"
}

fx_()
{
	find_ex "" "$@"
}

find_ex()
{
	xmsg="$1" && shift 1
	fd="`mktemp`" && x_ rm -f "$fd" && x_ touch "$fd"
	xx="$1" && shift 1
	$xmsg find "$@" 2>/dev/null | sort > "$fd" || \
	    err "!find $(echo "$@") > \"$fd\""
	while read -r fx; do
		$xx "$fx" || break; :
	done < "$fd"
	x_ rm -f "$fd"
}

x_()
{
	[ $# -lt 1 ] && printf "WARNING: x_ called without arguments\n" 1>&2
	[ $# -lt 1 ] || [ -n "$1" ] || err "Empty first arg: x_ $(echo "$@")"
	[ $# -lt 1 ] || "$@" || err "Unhandled error for: $(echo "$@")"; :
}

err()
{
	set -u -e

	xbmk_err_val=0
	real_err="" && [ -n "${xbmk_err+x}" ] && real_err="$xbmk_err"

	if [ -z "$real_err" ]; then
		printf "WARNING: err not set. Defaulting to 'err_'\n" 1>&2
		real_err="err_"
	fi

	(
	$real_err "$@" || err_ "Error function '$real_err' *returned* 1"
	err_ "Error function '$real_err' didn't exit"
	exit 1 # just in case!
	) || xbmk_err_val=1 # otherwise, it wrongly did exit 0, not exit 1

	[ $xbmk_err_val -eq 0 ] && err_ "Error function '$real_err' did exit 0"
	exit 1 # just in case!
}

err_()
{
	[ $# -lt 1 ] && printf "ERROR (but no error message provided)\n" 1>&2
	[ $# -lt 1 ] || printf "ERROR %s: %s\n" "$0" "$1" 1>&2 || :
	exit 1
}
