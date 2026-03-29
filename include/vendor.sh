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
bsdtar="$xbmkpwd/elf/libarchive/bsdtar"
bsdunzip="$xbmkpwd/elf/libarchive/bsdunzip"
vendir="vendorfiles"
appdir="$vendir/app"
vfix="DO_NOT_FLASH_YET._FIRST,_INJECT_FILES_VIA_INSTRUCTIONS_ON_LIBREBOOT.ORG_"

# lbmk-specific extension to the "checkvars" variable (not suitable for cbmk)
checkvarschk="CONFIG_INCLUDE_SMSC_SCH5545_EC_FW CONFIG_HAVE_MRC \
    CONFIG_HAVE_ME_BIN CONFIG_LENOVO_TBFW_BIN CONFIG_VGA_BIOS_FILE \
    CONFIG_FSP_M_FILE CONFIG_FSP_S_FILE CONFIG_KBC1126_FW1 CONFIG_KBC1126_FW2"

# lbmk-specific extensions to the "checkvars" variable (not suitable for cbmk)
checkvarsxbmk="CONFIG_ME_BIN_PATH CONFIG_SMSC_SCH5545_EC_FW_FILE \
    CONFIG_FSP_FULL_FD CONFIG_KBC1126_FW1_OFFSET CONFIG_KBC1126_FW2_OFFSET \
    CONFIG_FSP_USE_REPO CONFIG_VGA_BIOS_ID CONFIG_BOARD_DELL_E6400 \
    CONFIG_FSP_S_CBFS CONFIG_HAVE_REFCODE_BLOB CONFIG_REFCODE_BLOB_FILE \
    CONFIG_FSP_FD_PATH CONFIG_IFD_BIN_PATH CONFIG_MRC_FILE CONFIG_FSP_M_CBFS"

# lbmk-specific extensions; general variables
cbdir=""
cbfstoolref=""
has_hashes=""
hashfile=""
kbc1126_ec_dump=""
mecleaner=""
mfs=""
nuke=""
rom=""
vcfg=""
xromsize=""

_7ztest=""
_dest=""
_dl=""
_dl_bin=""
_me=""
_metmp=""
_pre_dest=""

# lbmk-specific extensions; declared in pkg.cfg files in config/vendor/
DL_hash=""
DL_url=""
DL_url_bkup=""
E6400_VGA_bin_hash=""
E6400_VGA_DL_hash=""
E6400_VGA_DL_url=""
E6400_VGA_DL_url_bkup=""
E6400_VGA_offset=""
E6400_VGA_romname=""
EC_FW1_hash=""
EC_FW2_hash=""
EC_hash=""
EC_url=""
EC_url_bkup=""
FSPFD_hash=""
FSPM_bin_hash=""
FSPS_bin_hash=""
ME11bootguard=""
ME11delta=""
ME11pch=""
ME11sku=""
ME11version=""
ME_bin_hash=""
MEcheck=""
MEclean=""
MEshrink=""
MRC_bin_hash=""
MRC_refcode_cbtree=""
MRC_refcode_gbe=""
REF_bin_hash=""
SCH5545EC_bin_hash=""
SCH5545EC_DL_hash=""
SCH5545EC_DL_url=""
SCH5545EC_DL_url_bkup=""
TBFW_bin_hash=""
TBFW_hash=""
TBFW_size=""
TBFW_url=""
TBFW_url_bkup=""
XBMKmecleaner=""

