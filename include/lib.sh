# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2025 Alper Nebi Yasak <alpernebiyasak@gmail.com>

cbfstool="elf/coreboot/default/cbfstool"
rmodtool="elf/coreboot/default/rmodtool"

remkdir()
{
	x_ rm -Rf "$@"
	x_ mkdir -p "$@"
}

mkrom_tarball()
{
	update_xbmkver "$1"
	mktarball "$1" "${1%/*}/${relname}_${1##*/}.tar.xz"
	x_ rm -Rf "$1"
}

update_xbmkver()
{
	xbmk_sanitize_version

	printf "%s\n" "$version" > "$1/.version" || \
	    err "can't write '$1'" "update_xbmkver" "$@"; :
	printf "%s\n" "$versiondate" > "$1/.versiondate" || \
	    err "can't write '$versiondate'" "update_xbmkver" "$@"; :
}

xbmk_sanitize_version()
{
	[ -n "$version" ] || return 0; :

	version="`printf "%s\n" "$version" | sed -e 's/\t//g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\ //g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\.\.//g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\.\///g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\//-/g'`"
	version="${version#-}"

	[ -n "$version" ] || err "'version' empty after sanitization" \
	    "xbmk_sanitize_version" "$@"; :
}

mktarball()
{
	printf "Creating tar archive '%s' from directory '%s'\n" "$2" "$1"
	[ "${2%/*}" = "$2" ] || x_ mkdir -p "${2%/*}"
	x_ tar -c "$1" | xz -T$XBMK_THREADS -9e > "$2" || \
	    err "can't make tarball '$1'" "mktarball" "$@"
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

setvars()
{
	_setvars=""
	if [ $# -lt 2 ]; then
		printf "err \"setvars: too few args\\n\""
		return 0
	fi
	val="$1"
	shift 1
	while [ $# -gt 0 ]; do
		printf "%s=\"%s\"\n" "$1" "$val"
		shift 1
	done
}

setcfg()
{
	[ $# -gt 1 ] && printf "e \"%s\" f missing && return %s;\n" "$1" "$2"
	[ $# -gt 1 ] || \
		printf "e \"%s\" f not && err \"Missing config '$1'\";\n" "$1"
	printf ". \"%s\" || err \"Could not read config '$1'\";\n" "$1"
}

chkvars()
{
	while [ $# -gt 0 ]; do
		eval "[ -n \"\${$1+x}\" ] || err \"$1 unset\""
		eval "[ -n \"\$$1\" ] || err \"$1 unset\"; shift 1; :"
	done; :
}

# return 0 if project is single-tree, otherwise 1
# e.g. coreboot is multi-tree, so 1
singletree()
{
	( fx_ "eval exit 1 && err" find "config/$1/"*/ -type f \
	    -name "target.cfg" ) || return 1; :
}

findpath()
{
	[ $# -gt 0 ] || err "findpath: No arguments provided" "findpath" "$@"
	while [ $# -gt 0 ]; do
		found="`readlink -f "$1" 2>/dev/null`" || return 1; :
		[ -n "$found" ] || found="`realpath "$1" 2>/dev/null`" || \
		    return 1; :
		printf "%s\n" "$found"
		shift 1
	done
}

pad_one_byte()
{
	paddedfile="`mktemp || err "mktemp pad_one_byte"`" || \
	    err "can't make tmp file" "pad_one_byte" "$@"
	x_ cat "$1" config/data/coreboot/0 > "$paddedfile" || \
	    err "could not pad file '$paddedfile'" "pad_one_byte" "$1"; :
	x_ mv "$paddedfile" "$1"
}

unpad_one_byte()
{
	xromsize="$(expr $(stat -c '%s' "$1") - 1)" || \
	    err "can't increment file size" "unpad_one_byte" "$@"
	[ $xromsize -lt 524288 ] && \
	    err "too small, $xromsize: $1" "unpad_one_byte" "$@"

	unpaddedfile="`mktemp || err "mktemp unpad_one_byte"`" || \
	    err "can't make tmp file" "unpad_one_byte" "$@"
	x_ dd if="$1" of="$unpaddedfile" bs=$xromsize count=1
	x_ mv "$unpaddedfile" "$1"
}

fx_()
{
	xchk fx_ "$@"
	fd="`mktemp || err "can't create tmpfile"`" || \
	    err "can't make tmpfile" "fx_" "$@"
	x_ rm -f "$fd" && x_ touch "$fd"
	xx="$1" && shift 1
	"$@" 2>/dev/null | sort 1>"$fd" 2>/dev/null || \
	    err "can't sort to '$fd'" "fx_" "$xx" "$@"
	dx_ "$xx" "$fd" || :
	x_ rm -f "$fd"
}

dx_()
{
	xchk dx_ "$@"
	[ ! -f "$2" ] || while read -r fx; do
		$1 "$fx" || return 1; :
	done < "$2" || err "cannot read '$2'" "dx_" "$@"; :
}

x_()
{
	[ $# -lt 1 ] || [ -n "$1" ] || err "Empty first arg" "x_" "$@"
	[ $# -lt 1 ] || "$@" || err "Unhandled error" "x_" "$@"
}

xchk()
{
	[ $# -lt 3 ] && err "$1 needs at least two arguments" "xchk" "$@"
	if [ -z "$2" ] || [ -z "$3" ]; then
		err "arguments must not be empty" "xchk" "$@"
	fi
}

err()
{
	if [ $# -eq 1 ]; then
		printf "ERROR %s: %s\n" "$0" "$1" 1>&2 || :
	elif [ $# -gt 1 ]; then
		printf "ERROR %s: %s: in command/function with arguments: " \
		    "$0" "$1" 1>&2
		shift 1
		xprintf "$@" 1>&2
	else
		printf "ERROR, but no arguments provided to err\n" 1>&2
	fi
	exit 1
}

xprintf()
{
	xprintfargs=0
	while [ $# -gt 0 ]; do
		printf "\"%s\"" "$1"
		xprintfargs=1
		[ $# -gt 1 ] && printf " "; :
		shift 1
	done
	[ $xprintfargs -gt 0 ] && printf "\n"; :
}
