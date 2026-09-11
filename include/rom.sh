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

	x_ xbmkdir "bin/serprog_$1"
	x_ mv "$serx" "bin/serprog_$1/serprog_$2.${serx##*.}"
}

copyps1bios()
{
	$if_dry_build \
		return 0

	remkdir "bin/playstation"
	x_ cp src/pcsx-redux/src/mips/openbios/openbios.bin bin/playstation

	printf "MIT License\n\nCopyright (c) 2019-2025 PCSX-Redux authors\n\n" \
	    > bin/playstation/COPYING.txt || \
	    err "can't write PCSX Redux copyright info" "copyps1bios" "$@"

	x_ cat config/snippet/mit >>bin/playstation/COPYING.txt || \
	    err "can't copy MIT license snippet" "copyps1bios" "$@"
}

mkpayload_grub()
{
	grub_modules=""
	grub_install_modules=""

	$if_dry_build \
		return 0

	. "$grubdata/module/$tree" || \
	    err "Can't read '$grubdata/module/$tree'" "mkpayload_grub" "$@"

	x_ rm -f "$srcdir/grub.elf"

	x_ "$srcdir/grub-mkstandalone" \
	    --grub-mkimage="$srcdir/grub-mkimage" \
	    -O i386-coreboot -o "$srcdir/grub.elf" -d "${srcdir}/grub-core/" \
	    --fonts= --themes= --locales=  --modules="$grub_modules" \
	    --install-modules="$grub_install_modules" \
	    "/boot/grub/grub_default.cfg=${srcdir}/.config" \
	    "/boot/grub/grub.cfg=$grubdata/memdisk.cfg"; :
}

corebootpremake()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	$if_not_dry_build \
		cook_coreboot_config

	fx_ check_coreboot_util printf "cbfstool\nifdtool\n"

	printf "%s\n" "${version%%-*}" > "$srcdir/.coreboot-version" || \
	    err "!mk $srcdir .coreboot-version" "corebootpremake" "$@"

	[ -z "$mode" ] && [ "$target" != "$tree" ] && \
		x_ ./mk download "$target"; :
}

cook_coreboot_config()
{
	if [ -z "$mode" ] && [ -f "$srcdir/.config" ]; then
		printf "CONFIG_CCACHE=y\n" >> "$srcdir/.config" || \
		    err "can't cook '$srcdir'" "cook_coreboot_config" "$@"
	fi
}

check_coreboot_util()
{
	[ "$badhash" = "y" ] && \
		x_ rm -f "elf/coreboot/$tree/$1"
	if e "elf/coreboot/$tree/$1" f; then
		return 0
	fi

	utilelfdir="elf/coreboot/$tree"
	utilsrcdir="src/coreboot/$tree/util/$1"

	utilmode=""
	[ -n "$mode" ] && \
		utilmode="clean"

	x_ make -C "$utilsrcdir" $utilmode -j$XBMK_THREADS $makeargs

	if [ -n "$mode" ]; then
		# TODO: is this rm command needed?

		x_ rm -Rf "$utilelfdir"

		return 0
	elif [ -n "$mode" ] || [ -f "$utilelfdir/$1" ]; then
		return 0
	fi

	x_ xbmkdir "$utilelfdir"
	x_ cp "$utilsrcdir/$1" "$utilelfdir"

	[ "$1" = "cbfstool" ] && \
		x_ cp "$utilsrcdir/rmodtool" "$utilelfdir"; :
}

coreboot_pad_one_byte()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	$if_not_dry_build \
		pad_one_byte "$srcdir/build/coreboot.rom"
}