download()
{
	if [ $# -lt 1 ]; then
		err "No argument given" "download" "$@"
	fi

	export PATH="$PATH:/sbin"
	board="$1"

	if check_target; then
		readkconfig download
	fi
}

getfiles()
{
	if [ -n "$CONFIG_HAVE_ME_BIN" ];then
		fetch intel_me "$DL_url" "$DL_url_bkup" "$DL_hash" \
		    "$CONFIG_ME_BIN_PATH" curl "$ME_bin_hash"
	fi
	if [ -n "$CONFIG_INCLUDE_SMSC_SCH5545_EC_FW" ]; then
		fetch sch5545ec "$SCH5545EC_DL_url" "$SCH5545EC_DL_url_bkup" \
		    "$SCH5545EC_DL_hash" "$CONFIG_SMSC_SCH5545_EC_FW_FILE" \
		    "curl" "$SCH5545EC_bin_hash"
	fi
	if [ -n "$CONFIG_KBC1126_FW1" ]; then
		fetch kbc1126ec "$EC_url" "$EC_url_bkup" "$EC_hash" \
		    "$CONFIG_KBC1126_FW1" curl "$EC_FW1_hash"
	fi
	if [ -n "$CONFIG_KBC1126_FW2" ]; then
		fetch kbc1126ec "$EC_url" "$EC_url_bkup" "$EC_hash" \
		    "$CONFIG_KBC1126_FW2" curl "$EC_FW2_hash"
	fi
	if [ -n "$CONFIG_VGA_BIOS_FILE" ]; then
		fetch e6400vga "$E6400_VGA_DL_url" "$E6400_VGA_DL_url_bkup" \
		    "$E6400_VGA_DL_hash" "$CONFIG_VGA_BIOS_FILE" "curl" \
		    "$E6400_VGA_bin_hash"
	fi
	if [ -n "$CONFIG_HAVE_MRC" ]; then
		fetch "mrc" "$MRC_url" "$MRC_url_bkup" "$MRC_hash" \
		    "$CONFIG_MRC_FILE" "curl" "$MRC_bin_hash"
	fi
	if [ -n "$CONFIG_REFCODE_BLOB_FILE" ]; then
		fetch "refcode" "$MRC_url" "$MRC_url_bkup" "$MRC_hash" \
		    "$CONFIG_REFCODE_BLOB_FILE" "curl" "$REF_bin_hash"
	fi
	if [ -n "$CONFIG_LENOVO_TBFW_BIN" ]; then
		fetch "tbfw" "$TBFW_url" "$TBFW_url_bkup" "$TBFW_hash" \
		    "$CONFIG_LENOVO_TBFW_BIN" "curl" "$TBFW_bin_hash"
	fi
	if [ -n "$CONFIG_FSP_M_FILE" ]; then
		fetch "fsp" "$CONFIG_FSP_FD_PATH" "$CONFIG_FSP_FD_PATH" \
		    "$FSPFD_hash" "$CONFIG_FSP_M_FILE" "copy" "$FSPM_bin_hash"
	fi
	if [ -n "$CONFIG_FSP_S_FILE" ]; then
		fetch "fsp" "$CONFIG_FSP_FD_PATH" "$CONFIG_FSP_FD_PATH" \
		    "$FSPFD_hash" "$CONFIG_FSP_S_FILE" "copy" "$FSPS_bin_hash"
	fi
}

fetch()
{
	dl_type="$1"
	dl="$2"
	dl_bkup="$3"
	dlsum="$4"
	_dest="${5##*../}"
	_pre_dest="$XBMK_CACHE/tmpdl/check"
	dlop="$6"
	binsum="$7"

	if [ -z "$binsum" ]; then
		err "binsum is empty (no checksum)" "fetch" "$@"
	fi

	_dl="$XBMK_CACHE/file/$dlsum" # internet file to extract from e.g. .exe
	_dl_bin="$XBMK_CACHE/file/$binsum" # extracted file e.g. me.bin

	if [ "$5" = "/dev/null" ]; then
		return 0
	fi

	# an extracted vendor file will be placed in pre_dest first, for
	# verifying its checksum. if it matches, it is later moved to _dest
	remkdir "${_pre_dest%/*}" "$appdir"

	# HACK: if grabbing fsp from coreboot, fix the path for lbmk
	if [ "$dl_type" = "fsp" ]
	then
		dl="${dl##*../}"
		_cdp="$dl"

		if [ ! -f "$_cdp" ]; then
			_cdp="$cbdir/$_cdp"
		fi
		if [ -f "$_cdp" ]; then
			dl="$_cdp"
		fi

		dl_bkup="${dl_bkup##*../}"
		_cdp="$dl_bkup"

		if [ ! -f "$_cdp" ]; then
			_cdp="$cbdir/$_cdp"
		fi
		if [ -f "$_cdp" ]; then
			dl_bkup="$_cdp"; :
		fi
	fi

	# download the file (from the internet) to extract from:

	xbget "$dlop" "$dl" "$dl_bkup" "$_dl" "$dlsum"
	x_ rm -Rf "${_dl}_extracted"

	# skip extraction if a cached extracted file exists:

	( xbget copy "$_dl_bin" "$_dl_bin" "$_dest" "$binsum" 2>/dev/null ) || :
	if [ -f "$_dest" ]; then
		return 0
	fi

	x_ xbmkdir "${_dest%/*}"

	if [ "$dl_type" != "fsp" ]; then
		extract_archive "$_dl" "$appdir" || \
		    [ "$dl_type" = "e6400vga" ] || \
		    err "$_dest $dl_type: !extract" "fetch" "$@"
	fi

	x_ extract_$dl_type "$_dl" "$appdir"
	set -u -e

	# some functions don't output directly to the given file, _pre_dest.
	# instead, they put multiple files there, but we need the one matching
	# the given hashsum. So, search for a matching file via bruteforce:
	( fx_ "mkdst $binsum" x_ find "${_pre_dest%/*}" -type f ) || :

	if ! bad_checksum "$binsum" "$_dest"; then
		if [ -f "$_dest" ]; then
			return 0
		fi
	fi

	if [ -z "$binsum" ]; then
		printf "'%s': checksum undefined\n" "$_dest" 1>&2
	fi

	if [ -L "$_dest" ]; then
		printf "WARNING: '%s' is a link!\n" "$_dest" 1>&2
	else
		x_ rm -f "$_dest"
	fi

	err "Can't safely extract '$_dest', for board '$board'" "fetch" "$@"
}

mkdst()
{
	if bad_checksum "$1" "$2" 2>/dev/null; then
		x_ rm -f "$2"
	else
		x_ mv "$2" "$_dl_bin"
		x_ cp "$_dl_bin" "$_dest"

		exit 1
	fi
}

extract_intel_me()
{
	if e "$mecleaner" f missing; then
		err "$cbdir: me_cleaner missing" "extract_intel_me" "$@"
	fi

	mfs=""
	_7ztest="$xbtmp/metmp/a"
	_metmp="$xbtmp/me.bin"

	x_ rm -f "$_metmp" "$xbtmp/a"
	x_ rm -Rf "$_7ztest"

	# maintain compatibility with older configs
	# because in the past, shrink was assumed
	if [ -z "$MEshrink" ]; then
		MEshrink="y"
	fi
	if [ "$MEshrink" != "y" ] && [ "$MEshrink" != "n" ]; then
		err "MEshrink set badly on '$board' vendor config"
	fi

	if [ "$ME11bootguard" = "y" ]; then
		if [ -z "$ME11delta" ] || [ -z "$ME11version" ] || \
		    [ -z "$ME11sku" ] || [ -z "$ME11pch" ]; then
			err "$board: ME11delta/ME11version/ME11sku/ME11pch" \
			    "extract_intel_me" "$@"
		fi

		x_ ./mk -f deguard
	fi

	set +u +e

	( fx_ find_me x_ find "$xbmkpwd/$appdir" -type f ) || :; :

	set -u -e

	if [ "$ME11bootguard" != "y" ]; then
		x_ mv "$_metmp" "$_pre_dest"
	else
		( apply_deguard_hack ) || \
		    err "deguard error on '$_dest'" "extract_intel_me" "$@"; :
	fi
}

# bruteforce Intel ME extraction.
# must be called inside a subshell.
find_me()
{
	if [ -f "$_metmp" ]; then
		# we found me.bin, so we stop searching

		exit 1
	elif [ -L "$1" ]; then
		return 0
	fi

	_7ztest="${_7ztest}a"

	_keep="" # -k: keep fptr modules even if they can be removed
	_pass="" # -p: skip fptr check
	_r="-r" # re-locate modules
	_trunc="-t" # -t: truncate the ME size

	if [ "$ME11bootguard" = "y" ]; then
		mfs="--whitelist MFS"
	fi
	if [ "$MEclean" = "n" ]; then
		MEshrink="n"

		_keep="-k" # keep ME modules, don't delete anything
		mfs="" # no MFS whitelist needed, due to -r:
	fi
	if [ "$MEclean" = "n" ] || [ "$MEshrink" != "y" ]; then
		# MEclean can still be y, this just means don't shrink,
		# so deleted modules would become padded space. this
		# could also be used alongside --whitelist, if
		# MEclean is otherwise enabled.

		_r="" # don't re-locate ME modules
		_trunc="" # don't shrink the me.bin file size
	fi
	if [ "$MEcheck" = "n" ]; then
		_pass="-p" # skip fptr check
	fi
	if [ -n "$mfs" ]; then
		_r="" # cannot re-locate modules if using --whitelist MFS
	fi

	if "$mecleaner" $mfs $_r $_keep $_pass $_trunc -O "$xbtmp/a" \
	    -M "$_metmp" "$1" || [ -f "$_metmp" ]; then
		# me.bin extracted from a full image with ifd, then shrunk
		:
	elif "$mecleaner" $mfs $_r $_pass $_keep $_trunc -O "$_metmp" "$1" || \
	    [ -f "$_metmp" ]; then
		# me.bin image already present, and we shrunk it
		:
	elif "$me7updateparser" $_keep -O "$_metmp" "$1"; then
		# thinkpad sandybridge me.bin image e.g. x220/t420
		:
	elif extract_archive "$1" "$_7ztest"; then
		# scan newly extracted archive within extracted archive
		:
	else
		# could not extract anything, so we'll try the next file
		return 0
	fi

	if [ -f "$_metmp" ]; then
		# we found me.bin, so we stop searching

		exit 1
	else
		# if the subshell does exit 1, we found me.bin, so exit 1
		( fx_ find_me x_ find "$_7ztest" -type f ) || exit 1; :
	fi
}

apply_deguard_hack()
{
	x_ cd src/deguard

	x_ ./finalimage.py --delta "data/delta/$ME11delta" \
	     --version "$ME11version" --pch "$ME11pch" --sku "$ME11sku" \
	    --fake-fpfs data/fpfs/zero --input "$_metmp" --output "$_pre_dest"
}

extract_archive()
{
	if innoextract "$1" -d "$2"; then
		:
	elif python "$pfs_extract" "$1" -e; then
		:
	elif 7z x "$1" -o"$2"; then
		:
	elif "$bsdtar" -C "$2" -xf "$1"; then
		:
	elif "$bsdunzip" "$1" -d "$2"; then
		:
	else
		return 1
	fi

	if [ -d "${_dl}_extracted" ]; then
		x_ cp -R "${_dl}_extracted" "$2"
	fi
}

extract_kbc1126ec()
{
	( extract_kbc1126ec_dump ) || \
	    err "$board: can't extract kbc1126 fw" "extract_kbc1126ec" "$@"

	# throw error if either file is missing
	x_ e "$appdir/ec.bin.fw1" f
	x_ e "$appdir/ec.bin.fw2" f

	x_ cp "$appdir/"ec.bin.fw* "${_pre_dest%/*}/"
}

extract_kbc1126ec_dump()
{
	x_ cd "$appdir/"

	if mv Rompaq/68*.BIN ec.bin; then
		:
	elif unar -D ROM.CAB Rom.bin; then
		:
	elif unar -D Rom.CAB Rom.bin; then
		:
	elif unar -D 68*.CAB Rom.bin; then
		:
	else
		err "!kbc1126 unar" "extract_kbc1126ec" "$@"
	fi

	if [ ! -f "ec.bin" ]; then
		x_ mv Rom.bin ec.bin
	fi

	if x_ e ec.bin f; then
		x_ "$kbc1126_ec_dump" ec.bin
	fi
}

extract_e6400vga()
{
	set +u +e

	if [ -z "$E6400_VGA_offset" ] || [ -z "$E6400_VGA_romname" ]; then
		err "$board: E6400_VGA_romname/E6400_VGA_offset unset" \
		    "extract_e6400vga" "$@"
	fi

	tail -c +$E6400_VGA_offset "$_dl" | gunzip > "$appdir/bios.bin" || :

	(
	x_ cd "$appdir"
	x_ e "bios.bin" f
	"$e6400_unpack" bios.bin || printf "TODO: fix dell extract util\n"
	) || err "can't extract e6400 vga rom" "extract_e6400vga" "$@"

	x_ cp "$appdir/$E6400_VGA_romname" "$_pre_dest"
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
	x_ cp "$_sch5545ec_fw" "$_pre_dest"
}

# Lenovo ThunderBolt firmware updates:
# https://pcsupport.lenovo.com/us/en/products/laptops-and-netbooks/thinkpad-t-series-laptops/thinkpad-t480-type-20l5-20l6/20l5/solutions/ht508988
extract_tbfw()
{
	if [ -z "$TBFW_size" ]; then
		err "$board: TBFW_size unset" "extract_tbfw" "$@"
	fi

	fx_ copytb x_ find "$appdir" -type f -name "TBT.bin"
}

copytb()
{
	if [ -f "$1" ] && [ ! -L "$1" ]; then
		x_ dd if=/dev/null of="$1" bs=1 seek=$TBFW_size
		x_ mv "$1" "$_pre_dest"

		return 1
	fi
}

extract_fsp()
{
	x_ python "$cbdir/3rdparty/fsp/Tools/SplitFspBin.py" split -f "$1" \
	    -o "${_pre_dest%/*}" -n "Fsp.fd"
}

setvfile()
{
	[ -n "$vcfg" ] && for c in $checkvarschk
	do
		do_getvfile="n"
		vcmd="[ \"\${$c}\" != \"/dev/null\" ] && [ -n \"\${$c}\" ]"

		eval "$vcmd && do_getvfile=\"y\""

		if [ "$do_getvfile" = "y" ]; then
			if getvfile "$@"; then
				return 0
			fi
		fi
	done && return 1; :
}

getvfile()
{
	if e "config/vendor/$vcfg/pkg.cfg" f missing; then
		return 1
	fi

	. "config/vendor/$vcfg/pkg.cfg" || \
	    err "Can't read 'config/vendor/$vcfg/pkg.cfg'" "getvfile" "$@"

	bootstrap

	if [ $# -gt 0 ]; then
		# download vendor files

		getfiles
	else
		# inject vendor files

		fx_ prep x_ find "$tmpromdir" -maxdepth 1 -type f -name "*.rom"
		( check_vendor_hashes ) || \
		    err "$archive: Can't verify hashes" "getvfile" "$@"; :
	fi

}

bootstrap()
{
	cbdir="src/coreboot/$tree"
	kbc1126_ec_dump="$xbmkpwd/$cbdir/util/kbc1126/kbc1126_ec_dump"
	cbfstool="elf/coreboot/$tree/cbfstool"
	rmodtool="elf/coreboot/$tree/rmodtool"

	mecleaner="$xbmkpwd/$cbdir/util/me_cleaner/me_cleaner.py"
	if [ "$XBMKmecleaner" = "y" ]; then
		mecleaner="$xbmkpwd/src/me_cleaner/me_cleaner.py"
	fi

	x_ ./mk -f coreboot "${cbdir##*/}"
	x_ ./mk -f me_cleaner

	x_ ./mk -b bios_extract
	x_ ./mk -b biosutilities
	x_ ./mk -b uefitool
	x_ ./mk -b libarchive # for bsdtar and bsdunzip

	if [ -d "${kbc1126_ec_dump%/*}" ]; then
		x_ make -C "$cbdir/util/kbc1126"
	fi

	if [ -n "$MRC_refcode_cbtree" ]; then
		cbfstoolref="elf/coreboot/$MRC_refcode_cbtree/cbfstool"
		x_ ./mk -d coreboot "$MRC_refcode_cbtree"; :
	fi
}

prep()
{
	_xrom="$1"
	_xromname="${1##*/}"
	_xromnew="${_xrom%/*}/${_xromname#"$vfix"}"

	if [ "$nuke" = "nuke" ]; then
		_xromnew="${_xrom%/*}/$vfix${_xrom##*/}"
	fi

	if e "$_xrom" f missing; then
		return 0
	fi

	if [ -z "${_xromname#"$vfix"}" ]; then
		err "$_xromname / $vfix: name match" "prep" "$@"
	fi

	# Remove the prefix and 1-byte pad
	if [ "${_xromname#"$vfix"}" != "$_xromname" ] \
	    && [ "$nuke" != "nuke" ]; then

		unpad_one_byte "$_xrom"
		x_ mv "$_xrom" "$_xromnew"

		_xrom="$_xromnew"
	fi

	if [ "$nuke" = "nuke" ]; then
		( mksha512 "$_xrom" "vendorhashes" ) || err; :
	fi

	if ! add_vfiles "$_xrom"; then
		# no need to insert files. we will later
		# still process MAC addresses as required

		return 1
	fi

	if [ "$nuke" = "nuke" ]; then
		pad_one_byte "$_xrom"
		x_ mv "$_xrom" "$_xromnew"
	fi
}

mksha512()
{
	build_sbase

	if [ "${1%/*}" != "$1" ]; then
		x_ cd "${1%/*}"
	fi

	x_ "$sha512sum" ./"${1##*/}" >> "$2" || \
	    err "!sha512sum \"$1\" > \"$2\"" "mksha512" "$@"
}

add_vfiles()
{
	rom="$1"

	if [ "$has_hashes" != "y" ] && [ "$nuke" != "nuke" ]; then
		printf "'%s' has no hash file. Skipping.\n" "$archive" 1>&2

		return 1
	elif [ "$has_hashes" = "y" ] && [ "$nuke" = "nuke" ]; then
		printf "'%s' has a hash file. Skipping nuke.\n" "$archive" 1>&2

		return 1
	fi

	if [ -n "$CONFIG_HAVE_REFCODE_BLOB" ]; then
		vfile "fallback/refcode" "$CONFIG_REFCODE_BLOB_FILE" "stage"
	fi
	if [ "$CONFIG_HAVE_MRC" = "y" ]; then
		vfile "mrc.bin" "$CONFIG_MRC_FILE" "mrc" "0xfffa0000"
	fi
	if [ "$CONFIG_HAVE_ME_BIN" = "y" ]; then
		vfile IFD "$CONFIG_ME_BIN_PATH" me
	fi
	if [ -n "$CONFIG_KBC1126_FW1" ]; then
		vfile ecfw1.bin "$CONFIG_KBC1126_FW1" raw \
		    "$CONFIG_KBC1126_FW1_OFFSET"
	fi
	if [ -n "$CONFIG_KBC1126_FW2" ]; then
		vfile ecfw2.bin "$CONFIG_KBC1126_FW2" raw \
		    "$CONFIG_KBC1126_FW2_OFFSET"
	fi
	if [ -n "$CONFIG_VGA_BIOS_FILE" ] && [ -n "$CONFIG_VGA_BIOS_ID" ]; then
		vfile "pci$CONFIG_VGA_BIOS_ID.rom" "$CONFIG_VGA_BIOS_FILE" \
		    optionrom
	fi
	if [ "$CONFIG_INCLUDE_SMSC_SCH5545_EC_FW" = "y" ] && \
	    [ -n "$CONFIG_SMSC_SCH5545_EC_FW_FILE" ]; then
		vfile sch5545_ecfw.bin "$CONFIG_SMSC_SCH5545_EC_FW_FILE" raw
	fi
	if [ -z "$CONFIG_FSP_USE_REPO" ] && [ -z "$CONFIG_FSP_FULL_FD" ] && \
	    [ -n "$CONFIG_FSP_M_FILE" ]; then
		vfile "$CONFIG_FSP_M_CBFS" "$CONFIG_FSP_M_FILE" fsp --xip
	fi
	if [ -z "$CONFIG_FSP_USE_REPO" ] && [ -z "$CONFIG_FSP_FULL_FD" ] && \
	   [ -n "$CONFIG_FSP_S_FILE" ]; then
		vfile "$CONFIG_FSP_S_CBFS" "$CONFIG_FSP_S_FILE" fsp
	fi

	xchanged="y"

	printf "ROM image successfully patched: %s\n" "$rom"
}

vfile()
{
	if [ "$2" = "/dev/null" ]; then
		return 0
	fi

	cbfsname="$1"
	_dest="${2##*../}"
	blobtype="$3"

	_offset=""

	if [ "$blobtype" = "fsp" ] && [ $# -gt 3 ]; then
		_offset="$4"
	elif [ $# -gt 3 ] && _offset="-b $4" && [ -z "$4" ]; then
		err "$rom: offset given but empty (undefined)" "vfile" "$@"
	fi

	if [ "$nuke" != "nuke" ]; then
		x_ e "$_dest" f
	fi

	if [ "$cbfsname" = "IFD" ]; then
		if [ "$nuke" = "nuke" ]; then
			x_ "$ifdtool" $ifdprefix --nuke $blobtype "$rom" \
			    -O "$rom"
		else
			x_ "$ifdtool" $ifdprefix -i $blobtype:$_dest "$rom" \
			    -O "$rom"
		fi
	elif [ "$nuke" = "nuke" ]; then
		x_ "$cbfstool" "$rom" remove -n "$cbfsname"
	elif [ "$blobtype" = "stage" ]; then
		# the only stage we handle is refcode

		x_ rm -f "$xbtmp/refcode"
		x_ "$rmodtool" -i "$_dest" -o "$xbtmp/refcode"
		x_ "$cbfstool" "$rom" add-stage -f "$xbtmp/refcode" \
		    -n "$cbfsname" -t stage
	else
		x_ "$cbfstool" "$rom" add -f "$_dest" -n "$cbfsname" \
		    -t $blobtype $_offset
	fi

	xchanged="y"
}

# must be called from a subshell
check_vendor_hashes()
{
	build_sbase

	x_ cd "$tmpromdir"

	if [ "$has_hashes" != "n" ] && [ "$nuke" != "nuke" ]; then
		( x_ "$sha512sum" -c "$hashfile" ) || \
		    x_ sha1sum -c "$hashfile"
	fi

	x_ rm -f "$hashfile"
}
