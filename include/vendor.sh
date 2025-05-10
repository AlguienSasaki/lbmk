# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

# These are variables and functions, extending the functionality of
# inject.sh, to be used with lbmk; they are kept separate here, so that
# the main inject.sh can be as similar as possible between lbmk and cbmk,
# so that cherry-picking lbmk patches into cbmk yields fewer merge conflicts.

# When reading this file, you should imagine that it is part of inject.sh,
# with inject.sh concatenated onto vendor.sh; they are inexorably intertwined.
# The main "mk" script sources vendor.sh first, and then inject.sh, in lbmk.

e6400_unpack="$xbmkpwd/src/bios_extract/dell_inspiron_1100_unpacker.py"
me7updateparser="$xbmkpwd/util/me7_update_parser/me7_update_parser.py"
pfs_extract="$xbmkpwd/src/biosutilities/Dell_PFS_Extract.py"
uefiextract="$xbmkpwd/elf/uefitool/uefiextract"
vendir="vendorfiles"
appdir="$vendir/app"

# lbmk-specific extension to the "cv" variable (not suitable for cbmk)
cvchk="CONFIG_INCLUDE_SMSC_SCH5545_EC_FW CONFIG_HAVE_MRC CONFIG_HAVE_ME_BIN \
    CONFIG_KBC1126_FIRMWARE CONFIG_LENOVO_TBFW_BIN CONFIG_VGA_BIOS_FILE \
    CONFIG_FSP_M_FILE CONFIG_FSP_S_FILE"

# lbmk-specific extensions to the "cv" variable (not suitable for cbmk)
cvxbmk="CONFIG_ME_BIN_PATH CONFIG_SMSC_SCH5545_EC_FW_FILE CONFIG_KBC1126_FW1 \
    CONFIG_KBC1126_FW2 CONFIG_KBC1126_FW1_OFFSET CONFIG_KBC1126_FW2_OFFSET \
    CONFIG_VGA_BIOS_ID CONFIG_BOARD_DELL_E6400 CONFIG_FSP_S_CBFS \
    CONFIG_HAVE_REFCODE_BLOB CONFIG_REFCODE_BLOB_FILE CONFIG_FSP_FD_PATH \
    CONFIG_IFD_BIN_PATH CONFIG_MRC_FILE CONFIG_FSP_M_CBFS"

# lbmk-specific extensions; mostly used for downloading vendor files
eval "`setvars "" has_hashes EC_hash DL_hash DL_url_bkup MRC_refcode_gbe vcfg \
    E6400_VGA_DL_hash E6400_VGA_DL_url E6400_VGA_DL_url_bkup E6400_VGA_offset \
    E6400_VGA_romname SCH5545EC_DL_url_bkup SCH5545EC_DL_hash _dest \
    mecleaner kbc1126_ec_dump MRC_refcode_cbtree _dl SCH5545EC_DL_url \
    EC_url rom DL_url nukemode cbfstoolref FSPFD_hash \
    _7ztest ME11bootguard ME11delta ME11version ME11sku ME11pch \
    _me _metmp mfs TBFW_url_bkup TBFW_url TBFW_hash \
    TBFW_size hashfile xromsize xchanged EC_url_bkup vfile \
    CONFIG_FSP_USE_REPO CONFIG_FSP_FULL_FD`"

