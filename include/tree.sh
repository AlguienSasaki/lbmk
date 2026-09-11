# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2022-2023 Alper Nebi Yasak <alpernebiyasak@gmail.com>
# Copyright (c) 2022 Ferass El Hafidi <vitali64pmemail@protonmail.com>
# Copyright (c) 2023-2025 Leah Rowe <leah@libreboot.org>

# flag e.g. ./mk -b <-- mkflag would be "b"
flag=""

# macros, overridden depending on the flag
if_do_make=""
if_dry_build=":"
if_not_do_make=":"
if_not_dry_build=""

eval "`newvar autoconfargs autogenargs badhash badtghash bootstrapargs \
    build_depend buildtype cleanargs cmakedir cmd defconfig dest_dir elfdir \
    forcepull gccdir gccfull gccver gnatdir gnatfull gnatver listfile \
    makeargs mdir mkhelper mkhelpercfg mode postmake premake project release \
    rev srcdir target target_dir targets tree xarch xgcctree xlang`"

trees()
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
		-c) mode="distclean" ;;
		-x) mode="crossgcc-clean" ;;
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
		return 1
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

	targets="$*"
	cmd="build_targets $targets"
	if singletree "$project"; then
		cmd="build_project"
	fi

	remkdir "${tmpgit%/*}"
}

build_project()
{
	if ! configure_project "$configdir"; then
		return 0
	elif [ -f "$listfile" ]; then
		if ! $if_not_dry_build elfcheck; then
			return 0
		fi
	fi

	[ "$mode" = "distclean" ] && \
		mode="clean"

	if ! run_make_command; then
		return 0
	fi

	[ -z "$mode" ] && \
		$if_not_dry_build \
			copy_elf; :
}