mkcorebootbin()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	$if_not_dry_build \
		check_coreboot_util cbfstool

	$if_not_dry_build \
		check_coreboot_util ifdtool

	for y in "$target_dir/config"/*; do
		defconfig="$y"
		mkcorebootbin_real
	done

	mkcoreboottar
}

mkcorebootbin_real()
{
	[ "$target" = "$tree" ] && \
		return 0

	tmprom="$xbtmp/coreboot.rom"

	initmode="${defconfig##*/}"
	displaymode="${initmode##*_}"
	if [ "$displaymode" = "$initmode" ]; then
		# blank it for "normal" or "fspgop" configs:

		displaymode=""
	fi
	initmode="${initmode%%_*}"
	cbfstool="elf/coreboot/$tree/cbfstool"

	# cbfstool option backends, if they exist
	cbfscfg="config/coreboot/$target/cbfs.cfg"

	elfrom="elf/coreboot/$tree/$target/$initmode"
	[ -n "$displaymode" ] && \
		elfrom="${elfrom}_$displaymode"
	elfrom="$elfrom/coreboot.rom"

	$if_not_dry_build \
		x_ cp "$elfrom" "$tmprom"

	$if_not_dry_build \
		unpad_one_byte "$tmprom"

	[ -n "$payload_uboot" ] && [ "$payload_uboot" != "amd64" ] && \
	    [ "$payload_uboot" != "i386" ] && [ "$payload_uboot" != "arm64" ] \
		&& err "'$target' defines bad u-boot type '$payload_uboot'" \
		    "mkcorebootbin_real" "$@"

	[ -n "$payload_uboot" ] && [ "$payload_uboot" != "arm64" ] && \
		payload_seabios="y"

	[ -z "$uboot_config" ] && \
		uboot_config="default"
	[ "$payload_grub" = "y" ] && \
		payload_seabios="y"
	[ "$payload_seabios" = "y" ] && [ "$payload_uboot" = "arm64" ] && \
		$if_not_dry_build \
			err "$target: U-Boot arm / SeaBIOS/GRUB both enabled" \
			    "mkcorebootbin_real" "$@"

	[ -z "$grub_scan_disk" ] && \
		grub_scan_disk="nvme ahci ata"
	[ -z "$grubtree" ] && \
		grubtree="default"
	grubelf="elf/grub/$grubtree/$grubtree/payload/grub.elf"

	[ "$payload_memtest" != "y" ] && \
		payload_memtest="n"
	[ "$(uname -m)" != "x86_64" ] && \
		payload_memtest="n"

	[ "$payload_grubsea" = "y" ] && [ "$initmode" = "normal" ] && \
		payload_grubsea="n"
	[ "$payload_grub" != "y" ] && \
		payload_grubsea="n"

	$if_dry_build \
		return 0

	[ -f "$cbfscfg" ] && \
		dx_ add_cbfs_option "$cbfscfg"

	if grep "CONFIG_PAYLOAD_NONE=y" "$defconfig"; then
		if [ "$payload_seabios" = "y" ]; then
			pname="seabios"
			add_seabios
		fi
		if [ "$payload_uboot" = "arm64" ]; then
			pname="uboot"
			add_uboot
		fi
	else
		pname="custom"
		cprom
	fi; :
}

# options for cbfs backend (as opposed to nvram/smmstore):

add_cbfs_option()
{
	# TODO: input sanitization (currently mitigated by careful config)

	op_name="`printf "%s\n" "$1" | awk '{print $1}'`"
	op_arg="`printf "%s\n" "$1" | awk '{print $2}'`"

	if [ -z "$op_name" ] || [ -z "$op_arg" ]; then
		return 0
	fi

	( x_ "$cbfstool" "$tmprom" remove -n "option/$op_name" 1>/dev/null \
	    2>/dev/null ) || :

	x_ "$cbfstool" "$tmprom" add-int -i "$op_arg" -n "option/$op_name"
}

# in our design, SeaBIOS is also responsible for starting either
# a GRUB or U-Boot payload. this is because SeaBIOS is generally
# a more reliable codebase, so it's less likely to cause a brick
# during testing and development, or user configuration. if one
# of the u-boot or grub payloads fails, the user still has a
# functional SeaBIOS setup to fall back on. watch:

add_seabios()
{
	[ -n "$payload_uboot" ] && [ "$payload_uboot" != "arm64" ] && \
		$if_not_dry_build \
			add_uboot # add u-boot first, due to fixed cbfs offset

	_seabioself="elf/seabios/default/default/$initmode/bios.bin.elf"
	[ "$initmode" = "fspgop" ] && \
	    _seabioself="elf/seabios/default/default/libgfxinit/bios.bin.elf"

	_seaname="fallback/payload"
	[ "$payload_grubsea" = "y" ] && \
		 _seaname="seabios.elf"

	cbfs "$tmprom" "$_seabioself" "$_seaname"

	x_ "$cbfstool" "$tmprom" add-int -i 3000 -n etc/ps2-keyboard-spinup

	opexec="2"
	[ "$initmode" = "vgarom" ] && \
		opexec="0"
	x_ "$cbfstool" "$tmprom" add-int -i $opexec -n etc/pci-optionrom-exec

	x_ "$cbfstool" "$tmprom" add-int -i 0 -n etc/optionroms-checksum
	if [ "$initmode" = "libgfxinit" ] || [ "$initmode" = "fspgop" ]; then
		cbfs "$tmprom" "$seavgabiosrom" vgaroms/seavgabios.bin raw
	fi

	[ "$payload_memtest" = "y" ] && \
		cbfs "$tmprom" "elf/memtest86plus/memtest.bin" img/memtest

	[ "$payload_grub" = "y" ] && \
		add_grub

	[ "$payload_grubsea" != "y" ] && \
		cprom # seabios, but don't load grub/u-boot first

	# now make seauboot(u-boot loaded by seabios) and SeaGRUB (GRUB loaded
	# by SeaBIOS) images; users can bypass via ESC to boot from SeaBIOS

	if [ "$payload_uboot" = "amd64" ] && \
	    [ "$displaymode" != "txtmode" ] && \
	    [ "$initmode" != "normal" ] && [ "$payload_grubsea" != "y" ]; then
		pname="seauboot"
		cprom "seauboot"
	fi

	if [ "$payload_grub" = "y" ]; then
		pname="seagrub"
		mkseagrub
	fi
}

