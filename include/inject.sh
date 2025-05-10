# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

cbcfgsdir="config/coreboot"
hashfiles="vendorhashes blobhashes" # blobhashes for backwards compatibility
tmpromdel="$XBMK_CACHE/DO_NOT_FLASH"
nvm="util/nvmutil/nvm"
ifdtool="elf/ifdtool/default/ifdtool"

cv="CONFIG_GBE_BIN_PATH"
[ -n "$cvxbmk" ] && cv="$cv $cvxbmk"
[ -n "$cvchk" ] && cv="$cv $cvchk"

eval "`setvars "" archive boarddir IFD_platform ifdprefix tree new_mac \
    tmpromdir board $cv`"

inject()
{
	remkdir "$tmpromdel"

	set +u +e
	[ $# -lt 1 ] && err "No options specified"
	eval "`setvars "" nukemode new_mac xchanged`"

	archive="$1";
	new_mac="xx:xx:xx:xx:xx:xx"

	[ $# -gt 1 ] && case "$2" in
	nuke)
		new_mac=""
		nukemode="nuke" ;;
	setmac)
		[ $# -gt 2 ] && new_mac="$3" && \
		    [ -z "$new_mac" ] && err "Empty MAC address specified" ;;
	*)
		err "Unrecognised inject mode: '$2'"
	esac
	[ "$new_mac" = "keep" ] && new_mac=""

	[ -n "$new_mac" ] && [ "$new_mac" != "restore" ] && \
		x_ make -C util/nvmutil clean && x_ make -C util/nvmutil

	check_release
	check_target && patch_release

	[ "$xchanged" = "y" ] && remktar

	xnot=" NOT" && [ "$xchanged" = "y" ] && xnot=""
	printf "\n'%s' was%s modified\n" "$archive" "$xnot" 1>&2

	x_ rm -Rf "$tmpromdel"
}

check_release()
{
	[ -L "$archive" ] && err "'$archive' is a symlink"
	e "$archive" f missing && err "'$archive' missing"

	archivename="`basename "$archive"`" || err "Can't get '$archive' name"
	[ -z "$archivename" ] && err "Can't determine archive name"

	case "$archivename" in
	*_src.tar.xz)
		err "'$archive' is a src archive, silly!" ;;
	grub_*|seagrub_*|custom_*|seauboot_*|seabios_withgrub_*)
		err "'$archive' is a ROM image (it must be a tarball)" ;;
	*.tar.xz) _stripped_prefix="${archivename#*_}"
		board="${_stripped_prefix%.tar.xz}" ;;
	*)
		err "'$archive': could not detect board type"
	esac; :
}

check_target()
{
	if [ "$board" = "serprog_rp2040" ] || [ "$board" = "serprog_stm32" ] \
	    || [ "$board" = "serprog_pico" ]; then
		return 1
	fi
	boarddir="$cbcfgsdir/$board"

	eval "`setcfg "$boarddir/target.cfg"`"
	chkvars tree && x_ ./mk -d coreboot "$tree"

	ifdtool="elf/ifdtool/$tree/ifdtool"
	[ -n "$IFD_platform" ] && ifdprefix="-p $IFD_platform"; :
}

patch_release()
{
	[ "$nukemode" = "nuke" ] || x_ ./mk download "$board"

	has_hashes="n"
	tmpromdir="$tmpromdel/bin/$board"

	remkdir "${tmpromdir%"/bin/$board"}"
	x_ tar -xf "$archive" -C "${tmpromdir%"/bin/$board"}"

	for _hashes in $hashfiles; do
		e "$tmpromdir/$_hashes" f && \
		    has_hashes="y" && hashfile="$_hashes" && break; :
	done

	readkconfig || exit 0

	[ -n "$new_mac" ] && [ -n "$CONFIG_GBE_BIN_PATH" ] && modify_mac; :
}

readkconfig()
{
	x_ rm -f "$xbmktmp/cbcfg"
	fx_ scankconfig x_ find "$boarddir/config" -type f
	eval "`setcfg "$xbmktmp/cbcfg" 1`"
	setvfile "$@" && return 1; :
}

scankconfig()
{
	for cbc in $cv; do
		grep "$cbc" "$1" 1>>"$xbmktmp/cbcfg" 2>/dev/null || :
	done
}

modify_mac()
{
	x_ cp "${CONFIG_GBE_BIN_PATH##*../}" "$xbmklocal/gbe"
	[ -n "$new_mac" ] && [ "$new_mac" != "restore" ] && \
	    x_ "$nvm" "$xbmklocal/gbe" setmac "$new_mac"

	fx_ newmac x_ find "$tmpromdir" -maxdepth 1 -type f -name "*.rom"

	printf "\nGbE NVM written to '%s':\n" "$archive"
	x_ "$nvm" "$xbmklocal/gbe" dump | grep -v "bytes read from file" || :
}

newmac()
{
	e "$1" f && xchanged="y" && x_ \
	    "$ifdtool" $ifdprefix -i GbE:"$xbmklocal/gbe" "$1" -O "$1"; :
}

remktar()
{
	(
	x_ cd "${tmpromdir%"/bin/$board"}"
	printf "Re-building tar archive (please wait)\n"
	mkrom_tarball "bin/$board" 1>/dev/null
	) || err "Cannot re-generate '$archive'"

	mv "${tmpromdir%"/bin/$board"}/bin/${relname}_${board}.tar.xz" \
	    "$archive" || err "'$archive' -> Can't overwrite"; :
}
