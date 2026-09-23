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
	    err "can't copy MIT license snippet" "copyps1bios" "$@"
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

corebootpremake()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	[ "$XBMK_RELEASE" = "y" ] && \
		payload_edk2="" # source-only payload, for now.

	forx 'check_coreboot_util %s' cbfstool ifdtool

	printf "%s\n" "${version%%-*}" > "$srcdir/.coreboot-version" || \
	    err "!mk $srcdir .coreboot-version" "corebootpremake" "$@"

	[ "$target" != "$tree" ] && \
		x_ "$mk" download "$target"; :
}

cook_coreboot_config()
{
	[ -n "$mode" ] && return 0
	[ -f "$srcdir/$cfgname" ] || return 0

	[ -n "$payload_edk2" ] && \
		cook_coreboot_smmstore

	x_ make -C "$srcdir" oldconfig
}

cook_coreboot_smmstore()
{
	# Enable SMMSTORE-related options for EDK2 variable storage.
	# Because edk2 is source-only (release=n) in lbmk, we don't
	# enable this in the configs themselves. Instead, we add it
	# when building a board with EDK2 support in lbmk.git

	# TODO: this relpaces the option backend. i need to make
	#       coreboot always use a cbfs backend, and set all
	#       boards universally that way, before starting edk2

	ecfg="CONFIG_SMMSTORE_BLOCK_SIZE=65536 \
	    CONFIG_DRIVERS_EFI_VARIABLE_STORE=y \
	    CONFIG_SMMSTORE=y \
	    CONFIG_SMMSTORE_SIZE=0x80000 \
	    CONFIG_SPI_FLASH_SMM=y \
	    CONFIG_DRIVERS_GENERIC_CFR_ASSET_TAG=y \
	    CONFIG_USE_UEFI_VARIABLE_STORE=y" # replace option backend

	for ecfgval in $ecfg; do
		printf "%s\n" "$ecfgval" >> "$srcdir/$cfgname"
	done
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

	for y in "$defscan"/*; do
		buildcfg="$y"
		$if_not_skip_build mkcorebootbin_real
	done

	$if_not_skip_build mkcoreboottar
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

	[ -z "$uboot_config" ] && \
		uboot_config="default"
	[ "$payload_grub" = "y" ] && \
		payload_seabios="y"

	[ -z "$grub_scan_disk" ] && \
		grub_scan_disk="nvme ahci ata"
	[ -z "$grubtree" ] && \
		grubtree="default"
	grubelf="elf/grub/$grubtree/$grubtree/noconfig/grub.elf"

	[ "$payload_memtest" != "y" ] && \
		payload_memtest="n"
	[ "$(uname -m)" != "x86_64" ] && \
		payload_memtest="n"

	[ "$payload_grubsea" = "y" ] && [ "$initmode" = "normal" ] && \
		payload_grubsea="n"
	[ "$payload_grub" != "y" ] && \
		payload_grubsea="n"

	if [ "$initmode" != "libgfxinit" ] || \
	    [ "$displaymode" != "corebootfb" ]; then
		[ "$initmode" != "fspgop" ] && [ "$initmode" != "normal" ] && \
		    payload_edk2="" # disable on txtmode
	fi

	$if_not_skip_build prep_tmprom

	if grep "CONFIG_PAYLOAD_NONE=y" "$buildcfg"; then
		if [ -n "$payload_edk2" ]; then
			pname="edk2"
			$if_not_skip_build add_edk2 "fallback/payload"
			$if_not_skip_build cprom # standalone edk2

			$if_not_skip_build prep_tmprom
		fi
		if [ "$payload_seabios" = "y" ]; then
			pname="seabios"
			$if_not_skip_build add_seabios
		fi
		if [ "$payload_uboot" = "arm64" ]; then
			pname="uboot"
			$if_not_skip_build add_uboot "" fallback/payload \
			    default "$target" u-boot.elf
		fi
	else
		pname="custom"
		$if_not_skip_build cprom
	fi; :
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
	edk2dir="elf/coreboot/default/$payload_edk2"
	edk2="$edk2dir/libgfxinit_corebootfb/UEFIPAYLOAD.fd"

	cbfs "$tmprom" "$edk2" "$1"
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
		cbfs "$tmprom" \
		    "elf/memtest86plus/default/default/noconfig/memtest.bin" \
		    img/memtest

	[ "$payload_grub" = "y" ] && \
		add_grub

	[ -n "$payload_edk2" ] && \
		add_edk2 "img/edk2"

	[ "$payload_grubsea" != "y" ] && \
		cprom # seabios, but don't load grub first

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
	ubootelf="elf/u-boot/$3/$4/$uboot_config/$5"
	[ ! -f "$ubootelf" ] && \
		ubootelf="${ubootelf%.elf}" # hack (fix arm64 build)

	cbfs "$tmprom" "$ubootelf" "$2" $1
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