build_targets()
{
	if [ ! -d "$configdir" ]; then
		err "directory '$configdir' doesn't exist" "build_targets" "$@"
	elif [ $# -lt 1 ]; then
		targets="$(ls -1 "$configdir")" || \
		    err "'$configdir': can't list targets" "build_targets" "$@"
	fi

	for x in $targets
	do
		unset CROSS_COMPILE
		export PATH="$xbmkpath"

		if [ "$x" = "list" ]; then
			x_ ls -1 "config/$project"
			listfile=""
			break
		fi

		printf "'make %s', '%s', '%s'\n" "$mode" "$project" "$x"

		target="$x"

		x_ handle_defconfig

		[ -z "$mode" ] && \
			x_ $postmake; :
	done; :
}

handle_defconfig()
{
	target_dir="$configdir/$target"

	[ ! -f "CHANGELOG" ] && \
		fetch_project "$project"
	if ! configure_project "$target_dir"; then
		return 0
	fi

	[ -z "$tree" ] && \
		err "$configdir: 'tree' not set" "handle_defconfig" "$@"

	srcdir="src/$project/$tree"

	[ "$mode" = "${mode%clean}" ] && [ ! -d "$srcdir" ] && \
		return 0

	for y in "$target_dir/config"/*
	do
		[ "$flag" != "-d" ] && [ ! -f "$y" ] && \
			continue
		[ "$flag" != "-d" ] && \
			defconfig="$y"

		if [ -z "$mode" ]; then
			check_defconfig || continue; :
		fi

		[ -z "$mode" ] && \
			for _xarch in $xarch; do
				$if_dry_build \
					break
				[ -n "$_xarch" ] && \
					check_cross_compiler "$_xarch"; :
			done; :

		handle_makefile

		[ -z "$mode" ] && \
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
	while e "$_tcfg" f || [ "$cmd" != "build_project" ]
	do
		printf "Loading %s config: %s\n" "$project" "$_tcfg"
		eval "`newvar rev tree`"

		. "$_tcfg" || \
		    err "Can't read '$_tcfg'" "configure_project" "$@"

		[ "$flag" = "-d" ] && \
			build_depend="" # dry run

		[ "$cmd" = "build_project" ] && \
			break # single-tree, so targeting is unnecessary
		$if_do_make \
			break
		[ "${_tcfg%/*/target.cfg}" = "${_tcfg%"/$tree/target.cfg"}" ] \
			&& break # target and tree matching was successful

		_tcfg="${_tcfg%/*/target.cfg}/$tree/target.cfg" # try next
	done

	[ "$XBMK_RELEASE" = "y" ] && [ "$release" = "n" ] && \
		return 1
	[ -n "$buildtype" ] && [ "${mode%config}" != "$mode" ] && \
		return 1

	[ -z "$mode" ] && \
		$if_not_dry_build \
			build_dependencies

	mdir="$xbmkpwd/config/submodule/$project"
	[ -n "$tree" ] && \
		mdir="$mdir/$tree"

	if [ ! -f "CHANGELOG" ]; then
		delete_old_project_files
		$if_not_do_make \
			fetch_${cmd#build_}
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
	if ! project_up_to_date hash "$tree" badhash "$datadir" \
	    "$configdir/$tree" "$mdir"; then
		x_ rm -Rf "src/$project/$tree" "elf/$project/$tree"
	fi

	x_ cp "$xbtmp/new.hash" "$XBMK_CACHE/hash/$project$tree"

	if singletree "$project" || [ -z "$target" ] || [ "$target" = "$tree" ]
	then
		return 0
	fi

	# delete only the builds of a given target, but not src.
	# this is useful when only the target config changes, for
	# example x200_8mb coreboot configs change, but not coreboot:

	if ! project_up_to_date tghash "$target" badtghash "$configdir/$target"
	then
		x_ rm -Rf "elf/$project/$tree/$target"
	fi

	x_ cp "$xbtmp/new.hash" "$XBMK_CACHE/tghash/$project$target"
}

project_up_to_date()
{
	eval "`newvar hash old_hash`"

	hashdir="$1"
	hashname="$2"
	badhashvar="$3"

	shift 3

	x_ xbmkdir "$XBMK_CACHE/$hashdir"

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

	xfix="${1%-*}"
	[ "$xfix" = "x86_64" ] && \
		xfix="x64"

	xgccfile="elf/coreboot/$tree/xgcc_${xfix}_was_compiled"
	xgccargs="crossgcc-$xfix UPDATED_SUBMODULES=1 CPUS=$XBMK_THREADS"

	x_ ./mk -f coreboot "${cbdir#src/coreboot/}"
	x_ xbmkdir "elf/coreboot/$tree" # TODO: is this needed?

	# tell build systems what cross-compiler to use
	export PATH="$xbmkpwd/$cbdir/util/crossgcc/xgcc/bin:$PATH"
	export CROSS_COMPILE="${xarch% *}-" # used by e.g. u-boot

	# coreboot-specific (we disable gnat on some trees)
	[ -n "$xlang" ] && \
		export BUILD_LANGUAGES="$xlang"

	[ -f "$xgccfile" ] && \
		return 0 # build exists already, so skip building

	check_gnu_path gcc gnat || x_ check_gnu_path gnat gcc
	make -C "$cbdir" $xgccargs || x_ make -C "$cbdir" $xgccargs

	x_ touch "$xgccfile" # prevent unnecessary re-build operations
	remkdir "$xbtmp/gnupath" # reset hostcc
}

# fix mismatching gcc/gnat versions on debian trixie/sid. as of december 2024,
# trixie/sid had gnat-13 as gnat and gcc-14 as gcc, but has gnat-14 in apt. in
# some cases, gcc 13+14 and gnat-13 are present; or gnat-14 and gcc-14, but
# gnat in PATH never resolves to gnat-14, because gnat-14 was "experimental"

check_gnu_path()
{
	if ! command -v "$1" 1>/dev/null; then
		err "Host '$1' unavailable" "check_gnu_path" "$@"
	fi

	eval "`newvar gccdir gccfull gccver gnatdir gnatfull gnatver`"

	if host_gcc_gnat_match "$@"; then
		return 0
	elif ! match_gcc_gnat_versions "$@"; then
		return 1
	fi
}

# check if gcc/gnat versions already match:

host_gcc_gnat_match()
{
	if ! gnu_setver "$1" "$1"; then
		err "Command '$1' unavailable." "check_gnu_path" "$@"
	fi
	gnu_setver "$2" "$2" || :

	eval "[ -z \"\$$1ver\" ] && err \"Cannot detect host '$1' version\""

	[ "$gnatfull" != "$gccfull" ] && \
		return 1; : # mismatched gcc/gnat versions
}

# find all gcc/gnat versions, matching them up in PATH:

match_gcc_gnat_versions()
{
	eval "$1dir=\"$(dirname "$(command -v "$1")")\""
	eval "_gnudir=\"\$$1dir\""
	eval "_gnuver=\"\$$1ver\""

	for _bin in "$_gnudir/$2-"*; do
		if [ "${_bin#"$_gnudir/$2-"}" = "$_gnuver" ] && [ -x "$_bin" ]
		then
			_gnuver="${_bin#"$_gnudir/$2-"}"
			break
		fi
	done

	if ! gnu_setver "$2" "$_gnudir/$2-$_gnuver"; then
		return 1
	elif [ "$gnatfull" != "$gccfull" ]; then
		return 1
	fi

	( link_gcc_gnat_versions "$@" "$_gnudir" "$_gnuver" ) || \
	    err "Can't link '$2-$_gnuver' '$_gnudir'" "check_gnu_path" "$@"; :
}

# create symlinks in PATH, so that the GCC/GNAT versions match:

link_gcc_gnat_versions()
{
	_gnudir="$3"
	_gnuver="$4"

	remkdir "$xbtmp/gnupath"

	x_ cd "$xbtmp/gnupath"

	for _gnubin in "$_gnudir/$2"*"-$_gnuver"
	do
		_gnuutil="${_gnubin##*/}"
		[ -e "$_gnubin" ] && \
			x_ ln -s "$_gnubin" "${_gnuutil%"-$_gnuver"}"; :
	done
}

# get the gcc/gnat version
# fail: return 1 if util not found
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
			x_ make -C "$srcdir" $cleanargs clean
	fi

	[ -f "$defconfig" ] && \
		x_ cp "$defconfig" "$srcdir/.config"

	run_make_command || \
	    err "no makefile!" "handle_makefile" "$@"

	_copy=".config"

	[ "$mode" = "savedefconfig" ] && \
		_copy="defconfig"

	[ "${mode%config}" != "$mode" ] && \
		$if_not_dry_build \
			x_ cp "$srcdir/$_copy" "$defconfig"; :

	[ -e "$srcdir/.git" ] && [ "$project" = "u-boot" ] && \
	    [ "$mode" = "distclean" ] && \
		$if_not_dry_build \
			x_ git -C "$srcdir" $cleanargs clean -fdx; :
}

