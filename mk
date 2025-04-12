#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2022-2023 Alper Nebi Yasak <alpernebiyasak@gmail.com>

set -u -e

if [ "./${0##*/}" != "${0}" ] || [ ! -f "mk" ] || [ -L "mk" ]; then
	printf "You must run this in the proper work directory.\n" 1>&2
	exit 1
fi

. "include/lib.sh"
. "include/vendor.sh"
. "include/mrc.sh"

eval "`setvars "" vdir src_dirname srcdir mode xp ser`"
err="fail"

main()
{
	[ $# -lt 1 ] && $err "bad command"
	rval=0

	for g in "command -v git" "git config --global user.name" \
	    "git config --global user.email" "git_init"; do
		eval "$g 1>/dev/null 2>/dev/null || $err \"Unconfigured: $g\""
	done

	case "$1" in
	version) printf "%s\nWebsite: %s\n" "$relname" "$projectsite" ;;
	release)
		shift 1
		mkrelease "$@" ;;
	inject)
		shift 1
		vendor_inject "$@" ;;
	download)
		shift 1
		vendor_download "$@" ;;
	-*)
		rval=1 ;;
	*) $err "bad command"
	esac
	set -u -e # some commands disable them. turn them on!
	return $rval
}

git_init()
{
	[ -L ".git" ] && return 1
	[ -e ".git" ] && return 0
	eval "`setvars "$(date -Rud @$versiondate)" cdate _nogit`"

	git init || return 1
	git add -A . || return 1
	git commit -m "$projectname $version" --date "$cdate" \
	    --author="xbmk <xbmk@example.com>" || return 1
	git tag -a "$version" -m "$projectname $version" || return 1
}

mkrelease()
{
	export XBMK_RELEASE="y"

	vdir="release"
	while getopts d:m: option; do
		[ -z "$OPTARG" ] && $err "empty argument not allowed"
		case "$option" in
		d) vdir="$OPTARG" ;;
		m) mode="$OPTARG" ;;
		*) $err "invalid option '-$option'" ;;
		esac
	done

	vdir="$vdir/$version"
	src_dirname="${relname}_src"
	srcdir="$vdir/$src_dirname"

	[ -e "$vdir" ] && $err "already exists: \"$vdir\""
	mkdir -p "$vdir" || $err "mkvdir: !mkdir -p \"$vdir\""
	git clone . "$srcdir" || $err "mkdir: !gitclone \"$srcdir\""
	touch "$srcdir/lock" || $err "can't make lock file in $srcdir/"

	build_release

	printf "\n\nDONE! Check release files under %s\n" "$vdir"
}

build_release()
{
	(
	cd "$srcdir" || $err "$vdir: !cd \"$srcdir\""
	./mk -f
	x_ rm -Rf tmp
	rmgit .
	x_ mv src/docs docs
	) || $err "can't create release files"

	git log --graph --pretty=format:'%Cred%h%Creset %s %Creset' \
	    --abbrev-commit > "$srcdir/CHANGELOG" || $err "!gitlog $srcdir"
	rm -f "$srcdir/lock" || $err "can't remove lock file in $srcdir"

	(
	cd "${srcdir%/*}" || $err "$vdir: mktarball \"$srcdir\""
	mktarball "${srcdir##*/}" "${srcdir##*/}.tar.xz" || $err "$vdir: mksrc"
	) || $err "can't create src tarball"
	[ "$mode" = "src" ] && return 0

	touch "$srcdir/lock" || $err "can't make lock file in $srcdir/"
	(
	cd "$srcdir" || $err "$vdir: 2 !cd \"$srcdir\""
	mk -b coreboot pico-serprog stm32-vserprog pcsx-redux
	x_ mv bin ../roms
	) || $err "can't build rom images"

	rm -Rf "$srcdir" || $err "!rm -Rf $srcdir"
}

fail()
{
	tmp_cleanup || printf "WARNING: can't rm tmpfiles: %s\n" "$TMPDIR" 1>&2
	err_ "${1}"
}

tmp_cleanup()
{
	[ "$xbmk_parent" = "y" ] || return 0
	[ "$TMPDIR" = "/tmp" ] || rm -Rf "$TMPDIR" || return 1
	rm -f lock || return 1
}

if main "$@"; then
	tmp_cleanup || err_ "can't rm TMPDIR upon non-zero exit: $TMPDIR"
	exit 0
