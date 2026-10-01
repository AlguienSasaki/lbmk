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
	[ -d "$1" ] || return 0;

	xbmk_sanitize_version

	if [ -z "${version-}" ] || [ -z "${versiondate-}" ]; then
		err "version and/or versiondate unset" "update_xbmkver" "$@"
	fi

	printf "%s\n" "$version" > "$1/.version" || \
	    err "can't write '$1'" "update_xbmkver" "$@"; :

	printf "%s\n" "$versiondate" > "$1/.versiondate" || \
	    err "can't write '$versiondate'" "update_xbmkver" "$@"; :
}

mkversion()
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

mkimport_uboot()
{
	upstream="${1-}"
	upstream="${upstream##*/}"
	[ -z "$upstream" ] && upstream="default"

	arm64="ROCKCHIP_RK3308 ROCKCHIP_RK3328 ROCKCHIP_RK3368 ROCKCHIP_RK3399 \
	    ROCKCHIP_RK3568 ROCKCHIP_RK3588"

	x_ "$mk" -f u-boot "$upstream"

#	mk_uboot_configs X86 "i386-elf" # TODO distinguish amd64 startup

	mk_uboot_configs "$arm64" "aarch64-elf arm-eabi" "$upstream"
	mk_uboot_configs RISCV "riscv64-elf"
}

mk_uboot_configs()
{
	ucfgs="src/u-boot/$3/configs"

	rm -f "$xbtmp/ucfg"
	for a in $1; do
		x_ grep -Rl "CONFIG_${a}=y" "$ucfgs" >> "$xbtmp/ucfg" || \
		    err "can't make ucfg" "mkimport_uboot" "$@"
	done

	while read -r a; do
		cp_uboot_cfg "$a" "$2"
	done < "$xbtmp/ucfg" || err "can't read ucfg" mkimport_uboot "$@";
}

cp_uboot_cfg()
{
	udir="${1##*/}"
	udir="$(printf "%s\n" "$udir" | sed 's,-,_,g')"

	ldir="config/module/u-boot/$udir"

	x_ remkdir "$ldir" "$ldir/config"
	x_ cp "$1" "$ldir/config/default"

	x_ printf "tree=\"default\"\n" > "$ldir/target.cfg" || \
	    err "can't make target.cfg tree=" cp_uboot_config "$@"
	x_ printf "xarch=\"%s\"\n" "$2" >> \
	    "$ldir/target.cfg" || err "targetfcg xarch" cp_uboot_config "$@";
	x_ printf "release=\"n\"" >> "$ldir/target.cfg" || \
	    err "can't set release=n" cp_uboot_config "$@"

	x_ yes '' | "$mk" -l u-boot $udir || \
	    err "can't update cfg" cp_uboot_config "$@";
}

pad_one_byte()
{
	paddedfile="`mktemp || err "mktemp pad_one_byte"`" || \
	    err "can't make tmp file" "pad_one_byte" "$@"

	x_ cat "$1" "config/snippet/0" > "$paddedfile" || \
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
