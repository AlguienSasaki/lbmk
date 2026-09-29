# SPDX-License-Identifier: GPL-3.0-only

# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2023-2026 Leah Rowe <leah@libreboot.org>

cbcfgsdir="config/module/coreboot"
tmpromdel="$XBMK_CACHE/DO_NOT_FLASH"
nvmutil="util/nvmutil/nvmutil"
ifdtool="elf/coreboot/default/ifdtool"

checkvars="CONFIG_GBE_BIN_PATH ${checkvarsxbmk-} ${checkvarschk-}"

eval "`newvar archive board boarddir IFD_platform ifdprefix \
    new_mac nuke tmpromdir tree xchanged $checkvars`"

mkinject()
{
	remkdir "$tmpromdel"

	[ $# -lt 1 ] && err "No options specified" "inject" "$@"

	archive="$1";
	new_mac="xx:xx:xx:xx:xx:xx"

	[ $# -gt 1 ] && case "$2" in
	nuke)
		new_mac=""
		nuke="nuke"
		;;
	setmac)
		if [ $# -gt 2 ]; then
			new_mac="$3" && \
			[ -z "$new_mac" ] && \
				err "Empty MAC address" "inject" "$@"; :
		fi
		;;
	*)
		err "Unrecognised inject mode: '$2'" "inject" "$@" ;;
	esac

	[ "$new_mac" = "keep" ] && \
		new_mac=""

	check_release
	if check_target; then
		patch_release || return 0
	fi
	[ "$xchanged" = "y" ] && \
		remktar

	x_ rm -Rf "$tmpromdel"
}

check_release()
{
	[ -L "$archive" ] && \
		err "'$archive' is a symlink" "check_release" "$@"
	e "$archive" f missing && \
		err "'$archive' missing" "check_release" "$@"

	archivename="`basename "$archive" || err "Can't get '$archive' name"`" \
	    || err "can't get '$archive' name" "check_release" "$@"

	[ -z "$archivename" ] && \
		err "Can't determine archive name" "check_release" "$@"

	case "$archivename" in
	*_src.tar.xz)
		err "'$archive' is a src archive!" "check_release" "$@"
		;;
	grub_*|seagrub_*|custom_*|seauboot_*|seabios_withgrub_*)
		err "'$archive' is a ROM image" "check_release" "$@"
		;;
	*.tar.xz) _stripped_prefix="${archivename#*_}"
		board="${_stripped_prefix%.tar.xz}"
		;;
	*)
		err "'$archive': cannot detect board" "check_release" "$@"
		;;
	esac; :
}

check_target()
{
	[ "$board" = "${board#serprog_}" ] || return 1

	boarddir="$cbcfgsdir/$board"

	. "$boarddir/target.cfg" || \
	    err "Can't read '$boarddir/target.cfg'" "check_target" "$@"

	[ -z "$tree" ] && \
		err "tree unset in '$boarddir/target.cfg'" "check_target" "$@"

	x_ "$mk" -d coreboot "$tree"

	ifdtool="elf/coreboot/$tree/ifdtool"
	[ -n "$IFD_platform" ] && \
		ifdprefix="-p $IFD_platform"; :
}

patch_release()
{
	[ "$nuke" != "nuke" ] && \
		x_ "$mk" download "$board"

	has_hashes="n"
	tmpromdir="$tmpromdel/bin/$board"

	remkdir "${tmpromdir%"/bin/$board"}"
	x_ tar -xf "$archive" -C "${tmpromdir%"/bin/$board"}"

	for _hashes in "vendorhashes" "blobhashes"; do
		if e "$tmpromdir/$_hashes" f; then
			has_hashes="y"
			hashfile="$_hashes"

			break
		fi
	done

	readkconfig || return 1

	[ -n "$new_mac" ] && [ -n "$CONFIG_GBE_BIN_PATH" ] && \
		modify_mac; :
}

readkconfig()
{
	x_ rm -f "$xbtmp/cbcfg"
	fx_ scankconfig x_ find "$boarddir/config" -type f

	e "$xbtmp/cbcfg" f missing && \
		return 1

	. "$xbtmp/cbcfg" || \
	    err "Can't read '$xbtmp/cbcfg'" "readkconfig" "$@"

	setvfile "$@" || return 1; :
}

scankconfig()
{
	for cbc in $checkvars; do
		grep "$cbc" "$1" 2>/dev/null | grep -v "\$(" \
		    1>>"$xbtmp/cbcfg" || :
	done
}

modify_mac()
{
	x_ cp "${CONFIG_GBE_BIN_PATH##*../}" "$xbtmp/gbe"

	if [ -n "$new_mac" ] && [ "$new_mac" != "restore" ]; then
		forx 'x_ make -C util/nvmutil %s' clean nvmutil
		x_ "$nvmutil" "$xbtmp/gbe" setmac "$new_mac"
	fi

	fx_ newmac x_ find "$tmpromdir" -mindepth 1 -maxdepth 1 \
	    -type f -name "*.rom"

	printf "\nThe following GbE NVM data will be written:\n"
	x_ "$nvmutil" "$xbtmp/gbe" dump | grep -v "bytes read from file" || :
}

newmac()
{
	e "$1" f || return 0
	xchanged="y"
	x_ "$ifdtool" $ifdprefix -i GbE:"$xbtmp/gbe" "$1" -O "$1"
}

remktar()
{
	(
	x_ cd "${tmpromdir%"/bin/$board"}"

	printf "Re-building tar archive (please wait)\n"
	mkrom_tarball "bin/$board" 1>/dev/null

	) || err "Cannot re-generate '$archive'" "remktar" "$@"

	mv "${tmpromdir%"/bin/$board"}/bin/${relname}_${board}.tar.xz" \
	    "$archive" || err "'$archive' -> Can't overwrite" "remktar" "$@"; :
}
