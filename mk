#!/bin/sh

# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>
# Copyright (c) 2022-2023 Alper Nebi Yasak <alpernebiyasak@gmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>

set -u -e

findpath()
{
	forx 'readlink -f \"%s\" 2>/dev/null || \
	    x_ realpath \"%s\" 2>/dev/null' "$@"
}

printf "%s\n" "%s" | grep -o "%s" 1>/dev/null || exit 1 # safety (-o check)
forx()
{
	awkarg="{printf \"${1-};\""
	shift 1 2>/dev/null || :

	scount="$(printf "%s\n" "$awkarg" | grep -o "%s" | wc -l)"
	while [ $scount -gt 0 ]; do
		awkarg="$awkarg,\$0"
		scount="$(expr $scount - 1 || :)"
	done

	while [ $# -gt 0 ]; do
		eval "$(printf '%s\n' "$1" | awk "$awkarg}")"
		shift 1
	done
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

forx 'export %s=\"C.UTF-8\"' LANG LANGUAGE LC_CTYPE LC_NUMERIC LC_TIME \
    LC_COLLATE LC_MONETARY LC_MESSAGES LC_PAPER LC_NAME LC_ADDRESS \
    LC_TELEPHONE LC_MEASUREMENT LC_IDENTIFICATION LC_ALL

(
mk="$0"
[ "${mk##*/}" != "$mk" ] || \
    mk="$(x_ command -v "$mk")" || exit 1

mk="$(x_ findpath "$mk")" || exit 1
xbmkpwd="$(x_ dirname "$mk")" || exit 1

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

for xmkarg in version test release download inject; do
	[ "$_f" = "$xmkarg" ] || continue;
	x_ mk$xmkarg "$@"
	exit 0
done

# macros, overridden depending on the flag
if_make_config=":"
if_build=":" # can be false even if make is true
if_dry_build=":"
eval "`newvar if_not_make_config if_not_build if_not_dry_build if_not_fetch`"
if_skip_build=":"

# these variables will also be initialised in configure_target
cfgvars="autogenargs cmakedir configureargs badhash badtghash bootstrapargs \
    build_depend cleanargs fetch_depend makeargs mkhelper noconfig postmake \
    premake release rev tree xarch xgcctree xlang cfgname \
    if_not_skip_build cook_config seen preconfig srclink"

eval "`newvar buildcfg defscan dest_dir flag forcepull listfile mdir \
    mkhelpercfg mode project srcdir target target_dir _tcfg \
    $cfgvars`"

# when saved to e.g. coreboot src. can be overridden by projects
cfgname_default=".config" # e.g. src/coreboot/default/.config

main()
{
	flags="f:F:b:m:u:s:l:n:d:"

	while getopts $flags option
	do
		[ -n "$flag" ] && \
			err "only one flag is permitted" "trees" "$@"

		flag="$1"

		# the "mode" variable is affixed to a make command, example:
		# "$mk" -m coreboot does: make menuconfig -C src/coreboot/tree

		case "$flag" in
		-b) : ;; # build a source tree
		-d) # dry builds. -d is like -b but skips many build steps
			if_dry_build=""
			if_not_dry_build=":"
			;;
		-f|-F) # download source code for a project
			if_not_fetch=":"
			if_dry_build=""
			if_not_dry_build=":"
			[ "$flag" = "-F" ] && \
				forcepull="y"; : # never skip git fetch/pull
			;;
		-u) mode="oldconfig" ;;
		-m) mode="menuconfig" ;;
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

		# user can type e.g. ./mk -f src/flashprog
		project="${OPTARG#src/}"
		project="${project#config/module/}"

		shift 2
	done

	[ -z "$flag" ] && err "missing flag ($flags)" "trees" "$@"
	[ -z "$project" ] && \
		x_ fx_ "x_ "$mk" $flag" x_ find "config/module" \
		    -mindepth 1 -maxdepth 1 -type d && return 0
	[ ! -d "config/module/$project" ] && \
		err "module '$project' not found" main "$flag" "" "$@"

	datadir="config/data/$project"
	configdir="config/module/$project"
	[ "${1-}" = "list" ] && \
		x_ ls -1 "$configdir" && \
		return 0

	listfile="$datadir/build.list"
	[ ! -f "$listfile" ] && \
		listfile="" # TODO: override listfile per-tree

	mkhelpercfg="$datadir/mkhelper.cfg"
	e "$mkhelpercfg" f missing && \
		mkhelpercfg="$xbtmp/mkhelper.cfg"
	x_ touch "$mkhelpercfg"

	remkdir "${tmpgit%/*}"
	x_ touch "$mkhelpercfg"

	[ ! -d "$configdir" ] && err "missing '$configdir'" handle_target "$@"


	if [ $# -lt 1 ]; then
		[ -n "$configdir" ] || err "configdir string empty" main "$@"
		for tgcfg in "$configdir"/*; do
			[ -d "$tgcfg" ] || continue
			x_ "$mk" $flag $project $tgcfg
		done
		return 0
	fi

	while [ $# -gt 0 ]; do
		handle_target "$1"
		shift 1
	done
}

handle_target()
{
	unset CROSS_COMPILE
	export PATH="$xbmkpath"

	printf "'make %s', '%s', '%s'\n" "$mode" "$project" "$1"

	target="${1##*/}"
	target_dir="$configdir/$target"
	defscan="$target_dir/config"

	configure_target "$target_dir" && \
		$if_not_fetch x_ handle_buildcfg

	$if_build $if_not_fetch \
		x_ $postmake; :
}

