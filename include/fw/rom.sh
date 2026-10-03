# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2014-2016,2020-2021,2023-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2021-2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022-2023 Alper Nebi Yasak <alpernebiyasak@gmail.com>
# Copyright (c) 2023-2024 Riku Viitanen <riku.viitanen@protonmail.com>

grubdata="config/data/grub"

buildser()
{
	if [ "$1" = "pico" ]; then
		x_ cmake -DPICO_BOARD="$2" \
		    -DPICO_SDK_PATH="$picosdk" -B "$sersrc/build" "$sersrc"
		x_ cmake --build "$sersrc/build"
	elif [ "$1" = "stm32" ]; then
		x_ make -C "$sersrc" libopencm3-just-make BOARD=$2
		x_ make -C "$sersrc" BOARD=$2
	fi

	x_ mkdir -p "bin/serprog_$1"
	x_ mv "$serx" "bin/serprog_$1/serprog_$2.${serx##*.}"
}

copyps1bios()
{
	remkdir "bin/playstation"
	x_ cp src/pcsx-redux/default/src/mips/openbios/openbios.bin \
	    bin/playstation

	printf "MIT License\n\nCopyright (c) 2019-2025 PCSX-Redux authors\n\n" \
	    > bin/playstation/COPYING.txt || \
	    err "can't write PCSX Redux copyright info" "copyps1bios" "$@"

	x_ cat config/snippet/mit >>bin/playstation/COPYING.txt || \
	    err "can't copy MIT license snippet" "copyps1bios" "$@";
}

mkpayload_grub()
{
	eval "`newvar grub_modules grub_install_modules`"

	. "$grubdata/module/$tree" || \
	    err "Can't read '$grubdata/module/$tree'" "mkpayload_grub" "$@"

	x_ rm -f "$srcdir/grub.elf"

	x_ "$srcdir/grub-mkstandalone" \
	    --grub-mkimage="$srcdir/grub-mkimage" \
	    -O i386-coreboot -o "$srcdir/grub.elf" -d "${srcdir}/grub-core/" \
	    --fonts= --themes= --locales=  --modules="$grub_modules" \
	    --install-modules="$grub_install_modules" \
	    "/boot/grub/grub_default.cfg=$grubdata/cfg/$tree.config" \
	    "/boot/grub/grub.cfg=$grubdata/memdisk.cfg"; :
}

corebootpreconfig()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	forx 'check_coreboot_util %s' cbfstool ifdtool
}

corebootpremake()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	printf "%s\n" "${version%%-*}" > "$srcdir/.coreboot-version" || \
	    err "!mk $srcdir .coreboot-version" "corebootpremake" "$@"

	[ -n "$payload_edk2" ] && $if_not_dry_build \
		prep_edk2_src

	[ "$target" != "$tree" ] && \
		x_ "$mk" download "$target"; :
}

cook_coreboot_config()
{
	[ -n "$mode" ] && return 0
	[ -f "$srcdir/$cfgname" ] || return 0

	x_ make -C "$srcdir" oldconfig
}

check_coreboot_util()
{
	e "elf/coreboot/$tree/$1" f && \
		return 0

	utilelfdir="elf/coreboot/$tree"
	utilsrcdir="src/coreboot/$tree/util/$1"

	utilmode=""
	[ -n "$mode" ] && \
		utilmode="clean"

	x_ make -C "$utilsrcdir" $utilmode -j$XBMK_THREADS $makeargs

	if [ -n "$mode" ]; then
		x_ rm -Rf "$utilelfdir"
		return 0
	elif [ -n "$mode" ] || [ -f "$utilelfdir/$1" ]; then
		return 0
	fi

	x_ mkdir -p "$utilelfdir"
	x_ cp "$utilsrcdir/$1" "$utilelfdir"

	[ "$1" = "cbfstool" ] && \
		x_ cp "$utilsrcdir/rmodtool" "$utilelfdir"; :
}

coreboot_pad_one_byte()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	$if_not_skip_build pad_one_byte "$srcdir/build/coreboot.rom"
}

