# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2023-2026 Leah Rowe <leah@libreboot.org>

eval "`newvar reldest reldir rsrc vdir mktest`"

mktest()
{
	mktest="y"
	x_ mkrelease "$@"
}

mkrelease()
{
	export XBMK_RELEASE="y"
	reldir="release"

	reldest="$reldir/$version"
	[ -e "$reldest" ] && \
		err "already exists: \"$reldest\"" "release" "$@"

	vdir="`mktemp -d || err "can't make vdir"`" || \
	    err "can't make tmp vdir" "release" "$@"
	vdir="$vdir/$version"
	rsrc="$vdir/${relname}_src"

	remkdir "$vdir"
	x_ git clone . "$rsrc"
	update_xbmkver "$rsrc"

	forx 'prep_release %s' src tarball bin

	x_ rm -Rf "$rsrc"

	x_ mkdir -p "$reldir"
	x_ mv "$vdir" "$reldir"
	x_ rm -Rf "${vdir%"/$version"}"

	printf "\n\nDONE! Check release files under %s\n" "$reldest"
}

prep_release()
{
	(
	if [ "$1" != "tarball" ]; then
		x_ cd "$rsrc"
		[ ! -e "cache" ] && \
			x_ ln -s "$XBMK_CACHE" "cache"; :
	fi

	prep_release_$1

	) || err "can't prep release $1" "prep_release" "$@"
}

prep_release_src()
{
	x_ cp -R "util/sbase" "util/sbase2"

	x_ ./mk -f

	forx 'fx_ \"x_ rm -Rf\" x_ find . -name \".%s\"' git gitmodules

	( fx_ nuke x_ find config/module -type f -name "nuke.list" ) || \
	    err "can't prune project files" "prep_release_src" "$@"; :
}

nuke()
{
	r="$rsrc/src/${1#config/module/}"

	if [ -d "${r%/*}" ]; then
		x_ cd "${r%/*}"
		dx_ "x_ rm -Rf" "$rsrc/$1"
	fi
}

prep_release_tarball()
{
	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$rsrc/CHANGELOG" || \
	    err "can't create '$rsrc/CHANGELOG'" "prep_release_tarball" "$@" 

	x_ rm -f "$rsrc/lock" "$rsrc/cache"
	x_ rm -Rf "$rsrc/xbmkwd" "$rsrc/util/sbase"
	x_ mv "$rsrc/util/sbase2" "$rsrc/util/sbase"

	[ "$mktest" = "y" ] && return 0

	(
	x_ cd "${rsrc%/*}"
	x_ mktarball "${rsrc##*/}" "${rsrc##*/}.tar.xz"

	) || err "can't create src tarball" "prep_release_tarball" "$@"; :
}

prep_release_bin()
{
	if [ "$mktest" = "y" ]; then # <- ./mk test
		prep_release_test
		return 0
	fi # ./mk release:

	x_ ./mk -d coreboot

	forx 'x_ ./mk -b' coreboot pico-serprog stm32-vserprog \
	    pcsx-redux

	fx_ mkrom_tarball x_ find bin -mindepth 1 -maxdepth 1 \
	    -type d -name "serprog_*"

	x_ mv bin ../roms
}

prep_release_test()
{
	# when we want to test other parts of lbmk, we may not
	# want to test coreboot roms. we want to test other projects.

	# this is an exhaustive test, but tests coreboot last, which
	# is the most time-consuming part. we often want to test that
	# earlier parts don't fail first.

	for ptest in bios_extract biosutilities deguard docs \
	    edk2 flashprog gpio-scripts grub int libarchive \
	    me_cleaner memtest86plus mxmdump pcsx-redux pico-sdk \
	    pico-serprog seabios stm32-vserprog u-boot uefitool \
	    coreboot; do # coreboot must come last for this test

		for ftest in d f s b; do
			x_ ./mk -$ftest "$ptest"
		done

		for ftest in l u; do
			yes '' | ./mk -$ftest "$ptest" || \
			    err "TEST FAIL: ./mk -$ftest '$ptest'"
		done
	done
}