configure_target()
{
	_tcfg="$1/target.cfg"

	eval "`newvar $cfgvars`"

	. "$mkhelpercfg" # mkhelper reinitialisation

	# override target/tree specific variables from per-target config:
	while [ -z "$rev" ] && try_target_config; do :; done
	[ -z "$tree" ] && \
		err "$project/$target: tree unset" configure_target "$@"

	srcdir="src/$project/$tree"

	# specify directory containing e.g. coreboot defconfigs
	x_ mkdir -p "$xbtmp/noconfig/config"
	x_ touch "$xbtmp/noconfig/config/noconfig"
	[ "$noconfig" = "y" ] && \
		defscan="$xbtmp/noconfig/config" # no configs on this project
	( fx_ "eval exit 1 && err" find "$target_dir/config" -type f ) || \
		[ -n "$defscan" ] || defscan="$target_dir/config"

	# filename in srcdir when copying e.g. coreboot defconfig -> .config
	[ -z "$cfgname" ] && cfgname="$cfgname_default" # e.g. $srcdir/.config

	# if noconfig isn't set, there needs to be a config dir in xbmk.
	# coreboot needs a .config or it will behave unpredictably. we will
	# use the do_build macro to disable builds in this situation. this
	# does not disable mkhelpers or dry builds.
	if [ ! -d "$target_dir/config" ] && [ "$noconfig" != "y" ]; then
		if_not_skip_build=":" # skip make on e.g. ./mk -b coreboot default
		if_skip_build=""
	fi

	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 1

	mdir="$xbmkpwd/config/submodule/$project/$tree"

	if [ ! -f "CHANGELOG" ]; then
		delete_old_project_files
		if [ -z "$srclink" ]; then
			x_ fetch_trees
		else
			# used when multiple targets use the same source
			# e.g. edk2 source trees
			srctree="${srcdir#"src/$project/"}"
			x_ "$mk" -f "$project" "$srclink"
			(
			x_ cd "src/$project/"
			x_ rm -f "$srctree"
			x_ ln -s "$srclink" "$srctree"
			) || err "can't link projects"
		fi
	fi

	$if_build $if_not_dry_build $if_not_fetch \
		mk_dependencies -b $build_depend; :
}

try_target_config()
{
	printf "Try %s/%s config: %s\n" "$project" "$_tcfg" "$target"
	eval "`newvar rev tree`"

	. "$_tcfg" || err "Can't read '$_tcfg'" configure_target "$@"

	_tname="${_tcfg%/*}"
	_tname="${_tname##*/}"

	for loopvar in $seen; do
		[ "$loopvar" = "$_tname" ] && \
			err "$project/$target: infinite tree loop"
	done

	seen="$seen $_tname"

	if [ -n "$rev" ] && [ -z "$tree" ]; then
		# this makes setting tree= optional on configless projects
		tree="${_tcfg%/target.cfg}"
		tree="${tree##*/}"
	elif [ -z "$tree" ]; then
		err "$project/$target: tree unset" try_target_config "$@"
	fi

	[ "${_tcfg%/*/target.cfg}" = "${_tcfg%"/$tree/target.cfg"}" ] && \
		return 1 # target and tree matching was successful

	_ncfg="${_tcfg%/*/target.cfg}/$tree/target.cfg"
	[ "$_ncfg" = "$_tcfg" ] && \
		err "$project/$target: can't determine tree" "$@" # no change

	_tcfg="$_ncfg" # try next
}