mkcorebootbin()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	$if_not_dry_build \
		clean_edk2_src

	[ -z "$defscan" ] && err "defscan empty" mkcorebootbin "$@"

	for y in "$defscan"/*; do
		[ -f "$y" ] || continue

		buildcfg="$y"
		$if_not_skip_build mkcorebootbin_real
	done

	$if_not_skip_build mkcoreboottar
}

prep_edk2_src()
{
	clean_edk2_src

	[ -f "$(edk2path)" ] && \
		return 0

	x_ mkdir "$srcdir/payloads/external/edk2/workspace"

	x_ cp -a "$(findpath "src/edk2/$payload_edk2/")" \
	    "$srcdir/payloads/external/edk2/workspace/mrchromebox"
}

clean_edk2_src()
{
	# replicates coreboot's handling after edk2 build,
	# which we patch out for copy. so now we clean it instead,
	# after we successfully copied it out-of-tree.
	# this enables clean re-building when modifying cb/edk2_mrchromebox
	#
	# so long as metadata in config/ doesn't change, and we
	# have a build in elf/, then a re-build/re-download will
	# not occur. this however means that we must set release=n
	# for now. proper edk2 integration will come to lbmk soon(tm)
	#
	x_ rm -Rf "$srcdir/payloads/external/edk2/workspace"
}

mkcorebootbin_real()
{
	[ "$target" = "$tree" ] && return 0

	tmprom="$xbtmp/coreboot.rom"

	initmode="${buildcfg##*/}"
	displaymode="${initmode##*_}"
	if [ "$displaymode" = "$initmode" ]; then
		# blank it for "normal" or "fspgop" configs:

		displaymode=""
	fi
	initmode="${initmode%%_*}"
	cbfstool="elf/coreboot/$tree/cbfstool"

	# cbfstool option backends, if they exist
	cbfscfg="config/module/coreboot/$target/cbfs.cfg"

	elfrom="elf/coreboot/$tree/$target/$initmode"
	[ -n "$displaymode" ] && \
		elfrom="${elfrom}_$displaymode"
	elfrom="$elfrom/coreboot.rom"

	[ -n "$payload_uboot" ] && [ "$payload_uboot" != "arm64" ] && \
		err "'$target' defines bad u-boot type '$payload_uboot'" \
		    "mkcorebootbin_real" "$@"

	[ "$payload_memtest" != "y" ] && \
		payload_memtest="n"
	[ "$(uname -m)" != "x86_64" ] && \
		payload_memtest="n"

	$if_skip_build \
		return 0

	prep_tmprom

	if ! grep "CONFIG_PAYLOAD_NONE=y" "$buildcfg"; then
		cprom custom
		prep_tmprom
		return 0
	fi

	[ -n "$payload_edk2" ] && \
		mk_coreboot_with edk2
	[ "$payload_seabios" = "y" ] && \
		mk_coreboot_with seabios
	[ "$payload_grub" = "y" ] && \
		mk_coreboot_with grub
	[ "$payload_uboot" = "arm64" ] && \
		mk_coreboot_with uboot; :

	[ "$rom_prepared" = "n" ] && \
		printf "WARNING: no payloads configured for cb/%s\n" \
		    "$target" 1>&2; :
}

mk_coreboot_with()
{
	add_$1 || return 0

	prep_tmprom
	rom_prepared="y"
}

prep_tmprom()
{
	x_ cp "$elfrom" "$tmprom"
	unpad_one_byte "$tmprom"

	[ -f "$cbfscfg" ] && \
		dx_ add_cbfs_option "$cbfscfg"; :
}

# options for cbfs backend (as opposed to nvram/smmstore):

add_cbfs_option()
{
	op_name="`printf "%s\n" "$1" | awk '{print $1}'`"
	op_arg="`printf "%s\n" "$1" | awk '{print $2}'`"

	if [ -z "$op_name" ] || [ -z "$op_arg" ]; then
		return 0
	fi

	( x_ "$cbfstool" "$tmprom" remove -n "option/$op_name" 1>/dev/null \
	    2>/dev/null ) || :

	x_ "$cbfstool" "$tmprom" add-int -i "$op_arg" -n "option/$op_name"
}

add_edk2()
{
	# allowed modes for edk2: libgfxinit_corebootfb, fspgop, normal

	# NOTE: "normal" removed from checks. in this setup, edk2 uses gop.
	[ "$initmode" = "vgarom" ] && return 1 # vga rom incompatible. use gop.
	[ "$initmode" = "libgfxinit" ] && [ "$displaymode" != "corebootfb" ] && \
		return 1 # text mode incompatible

	cbfs "$tmprom" "$(edk2path)" "fallback/payload"
	cprom edk2
}