download()
{
	[ $# -gt 0 ] || err "No argument given"
	export PATH="$PATH:/sbin"
	board="$1" && check_target && readkconfig && bootstrap && getfiles; :
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
	[ -n "$CONFIG_REFCODE_BLOB_FILE" ] && fetch "refcode" "$MRC_url" \
	    "$MRC_url_bkup" "$MRC_hash" "$CONFIG_REFCODE_BLOB_FILE"
	[ -z "$CONFIG_LENOVO_TBFW_BIN" ] || fetch "tbfw" "$TBFW_url" \
	    "$TBFW_url_bkup" "$TBFW_hash" "$CONFIG_LENOVO_TBFW_BIN"
	[ -z "$CONFIG_FSP_M_FILE" ] || fetch "fsp" "$CONFIG_FSP_FD_PATH" \
	    "$CONFIG_FSP_FD_PATH" "$FSPFD_hash" "$CONFIG_FSP_M_FILE" copy
	[ -z "$CONFIG_FSP_S_FILE" ] || fetch "fsp" "$CONFIG_FSP_FD_PATH" \
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
	# HACK: if grabbing fsp from coreboot, fix the path for lbmk
	[ "$dl_type" = "fsp" ] && for _cdl in dl dl_bkup; do
		eval "$_cdl=\"\${$_cdl##*../}\"; _cdp=\"\$$_cdl\""
		[ -f "$_cdp" ] || _cdp="$cbdir/$_cdp"
		[ -f "$_cdp" ] && eval "$_cdl=\"$_cdp\""
	done; :

	dlop="curl" && [ $# -gt 5 ] && dlop="$6"
	xbmkget "$dl" "$dl_bkup" "$_dl" "$dlsum" "$dlop"

	x_ rm -Rf "${_dl}_extracted"
	e "$_dest" f && return 0

	x_ mkdir -p "${_dest%/*}"
	remkdir "$appdir"
	[ "$dl_type" = "fsp" ] || extract_archive "$_dl" "$appdir" || \
	    [ "$dl_type" = "e6400vga" ] || err "$_dest $dl_type: !extract"

	x_ extract_$dl_type "$_dl" "$appdir"
	set -u -e
	e "$_dest" f missing && err "!extract_$dl_type. $dontflash"; :
}

extract_intel_me()
{
	e "$mecleaner" f not && err "$cbdir: me_cleaner missing. $dontflash"

	_7ztest="$xbmklocal/metmp/a"
	_me="$xbmkpwd/$_dest"
	_metmp="$xbmklocal/me.bin"
	x_ rm -f "$_metmp" "$xbmklocal/a"

	mfs="" && [ "$ME11bootguard" = "y" ] && mfs="--whitelist MFS" && \
	    chkvars ME11delta ME11version ME11sku ME11pch
	[ "$ME11bootguard" = "y" ] && x_ ./mk -f deguard

	set +u +e
	x_ rm -Rf "$xbmkpwd/metmp"
	( fx_ find_me x_ find "$xbmkpwd/$appdir" -type f ) || :
	[ "$ME11bootguard" != "y" ] && x_ mv "$_metmp" "$_me" && return 0

	(
	x_ cd src/deguard/
	x_ ./finalimage.py --delta "data/delta/$ME11delta" --version \
	    "$ME11version" --pch "$ME11pch" --sku "$ME11sku" \
	    --fake-fpfs data/fpfs/zero --input "$_metmp" --output "$_me"
	) || err "Error running deguard for $_me - $dontflash"; :
}

find_me()
{
	[ -f "$_metmp" ] && exit 1
	[ -L "$1" ] && return 0

	_7ztest="${_7ztest}a" && _r="-r" && [ -n "$mfs" ] && _r=""

	"$mecleaner" $mfs $_r -t -O "$xbmklocal/a" -M "$_metmp" "$1" || \
	    "$mecleaner" $mfs $_r -t -O "$_metmp" "$1" || "$me7updateparser" \
	    -O "$_metmp" "$1" || extract_archive "$1" "$_7ztest" || return 0

	[ -f "$_metmp" ] && exit 1
	( fx_ find_me x_ find "$_7ztest" -type f ) || exit 1; :
}

extract_archive()
{
	innoextract "$1" -d "$2" || python "$pfs_extract" "$1" -e || 7z x \
	    "$1" -o"$2" || unar "$1" -o "$2" || unzip "$1" -d "$2" || return 1

	[ ! -d "${_dl}_extracted" ] || x_ cp -R "${_dl}_extracted" "$2"; :
}

extract_kbc1126ec()
{
	(
	x_ cd "$appdir/"
	mv Rompaq/68*.BIN ec.bin || unar -D ROM.CAB Rom.bin || unar -D \
	    Rom.CAB Rom.bin || unar -D 68*.CAB Rom.bin || err "!kbc1126 unar"
	[ -f "ec.bin" ] || x_ mv Rom.bin ec.bin
	x_ e ec.bin f && x_ "$kbc1126_ec_dump" ec.bin
	) || err "$board: can't extract kbc1126 ec firmware - $dontflash"

	x_ e "$appdir/ec.bin.fw1" f && x_ e "$appdir/ec.bin.fw2" f
	x_ cp "$appdir/"ec.bin.fw* "${_dest%/*}/"
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
	) || err "can't extract e6400 vga rom - $dontflosh"
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

	x_ "$uefiextract" "$_bios"
	x_ cp "$_sch5545ec_fw" "$_dest"
}

# Lenovo ThunderBolt firmware updates:
# https://pcsupport.lenovo.com/us/en/products/laptops-and-netbooks/thinkpad-t-series-laptops/thinkpad-t480-type-20l5-20l6/20l5/solutions/ht508988
extract_tbfw()
{
	chkvars TBFW_size; fx_ copytb x_ find "$appdir" -type f -name "TBT.bin"
}

copytb()
{
	[ -f "$1" ] && [ ! -L "$1" ] && x_ dd if=/dev/null of="$1" bs=1 \
	    seek=$TBFW_size && x_ mv "$1" "$_dest" && return 1; :
}

extract_fsp()
{
	x_ python "$cbdir/3rdparty/fsp/Tools/SplitFspBin.py" split -f "$1" \
	    -o "${_dest%/*}" -n "Fsp.fd"
}

check_vcfg()
{
	vfile="config/vendor/$vcfg/pkg.cfg"
	[ -L "$vfile" ] && err "'$archive', '$board': $vfile is a symlink"; :
	[ -f "$vfile" ] || err "'$archive', '$board': $vfile missing"; :
}

setvfile()
{
	[ -n "$vfile" ] && for c in $cvchk; do
		eval "[ \"\${$c}\" = \"/dev/null\" ] && continue"
		eval "[ -z \"\${$c}\" ] && continue"
		eval "`setcfg "$vfile"`"
		return 1
	done && return 0; return 1
}

prep()
{
	_xrom="$1"
	_xromname="${1##*/}"
	_xromnew="${_xrom%/*}/${_xromname#"$vfix"}"
	[ "$nukemode" = "nuke" ] && _xromnew="${_xrom%/*}/$vfix${_xrom##*/}"

	e "$_xrom" f missing && return 0
	[ -z "${_xromname#"$vfix"}" ] && err "$_xromname / $vfix: name match"

	# Remove the prefix and 1-byte pad
	if [ "$nukemode" != "nuke" ] && \
	    [ "${_xromname#"$vfix"}" != "$_xromname" ]; then
		xromsize="$(expr $(stat -c '%s' "$_xrom") - 1)" || err "!int"
		[ $xromsize -lt 524288 ] && err "too small, $xromsize: $_xrom"

		x_ dd if="$_xrom" of="$_xromnew" bs=$xromsize count=1
		x_ rm -f "$_xrom"

		_xrom="$_xromnew"
	fi

	[ "$nukemode" = "nuke" ] && mksha512sum "$_xrom" "vendorhashes"

	add_vfiles "$_xrom" || return 1 # if break return, can still change MAC
	[ "$nukemode" != "nuke" ] && return 0

	# Rename the file, prefixing a warning saying not to flash
	cat "$_xrom" config/data/coreboot/0 > "$_xromnew" || err "!pad $_xrom"
	x_ rm -f "$_xrom"
}

add_vfiles()
{
	rom="$1"

	if [ "$has_hashes" != "y" ] && [ "$nukemode" != "nuke" ]; then
		printf "'%s' has no hash file. Skipping.\n" "$archive" 1>&2
		return 1
	elif [ "$has_hashes" = "y" ] && [ "$nukemode" = "nuke" ]; then
		printf "'%s' has a hash file. Skipping nuke.\n" "$archive" 1>&2
		return 1
	fi

	[ -n "$CONFIG_HAVE_REFCODE_BLOB" ] && vfile "fallback/refcode" \
	    "$CONFIG_REFCODE_BLOB_FILE" "stage"
	[ "$CONFIG_HAVE_MRC" = "y" ] && vfile "mrc.bin" "$CONFIG_MRC_FILE" \
	    "mrc" "0xfffa0000"
	[ "$CONFIG_HAVE_ME_BIN" = "y" ] && vfile IFD "$CONFIG_ME_BIN_PATH" me
	[ "$CONFIG_KBC1126_FIRMWARE" = "y" ] && vfile ecfw1.bin \
	    "$CONFIG_KBC1126_FW1" raw "$CONFIG_KBC1126_FW1_OFFSET" && vfile \
	    ecfw2.bin "$CONFIG_KBC1126_FW2" raw "$CONFIG_KBC1126_FW2_OFFSET"
	[ -n "$CONFIG_VGA_BIOS_FILE" ] && [ -n "$CONFIG_VGA_BIOS_ID" ] && \
	  vfile "pci$CONFIG_VGA_BIOS_ID.rom" "$CONFIG_VGA_BIOS_FILE" optionrom
	[ "$CONFIG_INCLUDE_SMSC_SCH5545_EC_FW" = "y" ] && \
	    [ -n "$CONFIG_SMSC_SCH5545_EC_FW_FILE" ] && \
		vfile sch5545_ecfw.bin "$CONFIG_SMSC_SCH5545_EC_FW_FILE" raw
	[ -z "$CONFIG_FSP_USE_REPO" ] && [ -z "$CONFIG_FSP_FULL_FD" ] && \
	   [ -n "$CONFIG_FSP_M_FILE" ] && \
		vfile "$CONFIG_FSP_M_CBFS" "$CONFIG_FSP_M_FILE" fsp --xip
	[ -z "$CONFIG_FSP_USE_REPO" ] && [ -z "$CONFIG_FSP_FULL_FD" ] && \
	   [ -n "$CONFIG_FSP_S_FILE" ] && \
		vfile "$CONFIG_FSP_S_CBFS" "$CONFIG_FSP_S_FILE" fsp

	printf "ROM image successfully patched: %s\n" "$rom"
	xchanged="y"
}

vfile()
{
	[ "$2" = "/dev/null" ] && return 0

	cbfsname="$1"
	_dest="${2##*../}"
	_t="$3"

	_offset=""

	if [ "$_t" = "fsp" ] && [ $# -gt 3 ]; then
		_offset="$4"
	elif [ $# -gt 3 ] && _offset="-b $4" && [ -z "$4" ]; then
		err "vfile $*, $rom: offset given but empty (undefined)"
	fi

	[ "$nukemode" = "nuke" ] || x_ e "$_dest" f

	if [ "$cbfsname" = "IFD" ]; then
		[ "$nukemode" = "nuke" ] || x_ "$ifdtool" $ifdprefix -i \
		    $_t:$_dest "$rom" -O "$rom"
		[ "$nukemode" != "nuke" ] || x_ "$ifdtool" $ifdprefix --nuke \
		    $_t "$rom" -O "$rom"
	elif [ "$nukemode" = "nuke" ]; then
		x_ "$cbfstool" "$rom" remove -n "$cbfsname"
	elif [ "$_t" = "stage" ]; then # the only stage we handle is refcode
		x_ rm -f "$xbmklocal/refcode"
		x_ "$rmodtool" -i "$_dest" -o "$xbmklocal/refcode"
		x_ "$cbfstool" "$rom" add-stage -f "$xbmklocal/refcode" \
		    -n "$cbfsname" -t stage
	else
		x_ "$cbfstool" "$rom" add -f "$_dest" -n "$cbfsname" \
		    -t $_t $_offset
	fi
	xchanged="y"; :
}

check_vendor_hashes()
{
	x_ cd "$tmpromdir"
	[ "$has_hashes" = "n" ] || [ "$nukemode" = "nuke" ] || sha512sum \
	    --status -c "$hashfile" || x_ sha1sum --status -c "$hashfile"
	x_ rm -f "$hashfile"
}
