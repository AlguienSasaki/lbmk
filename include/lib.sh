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
	printf "%s\n" "$version" > "$1/.version" || err "$1 !version"; :
	printf "%s\n" "$versiondate" > "$1/.versiondate" || err "$1 !vdate"; :
}

mktarball()
{
	printf "Creating tar archive '%s' from directory '%s'\n" "$2" "$1"
	[ "${2%/*}" = "$2" ] || x_ mkdir -p "${2%/*}"
	x_ tar -c "$1" | xz -T$XBMK_THREADS -9e > "$2" || err "mktarball2, $1"
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
		printf "e \"%s\" f not && err \"Missing config\";\n" "$1"
	printf ". \"%s\" || err \"Could not read config\";\n" "$1"
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
	[ $# -gt 0 ] || err "findpath: No arguments provided"
	while [ $# -gt 0 ]; do
		found="`readlink -f "$1" 2>/dev/null`" || return 1; :
		[ -n "$found" ] || found="`realpath "$1" 2>/dev/null`" || \
		    return 1; :
		printf "%s\n" "$found"
		shift 1
	done
}

fx_()
{
	fd="`mktemp`" && x_ rm -f "$fd" && x_ touch "$fd"
	xx="$1" && shift 1
	"$@" 2>/dev/null | sort 1>"$fd" 2>/dev/null || err "FATAL: !sort fx_"
	dx_ "$xx" "$fd" || :
	x_ rm -f "$fd"
}

dx_()
{
	[ ! -f "$2" ] || while read -r fx; do
		$1 "$fx" || return 1; :
	done < "$2" || err "dx_ $*: cannot read '$2'"; :
}

x_()
{
	[ $# -lt 1 ] || [ -n "$1" ] || err "Empty first arg: x_ $(echo "$@")"
	[ $# -lt 1 ] || "$@" || err "Unhandled error for: $(echo "$@")"; :
}

err()
{
	[ $# -lt 1 ] || printf "ERROR %s: %s\n" "$0" "$1" 1>&2 || :
	exit 1
}
