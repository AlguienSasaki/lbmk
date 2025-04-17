# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

e6400_unpack="$xbmkpwd/src/bios_extract/dell_inspiron_1100_unpacker.py"
me7updateparser="$xbmkpwd/util/me7_update_parser/me7_update_parser.py"
pfs_extract="$xbmkpwd/src/biosutilities/Dell_PFS_Extract.py"
uefiextract="$xbmkpwd/elf/uefitool/uefiextract"
vendir="vendorfiles"
appdir="$vendir/app"
cbcfgsdir="config/coreboot"
hashfiles="vendorhashes blobhashes" # blobhashes for backwards compatibility
dontflash="!!! AN ERROR OCCURED! Please DO NOT flash if injection failed. !!!"
vfix="DO_NOT_FLASH_YET._FIRST,_INJECT_FILES_VIA_INSTRUCTIONS_ON_LIBREBOOT.ORG_"
vguide="https://libreboot.org/docs/install/ivy_has_common.html"
tmpromdel="$xbmkpwd/tmp/DO_NOT_FLASH"
nvm="util/nvmutil/nvm"

cvchk="CONFIG_HAVE_MRC CONFIG_HAVE_ME_BIN CONFIG_KBC1126_FIRMWARE \
	CONFIG_VGA_BIOS_FILE CONFIG_INCLUDE_SMSC_SCH5545_EC_FW \
	CONFIG_LENOVO_TBFW_BIN CONFIG_FSP_M_FILE CONFIG_FSP_S_FILE"

cv="CONFIG_ME_BIN_PATH CONFIG_SMSC_SCH5545_EC_FW_FILE CONFIG_KBC1126_FW1 \
    CONFIG_KBC1126_FW2 CONFIG_KBC1126_FW1_OFFSET CONFIG_KBC1126_FW2_OFFSET \
    CONFIG_VGA_BIOS_ID CONFIG_BOARD_DELL_E6400 CONFIG_FSP_S_CBFS \
    CONFIG_HAVE_REFCODE_BLOB CONFIG_REFCODE_BLOB_FILE CONFIG_GBE_BIN_PATH \
    CONFIG_IFD_BIN_PATH CONFIG_FSP_FD_PATH CONFIG_MRC_FILE CONFIG_FSP_M_CBFS \
    CONFIG_FSP_USE_REPO CONFIG_FSP_FULL_FD $cvchk"

eval "`setvars "" has_hashes EC_hash DL_hash DL_url_bkup MRC_refcode_gbe vcfg \
    E6400_VGA_DL_hash E6400_VGA_DL_url E6400_VGA_DL_url_bkup E6400_VGA_offset \
    E6400_VGA_romname SCH5545EC_DL_url_bkup SCH5545EC_DL_hash _dest tree \
    mecleaner kbc1126_ec_dump MRC_refcode_cbtree new_mac _dl SCH5545EC_DL_url \
    archive EC_url boarddir rom cbdir DL_url nukemode cbfstoolref FSPFD_hash \
    _7ztest ME11bootguard ME11delta ME11version ME11sku ME11pch tmpromdir \
    IFD_platform ifdprefix cdir sdir _me _metmp mfs TBFW_url_bkup TBFW_url \
    TBFW_hash TBFW_size hashfile xromsize xchanged EC_url_bkup need_files \
    vfile cbcfg $cv`"

