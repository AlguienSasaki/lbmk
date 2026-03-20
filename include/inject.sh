# SPDX-License-Identifier: GPL-3.0-only

# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

cbcfgsdir="config/coreboot"
tmpromdel="$XBMK_CACHE/DO_NOT_FLASH"
nvmutil="util/libreboot-utils/nvmutil"
ifdtool="elf/coreboot/default/ifdtool"

checkvars="CONFIG_GBE_BIN_PATH"
if [ -n "$checkvarsxbmk" ]; then
	checkvars="$checkvars $checkvarsxbmk"
fi
if [ -n "$checkvarschk" ]; then
	checkvars="$checkvars $checkvarschk"
fi

archive=""
board=""
boarddir=""
IFD_platform=""
ifdprefix=""
new_mac=""
tmpromdir=""
tree=""
xchanged=""

eval "`setvars "" $checkvars`"

inject()
{
	remkdir "$tmpromdel"

	if [ $# -lt 1 ]; then
		err "No options specified" "inject" "$@"
	fi

	archive="$1";
	new_mac="xx:xx:xx:xx:xx:xx"

	nuke=""
	xchanged=""

	[ $# -gt 1 ] && case "$2" in
	nuke)
		new_mac=""
		nuke="nuke"
		;;
	setmac)
		if [ $# -gt 2 ]; then
			new_mac="$3" && \
			if [ -z "$new_mac" ]; then
				err "Empty MAC address specified" "inject" "$@"
			fi
		fi
		;;
	*)
		err "Unrecognised inject mode: '$2'" "inject" "$@" ;;
	esac

	if [ "$new_mac" = "keep" ]; then
		new_mac=""
	fi

	check_release
	if check_target; then
		if ! patch_release; then
			return 0
		fi
	fi
	if [ "$xchanged" = "y" ]; then
		remktar
	fi

	if [ "$xchanged" = "y" ]; then
		printf "\n'%s' was modified\n" "$archive" 1>&2
	else
		printf "\n'%s' was NOT modified\n" "$archive" 1>&2
	fi

	x_ rm -Rf "$tmpromdel"
}

check_release()
{
	if [ -L "$archive" ]; then
		err "'$archive' is a symlink" "check_release" "$@"
	fi
	if e "$archive" f missing; then
		err "'$archive' missing" "check_release" "$@"
	fi

	archivename="`basename "$archive" || err "Can't get '$archive' name"`" \
	    || err "can't get '$archive' name" "check_release" "$@"

	if [ -z "$archivename" ]; then
		err "Can't determine archive name" "check_release" "$@"
	fi

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
	if [ "$board" != "${board#serprog_}" ]; then
		return 1
	fi

	boarddir="$cbcfgsdir/$board"

	. "$boarddir/target.cfg" || \
	    err "Can't read '$boarddir/target.cfg'" "check_target" "$@"

	if [ -z "$tree" ]; then
		err "tree unset in '$boarddir/target.cfg'" "check_target" "$@"
	fi

	x_ ./mk -d coreboot "$tree"

	ifdtool="elf/coreboot/$tree/ifdtool"

	if [ -n "$IFD_platform" ]; then
		ifdprefix="-p $IFD_platform"
	fi
}

patch_release()
{
	if [ "$nuke" != "nuke" ]; then
		x_ ./mk download "$board"
	fi

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

	if ! readkconfig; then
		return 1
	elif [ -n "$new_mac" ] && [ -n "$CONFIG_GBE_BIN_PATH" ]; then
		modify_mac
	fi
}

readkconfig()
{
	x_ rm -f "$xbtmp/cbcfg"

	fx_ scankconfig x_ find "$boarddir/config" -type f

	if e "$xbtmp/cbcfg" f missing; then
		return 1
	fi

	. "$xbtmp/cbcfg" || \
	    err "Can't read '$xbtmp/cbcfg'" "readkconfig" "$@"

	if ! setvfile "$@"; then
		return 1
	fi
}

scankconfig()
{
	for cbc in $checkvars; do
		grep "$cbc" "$1" 2>/dev/null 1>>"$xbtmp/cbcfg" || :
	done
}

modify_mac()
{
	x_ cp "${CONFIG_GBE_BIN_PATH##*../}" "$xbtmp/gbe"

	if [ -n "$new_mac" ] && [ "$new_mac" != "restore" ]; then
		x_ make -C util/libreboot-utils clean
		x_ make -C util/libreboot-utils

		x_ "$nvmutil" "$xbtmp/gbe" setmac "$new_mac"
	fi

	fx_ newmac x_ find "$tmpromdir" -maxdepth 1 -type f -name "*.rom"

	printf "\nThe following GbE NVM data will be written:\n"
	x_ "$nvmutil" "$xbtmp/gbe" dump | grep -v "bytes read from file" || :
}

newmac()
{
	if e "$1" f; then
		xchanged="y"
		x_ "$ifdtool" $ifdprefix -i GbE:"$xbtmp/gbe" "$1" -O "$1"
	fi
}

remktar()
{
	(
		x_ cd "${tmpromdir%"/bin/$board"}"

		printf "Re-building tar archive (please wait)\n"
		mkrom_tarball "bin/$board" 1>/dev/null

	) || err "Cannot re-generate '$archive'" "remktar" "$@"

	mv "${tmpromdir%"/bin/$board"}/bin/${relname}_${board}.tar.xz" \
	    "$archive" || \
	    err "'$archive' -> Can't overwrite" "remktar" "$@"; :
}
