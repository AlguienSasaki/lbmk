#!/bin/sh

# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022-2023 Alper Nebi Yasak <alpernebiyasak@gmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>

set -u -e

eval "`printf "LANG LANGUAGE LC_CTYPE LC_NUMERIC LC_TIME LC_COLLATE \
    LC_MONETARY LC_MESSAGES LC_PAPER LC_NAME LC_ADDRESS LC_TELEPHONE \
    LC_MEASUREMENT LC_IDENTIFICATION LC_ALL\n" | \
    awk '{ for (i=1; i<=NF; i++) $i = "export " $i "=C.UTF-8;"; print }'`"

findpath()
{
	eval "$(printf '%s\n' "$@" | awk '{printf "readlink -f \"%s\" \
	    2>/dev/null || realpath \"%s\" 2>/dev/null || return 1;",$0,$0}')"
}

x_()
{
	[ $# -lt 1 ] || "$@" || err "UNHANDLED ERROR" "x_" "$@"; :
}

err()
{
	printf '%s\n' "$@" | awk '{printf "\"%s\" ", $0}'
	printf "ERROR!\n"

	exit 1
}

(
xbarg0="$0"
[ "${xbarg0##*/}" = "$xbarg0" ] && \
    xbarg0="`command -v "$xbarg0"`"

xbmkpwd="$(x_ dirname "$(x_ findpath "$xbarg0")")" || exit 1

x_ cd "$xbmkpwd"

. "include/lib.sh"
. "include/env/init.sh"
x_ xbmk_init "$@"

. "include/env/get.sh"

. "include/fw/vendor.sh"
. "include/fw/mrc.sh"
. "include/fw/inject.sh"
. "include/fw/rom.sh"

. "include/mk/release.sh"

_f="${1-}"
shift 1 2>/dev/null || :

eval "$(printf "version release download inject\n" | awk '{ for (i=1; i<=NF; \
    i++) $i = "[ \""$i"\" = \"$_f\" ] && x_ "$i" \"$@\" && exit 0;"; print }')"

# flag e.g. ./mk -b <-- mkflag would be "b"
flag=""

# macros, overridden depending on the flag
# : means false
if_do_make=""
if_not_do_make=":"
if_make_config=":"
if_not_make_config=""
if_build=":" # can be false even if make is true
if_not_build=""
if_dry_build=":"
if_not_dry_build=""

if_single_tree=":"

eval "`newvar autoconfargs autogenargs badhash badtghash bootstrapargs \
    build_depend buildtype cleanargs cmakedir cmd defconfig dest_dir elfdir \
    forcepull listfile makeargs mdir mkhelper mkhelpercfg mode postmake \
    premake project release rev srcdir target target_dir tree xarch xgcctree \
    xlang`"

main()
{
	flags="f:F:b:m:u:c:x:s:l:n:d:"

	while getopts $flags option
	do
		[ -n "$flag" ] && \
			err "only one flag is permitted" "trees" "$@"

		flag="$1"

		# the "mode" variable is affixed to a make command, example:
		# ./mk -m coreboot does: make menuconfig -C src/coreboot/tree

		case "$flag" in
		-d)
			# -d is similar to -b, except that
			# a large number of operations will be
			# skipped. these are "if_not_dry_build build" scenarios
			# where only a subset of build tasks are done,
			 # and $if_not_dry_build is prefixed to skipped commands

			if_dry_build=""
			if_not_dry_build=":"
			;;
		-b) : ;;
		-u) mode="oldconfig" ;;
		-m) mode="menuconfig" ;;
		-f|-F) # download source code for a project
			# macros. colon means false.
			if_do_make=":"
			if_dry_build=""
			if_not_do_make=""
			if_not_dry_build=":"
			[ "$flag" = "-F" ] && \
				forcepull="y"; : # never skip git fetch/pull
			;;
		-s) mode="savedefconfig" ;;
		-l) mode="olddefconfig" ;;
		-n) mode="nconfig" ;;
		*) err "invalid option '-$option'" "trees" "$@" ;;
		esac

		if [ -z "$mode" ]; then
			if_build=""
			if_not_build=":"
		fi

		if [ "${mode%config}" != "$mode" ]; then
			if_make_config=""
			if_not_make_config=":"
		fi

		if [ -z "${OPTARG+x}" ]; then
			shift 1
			break
		fi

		project="${OPTARG#src/}"
		project="${project#config/git/}"

		shift 2
	done

	if [ -z "$flag" ]; then
		err "missing flag ($flags)" "trees" "$@"
	elif [ -z "$project" ]; then
		fx_ "x_ ./mk $flag" x_ ls -1 config/git
		return 0
	elif [ ! -f "config/git/$project/pkg.cfg" ]; then
		err "config/git/$project/pkg.cfg missing" "trees" "$@"
	fi

	elfdir="elf/$project"
	datadir="config/data/$project"
	configdir="config/$project"
	srcdir="src/$project"
	dest_dir="$elfdir"

	listfile="$datadir/build.list"
	[ ! -f "$listfile" ] && \
		listfile="" # build.list is optional on all projects

	mkhelpercfg="$datadir/mkhelper.cfg"
	if e "$mkhelpercfg" f missing; then
		mkhelpercfg="$xbtmp/mkhelper.cfg"
		x_ touch "$mkhelpercfg"
	fi

	if singletree "$project"; then
		cmd="single_tree_build"
		if_single_tree=""
	else
		cmd="multi_tree_build"
	fi

	remkdir "${tmpgit%/*}"
	x_ touch "$mkhelpercfg"

	. "$mkhelpercfg"
	$cmd "$@"
}