add_grub()
{
	# path in CBFS for the GRUB payload
	_grubname="img/grub2"
	[ "$payload_grubsea" = "y" ] && \
		_grubname="fallback/payload"

	cbfs "$tmprom" "$grubelf" "$_grubname"

	printf "set grub_scan_disk=\"%s\"\n" "$grub_scan_disk" \
	    > "$xbtmp/tmpcfg" || \
	    err "$target: !insert scandisk" "add_grub" "$@"

	cbfs "$tmprom" "$xbtmp/tmpcfg" scan.cfg raw

	[ "$initmode" != "normal" ] && [ "$displaymode" != "txtmode" ] && \
		cbfs "$tmprom" "$grubdata/background/background1280x800.png" \
		    "background.png" raw; :
}

mkseagrub()
{
	if [ "$payload_grubsea" = "y" ]; then
		pname="grub"
	else
		cbfs "$tmprom" "$grubdata/bootorder" bootorder raw
	fi

	fx_ cprom x_ find "$grubdata/keymap" -type f -name "*.gkb"
}

add_uboot()
{
	if [ "$displaymode" = "txtmode" ]; then
		printf "cb/%s: Can't use U-Boot in text mode\n" "$target" 1>&2
		return 0
	elif [ "$initmode" = "normal" ]; then
		printf "cb/%s: Can't use U-Boot in normal initmode\n" \
		    "$target" 1>&2
		return 0
	fi

	# TODO: re-work to allow each coreboot target to say which ub tree
	# instead of hardcoding as in the current logic below:

	# aarch64 targets:
	ubcbfsargs=""
	ubpath="fallback/payload"
	ubtree="default"
	ubtarget="$target"

	# override for x86/x86_64 targets:
	if [ -n "$payload_uboot" ] && [ "$payload_uboot" != "arm64" ]; then
		ubcbfsargs="-l 0x1110000 -e 0x1110000" # 64-bit and 32-bit
			# on 64-bit, 0x1120000 is the SPL, with a stub that
			# loads it, located at 0x1110000

		ubpath="img/u-boot" # 64-bit
		ubtree="x86_64"
		ubtarget="amd64coreboot"

		if [ "$payload_uboot" = "i386" ]
		then
			ubpath="u-boot" # 32-bit
			ubtree="x86"
			ubtarget="i386coreboot"; :
		fi
	fi

	ubdir="elf/u-boot/$ubtree/$ubtarget/$uboot_config"

	# aarch64 targets:
	ubootelf="$ubdir/u-boot.elf"
	if [ ! -f "$ubootelf" ]; then
		ubootelf="$ubdir/u-boot"
	fi

	# override for x86/x86_64 targets:
	if [ "$payload_uboot" = "i386" ]; then
		ubootelf="$ubdir/u-boot-dtb.bin"
	elif [ "$payload_uboot" = "amd64" ]; then
		ubootelf="$ubdir/u-boot-x86-with-spl.bin" # EFI-compatible
	fi

	cbfs "$tmprom" "$ubootelf" "$ubpath" $ubcbfsargs
	[ "$payload_seabios" != "y" ] && \
		cprom; :
}

# prepare the final image in bin/ for user installation:

cprom()
{
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
		elif [ "$1" = "seauboot" ]; then
			cbfs "$irom" "$grubdata/bootorder_uboot" bootorder raw
		fi
	fi

	printf "Creating new %s image: '%s'\n" "$projectname" "$newrom"

	x_ xbmkdir "bin/$target"
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
	$if_dry_build \
		return 0

	if [ "$target" = "$tree" ] || [ "$XBMK_RELEASE" != "y" ] || \
	    [ "$release" = "n" ]; then
		return 0
	fi

	mkrom_tarball "bin/$target"
	x_ ./mk inject "bin/${relname}_${target}.tar.xz" nuke
}
