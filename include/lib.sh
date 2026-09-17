# SPDX-License-Identifier: GPL-3.0-only

# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2025 Alper Nebi Yasak <alpernebiyasak@gmail.com>

cbfstool="elf/coreboot/default/cbfstool"
rmodtool="elf/coreboot/default/rmodtool"

build_sbase="[ ! -f \"\$sha512sum\" ] && x_ make -C util/sbase"

newvar()
{
	printf "%s\n" "$*" | \
	    awk '{ for (i=1; i<=NF; i++) $i = $i "=;"; print }'; :
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

	if [ -z "${version-}" ] || [ -z "${versiondate-}" ]; then
		err "version and/or versiondate unset" "update_xbmkver" "$@"
	fi

	printf "%s\n" "$version" > "$1/.version" || \
	    err "can't write '$1'" "update_xbmkver" "$@"; :

	printf "%s\n" "$versiondate" > "$1/.versiondate" || \
	    err "can't write '$versiondate'" "update_xbmkver" "$@"; :
}

version()
{
	printf "%s\nWebsite: %s\n" "$relname" "$projectsite"
}

xbmk_sanitize_version()
{
	[ -n "$version" ] && version="`printf '%s\n' "$version" | sed \
		's/[[:space:]]//g; s/\.\.//g; s/\.\///g; s/\//-/g; s/^-//'`" \
	&& [ -z "$version" ] && \
		err "'version' empty after sanitization" \
		    "xbmk_sanitize_version" "$@"; :
}

mktarball()
{
	printf "Creating tar archive '%s' from directory '%s'\n" "$2" "$1"

	[ "${2%/*}" != "$2" ] && \
		x_ mkdir -p "${2%/*}"

	x_ tar -c "$1" | xz -T$XBMK_THREADS -9e > "$2" || \
	    err "can't make tarball '$1'" "mktarball" "$@"
}

e()
{
	es_t="e"
	[ $# -gt 1 ] && \
		es_t="$2"

	es2="already exists"
	estr="[ -$es_t \"\$1\" ] || return 1"

	if [ $# -gt 2 ]; then
		estr="[ -$es_t \"\$1\" ] && return 1"
		es2="missing"
	fi

	eval "$estr"
	printf "%s %s\n" "$1" "$es2" 1>&2
}

singletree()
{
	( fx_ "eval exit 1 && err" find "config/$1/"*/ -type f \
	    -name "target.cfg" ) || return 1; :
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

remkdir()
{
	x_ rm -Rf "$@"
	x_ mkdir -p "$@"
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

	[ ! -f "$2" ] && return 0

	while read -r fx; do
		$1 "$fx" || return 1; :
	done < "$2" || err "cannot read '$2'" "dx_" "$@"; :
}

xchk()
{
	if [ $# -lt 3 ]; then
		err "$1 needs at least two arguments" "xchk" "$@"
	elif [ -z "$2" ] || [ -z "$3" ]; then
		err "arguments must not be empty" "xchk" "$@"
	fi
}