fi

# what follows was formerly script/trees, whose main() is now trees()

. "include/git.sh"

eval "`setvars "" xarch srcdir premake gnatdir xlang mode makeargs elfdir cmd \
    project target target_dir targets xtree _f release bootstrapargs mkhelper \
    autoconfargs listfile autogenargs btype tree rev tree_depend build_depend \
    defconfig postmake mkhelpercfg dry dest_dir mdir cleanargs gccver gccfull \
    gnatver gnatfull gccdir cmakedir do_make badhash`"

trees()
{
	flags="f:b:m:u:c:x:s:l:n:d:"

	[ $# -lt 1 ] && $err "No argument provided"
	[ "${1%-*}" = "$1" ] && $err \
	    "First argument must be a flag ($flags)"

	while getopts $flags option; do
		[ -n "$_f" ] && $err "only one flag is permitted"
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
		*) $err "invalid option '-$option'" ;;
		esac

		if [ -z "${OPTARG+x}" ]; then
			shift 1
			break
		fi

		project="${OPTARG#src/}"
		shift 2
	done
	[ -z "$_f" ] && $err "missing flag ($flags)"
	if [ -z "$project" ]; then
		mk $_f $(ls -1 config/git)
		return 1
	fi

	[ -f "config/git/$project/pkg.cfg" ] || $err "'$project' not defined"

	for d in "elf" "config/data" "config" "src"; do
		eval "${d#*/}dir=\"$d/$project\""
	done
	dest_dir="$elfdir"

	listfile="$datadir/build.list"
	[ -f "$listfile" ] || listfile="" # optional on all projects

	mkhelpercfg="$datadir/mkhelper.cfg"
	if e "$mkhelpercfg" f missing; then
		mkhelpercfg="$TMPDIR/mkhelper.cfg"
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
	[ -d "$configdir" ] || $err "directory, $configdir, does not exist"
	[ $# -gt 0 ] || targets="$(ls -1 "$configdir")" || $err "!o $configdir"

	for x in $targets; do
		unset CROSS_COMPILE
		export PATH="$xbmkpath"
		[ "$x" = "list" ] && x_ ls -1 "config/$project" && \
		    listfile="" && break
		target="$x"
		printf "'make %s', '%s', '%s'\n" "$mode" "$project" "$target"
		x_ handle_defconfig
		mkhelp "$postmake"
	done; :
}

handle_defconfig()
{
	target_dir="$configdir/$target"

	[ -f "CHANGELOG" ] || fetch_project "$project"
	configure_project "$target_dir" || return 0
	x_ mkdir -p "$elfdir/$target"

	chkvars tree
	srcdir="src/$project/$tree"

	if [ "$mode" = "distclean" ] || [ "$mode" = "crossgcc-clean" ]; then
		[ -d "$srcdir" ] || return 0
	fi
	[ -z "$mode" ] && $dry check_cross_compiler

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
	    tree_depend makeargs btype mkhelper bootstrapargs premake release \
	    xarch xlang badhash`"
	_tcfg="$1/target.cfg"
	[ -f "$_tcfg" ] || btype="auto"
	[ -f "$datadir/mkhelper.cfg" ] && \
	    eval "`setcfg "$datadir/mkhelper.cfg"`"

	while [ -f "$_tcfg" ] || [ "$cmd" != "build_project" ]; do
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
	[ -f "CHANGELOG" ] || check_project_hashes

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
		[ -z "$bd_p" ] && $dry $err "$project/$tree: !bd '$bd'"
		[ "${bd##*/}" = "$bd" ] && bd_t=""
		[ -z "$bd_p" ] || $dry ./mk -b $bd_p $bd_t \
		    || $err "!mk $project/$tree $bd_p/$bd_t"; :
	done; :
}

check_project_hashes()
{
	mkdir -p "$XBMK_CACHE/hash" || $err "!mkdir '$XBMK_CACHE/hash'"
	old_pjhash=""
	[ ! -f "$XBMK_CACHE/hash/$project$tree" ] || \
	    read -r old_pjhash < "$XBMK_CACHE/hash/$project$tree"

	x_ rm -f "$TMPDIR/project.list" "$TMPDIR/project.hash" \
	    "$TMPDIR/project.tmp"
	x_ touch "$TMPDIR/project.tmp"
	x_ touch "$TMPDIR/project.hash"

	for rmchk in "$datadir" "$configdir/$tree" "$mdir"; do
		[ -d "$rmchk" ] || continue
		find "$rmchk" -type f -not -path "*/.git*/*" >> \
		    "$TMPDIR/project.tmp" || $err "!find $rmchk > project.tmp"
	done
	sort "$TMPDIR/project.tmp" > "$TMPDIR/project.list" || \
	    $err "!sort project tmp/list"

	while read -r rmchk; do
		[ ! -f "$rmchk" ] || sha512sum "$rmchk" | awk \
		    '{print $1}' >> "$TMPDIR/project.hash" || $err "!h $rmchk"
	done < "$TMPDIR/project.list"

	pjhash="$(sha512sum "$TMPDIR/project.hash" | awk '{print $1}')" || :
	[ "$pjhash" != "$old_pjhash" ] && badhash="y"
	[ -f "$XBMK_CACHE/hash/$project$tree" ] || badhash="y"

	printf "%s\n" "$pjhash" > "$XBMK_CACHE/hash/$project$tree" || \
	    $err "!mk $XBMK_CACHE/hash/$project$tree"

	[ "$badhash" != "y" ] || x_ rm -Rf "src/$project/$tree" \
	    "elf/$project/$tree" "elf/$project/$target"; :
}

check_cross_compiler()
{
	xgccargs="UPDATED_SUBMODULES=1 CPUS=$XBMK_THREADS"
	for _xarch in $xarch; do
		cbdir="src/coreboot/$tree"
		[ "$project" != "coreboot" ] && cbdir="src/coreboot/default"
		[ -n "$xtree" ] && cbdir="src/coreboot/$xtree"

		x_ ./mk -f coreboot "${cbdir#src/coreboot/}"

		export PATH="$xbmkpwd/$cbdir/util/crossgcc/xgcc/bin:$PATH"
		export CROSS_COMPILE="${xarch% *}-"
		[ -n "$xlang" ] && export BUILD_LANGUAGES="$xlang"

		xfix="${_xarch%-*}" && [ "$xfix" = "x86_64" ] && xfix="x64"

		# match gnat-X to gcc
		check_gnu_path gcc gnat || check_gnu_path gnat gcc || \
		    $err "Cannot match host GCC/GNAT versions"

		# sometimes buildgcc fails for like no reason. try twice.
		make -C "$cbdir" crossgcc-$xfix $xgccargs || \
		    x_ make -C "$cbdir" crossgcc-$xfix $xgccargs

		# we only want to mess with hostcc to build xgcc
		rm -f "$XBMK_CACHE/gnupath/"* || \
		    $err "Cannot clear gnupath/"; :
	done; :
}

# fix mismatching gcc/gnat versions on debian trixie/sid. as of december 2024,
# trixie/sid had gnat-13 as gnat and gcc-14 as gcc, but has gnat-14 in apt. in
# some cases, gcc 13+14 and gnat-13 are present; or gnat-14 and gcc-14, but
# gnat in PATH never resolves to gnat-14, because gnat-14 was "experimental"
check_gnu_path()
{
	[ $# -lt 2 ] && $err "check_gnu_path: Too few arguments"
	[ "$1" = "$2" ] && $err "check_gnu_path: Both arguments identical"
	for _gnuarg in 1 2; do
		eval "[ \"\$$_gnuarg\" = \"gcc\" ] && continue"
		eval "[ \"\$$_gnuarg\" = \"gnat\" ] && continue"
		$err "check_gnu_path: Invalid argument \"$_gnuarg\""
	done
	command -v "$1" 1>/dev/null || $err "Host '$1' unavailable"

	eval "`setvars "" gccver gccfull gnatver gnatfull gccdir gnatdir`"
	gnu_setver "$1" "$1" || $err "Command '$1' unavailable."
	gnu_setver "$2" "$2" || :

	eval "[ -z \"\$$1ver\" ] && $err \"Cannot detect host '$1' version\""
	[ "$gnatfull" = "$gccfull" ] && return 0

	eval "$1dir=\"$(dirname "$(command -v "$1")")\""
	eval "_gnudir=\"\$$1dir\"; _gnuver=\"\$$1ver\""
	for _gnubin in "$_gnudir/$2-"*; do
		[ -f "$_gnubin" ] || continue
		[ "${_gnubin#"$_gnudir/$2-"}" = "$_gnuver" ] || continue
		_gnuver="${_gnubin#"$_gnudir/$2-"}"; break
	done
	gnu_setver "$2" "$_gnudir/$2-$_gnuver" || return 1
	[ "$gnatfull" = "$gccfull" ] || return 1

	(
	rm -f "$XBMK_CACHE/gnupath/"* || $err "Cannot clear gnupath/"
	cd "$XBMK_CACHE/gnupath" || $err "Can't cd to gnupath/"
	for _gnubin in "$_gnudir/$2"*"-$_gnuver"; do
		[ -e "$_gnubin" ] || continue
		_gnuutil="${_gnubin##*/}"
		x_ ln -s "$_gnubin" "${_gnuutil%"-$_gnuver"}"
	done
	) || $err "Cannot create $2-$_gnuver link in $_gnudir"; :
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
	[ -f "$defconfig" ] || $dry $err "$project/$target: missing defconfig"
	dest_dir="$elfdir/$target/${defconfig#"$target_dir/config/"}"

	$dry elfcheck || return 1 # skip build if a previous one exists
	$dry x_ mkdir -p "$dest_dir"
}

elfcheck()
{
	# TODO: very hacky check. do it properly (based on build.list)
	for elftest in "$dest_dir"/*; do
		[ -e "$elftest" ] && e "$elftest" f && return 1
	done; :
}

handle_makefile()
{
	$dry check_makefile "$srcdir" && x_ make -C "$srcdir" $cleanargs clean
	[ -f "$defconfig" ] && x_ cp "$defconfig" "$srcdir/.config"
	[ -n "$mode" ] || [ -n "$btype" ] || $dry make -C \
	    "$srcdir" silentoldconfig || make -C "$srcdir" oldconfig || :

	run_make_command || $err "handle_makefile $srcdir: no makefile!"

	_copy=".config" && [ "$mode" = "savedefconfig" ] && _copy="defconfig"
	[ "${mode%config}" = "$mode" ] || \
	    $dry x_ cp "$srcdir/$_copy" "$defconfig"

	[ -e "$srcdir/.git" ] && [ "$project" = "u-boot" ] && \
	    [ "$mode" = "distclean" ] && \
		$dry x_ git -C "$srcdir" $cleanargs clean -fdx; :
}

run_make_command()
{
	mkhelp "$premake"
	$dry check_cmake "$srcdir" && [ -z "$mode" ] && \
	    $dry check_autoconf "$srcdir"
	$dry check_makefile "$srcdir" || return 1

	$dry make -C "$srcdir" $mode -j$XBMK_THREADS $makeargs || $err "!$mode"
	mkhelp "$mkhelper"

	[ "$mode" != "clean" ] || \
	    $dry make -C "$srcdir" $cleanargs distclean || :; :
}

check_cmake()
{
	[ -z "$cmakedir" ] || $dry check_makefile "$1" || cmake -B "$1" \
	    "$1/$cmakedir" || $dry check_makefile "$1" || $err \
	    "$1: !cmk $cmakedir"
	[ -z "$cmakedir" ] || $dry check_makefile "$1" || \
	    $err "check_cmake $1: can't generate Makefile"; :
}

check_autoconf()
{
	(
	cd "$1" || $err "!cd $1"
	[ -f "bootstrap" ] && x_ ./bootstrap $bootstrapargs
	[ -f "autogen.sh" ] && x_ ./autogen.sh $autogenargs
	[ -f "configure" ] && x_ ./configure $autoconfargs; :
	) || $err "can't bootstrap project: $1"; :
}

check_makefile()
{
	[ -f "$1/Makefile" ] || [ -f "$1/makefile" ] || \
	    [ -f "$1/GNUmakefile" ] || return 1; :
}

mkhelp()
{
	[ -z "$1" ] || [ -n "$mode" ] || "$1" || $err "mkhelp: !$1"; :
}

copy_elf()
{
	[ -f "$listfile" ] && x_ mkdir -p "$dest_dir" && while read -r f; do
		[ -f "$srcdir/$f" ] && x_ cp "$srcdir/$f" "$dest_dir"
	done < "$listfile"
	x_ make clean -C "$srcdir" $cleanargs
}

if trees "$@"; then
	. "$mkhelpercfg"
	$cmd
fi

tmp_cleanup || err_ "can't rm TMPDIR upon non-zero exit: $TMPDIR"
