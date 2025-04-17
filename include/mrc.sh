# SPDX-License-Identifier: GPL-2.0-only

# Logic based on util/chromeos/crosfirmware.sh in coreboot cfc26ce278.
# Modifications in this version are Copyright 2021, 2023 and 2024 Leah Rowe.
# Original copyright detailed in repo: https://review.coreboot.org/coreboot/

eval "`setvars "" MRC_url MRC_url_bkup MRC_hash MRC_board SHELLBALL`"

extract_mrc()
{
	chkvars "MRC_board" "CONFIG_MRC_FILE"
	SHELLBALL="chromeos-firmwareupdate-$MRC_board"

	(
	x_ cd "$appdir"
	extract_partition "${MRC_url##*/}"
	extract_archive "$SHELLBALL" .
	) || $err "mrc download/extract failure"

	"$cbfstool" "$appdir/"bios.bin extract -n mrc.bin \
	    -f "$_dest" -r RO_SECTION || $err "extract_mrc: !$cbfstool $_dest"

	[ -n "$CONFIG_REFCODE_BLOB_FILE" ] && extract_refcode; :
}

extract_partition()
{
	printf "Extracting ROOT-A partition\n"
	ROOTP=$( printf "unit\nB\nprint\nquit\n" | \
	    parted "${1%.zip}" 2>/dev/null | grep "ROOT-A" )

	START=$(( $( echo $ROOTP | cut -f2 -d\ | tr -d "B" ) ))
	SIZE=$(( $( echo $ROOTP | cut -f4 -d\ | tr -d "B" ) ))

	dd if="${1%.zip}" of="root-a.ext2" bs=1024 skip=$(( $START / 1024 )) \
	    count=$(( $SIZE / 1024 )) || $err "ex dd ${1%.zip}, root-a.ext2"

	printf "cd /usr/sbin\ndump chromeos-firmwareupdate %s\nquit" \
	    "$SHELLBALL" | debugfs "root-a.ext2" || $err "!extract shellball"
}

extract_refcode()
{
	_refdest="${CONFIG_REFCODE_BLOB_FILE##*../}"
	e "$_refdest" f && return 0

	# cbfstool changed the attributes scheme for stage files,
	# incompatible with older versions before coreboot 4.14,
	# so we need coreboot 4.13 cbfstool for certain refcode files
	chkvars cbfstoolref
	mkdir -p "${_refdest%/*}" || $err "ref: !mkdir -p ${_refdest%/*}"

	x_ "$cbfstoolref" "$appdir/bios.bin" extract \
	    -m x86 -n fallback/refcode -f "$_refdest" -r RO_SECTION

	# enable the Intel GbE device, if told by offset MRC_refcode_gbe
	[ -z "$MRC_refcode_gbe" ] || x_ dd if="config/ifd/hp820g2/1.bin" \
	    of="$_refdest" bs=1 seek=$MRC_refcode_gbe count=1 conv=notrunc; :
}
