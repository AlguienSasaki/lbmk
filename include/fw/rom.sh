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
	grub_modules=""

	x_ rm -f "$srcdir/grub.elf"

	x_ find "$srcdir" -type f -name "*.mod" > "$xbtmp/gmod.list" || \
	    err "can't make glist '$srcdir'" mkpayload_grub "$@"; :

	while read -r gmod; do

		gmodname="${gmod##*/}"
		gmodname="${gmodname%.mod}"

		grub_modules="$grub_modules $gmodname"

	done < "$xbtmp/gmod.list" || \
	    err "can't read gmod.list '$srcdir'" mkpayload_grub "$@"; :

	garch="i386-coreboot"
	[ "$srcdir" = "src/grub/efi" ] && \
		garch="x86_64-efi"

	x_ "$srcdir/grub-mkstandalone" \
	    --grub-mkimage="$srcdir/grub-mkimage" \
	    -O $garch -o "$srcdir/grub.elf" -d "${srcdir}/grub-core/" \
	    --fonts= --themes= --locales=  --modules="${grub_modules# }" \
	    --install-modules="" \
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

	[ "$target" != "$tree" ] && \
		x_ "$mk" download "$target"; :
}

cook_coreboot_config()
{
	[ -n "$mode" ] && return 0
	[ -f "$srcdir/$cfgname" ] || return 0

	x_ make -C "$srcdir" olddefconfig

	[ "$XBMK_RELEASE" != "y" ] && [ "$skip_gbe_cook" != "y" ] && \
	    $if_build $if_not_dry_build \
		x_ cook_ifd_mac_address; :
}

# cook CONFIG_GBE_BIN_PATH in config; copy the actual
# one to a tmpfile and modify the MAC address. this
# avoids the user having to change it post-build.
# NOTE: this function is not called during release builds
cook_ifd_mac_address()
{
	cook_rval=0

	gbepath=""
	MAINBOARDDIR="" # populated later
	VARIANT_DIR="" # populated later
	CONFIG_VARIANT_DIR=""
	CONFIG_MAINBOARD_DIR=""

	# variables inside defconfig
	obj="$srcdir/build"

	set +u +e
	(
	x_ cp "$srcdir/$cfgname" "$xbtmp/gbecfg"

	x_ sed -i -e 's,$(,${,g' "$xbtmp/gbecfg"
	x_ sed -i -e 's,),},g' "$xbtmp/gbecfg"

	. "$xbtmp/gbecfg" 2>/dev/null || :; :

	# again so we get proper strings
	MAINBOARDDIR="$CONFIG_MAINBOARD_DIR"
	VARIANT_DIR="$CONFIG_VARIANT_DIR"
	. "$xbtmp/gbecfg" || \
	    err "can't read defconfig" cook_ifd_mac_address "$@"; :

	[ -z "$CONFIG_GBE_BIN_PATH" ] && \
		exit 0; # GBE file not specified

	gbepath="$(findpath "$srcdir/$CONFIG_GBE_BIN_PATH" || \
	    err "can't resolve path" cook_ifd_mac_address "$@")" || \
	    err "can't resolve path" cook_ifd_mac_address "$@"; :

	x_ cp "$gbepath" "$xbtmp/gbecook.bin"

	x_ make -C util/nvmutil clean 1>/dev/null 2>/dev/null
	x_ make -C util/nvmutil 1>/dev/null 2>/dev/null
	x_ util/nvmutil/nvmutil "$xbtmp/gbecook.bin" setmac "$XBMK_MAC"

	x_ grep -v CONFIG_GBE_BIN_PATH "$srcdir/$cfgname" > "$xbtmp/gbecfg" \
	    || err "can't strip GBE" cook_ifd_mac_address "$@"; :

	printf "CONFIG_GBE_BIN_PATH=\"%s\"\n" "$xbtmp/gbecook.bin" \
	    >> "$xbtmp/gbecfg" || \
	    err "can't cook GbE path" cook_ifd_mac_address "$@"; :

	x_ mv "$xbtmp/gbecfg" "$srcdir/$cfgname"

	) || cook_rval=1; :

	set -u -e
	[ $cook_rval -gt 0 ] && \
		err "can't cook GbE file" cook_ifd_mac_address "$@"; :
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

cook_coreboot_build()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	[ "$skip_unlock" != "y" ] && $if_build $if_not_skip_build \
		x_ unlock_rom; :

	$if_not_skip_build pad_one_byte "$srcdir/build/coreboot.rom"
}

