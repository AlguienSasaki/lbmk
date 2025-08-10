# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2022-2023 Alper Nebi Yasak <alpernebiyasak@gmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

eval "`setvars "" xarch srcdir premake gnatdir xlang mode makeargs elfdir cmd \
    project target target_dir targets xtree _f release bootstrapargs mkhelper \
    autoconfargs listfile autogenargs btype rev build_depend gccdir cmakedir \
    defconfig postmake mkhelpercfg dry dest_dir mdir cleanargs gccver gccfull \
    gnatver gnatfull do_make badhash badtghash tree`"

trees()
{
	flags="f:b:m:u:c:x:s:l:n:d:"

	while getopts $flags option; do
		[ -n "$_f" ] && err "only one flag is permitted"
		_f="$1"

		case "$_f" in
		-d) dry=":" ;;
		-b) : ;;
		-u) mode="oldconfig" ;;
		-m) mode="menuconfig" ;;
		-c) mode="distclean" ;;
		-x) mode="crossgcc-clean" ;;
		-f)
			do_make="n"
			dry=":" ;;
		-s) mode="savedefconfig" ;;
		-l) mode="olddefconfig" ;;
		-n) mode="nconfig" ;;
		*) err "invalid option '-$option'" ;;
		esac

		if [ -z "${OPTARG+x}" ]; then
			shift 1
			break
		fi

		project="${OPTARG#src/}"
		project="${project#config/git/}"
		shift 2
	done
	[ -z "$_f" ] && err "missing flag ($flags)"
	[ -z "$project" ] && fx_ "x_ ./mk $_f" x_ ls -1 config/git && return 1

	[ -f "config/git/$project/pkg.cfg" ] || \
	    err "config/git/$project/pkg.cfg missing"

	for d in "elf" "config/data" "config" "src"; do
		eval "${d#*/}dir=\"$d/$project\""
	done
	dest_dir="$elfdir"

	listfile="$datadir/build.list"
	[ -f "$listfile" ] || listfile="" # optional on all projects

	mkhelpercfg="$datadir/mkhelper.cfg"
	if e "$mkhelpercfg" f missing; then
		mkhelpercfg="$xbtmp/mkhelper.cfg"
		x_ touch "$mkhelpercfg"
	fi

	targets="$*"
	cmd="build_targets $targets"
	singletree "$project" && cmd="build_project"

	remkdir "${tmpgit%/*}"
}

build_project()
{
	configure_project "$configdir" || return 0
	[ ! -f "$listfile" ] || $dry elfcheck || return 0

	[ "$mode" = "distclean" ] && mode="clean"
	run_make_command || return 0

	[ -n "$mode" ] || $dry copy_elf; :
}

