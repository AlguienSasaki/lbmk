# SPDX-License-Identifier: GPL-3.0-only

# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2025 Alper Nebi Yasak <alpernebiyasak@gmail.com>

cbfstool="elf/coreboot/default/cbfstool"
rmodtool="elf/coreboot/default/rmodtool"

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
	if [ -z "$version" ]; then
		return 0
	fi

	version="`printf "%s\n" "$version" | sed -e 's/\t//g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\ //g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\.\.//g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\.\///g'`"
	version="`printf "%s\n" "$version" | sed -e 's/\//-/g'`"

	version="${version#-}"

	if [ -z "$version" ]; then
		err "'version' empty after sanitization" \
		    "xbmk_sanitize_version" "$@"
	fi
}

mktarball()
{
	printf "Creating tar archive '%s' from directory '%s'\n" "$2" "$1"

	if [ "${2%/*}" != "$2" ]; then
		x_ xbmkdir "${2%/*}"
	fi

	x_ tar -c "$1" | xz -T$XBMK_THREADS -9e > "$2" || \
	    err "can't make tarball '$1'" "mktarball" "$@"
}

e()
{
	es_t="e"

	if [ $# -gt 1 ]; then
		es_t="$2"
	fi

	es2="already exists"
	estr="[ -$es_t \"\$1\" ] || return 1"

	if [ $# -gt 2 ]; then
		estr="[ -$es_t \"\$1\" ] && return 1"
		es2="missing"
	fi

	eval "$estr"

	printf "%s %s\n" "$1" "$es2" 1>&2
}

setvars()
{
	_setvars=""

	if [ $# -lt 2 ]; then
		return 0
	fi

	val="$1"
	shift 1

	while [ $# -gt 0 ]; do
		printf "%s=\"%s\"\n" "$1" "$val"
		shift 1
	done
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
	if [ $# -lt 1 ]; then
		err "findpath: No arguments provided" "findpath" "$@"
	fi

	while [ $# -gt 0 ]
	do
		found="`readlink -f "$1" 2>/dev/null`" || return 1; :

		if [ -z "$found" ]; then
			found="`realpath "$1" 2>/dev/null`" || \
			    return 1
		fi

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

	if [ $xromsize -lt 524288 ]; then
		err "too small, $xromsize: $1" "unpad_one_byte" "$@"
	fi

	unpaddedfile="`mktemp || err "mktemp unpad_one_byte"`" || \
	    err "can't make tmp file" "unpad_one_byte" "$@"

	x_ dd if="$1" of="$unpaddedfile" bs=$xromsize count=1
	x_ mv "$unpaddedfile" "$1"
}

build_sbase()
{
	if [ ! -f "$sha512sum" ]; then
		x_ make -C "$xbmkpwd/util/sbase"
	fi
}

remkdir()
{
	x_ rm -Rf "$@"
	x_ xbmkdir "$@"
}

xbmkdir()
{
	while [ $# -gt 0 ]
	do
		if [ ! -d "$1" ]; then
			x_ mkdir -p "$1"
		fi

		shift 1
	done
}

fx_()
{
	xchk fx_ "$@"
	xcmd="$1"

	xfile="`mktemp || err "can't create tmpfile"`" || \
	    err "can't make tmpfile" "fx_" "$@"

	x_ rm -f "$xfile"
	x_ touch "$xfile"

	shift 1

	"$@" 2>/dev/null | sort 1>"$xfile" 2>/dev/null || \
	    err "can't sort to '$xfile'" "fx_" "$xcmd" "$@"

	dx_ "$xcmd" "$xfile" || :
	x_ rm -f "$xfile"
}

dx_()
{
	xchk dx_ "$@"

	if [ ! -f "$2" ]; then
		return 0
	fi

	while read -r fx; do
		$1 "$fx" || return 1; :
	done < "$2" || err "cannot read '$2'" "dx_" "$@"; :
}

x_()
{
	if [ $# -lt 1 ]; then
		return 0
	elif [ -z "$1" ]; then
		err "Empty first arg" "x_" "$@"
	else
		"$@" || err "Unhandled error" "x_" "$@"
	fi
}

xchk()
{
	if [ $# -lt 3 ]; then
		err "$1 needs at least two arguments" "xchk" "$@"
	elif [ -z "$2" ] || [ -z "$3" ]; then
		err "arguments must not be empty" "xchk" "$@"
	fi
}

err()
{
	if [ $# -eq 1 ]; then
		printf "ERROR %s: %s\n" "$0" "$1" 1>&2 || :
	elif [ $# -gt 1 ]; then
		printf "ERROR %s: %s: in command with args: " "$0" "$1" 1>&2
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
		if [ $# -gt 1 ]; then
			printf " "
		fi

		xprintfargs=1
		shift 1
	done
	if [ $xprintfargs -gt 0 ]; then
		printf "\n"
	fi
}