# projects can specify which other projects
# to build first, as declared dependencies:
mk_dependencies()
{
	bdf="${1-}"
	shift 1 2>/dev/null || :
	while [ $# -gt 0 ]; do
		bd_tree="${1##*/}"
		[ "${1%%/*}" = "$1" ] && bd_tree="" # build all trees

		[ -z "${1%%/*}" ] && \
			err "$project/$tree: !bd '$1'" mk_dependencies "$@"

		x_ "$mk" $bdf ${1%%/*} $bd_tree; :
		shift 1
	done; :
}

handle_buildcfg()
{
	$if_build \
		x_ $preconfig # for e.g. ./mk -d coreboot default (build utils)

	[ -z "$defscan" ] && err "defscan empty" handle_buildcfg "$@"

	[ ! -d "$srcdir" ] || for y in "$defscan"/*
	do
		[ -f "$y" ] || continue

		buildcfg="$y"
		dest_dir="elf/$project/$tree/$target/${y##*/}"

		$if_build $if_not_dry_build \
			check_build_environment "$y" || continue

		$if_build \
			x_ $premake

		$if_not_skip_build \
			handle_makefile # also supports non-build operations

		$if_build \
			x_ $mkhelper; :

		$if_build $if_not_dry_build $if_not_skip_build \
			copy_elf; :
	done; :
}

check_build_environment()
{
	[ ! -f "$1" ] && return 1

	( fx_ "eval exit 1 && err" find "$dest_dir" -type f ) || return 1

	eval "$(printf '%s\n' "$xarch" | awk \
	    '{ for (i=1; i<=NF; i++) $i = "check_xgcc \""$i"\";"; print }')"
}

check_xgcc()
{
	cbdir="src/coreboot/$tree"

	[ "$project" != "coreboot" ] && \
		cbdir="src/coreboot/default"
	[ -n "$xgcctree" ] && \
		cbdir="src/coreboot/$xgcctree"

	cbelfdir="elf${cbdir#src}"

	xfix="${1%-*}"
	[ "$xfix" = "x86_64" ] && xfix="x64"

	xgccfile="$cbelfdir/xgcc_${xfix}_was_compiled"
	xgccargs="crossgcc-$xfix UPDATED_SUBMODULES=1 CPUS=$XBMK_THREADS"

	x_ "$mk" -f coreboot "${cbdir#src/coreboot/}"
	x_ mkdir -p "$cbelfdir"

	# tell build systems what cross-compiler to use
	export PATH="$xbmkpwd/$cbdir/util/crossgcc/xgcc/bin:$PATH"
	export CROSS_COMPILE="${xarch% *}-"

	# coreboot-specific (we disable gnat on some trees)
	[ -n "$xlang" ] && \
		export BUILD_LANGUAGES="$xlang"

	[ -f "$xgccfile" ] && return 0 # skip (build already exists)

	make -C "$cbdir" $xgccargs || x_ make -C "$cbdir" $xgccargs

	x_ touch "$xgccfile" # prevent unnecessary re-build operations
}

handle_makefile()
{
	clean_src

	[ -f "$buildcfg" ] && \
		x_ cp "$buildcfg" "$srcdir/$cfgname"

	$if_not_skip_build $if_not_dry_build \
		run_make_command

	_copy="$cfgname"
	[ "$mode" = "savedefconfig" ] && \
		_copy="defconfig"

	$if_make_config $if_not_skip_build \
		x_ cp "$srcdir/$_copy" "$buildcfg"; :
}

run_make_command()
{
	[ -z "$cmakedir" ] || \
		x_ cmake -B "$srcdir" "$srcdir/$cmakedir"
	$if_build \
		check_autoconf "$srcdir"

	$if_build \
		x_ $cook_config

	check_makefile "$srcdir" && \
	    x_ make -C "$srcdir" $mode -j$XBMK_THREADS $makeargs
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
		x_ ./configure $configureargs; :

	) || err "can't bootstrap project: $1" check_autoconf "$@"; :
}

check_makefile()
{
	[ ! -f "$1/Makefile" ] && [ ! -f "$1/makefile" ] && \
	    [ ! -f "$1/GNUmakefile" ] && return 1; :
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

	clean_src
}

clean_src()
{
	check_makefile "$srcdir" || return 0
	$if_dry_build return 0

	make -C "$srcdir" distclean $cleanargs || \
	    x_ make -C "$srcdir" clean $cleanargs
}

x_ main "$_f" "$@"

) || exit 1; :