vendor_download()
{
	[ $# -gt 0 ] || $err "No argument given"
	export PATH="$PATH:/sbin"
	board="$1"
	readcfg && readkconfig && bootstrap && getfiles; :
}

readkconfig()
{
	x_ rm -f "$TMPDIR/cbcfg"
	cbcfg="`check_defconfig "$boarddir"`" || for cbc in $cv; do
		grep "$cbc" "$cbcfg" 1>>"$TMPDIR/cbcfg" 2>/dev/null || :
	done
	eval "`setcfg "$TMPDIR/cbcfg" 1`"

	for c in $cvchk; do
		eval "[ \"\${$c}\" = \"/dev/null\" ] && continue"
		eval "[ -z \"\${$c}\" ] && continue"
		eval "`setcfg "$vfile"`"
		return 0
	done
	return 1
}

bootstrap()
{
	x_ ./mk -f coreboot ${cbdir##*/}
	mk -b uefitool biosutilities bios_extract
	[ -d "${kbc1126_ec_dump%/*}" ] && x_ make -C "$cbdir/util/kbc1126"
	[ -n "$MRC_refcode_cbtree" ] && \
	    cbfstoolref="elf/cbfstool/$MRC_refcode_cbtree/cbfstool" && \
	    x_ ./mk -d coreboot "$MRC_refcode_cbtree"; :
}

getfiles()
{
	[ -z "$CONFIG_HAVE_ME_BIN" ] || fetch intel_me "$DL_url" \
	    "$DL_url_bkup" "$DL_hash" "$CONFIG_ME_BIN_PATH"
	[ -z "$CONFIG_INCLUDE_SMSC_SCH5545_EC_FW" ] || fetch sch5545ec \
	    "$SCH5545EC_DL_url" "$SCH5545EC_DL_url_bkup" "$SCH5545EC_DL_hash" \
	    "$CONFIG_SMSC_SCH5545_EC_FW_FILE"
	[ -z "$CONFIG_KBC1126_FIRMWARE" ] || fetch kbc1126ec "$EC_url" \
	    "$EC_url_bkup" "$EC_hash" "$CONFIG_KBC1126_FW1"
	[ -z "$CONFIG_VGA_BIOS_FILE" ] || fetch e6400vga "$E6400_VGA_DL_url" \
	  "$E6400_VGA_DL_url_bkup" "$E6400_VGA_DL_hash" "$CONFIG_VGA_BIOS_FILE"
	[ -z "$CONFIG_HAVE_MRC" ] || fetch "mrc" "$MRC_url" "$MRC_url_bkup" \
	    "$MRC_hash" "$CONFIG_MRC_FILE"
	[ -z "$CONFIG_LENOVO_TBFW_BIN" ] || fetch "tbfw" "$TBFW_url" \
	    "$TBFW_url_bkup" "$TBFW_hash" "$CONFIG_LENOVO_TBFW_BIN"
	#
	# in the future, we might have libre fsp-s and then fsp-m.
	# therefore, handle them separately, in case one of them is libre; if
	# one of them was, the path wouldn't be set.
	#
	[ -z "$CONFIG_FSP_M_FILE" ] || fetch "fspm" "$CONFIG_FSP_FD_PATH" \
	    "$CONFIG_FSP_FD_PATH" "$FSPFD_hash" "$CONFIG_FSP_M_FILE" copy
	[ -z "$CONFIG_FSP_S_FILE" ] || fetch "fsps" "$CONFIG_FSP_FD_PATH" \
	    "$CONFIG_FSP_FD_PATH" "$FSPFD_hash" "$CONFIG_FSP_S_FILE" copy; :
}

fetch()
{
	dl_type="$1"
	dl="$2"
	dl_bkup="$3"
	dlsum="$4"
	_dest="${5##*../}"

	[ "$5" = "/dev/null" ] && return 0
	_dl="$XBMK_CACHE/file/$dlsum"
	if [ "$dl_type" = "fspm" ] || [ "$dl_type" = "fsps" ]; then
		# HACK: if grabbing fsp from coreboot, fix the path for lbmk
		for _cdl in dl dl_bkup; do
			eval "$_cdl=\"\${$_cdl##*../}\"; _cdp=\"\$$_cdl\""
			[ -f "$_cdp" ] || _cdp="$cbdir/$_cdp"
			[ -f "$_cdp" ] && eval "$_cdl=\"$_cdp\""
		done
	fi

	dlop="curl" && [ $# -gt 5 ] && dlop="$6"
	download "$dl" "$dl_bkup" "$_dl" "$dlsum" "$dlop"

	rm -Rf "${_dl}_extracted" || $err "!rm ${_ul}_extracted. $dontflash"
	e "$_dest" f && return 0

	x_ mkdir -p "${_dest%/*}"
	remkdir "$appdir"
	extract_archive "$_dl" "$appdir" "$dl_type" || \
	    [ "$dl_type" = "e6400vga" ] || \
	    $err "mkd $_dest $dl_type: !extract. $dontflash"

	eval "extract_$dl_type"
	set -u -e
	e "$_dest" f missing && $err "!extract_$dl_type. $dontflash"; :
}

extract_intel_me()
{
	e "$mecleaner" f not && $err "$cbdir: me_cleaner missing. $dontflash"

	cdir="$xbmkpwd/$appdir"
	_me="$xbmkpwd/$_dest"
	_metmp="$xbmkpwd/tmp/me.bin"

	mfs="" && [ "$ME11bootguard" = "y" ] && mfs="--whitelist MFS" && \
	    chkvars ME11delta ME11version ME11sku ME11pch
	[ "$ME11bootguard" = "y" ] && x_ ./mk -f deguard

	x_ mkdir -p tmp

	extract_intel_me_bruteforce
	if [ "$ME11bootguard" = "y" ]; then
		apply_me11_deguard_mod
	else
		mv "$_metmp" "$_me" || $err "!mv $_metmp $_me - $dontflash"
	fi
}

extract_intel_me_bruteforce()
{
	[ $# -gt 0 ] && cdir="$1"
	e "$_metmp" f && return 0

	[ -z "$sdir" ] && sdir="$(mktemp -d)"
	x_ mkdir -p "$sdir"

	set +u +e
	(
	[ "${cdir#/a}" != "$cdir" ] && cdir="${cdir#/}"
	cd "$cdir" || $err "extract_intel_me: !cd \"$cdir\" - $dontflash"
	for i in *; do
		e "$_metmp" f && break
		[ -L "$i" ] && continue
		if [ -f "$i" ]; then
			_r="-r" && [ -n "$mfs" ] && _r=""
			"$mecleaner" $mfs $_r -t -O "$sdir/vendorfile" \
			    -M "$_metmp" "$i" && break
			"$mecleaner" $mfs $_r -t -O "$_metmp" "$i" && break
			"$me7updateparser" -O "$_metmp" "$i" && break
			_7ztest="${_7ztest}a"
			extract_archive "$i" "$_7ztest" || continue
			extract_intel_me_bruteforce "$cdir/$_7ztest"
		elif [ -d "$i" ]; then
			extract_intel_me_bruteforce "$cdir/$i"
		else
			continue
		fi
		cdir="$1"; [ "${cdir#/a}" != "$cdir" ] && cdir="${cdir#/}"
		cd "$cdir" || :
	done
	) || :
	rm -Rf "$sdir" || $err "extract_intel_me: !rm -Rf $sdir - $dontflash"
}

apply_me11_deguard_mod()
{
	(
	x_ cd src/deguard/
	x_ ./finalimage.py --delta "data/delta/$ME11delta" \
	    --version "$ME11version" \
	    --pch "$ME11pch" --sku "$ME11sku" --fake-fpfs data/fpfs/zero \
	    --input "$_metmp" --output "$_me"
	) || $err "Error running deguard for $_me - $dontflash"
}

extract_archive()
{
	if [ $# -gt 2 ]; then
		if [ "$3" = "fspm" ] || [ "$3" = "fsps" ]; then
			decat_fspfd "$1" "$2"
			return 0
		fi
	fi

	innoextract "$1" -d "$2" || python "$pfs_extract" "$1" -e || 7z x \
	    "$1" -o"$2" || unar "$1" -o "$2" || unzip "$1" -d "$2" || return 1

	[ ! -d "${_dl}_extracted" ] || cp -R "${_dl}_extracted" "$2" || \
	    $err "!mv '${_dl}_extracted' '$2' - $dontflash"; :
}

decat_fspfd()
{
	_fspfd="$1"
	_fspdir="$2"
	_fspsplit="$cbdir/3rdparty/fsp/Tools/SplitFspBin.py"

	x_ $python "$_fspsplit" split -f "$_fspfd" -o "$_fspdir" -n "Fsp.fd"
}

extract_kbc1126ec()
{
	x_ e "$kbc1126_ec_dump" f
	(
	x_ cd "$appdir/"
	mv Rompaq/68*.BIN ec.bin || :
	if [ ! -f "ec.bin" ]; then
		unar -D ROM.CAB Rom.bin || unar -D Rom.CAB Rom.bin || \
		    unar -D 68*.CAB Rom.bin || $err "kbc1126 unar failed"
		x_ mv Rom.bin ec.bin
	fi
	x_ e ec.bin f && x_ "$kbc1126_ec_dump" ec.bin
	) || $err "$board: can't extract kbc1126 ec firmware - $dontflash"

	x_ e "$appdir/ec.bin.fw1" f && x_ e "$appdir/ec.bin.fw2" f

	cp "$appdir/"ec.bin.fw* "${_dest%/*}/" || \
	    $err "!cp 1126ec $_dest - $dontflash"; :
}

extract_e6400vga()
{
	set +u +e
	chkvars E6400_VGA_offset E6400_VGA_romname
	tail -c +$E6400_VGA_offset "$_dl" | gunzip > "$appdir/bios.bin" || :
	(
	x_ cd "$appdir"
	x_ e "bios.bin" f
	"$e6400_unpack" bios.bin || printf "TODO: fix dell extract util\n"
	) || $err "can't extract e6400 vga rom - $dontflosh"
	x_ cp "$appdir/$E6400_VGA_romname" "$_dest"
}

extract_sch5545ec()
{
	# full system ROM (UEFI), to extract with UEFIExtract:
	_bios="${_dl}_extracted/Firmware/1 $dlsum -- 1 System BIOS vA.28.bin"
	# this is the SCH5545 firmware, inside of the extracted UEFI ROM:
	_sch5545ec_fw="$_bios.dump/4 7A9354D9-0468-444A-81CE-0BF617D890DF"
	_sch5545ec_fw="$_sch5545ec_fw/54 D386BEB8-4B54-4E69-94F5-06091F67E0D3"
	_sch5545ec_fw="$_sch5545ec_fw/0 Raw section/body.bin" # <-- this!

	"$uefiextract" "$_bios" || $err "sch5545 !extract - $dontflash"
	x_ cp "$_sch5545ec_fw" "$_dest"
}

# Lenovo ThunderBolt firmware updates:
# https://pcsupport.lenovo.com/us/en/products/laptops-and-netbooks/thinkpad-t-series-laptops/thinkpad-t480-type-20l5-20l6/20l5/solutions/ht508988
extract_tbfw()
{
	chkvars TBFW_size # size in bytes, matching TBFW's flash IC
	x_ mkdir -p tmp
	x_ rm -f tmp/tb.bin
	find "$appdir" -type f -name "TBT.bin" > "tmp/tb.txt" || \
	    $err "extract_tbfw $_dest: Can't extract TBT.bin - $dontflash"
	while read -r f; do
		[ -f "$f" ] || continue
		[ -L "$f" ] && continue
		x_ cp "$f" "tmp/tb.bin"
		break
	done < "tmp/tb.txt"
	x_ dd if=/dev/null of=tmp/tb.bin bs=1 seek=$TBFW_size
	x_ cp "tmp/tb.bin" "$_dest"
}

extract_fspm()
{
	copy_fsp M; :
}

extract_fsps()
{
	copy_fsp S; :
}

# this copies the fsp s/m; re-base is handled by ./mk inject
copy_fsp()
{
	x_ cp "$appdir/Fsp_$1.fd" "$_dest"
}

fail_inject()
{
	[ -L "$tmpromdel" ] || [ ! -d "$tmpromdel" ] || \
	    rm -Rf "$tmpromdel" || :
	printf "\n\n%s\n\n" "$dontflash" 1>&2
	printf "WARNING: File '%s' was NOT modified.\n\n" "$archive" 1>&2
	printf "Please MAKE SURE vendor files are inserted before flashing\n\n"
	err_ "$1"
}

vendor_inject()
{
	need_files="n"
	_olderr="$err"
	err="fail_inject"
	remkdir "$tmpromdel"

	set +u +e
	[ $# -lt 1 ] && $err "No options specified. - $dontflash"
	eval "`setvars "" nukemode new_mac xchanged`"

	archive="$1";
	new_mac="??:??:??:??:??:??"

	[ $# -gt 1 ] && case "$2" in
	nuke)
		new_mac=""
		nukemode="nuke" ;;
	setmac)
		[ $# -gt 2 ] && new_mac="$3" && \
		    [ -z "$new_mac" ] && $err "Empty MAC address specified" ;;
	*) $err "Unrecognised inject mode: '$2'"
	esac

	[ "$new_mac" = "keep" ] && new_mac=""

	check_release "$archive" || \
	    $err "You must run this script on a release archive. - $dontflash"

	[ "$new_mac" = "restore" ] && \
	    printf "Restoring default GbE for '$archive', board '$board'\n"

	readcfg && need_files="y"
	if [ "$need_files" = "y" ] || [ -n "$new_mac" ]; then
		if [ "$nukemode" != "nuke" ] && [ "$need_files" = "y" ]; then
			x_ ./mk download "$board"
		fi
		patch_release_roms
	fi
	[ "$need_files" != "y" ] && printf \
	    "\nTarball '%s' (board '%s) doesn't need vendorfiles.\n" \
	    "$archive" "$board" 1>&2

	xtype="patched" && [ "$nukemode" = "nuke" ] && xtype="nuked"
	[ "$xchanged" != "y" ] && \
		printf "\nRelease archive '%s' was *NOT* modified.\n" \
		    "$archive" && [ "$has_hashes" = "y" ] && \
		    printf "WARNING: '%s' contains '%s'. DO NOT FLASH!\n" \
		    "$archive" "$hashfile" 1>&2 && \
		    printf "(vendorfiles may be needed and aren't there)\n" \
		    1>&2
	[ "$xchanged" = "y" ] && \
		printf "\nRelease archive '%s' successfully %s.\n" \
		    "$archive" "$xtype" && [ "$nukemode" != "nuke" ] && \
		printf "You may now extract '%s' and flash images from it.\n" \
		    "$archive"
	[ "$xchanged" = "y" ] && [ "$nukemode" = "nuke" ] && \
		printf "WARNING! Vendorfiles *removed*. DO NOT FLASH.\n" 1>&2 \
		    && printf "DO NOT flash images from '%s'\n" \
		    "$archive" 1>&2

	[ "$need_files" = "n" ] && printf \
	    "Board '%s' doesn't use vendorfiles, so none were inserted.\n" \
	    "$board"

	if [ "$xchanged" != "y" ] && [ "$need_files" = "y" ] && \
	    [ "$nukemode" = "nuke" ] && [ "$has_hashes" != "y" ]; then
		printf "FAILED NUKE: tarball '$archive', board '$board'\n" 1>&2
		$err "Unhandled vendorfile deletion: DO NOT RELEASE TO RSYNC"
	fi

	err="$_olderr"; :
}

check_release()
{
	[ -L "$archive" ] && \
	    $err "'$archive' is a symlink, not a file - $dontflash"
	e "$archive" f missing && return 1

	archivename="`basename "$archive"`"
	[ -z "$archivename" ] && \
	    $err "Cannot determine archive file name - $dontflash"

	case "$archivename" in
	*_src.tar.xz)
		$err "'$archive' is a src archive, silly!" ;;
	grub_*|seagrub_*|custom_*|seauboot_*|seabios_withgrub_*)
		return 1 ;;
	*.tar.xz) _stripped_prefix="${archivename#*_}"
		board="${_stripped_prefix%.tar.xz}" ;;
	*) $err "'$archive': could not detect board type - $dontflash"
	esac; :
}

readcfg()
{
	if [ "$board" = "serprog_rp2040" ] || [ "$board" = "serprog_stm32" ] \
	    || [ "$board" = "serprog_pico" ]; then
		return 1
	fi
	boarddir="$cbcfgsdir/$board"

	eval "`setcfg "$boarddir/target.cfg"`"
	chkvars tree
	x_ ./mk -d coreboot "$tree" # even if vendorfiles not used, see: setmac

	[ -z "$vcfg" ] && return 1

	vfile="config/vendor/$vcfg/pkg.cfg"
	[ -L "$vfile" ] && $err "'$archive', '$board': $vfile is a symlink"
	[ -f "$vfile" ] || $err "'$archive', '$board': $vfile doesn't exist"

	cbdir="src/coreboot/$tree"
	cbfstool="elf/cbfstool/$tree/cbfstool"
	rmodtool="elf/cbfstool/$tree/rmodtool"
	mecleaner="$xbmkpwd/$cbdir/util/me_cleaner/me_cleaner.py"
	kbc1126_ec_dump="$xbmkpwd/$cbdir/util/kbc1126/kbc1126_ec_dump"
	cbfstool="elf/cbfstool/$tree/cbfstool"
	ifdtool="elf/ifdtool/$tree/ifdtool"
	[ -n "$IFD_platform" ] && ifdprefix="-p $IFD_platform"; :
}

patch_release_roms()
{
	has_hashes="n"

	tmpromdir="tmp/DO_NOT_FLASH/bin/$board"
	remkdir "${tmpromdir%"/bin/$board"}"
	x_ tar -xf "$archive" -C "${tmpromdir%"/bin/$board"}"

	for _hashes in $hashfiles; do
		[ "$need_files" = "y" ] || break
		e "$tmpromdir/$_hashes" f && has_hashes="y" && \
		    hashfile="$_hashes" && break; :
	done

	x_ mkdir -p "tmp"
	[ -L "tmp/rom.list" ] && \
	    $err "'$archive' -> tmp/rom.list is a symlink - $dontflash"
	x_ rm -f "tmp/rom.list" "tmp/zero.1b"
	x_ dd if=/dev/zero of=tmp/zero.1b bs=1 count=1

	find "$tmpromdir" -maxdepth 1 -type f -name "*.rom" > "tmp/rom.list" \
	    || $err "'$archive' -> Can't make tmp/rom.list - $dontflash"

	if readkconfig; then
		while read -r _xrom ; do
			process_release_rom "$_xrom" || break
		done < "tmp/rom.list"
		rm -f "$tmpromdir/README.md" || :
		[ "$nukemode" != "nuke" ] || \
		    printf "Make sure you inserted vendor files: %s\n" \
		    "$vguide" > "$tmpromdir/README.md" || :
	else
		printf "Skipping vendorfiles on '%s'\n" "$archive" 1>&2
		need_files="n"
	fi

	(
	[ "$need_files" = "y" ] || exit 0
	cd "$tmpromdir" || $err "patch '$archive': can't cd $tmpromdir"
	# NOTE: For compatibility with older rom releases, defer to sha1
	if [ "$has_hashes" = "y" ] && [ "$nukemode" != "nuke" ]; then
		sha512sum --status -c "$hashfile" || \
		    sha1sum --status -c "$hashfile" || \
		    $err "'$archive' -> Can't verify vendor hashes. $dontflash"
		x_ rm -f "$hashfile"
	fi
	) || $err "'$archive' -> Can't verify vendor hashes. $dontflash"

	[ -z "$new_mac" ] || modify_mac_addresses || printf \
	    "\nNo GbE region defined for '%s'\n" "$board" 1>&2

	[ "$xchanged" = "y" ] || rm -Rf "$tmpromdel" || :
	[ "$xchanged" = "y" ] || return 0
	(
		x_ cd "${tmpromdir%"/bin/$board"}"
		mkrom_tarball "bin/$board"
	) || $err "Cannot re-generate '$archive' - $dontflash"

	mv "${tmpromdir%"/bin/$board"}/bin/${relname}_${board}.tar.xz" \
	    "$archive" || $err "'$archive' -> Can't overwrite - $dontflash"; :
}

process_release_rom()
{
	_xrom="$1"
	_xromname="${1##*/}"

	e "$_xrom" f missing && return 0

	[ -z "${_xromname#"$vfix"}" ] && \
	    $err "'$_xromname'->'"${_xromname#"$vfix"}"' empty. $dontflash"

	# Remove the prefix and 1-byte pad
	if [ "$nukemode" != "nuke" ] && \
	    [ "${_xromname#"$vfix"}" != "$_xromname" ]; then
		_xromnew="${_xrom%/*}/${_xromname#"$vfix"}"

		stat -c '%s' "$_xrom" > "tmp/rom.size" || $err "!resize $_xrom"
		read -r xromsize < "tmp/rom.size" || $err "!readsize $_xrom"
		expr "X$xromsize" : "X-\{0,1\}[0123456789][0123456789]*$" \
		    1>/dev/null 2>/dev/null || $err "$_xrom size non-integer"

		xromsize="`expr $xromsize - 1`"
		[ $xromsize -lt 524288 ] && $err "too small, $xromsize: $_xrom"

		x_ dd if="$_xrom" of="$_xromnew" bs=$xromsize count=1
		rm -f "$_xrom" || $err "Can't rm $_xrom - $dontflash"

		_xrom="$_xromnew"
	fi

	[ "$nukemode" = "nuke" ] && \
		mksha512sum "$_xrom" "vendorhashes"

	patch_rom "$_xrom" || return 1 # if break return, can still change MAC
	[ "$nukemode" != "nuke" ] && return 0

	# Rename the file, prefixing a warning saying not to flash
	cat "$_xrom" tmp/zero.1b > "${_xrom%/*}/$vfix${_xrom##*/}" || \
	    $err "'$archive' -> can't pad/rename '$_xrom'. $dontflash"
	x_ rm -f "$_xrom"
}

patch_rom()
{
	rom="$1"

	if [ "$has_hashes" != "y" ] && [ "$nukemode" != "nuke" ]; then
		printf "'%s' has no hash file. Skipping.\n" "$archive" 1>&2
		return 1
	elif [ "$has_hashes" = "y" ] && [ "$nukemode" = "nuke" ]; then
		printf "'%s' has a hash file. Skipping nuke.\n" "$archive" 1>&2
		return 1
	fi

	[ -n "$CONFIG_HAVE_REFCODE_BLOB" ] && inject "fallback/refcode" \
	    "$CONFIG_REFCODE_BLOB_FILE" "stage"
	[ "$CONFIG_HAVE_MRC" = "y" ] && inject "mrc.bin" "$CONFIG_MRC_FILE" \
	    "mrc" "0xfffa0000"
	[ "$CONFIG_HAVE_ME_BIN" = "y" ] && inject IFD "$CONFIG_ME_BIN_PATH" me
	[ "$CONFIG_KBC1126_FIRMWARE" = "y" ] && inject ecfw1.bin \
	    "$CONFIG_KBC1126_FW1" raw "$CONFIG_KBC1126_FW1_OFFSET" && inject \
	    ecfw2.bin "$CONFIG_KBC1126_FW2" raw "$CONFIG_KBC1126_FW2_OFFSET"
	[ -n "$CONFIG_VGA_BIOS_FILE" ] && [ -n "$CONFIG_VGA_BIOS_ID" ] && \
	  inject "pci$CONFIG_VGA_BIOS_ID.rom" "$CONFIG_VGA_BIOS_FILE" optionrom
	[ "$CONFIG_INCLUDE_SMSC_SCH5545_EC_FW" = "y" ] && \
	    [ -n "$CONFIG_SMSC_SCH5545_EC_FW_FILE" ] && \
		inject sch5545_ecfw.bin "$CONFIG_SMSC_SCH5545_EC_FW_FILE" raw
	[ -z "$CONFIG_FSP_USE_REPO" ] && [ -z "$CONFIG_FSP_FULL_FD" ] && \
	   [ -n "$CONFIG_FSP_M_FILE" ] && \
		inject "$CONFIG_FSP_M_CBFS" "$CONFIG_FSP_M_FILE" fsp --xip
	[ -z "$CONFIG_FSP_USE_REPO" ] && [ -z "$CONFIG_FSP_FULL_FD" ] && \
	   [ -n "$CONFIG_FSP_S_FILE" ] && \
		inject "$CONFIG_FSP_S_CBFS" "$CONFIG_FSP_S_FILE" fsp

	printf "ROM image successfully patched: %s\n" "$rom"
	xchanged="y"
}

inject()
{
	[ $# -lt 3 ] && $err "bad command: $*, $rom"
	[ "$2" = "/dev/null" ] && return 0

	cbfsname="$1"
	_dest="${2##*../}"
	_t="$3"

	_offset=""

	if [ "$_t" = "fsp" ]; then
		[ $# -gt 3 ] && _offset="$4"
	else
		[ $# -gt 3 ] && _offset="-b $4" && [ -z "$4" ] && \
		    $err "inject $*, $rom: offset given but empty (undefined)"
	fi

	[ "$nukemode" = "nuke" ] || x_ e "$_dest" f

	if [ "$cbfsname" = "IFD" ]; then
		[ "$nukemode" = "nuke" ] || "$ifdtool" $ifdprefix -i \
		    $_t:$_dest "$rom" -O "$rom" || \
		    $err "failed: inject '$_t' '$_dest' on '$rom'"
		[ "$nukemode" != "nuke" ] || "$ifdtool" $ifdprefix --nuke $_t \
		    "$rom" -O "$rom" || $err "$rom: !nuke IFD/$_t"
	elif [ "$nukemode" = "nuke" ]; then
		x_ "$cbfstool" "$rom" remove -n "$cbfsname"
	elif [ "$_t" = "stage" ]; then # the only stage we handle is refcode
		x_ mkdir -p tmp
		x_ rm -f "tmp/refcode"
		"$rmodtool" -i "$_dest" -o "tmp/refcode" || "!reloc refcode"
		"$cbfstool" "$rom" add-stage -f "tmp/refcode" -n "$cbfsname" \
		    -t stage || $err "$rom: !add ref"
	else
		"$cbfstool" "$rom" add -f "$_dest" -n "$cbfsname" \
		    -t $_t $_offset || $err "$rom !add $_t ($_dest)"
	fi
	xchanged="y"; :
}

modify_mac_addresses()
{
	[ -n "$CONFIG_GBE_BIN_PATH" ] || return 1

	x_ mkdir -p tmp && x_ cp "${CONFIG_GBE_BIN_PATH##*../}" tmp/gbe

	if [ "$new_mac" != "restore" ]; then
		x_ make -C util/nvmutil
		x_ "$nvm" tmp/gbe setmac "$new_mac"
	fi

	find "$tmpromdir" -maxdepth 1 -type f -name "*.rom" > "tmp/rom.list" \
	    || $err "'$archive' -> Can't make tmp/rom.list - $dontflash"

	while read -r _xrom; do
		e "$_xrom" f && xchanged="y" && x_ \
		    "$ifdtool" $ifdprefix -i GbE:tmp/gbe "$_xrom" -O "$_xrom"
	done < "tmp/rom.list"

	printf "\nGbE NVM written to '%s':\n" "$archive"
	x_ "$nvm" tmp/gbe dump | grep -v "bytes read from file" || :

	[ "$new_mac" = "restore" ] && \
		printf "\nDefault GbE file '%s' written, unmodified.\n" \
		    "${CONFIG_GBE_BIN_PATH##*../}"; :
}