build_targets()
{
	[ -d "$configdir" ] || err "directory, $configdir, does not exist"
	[ $# -gt 0 ] || targets="$(ls -1 "$configdir")" || err "!o $configdir"

	for x in $targets; do
		unset CROSS_COMPILE
		export PATH="$xbmkpath"
		[ "$x" = "list" ] && x_ ls -1 "config/$project" && \
		    listfile="" && break

		target="$x"
		printf "'make %s', '%s', '%s'\n" "$mode" "$project" "$target"
		x_ handle_defconfig

		[ -n "$mode" ] || x_ $postmake
	done; :
}

handle_defconfig()
{
	target_dir="$configdir/$target"

	[ -f "CHANGELOG" ] || fetch_project "$project"
	configure_project "$target_dir" || return 0

	chkvars tree
	srcdir="src/$project/$tree"

	[ "$mode" = "${mode%clean}" ] && [ ! -d "$srcdir" ] && return 0

	[ -z "$mode" ] && for _xarch in $xarch; do
		$dry check_cross_compiler "$_xarch"
	done; :

	for y in "$target_dir/config"/*; do
		[ "$_f" = "-d" ] || [ -f "$y" ] || continue
		[ "$_f" = "-d" ] || defconfig="$y"

		[ -n "$mode" ] || check_defconfig || continue
		handle_makefile
		[ -n "$mode" ] || $dry copy_elf
	done; :
}

configure_project()
{
	eval "`setvars "" cleanargs build_depend autoconfargs xtree postmake \
	    makeargs btype mkhelper bootstrapargs premake release xlang xarch \
	    badhash badtghash`"
	_tcfg="$1/target.cfg"
	[ -f "$_tcfg" ] || btype="auto"
	e "$datadir/mkhelper.cfg" f && eval "`setcfg "$datadir/mkhelper.cfg"`"

	while e "$_tcfg" f || [ "$cmd" != "build_project" ]; do
		eval "`setvars "" rev tree`"
		eval "`setcfg "$_tcfg"`"
		printf "Loading %s config: %s\n" "$project" "$_tcfg"

		[ "$_f" = "-d" ] && build_depend="" # dry run
		[ "$cmd" = "build_project" ] && break
		[ "$do_make" != "n" ] && break

		[ "${_tcfg%/*/target.cfg}" = "${_tcfg%"/$tree/target.cfg"}" ] \
		    && break
		_tcfg="${_tcfg%/*/target.cfg}/$tree/target.cfg"
	done
	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && return 1
	[ -z "$btype" ] || [ "${mode%config}" = "$mode" ] || return 1
	[ -z "$mode" ] && $dry build_dependencies

	mdir="$xbmkpwd/config/submodule/$project"
	[ -n "$tree" ] && mdir="$mdir/$tree"
	[ -f "CHANGELOG" ] || delete_old_project_files

	if [ "$do_make" = "n" ]; then
		[ -f "CHANGELOG" ] || fetch_${cmd#build_}
		return 1
	fi
	x_ ./mk -f "$project" "$target"
}

build_dependencies()
{
	for bd in $build_depend; do
		bd_p="${bd%%/*}"
		bd_t="${bd##*/}"
		[ -z "$bd_p" ] && $dry err "$project/$tree: !bd '$bd'"
		[ "${bd##*/}" = "$bd" ] && bd_t=""
		[ -z "$bd_p" ] || $dry x_ ./mk -b $bd_p $bd_t; :
	done; :
}

delete_old_project_files()
{
	project_up_to_date hash "$tree" badhash "$datadir" "$configdir/$tree" \
	    "$mdir" || x_ rm -Rf "src/$project/$tree" "elf/$project/$tree"; :

	singletree "$project" || [ -z "$target" ] || [ "$target" = "$tree" ] \
	    || project_up_to_date tghash "$target" badtghash \
	    "$configdir/$target" || x_ rm -Rf "elf/$project/$tree/$target"; :
}

project_up_to_date()
{
	eval "`setvars "" old_hash hash`"
	hashname="$1"
	hashtype="$2"
	badhashvar="$3"
	shift 3

	x_ mkdir -p "$XBMK_CACHE/$hashname"

	[ ! -f "$XBMK_CACHE/$hashname/$project$hashtype" ] || \
	    read -r old_hash < "$XBMK_CACHE/$hashname/$project$hashtype" || \
	    err "$hashname: read err '$XBMK_CACHE/$hashname/$project$hashtype'"

	fx_ "x_ sha512sum" find "$@" -type f -not -path "*/.git*/*" | awk \
	    '{print $1}' > "$xbtmp/tmp.hash" || err "!h $project $hashname"

	hash="$(x_ sha512sum "$xbtmp/tmp.hash" | awk '{print $1}' || \
	    err)" || err "$hashtype: Can't read sha512 of '$xbtmp/tmp.hash'"

	[ "$hash" != "$old_hash" ] && eval "$badhashvar=\"y\""
	[ -f "$XBMK_CACHE/$hashname/$project$hashtype" ] || \
	    eval "$badhashvar=\"y\""

	printf "%s\n" "$hash" > "$XBMK_CACHE/$hashname/$project$hashtype" || \
	    err "!mk $XBMK_CACHE/$hashname/$project$hashtype"

	eval "[ \"\$$badhashvar\" = \"y\" ] && return 1"; :
}

check_cross_compiler()
{
	cbdir="src/coreboot/$tree"
	[ "$project" != "coreboot" ] && cbdir="src/coreboot/default"
	[ -n "$xtree" ] && cbdir="src/coreboot/$xtree"

	x_ ./mk -f coreboot "${cbdir#src/coreboot/}"

	export PATH="$xbmkpwd/$cbdir/util/crossgcc/xgcc/bin:$PATH"
	export CROSS_COMPILE="${xarch% *}-"
	[ -n "$xlang" ] && export BUILD_LANGUAGES="$xlang"

	# match gnat-X to gcc
	check_gnu_path gcc gnat || x_ check_gnu_path gnat gcc

	xfix="${1%-*}" && [ "$xfix" = "x86_64" ] && xfix="x64"
	xgccargs="crossgcc-$xfix UPDATED_SUBMODULES=1 CPUS=$XBMK_THREADS"
	make -C "$cbdir" $xgccargs || x_ make -C "$cbdir" $xgccargs

	# we only want to mess with hostcc to build xgcc
	remkdir "$XBMK_CACHE/gnupath"
}

# fix mismatching gcc/gnat versions on debian trixie/sid. as of december 2024,
# trixie/sid had gnat-13 as gnat and gcc-14 as gcc, but has gnat-14 in apt. in
# some cases, gcc 13+14 and gnat-13 are present; or gnat-14 and gcc-14, but
# gnat in PATH never resolves to gnat-14, because gnat-14 was "experimental"
check_gnu_path()
{
	command -v "$1" 1>/dev/null || err "Host '$1' unavailable"

	eval "`setvars "" gccver gccfull gnatver gnatfull gccdir gnatdir`"
	x_ gnu_setver "$1" "$1" || err "Command '$1' unavailable."
	gnu_setver "$2" "$2" || :

	eval "[ -z \"\$$1ver\" ] && err \"Cannot detect host '$1' version\""
	[ "$gnatfull" = "$gccfull" ] && return 0

	eval "$1dir=\"$(dirname "$(command -v "$1")")\""
	eval "_gnudir=\"\$$1dir\"; _gnuver=\"\$$1ver\""
	for _bin in "$_gnudir/$2-"*; do
		[ "${_bin#"$_gnudir/$2-"}" = "$_gnuver" ] && [ -x "$_bin" ] \
		    && _gnuver="${_bin#"$_gnudir/$2-"}" && break; :
	done
	gnu_setver "$2" "$_gnudir/$2-$_gnuver" || return 1
	[ "$gnatfull" = "$gccfull" ] || return 1

	(
	remkdir "$XBMK_CACHE/gnupath" && x_ cd "$XBMK_CACHE/gnupath"
	for _gnubin in "$_gnudir/$2"*"-$_gnuver"; do
		_gnuutil="${_gnubin##*/}" && [ -e "$_gnubin" ] && \
		    x_ ln -s "$_gnubin" "${_gnuutil%"-$_gnuver"}"
	done
	) || err "Cannot create $2-$_gnuver link in $_gnudir"; :
}

gnu_setver()
{
	eval "$2 --version 1>/dev/null 2>/dev/null || return 1"
	eval "$1ver=\"`"$2" --version 2>/dev/null | head -n1`\""
	eval "$1ver=\"\${$1ver##* }\""
	eval "$1full=\"\$$1ver\""
	eval "$1ver=\"\${$1ver%%.*}\""; :
}

check_defconfig()
{
	[ -f "$defconfig" ] || $dry err "$project/$target: missing defconfig"
	dest_dir="$elfdir/$tree/$target/${defconfig#"$target_dir/config/"}"

	$dry elfcheck || return 1; : # skip build if a previous one exists
}

elfcheck()
{
	# TODO: *STILL* very hacky check. do it properly (based on build.list)
	( fx_ "eval exit 1 && err" find "$dest_dir" -type f ) || return 1; :
}

handle_makefile()
{
	$dry check_makefile "$srcdir" && \
	    $dry x_ make -C "$srcdir" $cleanargs clean

	[ -f "$defconfig" ] && x_ cp "$defconfig" "$srcdir/.config"

	run_make_command || err "handle_makefile $srcdir: no makefile!"

	_copy=".config" && [ "$mode" = "savedefconfig" ] && _copy="defconfig"
	[ "${mode%config}" = "$mode" ] || \
	    $dry x_ cp "$srcdir/$_copy" "$defconfig"

	[ -e "$srcdir/.git" ] && [ "$project" = "u-boot" ] && \
	    [ "$mode" = "distclean" ] && \
		$dry x_ git -C "$srcdir" $cleanargs clean -fdx; :
}

run_make_command()
{
	[ -n "$mode" ] || x_ $premake

	$dry check_cmake "$srcdir" && [ -z "$mode" ] && \
	    $dry check_autoconf "$srcdir"
	$dry check_makefile "$srcdir" || return 1

	$dry x_ make -C "$srcdir" $mode -j$XBMK_THREADS $makeargs
	[ -n "$mode" ] || x_ $mkhelper

	check_makefile "$srcdir" || return 0
	[ "$mode" != "clean" ] || \
	    $dry make -C "$srcdir" $cleanargs distclean || \
	    $dry x_ make -C "$srcdir" $cleanargs clean; :
}

check_cmake()
{
	[ -z "$cmakedir" ] || $dry check_makefile "$1" || cmake -B "$1" \
	    "$1/$cmakedir" || $dry x_ check_makefile "$1"
	[ -z "$cmakedir" ] || $dry x_ check_makefile "$1"; :
}

check_autoconf()
{
	(
	x_ cd "$1"
	[ -f "bootstrap" ] && x_ ./bootstrap $bootstrapargs
	[ -f "autogen.sh" ] && x_ ./autogen.sh $autogenargs
	[ -f "configure" ] && x_ ./configure $autoconfargs; :
	) || err "can't bootstrap project: $1"; :
}

check_makefile()
{
	[ -f "$1/Makefile" ] || [ -f "$1/makefile" ] || \
	    [ -f "$1/GNUmakefile" ] || return 1; :
}

copy_elf()
{
	[ -f "$listfile" ] && x_ mkdir -p "$dest_dir"
	[ ! -f "$listfile" ] || while read -r f; do
		[ -f "$srcdir/$f" ] && x_ cp "$srcdir/$f" "$dest_dir"; :
	done < "$listfile" || err "copy_elf $*: cannot read '$listfile'"; :
	x_ make clean -C "$srcdir" $cleanargs
}
