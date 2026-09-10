# SPDX-License-Identifier: GPL-2.0-only

# Logic based on util/chromeos/crosfirmware.sh in coreboot cfc26ce278.
# Modifications in this version are Copyright 2021,2023-2025 Leah Rowe.
# Original copyright detailed in repo: https://review.coreboot.org/coreboot/

MRC_board=""
MRC_hash=""
MRC_url=""
MRC_url_bkup=""
SHELLBALL=""

extract_refcode()
{
	extract_mrc

	# cbfstool after coreboot 4.13 changed the stage file attribute scheme,
	# and refcode is extracted from an image using the old scheme. we use
	# cbfstool from coreboot 4.11_branch, the tree used by ASUS KGPE-D16:

	if [ -z "$cbfstoolref" ]; then
		err "cbfstoolref not set" "extract_refcode" "$@"
	fi

	x_ xbmkdir "${_pre_dest%/*}"

	x_ "$cbfstoolref" "$appdir/bios.bin" extract \
	    -m x86 -n fallback/refcode -f "$appdir/ref" -r RO_SECTION

	# enable the Intel GbE device, if told by offset MRC_refcode_gbe
	if [ -n "$MRC_refcode_gbe" ]; then
		x_ dd if="config/ifd/hp820g2/1.bin" of="$appdir/ref" bs=1 \
		    seek=$MRC_refcode_gbe count=1 conv=notrunc; :
	fi

	x_ mv "$appdir/ref" "$_pre_dest"
}

extract_mrc()
{
	if [ -z "$MRC_board" ]; then
		err "MRC_board unset" "extract_mrc" "$@"
	elif [ -z "$CONFIG_MRC_FILE" ]; then
		err "CONFIG_MRC_FILE unset" "extract_mrc" "$@"
	fi

	SHELLBALL="chromeos-firmwareupdate-$MRC_board"

	(
		x_ cd "$appdir"
		extract_partition "${MRC_url##*/}"
		extract_archive "$SHELLBALL" .

	) || err "mrc download/extract failure" "extract_mrc" "$@"

	x_ "$cbfstool" "$appdir/"bios.bin extract -n mrc.bin \
	    -f "${_pre_dest%/*}/mrc.bin" -r RO_SECTION
}

extract_partition()
{
	printf "Extracting ROOT-A partition\n"

	ROOTP=$( printf "unit\nB\nprint\nquit\n" | \
	    parted "${1%.zip}" 2>/dev/null | grep "ROOT-A" )

	START=$(( $( echo $ROOTP | cut -f2 -d\ | tr -d "B" ) ))

	SIZE=$(( $( echo $ROOTP | cut -f4 -d\ | tr -d "B" ) ))

	x_ dd if="${1%.zip}" of="root-a.ext2" bs=1024 \
	    skip=$(( $START / 1024 )) count=$(( $SIZE / 1024 ))

	printf "cd /usr/sbin\ndump chromeos-firmwareupdate %s\nquit" \
	    "$SHELLBALL" | debugfs "root-a.ext2" || \
	    err "!extract shellball" "extract_partition" "$@"
}