edk2path()
{
	# on non-mrchromebox targets, we only use one edk2 tree,
	# universal to all boards. on mrchromebox, we support
	# multiple edk2 payloads all under coreboot/mrchromebox

	edk2dir="elf/coreboot/default"
	[ "$payload_edk2" != "edk2_mrchromebox" ] && \
	[ "$payload_edk2" != "edk2_mrchromebox_nohpet" ] && \
		edk2dir="elf/coreboot/mrchromebox"

	printf "%s/%s/libgfxinit_corebootfb/UEFIPAYLOAD.fd\n" \
	    "$edk2dir" "$payload_edk2"
}

# in our design, SeaBIOS is also responsible for starting either
# a GRUB or U-Boot payload. this is because SeaBIOS is generally
# a more reliable codebase, so it's less likely to cause a brick
# during testing and development, or user configuration. if one
# of the u-boot or grub payloads fails, the user still has a
# functional SeaBIOS setup to fall back on. watch:

add_seabios()
{
	_seabioself="elf/seabios/default/build/$initmode/bios.bin.elf"
	[ "$initmode" = "fspgop" ] && \
	    _seabioself="elf/seabios/default/build/libgfxinit/bios.bin.elf"

	cbfs "$tmprom" "$_seabioself" "fallback/payload"

	x_ "$cbfstool" "$tmprom" add-int -i 3000 -n etc/ps2-keyboard-spinup

	opexec="2"
	[ "$initmode" = "vgarom" ] && \
		opexec="0"
	x_ "$cbfstool" "$tmprom" add-int -i $opexec -n etc/pci-optionrom-exec

	x_ "$cbfstool" "$tmprom" add-int -i 0 -n etc/optionroms-checksum
	if [ "$initmode" = "libgfxinit" ] || [ "$initmode" = "fspgop" ]; then
		cbfs "$tmprom" "$seavgabiosrom" vgaroms/seavgabios.bin raw
	fi

	add_memtest86plus

	cprom seabios
}

add_grub()
{
	[ "$initmode" = "normal" ] && \
		return 1 # no gfxinit required by grub ("normal" not allowed)

	[ -z "$grub_scan_disk" ] && \
		grub_scan_disk="nvme ahci ata"
	[ -z "$grubtree" ] && \
		grubtree="default"

	printf "set grub_scan_disk=\"%s\"\n" "$grub_scan_disk" \
	    > "$xbtmp/tmpcfg" || \
	    err "$target: !insert scandisk" "add_grub" "$@"

	cbfs "$tmprom" "$xbtmp/tmpcfg" scan.cfg raw

	[ "$initmode" != "normal" ] && [ "$displaymode" != "txtmode" ] && \
		cbfs "$tmprom" "$grubdata/background/background1280x800.png" \
		    "background.png" raw; :

	cbfs "$tmprom" "elf/grub/$grubtree/$grubtree/noconfig/grub.elf" \
	    "fallback/payload"

	add_memtest86plus

	fx_ "cprom_grub" x_ find "$grubdata/keymap" -type f -name "*.gkb"
}

add_memtest86plus()
{
	[ "$payload_memtest" = "y" ] && \
		cbfs "$tmprom" \
		    "elf/memtest86plus/default/default/noconfig/memtest.bin" \
		    img/memtest; :
}

add_uboot()
{
	[ -z "$uboot_config" ] && \
		uboot_config="default"

	ubootelf="elf/u-boot/default/$target/$uboot_config/u-boot.elf"
	[ ! -f "$ubootelf" ] && \
		ubootelf="${ubootelf%.elf}" # hack (fix arm64 build)

	cbfs "$tmprom" "$ubootelf" "fallback/payload"

	cprom uboot
}

# prepare the final image in bin/ for user installation:

cprom_grub()
{
	cprom grub "$@"
}