unlock_rom()
{
	# We will analyse IFD/ME inclusion and modify the ROM. it's more
	# more reliable to do it here, on the full config (not defconfig)

	eval "$(newvar ifd_quirk ifd_chipset is_ifd is_me)"

	while read -r cfgread; do
		[ -n "$ifd_chipset" ] || \
		    [ "$cfgread" = "${cfgread#"CONFIG_IFD_CHIPSET="}" ] || \
			ifd_chipset="$cfgread"; :

		[ "$cfgread" = "CONFIG_HAVE_IFD_BIN=y" ] && \
			is_ifd="y"; :
		[ "$cfgread" = "CONFIG_HAVE_ME_BIN=y" ] && \
			is_me="y"; :

	done < "$srcdir/$cfgname" || \
	    err "can't read '$srcdir/$cfgname'" unlock_rom "$@"

	[ "$is_ifd" != "y" ] && \
		return 0; :

	ifd_chipset="$(printf "%s\n" "$ifd_chipset" | awk -F '"' '{print $2}' \
	    || err "couldn't run awk" unlock_rom "$@")" \
	    || err "couldn't run awk" unlock_rom "$@"

	# for ifdtool platform quirks
	for ifd_test in adl aplk cnl lbg dnv ehl glk icl ifd2 jsl mtl sklkbl \
	    tgl ptl nvl wbg; do
		[ "$ifd_test" = "$ifd_chipset" ] || \
			continue; :

		ifd_quirk="-p $ifd_test"
		break
	done

	u_ifdtool="elf/coreboot/$tree/ifdtool"

	# unlock IFD regions (set read-write)
	x_ "$u_ifdtool" $ifd_quirk -u "$srcdir/build/coreboot.rom" \
	    -O "$srcdir/build/coreboot.rom"

	[ "$is_me" != "y" ] && \
		return 0; :

	# disable ME via HAP bit
	x_ "$u_ifdtool" $ifd_quirk -M 1 "$srcdir/build/coreboot.rom" \
	    -O "$srcdir/build/coreboot.rom"; :
}

mkcorebootbin()
{
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 0

	[ -z "$defscan" ] && err "defscan empty" mkcorebootbin "$@"

	for y in "$defscan"/*; do
		[ -f "$y" ] || continue

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

	[ "$payload_memtest" != "y" ] && \
		payload_memtest="n"
	[ "$(uname -m)" != "x86_64" ] && \
		payload_memtest="n"

	$if_skip_build \
		return 0

	prep_tmprom

	if ! grep "CONFIG_PAYLOAD_NONE=y" "$buildcfg" 1>/dev/null; then
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

	# we add GRUB EFI to EDK2 images, as a boot option, so now
	# we will use the GRUB logic; in this context, GRUB is already
	# inserted and the logic to insert the coreboot one is skipped

	# actual rom will begin with edk2_, not grub_, due to srcdir path
	mk_coreboot_with grub
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

	[ "$grubtree" != "efi" ] && \
		x_ add_grub_coreboot_payloads; :

	fx_ "cprom_grub" x_ find "$grubdata/keymap" -type f -name "*.gkb"
}

# only used on payload_grub (not edk2 grub)
add_grub_coreboot_payloads()
{
	cbfs "$tmprom" "elf/grub/$grubtree/$grubtree/noconfig/grub.elf" \
	    "fallback/payload"

	add_memtest86plus
}

add_memtest86plus()
{
	[ "$payload_memtest" = "y" ] && \
		cbfs "$tmprom" \
		    "elf/memtest86plus/default/default/noconfig/mt86plus" \
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
	if [ "$grubtree" = "efi" ]; then
		cprom edk2 "$@" # grub efi inside edk2 payload
	else
		cprom grub "$@"
	fi
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