single_tree_build()
{
	configure_project "$configdir" || \
		return 0

	[ ! -f "$listfile" ] || \
		$if_not_dry_build elfcheck || \
			return 0

	run_make_command && \
	    $if_build $if_not_dry_build \
		copy_elf; :
}

multi_tree_build()
{
	[ ! -d "$configdir" ] && \
		err "directory '$configdir' doesn't exist" "multi_tree_build" "$@"
	[ $# -lt 1 ] && \
		fx_ multi_tree_build find "$configdir" \
		    -mindepth 1 -maxdepth 1 -type d
	[ $# -gt 0 ] && for x in "$@"
	do
		unset CROSS_COMPILE
		export PATH="$xbmkpath"

		if [ "$x" = "list" ]; then
			x_ ls -1 "config/$project"
			listfile=""
			break
		fi

		printf "'make %s', '%s', '%s'\n" "$mode" "$project" "$x"

		target="${x##*/}"

		x_ handle_defconfig

		$if_build \
			x_ $postmake; :
	done; :
}

handle_defconfig()
{
	target_dir="$configdir/$target"

	[ ! -f "CHANGELOG" ] && \
		single_tree_fetch "$project"

	configure_project "$target_dir" || \
		return 0

	[ -z "$tree" ] && \
		err "$configdir: 'tree' not set" "handle_defconfig" "$@"

	srcdir="src/$project/$tree"

	[ -d "$srcdir" ] && for y in "$target_dir/config"/*
	do
		[ ! -f "$y" ] && \
			$if_not_dry_build \
				continue

		$if_not_dry_build \
			eval defconfig="$y"

		$if_build \
			check_defconfig || \
			    continue; :

		for _xarch in $xarch; do
			[ -n "$_xarch" ] && $if_not_build $if_dry_build \
				check_cross_compiler "$_xarch"; :
		done; :

		handle_makefile

		$if_build \
			$if_not_dry_build \
				copy_elf; :
	done; :
}

configure_project()
{
	_tcfg="$1/target.cfg"

	eval "`newvar autoconfargs badhash badtghash bootstrapargs \
	    build_depend buildtype cleanargs makeargs mkhelper postmake \
	    premake release xarch xgcctree xlang`"

	[ ! -f "$_tcfg" ] && \
		buildtype="auto"

	# globally initialise all variables for a source tree / target:
	if e "$datadir/mkhelper.cfg" f; then
		. "$datadir/mkhelper.cfg" || \
		    err "Can't read '$datadir/mkhelper.cfg'" \
		    "configure_project" "$@"
	fi

	# override target/tree specific variables from per-target config:
	while e "$_tcfg" f || [ "$cmd" != "single_tree_build" ]
	do
		printf "Loading %s config: %s\n" "$project" "$_tcfg"
		eval "`newvar rev tree`"

		. "$_tcfg" || \
		    err "Can't read '$_tcfg'" "configure_project" "$@"

		$if_dry_build \
			eval build_depend=""

		$if_single_tree \
			break
		$if_do_make \
			break
		[ "${_tcfg%/*/target.cfg}" = "${_tcfg%"/$tree/target.cfg"}" ] \
			&& break # target and tree matching was successful

		_tcfg="${_tcfg%/*/target.cfg}/$tree/target.cfg" # try next
	done

	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 1
	[ -n "$buildtype" ] && \
		$if_make_config \
			return 1

	$if_build \
		$if_not_dry_build \
			build_dependencies

	mdir="$xbmkpwd/config/submodule/$project"
	[ -n "$tree" ] && \
		mdir="$mdir/$tree"

	if [ ! -f "CHANGELOG" ]; then
		delete_old_project_files
		$if_not_do_make \
			${cmd%_build}_fetch
	fi
	$if_not_do_make \
		return 1

	x_ ./mk -f "$project" "$target"
}

# projects can specify which other projects
# to build first, as declared dependencies:

build_dependencies()
{
	for bd in $build_depend
	do
		bd_project="${bd%%/*}"
		bd_tree="${bd##*/}"

		[ -z "$bd_project" ] && \
			$if_not_dry_build \
				err "$project/$tree: !bd '$bd'" \
				    "build_dependencies" "$@"
		[ "${bd##*/}" = "$bd" ] && \
			bd_tree=""
		[ -n "$bd_project" ] && \
			$if_not_dry_build \
				x_ ./mk -b $bd_project $bd_tree; :
	done; :
}

# delete_old_project_files along with project_up_to_date,
# concatenates the sha512sum hashes of all files related to
# a project, tree or target, then gets the sha512sum of that
# concatenation. this is checked against any existing
# calculation previously cached; if the result differs, or
# nothing was previously stored, we know to delete resources
# such as builds, project sources and so on, for auto-rebuild:

delete_old_project_files()
{
	# delete an entire source tree along with its builds:
	project_up_to_date hash "$tree" badhash "$datadir" \
	    "$configdir/$tree" "$mdir" || \
		x_ rm -Rf "src/$project/$tree" "elf/$project/$tree"

	x_ cp "$xbtmp/new.hash" "$XBMK_CACHE/hash/$project$tree"

	if singletree "$project" || [ -z "$target" ] || [ "$target" = "$tree" ]
	then
		return 0
	fi

	# delete only the builds of a given target, but not src.
	# this is useful when only the target config changes, for
	# example x200_8mb coreboot configs change, but not coreboot:

	project_up_to_date tghash "$target" badtghash "$configdir/$target" || \
		x_ rm -Rf "elf/$project/$tree/$target"

	x_ cp "$xbtmp/new.hash" "$XBMK_CACHE/tghash/$project$target"
}

project_up_to_date()
{
	eval "`newvar hash old_hash`"

	hashdir="$1"
	hashname="$2"
	badhashvar="$3"

	shift 3

	x_ mkdir -p "$XBMK_CACHE/$hashdir"

	if [ -f "$XBMK_CACHE/$hashdir/$project$hashname" ]; then
		read -r old_hash < "$XBMK_CACHE/$hashdir/$project$hashname" \
		    || err \
		    "$hashdir: err '$XBMK_CACHE/$hashdir/$project$hashname'" \
		    "project_up_to_date" "$hashdir" "$hashname" "$badhashvar" \
		    "$@"
	fi

	build_sbase
	fx_ "x_ util/sbase/sha512sum" find "$@" -type f -not -path \
	    "*/.git*/*" | awk '{print $1}' > "$xbtmp/tmp.hash" || \
	    err "!h $project $hashdir" \
	    "project_up_to_date" "$hashdir" "$hashname" "$badhashvar" "$@"

	hash="$(x_ "$sha512sum" "$xbtmp/tmp.hash" | awk '{print $1}' || \
	    err)" || err "$hashname: Can't read sha512 of '$xbtmp/tmp.hash'" \
	    "project_up_to_date" "$hashdir" "$hashname" "$badhashvar" "$@"

	if [ "$hash" != "$old_hash" ] || \
	    [ ! -f "$XBMK_CACHE/$hashdir/$project$hashname" ]; then
		eval "$badhashvar=\"y\""
	fi

	printf "%s\n" "$hash" > "$xbtmp/new.hash" || \
	    err "!mkhash $xbtmp/new.hash ($hashdir $hashname $badhashvar)" \
	    "project_up_to_date" "$hashdir" "$hashname" "$badhashvar" "$@"

	eval "[ \"\$$badhashvar\" = \"y\" ] && return 1"; :
}

check_cross_compiler()
{
	cbdir="src/coreboot/$tree"

	[ "$project" != "coreboot" ] && \
		cbdir="src/coreboot/default"
	[ -n "$xgcctree" ] && \
		cbdir="src/coreboot/$xgcctree"

	cbelfdir="elf${cbdir#src}"

	xfix="${1%-*}"
	[ "$xfix" = "x86_64" ] && \
		xfix="x64"

	xgccfile="$cbelfdir/xgcc_${xfix}_was_compiled"
	xgccargs="crossgcc-$xfix UPDATED_SUBMODULES=1 CPUS=$XBMK_THREADS"

	x_ ./mk -f coreboot "${cbdir#src/coreboot/}"
	x_ mkdir -p "$cbelfdir"

	# tell build systems what cross-compiler to use
	export PATH="$xbmkpwd/$cbdir/util/crossgcc/xgcc/bin:$PATH"
	export CROSS_COMPILE="${xarch% *}-" # used by e.g. u-boot

	# coreboot-specific (we disable gnat on some trees)
	[ -n "$xlang" ] && \
		export BUILD_LANGUAGES="$xlang"

	[ -f "$xgccfile" ] && \
		return 0 # build exists already, so skip building

	                           # it sometimes fails for like, no reason
	make -C "$cbdir" $xgccargs || x_ make -C "$cbdir" $xgccargs

	x_ touch "$xgccfile" # prevent unnecessary re-build operations
}

check_defconfig()
{
	[ ! -f "$defconfig" ] && \
		$if_not_dry_build \
			err "$project/$target: no config" check_defconfig "$@"

	dest_dir="$elfdir/$tree/$target/${defconfig#"$target_dir/config/"}"

	# skip build if a previous one exists:

	$if_dry_build \
		return 0
	elfcheck || \
		return 1; :
}

elfcheck()
{
	# TODO: *STILL* very hacky check. do it properly (based on build.list)

	( fx_ "eval exit 1 && err" find "$dest_dir" -type f ) || return 1; :
}

handle_makefile()
{
	if $if_not_dry_build check_makefile "$srcdir"; then
		$if_not_dry_build \
			x_ make -C "$srcdir" clean $cleanargs
	fi

	[ -f "$defconfig" ] && \
		x_ cp "$defconfig" "$srcdir/.config"

	run_make_command || \
	    err "no makefile!" "handle_makefile" "$@"

	_copy=".config"

	[ "$mode" = "savedefconfig" ] && \
		_copy="defconfig"

	$if_make_config \
		$if_not_dry_build \
			x_ cp "$srcdir/$_copy" "$defconfig"; :
}

run_make_command()
{
	$if_build \
		x_ $premake

	(
	$if_dry_build \
		exit 0

	x_ check_cmake "$srcdir"

	$if_build \
		check_autoconf "$srcdir"

	check_makefile "$srcdir" || return 1

	x_ make -C "$srcdir" $mode -j$XBMK_THREADS $makeargs

	) || err "run_make_file" "$@"

	$if_build \
		x_ $mkhelper; :
}

check_cmake()
{
	$if_dry_build \
		return 0

	[ -z "$cmakedir" ] && \
		return 0

	check_makefile "$1" || \
		cmake -B "$1" "$1/$cmakedir" || \
			x_ check_makefile "$1"

	x_ check_makefile "$1"; :
}

check_autoconf()
{
	(
	x_ cd "$1"

	[ -f "bootstrap" ] && \
		x_ ./bootstrap $bootstrapargs
	[ -f "autogen.sh" ] && \
		x_ ./autogen.sh $autogenargs
	[ -f "configure" ] && \
		x_ ./configure $autoconfargs; :

	) || err "can't bootstrap project: $1" "check_autoconf" "$@"; :
}

check_makefile()
{
	[ ! -f "$1/Makefile" ] && [ ! -f "$1/makefile" ] && \
	    [ ! -f "$1/GNUmakefile" ] && \
		return 1; :
}

copy_elf()
{
	[ -f "$listfile" ] && \
		x_ mkdir -p "$dest_dir"

	if [ -f "$listfile" ]; then
		while read -r f; do
			[ -f "$srcdir/$f" ] && \
				x_ cp "$srcdir/$f" "$dest_dir"; :

		done < "$listfile" || err \
		    "cannot read '$listfile'" "copy_elf" "$@"; :
	fi

	( x_ make -C "$srcdir" clean $cleanargs ) || \
	    err "can't make-clean '$srcdir'" "copy_elf" "$@"; :
}

x_ main "$_f" "$@"

) || exit 1; :