cprom()
{
	pname="${1-}"
	[ -z "$pname" ] && pname="undefined"
	shift 1 2>/dev/null || :

	cpcmd="cp"

	tmpnew=""
	newrom="bin/$target/${pname}_${target}_$initmode.rom"

	[ -n "$displaymode" ] && \
		newrom="${newrom%.rom}_$displaymode.rom"
	if [ $# -gt 0 ] && [ "${1%.gkb}" != "$1" ]; then
		tmpnew="${1##*/}"
		newrom="${newrom%.rom}_${tmpnew%.gkb}.rom"
	fi

	irom="$tmprom"

	if [ $# -gt 0 ]; then
		irom="$(mktemp || err "!mk irom, $(echo "$@")")" || \
		    err "can't copy rom" "cprom" "$@"

		x_ cp "$tmprom" "$irom" && cpcmd="mv"

		if [ "${1%.gkb}" != "$1" ]; then
			cbfs "$irom" "$grubdata/keymap/$tmpnew" keymap.gkb raw
		fi
	fi

	printf "Creating new %s image: '%s'\n" "$projectname" "$newrom"

	x_ mkdir -p "bin/$target"
	x_ $cpcmd "$irom" "$newrom"
}

cbfs()
{
	ccmd="add-payload"
	lzma="-c lzma"

	if [ $# -gt 3 ] && [ $# -lt 5 ]; then
		ccmd="add"
		lzma="-t $4"
	elif [ $# -gt 4 ] && [ "$5" = "0x1110000" ]; then
		ccmd="add-flat-binary" && \
		lzma="-c lzma -l 0x1110000 -e 0x1110000"
	fi

	x_ "$cbfstool" "$1" $ccmd -f "$2" -n "$3" $lzma
}

# for release files:

mkcoreboottar()
{
	if [ "$target" = "$tree" ] || [ "$XBMK_RELEASE" != "y" ] || \
	    [ "$release" = "n" ]; then
		return 0
	fi

	mkrom_tarball "bin/$target"
	x_ "$mk" inject "bin/${relname}_${target}.tar.xz" nuke
}

# when we do ./mk -u coreboot after an update, a board may have changed to
# a variant upstream, or otherwise renamed. in some instances, we may have
# inadvertently changed the board because Kconfig will otherwise then default
# to the first board Kconfig list for src/mainboard/VENDOR. Here then, we tie
# current lbmk board names to CONFIG_BOARD_*=y and check if it changed since
# last time. WARNING: this function is a giant nightmarish HACK, like kconfig
mkcbtest()
{
	(
	testval=0
	cbsafe="$xbtmp/newtargetlist"
	cbsafe_final="config/data/coreboot/targets.list"

	writesafe="y"
	[ -f "$cbsafe_final" ] && writesafe="n"

	for cbhotd in config/module/coreboot/*; do
		[ -d "$cbhotd" ] || continue;

		cbmatch=""

		for cbhot in "$cbhotd/config/"*; do
			[ -f "$cbhot" ] || continue;

			cbtname="${cbhot#config/module/coreboot/}"
			cbtname="${cbtname%%/*}"

			cbkname="$(x_ grep CONFIG_BOARD_ "$cbhot" | \
			    grep "=y" | \
			    grep -v SPECIFIC | head -n1 | \
			    awk -F '=' '{print $1}' || \
			    err "Can't find CONFIG_BOARD in '$cbhot'")" \
			    || err "Please fix the configs for: '$cbhotd'";

			if [ -z "$cbmatch" ]; then
				x_ printf "%s:%s\n" "$cbtname" "$cbkname" >> \
				    "$cbsafe" || err "can't write to '$cbsafe'";
			fi

			[ -f "$cbsafe_final" ] && for cbchange in \
			    $(x_ grep "$cbtname:" "$cbsafe_final"); do
				[ "$cbtname:$cbkname" != "$cbchange" ] ||
					continue;

				testval=1;
				( err "'$cbhotd' name change: $cbkname" ) || :
			
				break	
			done;
			[ $testval -gt 0 ] && break;

			[ -z "$cbmatch" ] && cbmatch="$cbtname:$cbkname";
			[ "$cbtname:$cbkname" != "$cbmatch" ] && \
				err "differing boards for configs: '$cbhotd'";
		done
	done
	[ $testval -gt 0 ] || \
		x_ mv "$cbsafe" "$cbsafe_final";

	exit $testval
	) || err "Issue with configs under config/module/coreboot/";
}