run_make_command()
{
	[ -z "$mode" ] && \
		x_ $premake

	if $if_not_dry_build check_cmake "$srcdir"; then
		[ -z "$mode" ] && \
			$if_not_dry_build \
				check_autoconf "$srcdir"; :
	fi
	if ! $if_not_dry_build check_makefile "$srcdir"; then
		return 1
	fi

	$if_not_dry_build \
		x_ make -C "$srcdir" $mode -j$XBMK_THREADS $makeargs

	[ -z "$mode" ] && \
		x_ $mkhelper

	if ! check_makefile "$srcdir"; then
		return 0
	fi

	if [ "$mode" = "clean" ]; then
		$if_dry_build \
			return 0
		if ! make -C "$srcdir" $cleanargs distclean; then
			x_ make -C "$srcdir" $cleanargs clean
		fi
	fi
}

check_cmake()
{
	$if_dry_build \
		return 0
	if [ ! -n "$cmakedir" ]; then
		return 0
	elif ! check_makefile "$1"; then
		if ! cmake -B "$1" "$1/$cmakedir"; then
			x_ check_makefile "$1"
		fi
	fi
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
		x_ xbmkdir "$dest_dir"

	if [ -f "$listfile" ]; then
		while read -r f; do
			[ -f "$srcdir/$f" ] && \
				x_ cp "$srcdir/$f" "$dest_dir"; :

		done < "$listfile" || err \
		    "cannot read '$listfile'" "copy_elf" "$@"; :
	fi

	( x_ make clean -C "$srcdir" $cleanargs ) || \
	    err "can't make-clean '$srcdir'" "copy_elf" "$@"; :
}
