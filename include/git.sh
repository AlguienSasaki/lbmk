# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020-2021,2023-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

eval "`setvars "" loc url bkup_url subfile subhash subrepo subrepo_bkup \
    depend subfile_bkup repofail`"

tmpgit="$xbmkpwd/tmp/gitclone"

fetch_targets()
{
	[ -n "$tree_depend" ] && [ "$tree_depend" != "$tree" ] && \
		x_ ./mk -f "$project" "$tree_depend"
	e "src/$project/$tree" d && return 0

	printf "Creating %s tree %s\n" "$project" "$tree"
	git_prep "$loc" "$loc" "$xbmkpwd/$configdir/$tree/patches" \
	    "src/$project/$tree" with_submodules
	nuke "$project/$tree" "$project/$tree"
}

fetch_project()
{
	eval "`setvars "" xtree tree_depend`"
	eval "`setcfg "config/git/$project/pkg.cfg"`"

	chkvars url

	[ -n "$xtree" ] && x_ ./mk -f coreboot "$xtree"
	[ -z "$depend" ] || for d in $depend ; do
		printf "'%s' needs '%s'; grabbing '%s'\n" "$project" "$d" "$d"
		x_ ./mk -f $d
	done
	clone_project

	for x in config/git/*; do
		[ -d "$x" ] && nuke "${x##*/}" "src/${x##*/}" 2>/dev/null; :
	done; :
}

clone_project()
{
	loc="$XBMK_CACHE/repo/$project" && singletree "$project" && \
	    loc="src/$project"
	printf "Downloading project '%s' to '%s'\n" "$project" "$loc"

	e "$loc" d missing && remkdir "${tmpgit%/*}" && git_prep \
	    "$url" "$bkup_url" "$xbmkpwd/config/$project/patches" "$loc"; :
}

git_prep()
{
	_patchdir="$3"
	_loc="$4" # $1 and $2 are gitrepo and gitrepo_backup

	chkvars rev
	tmpclone "$1" "$2" "$tmpgit" "$rev" "$_patchdir"
	if singletree "$project" || [ $# -gt 4 ]; then
		[ -f "$mdir/module.list" ] && while read -r msrcdir; do
			fetch_submodule "$msrcdir"
		done < "$mdir/module.list"; :
	fi

	if [ "$project" = "coreboot" ] && [ -n "$xtree" ] && [ $# -gt 2 ] && \
	    [ "$xtree" != "$tree" ]; then (
		x_ cd "$tmpgit/util" && x_ rm -Rf crossgcc
		x_ ln -s "../../$xtree/util/crossgcc" crossgcc
	) || $err "$_loc: !xgcc link"; fi

	[ "$_loc" != "$XBMK_CACHE/repo/$project" ] && \
	    [ "$XBMK_RELEASE" = "y" ] && rmgit "$tmpgit"

	[ "$_loc" = "${_loc%/*}" ] || x_ mkdir -p "${_loc%/*}"
	mv "$tmpgit" "$_loc" || $err "git_prep: !mv $tmpgit $_loc"
}

fetch_submodule()
{
	mcfgdir="$mdir/${1##*/}"; eval \
	    "`setvars "" subhash subrepo subrepo_bkup subfile subfile_bkup st`"
	[ ! -f "$mcfgdir/module.cfg" ] || . "$mcfgdir/module.cfg" || \
	    $err "! . $mcfgdir/module.cfg"

	for xt in repo file; do
		_seval="if [ -n \"\$sub$xt\" ] || [ -n \"\$sub${xt}_bkup\" ]"
		eval "$_seval; then st=\"\$st \$xt\"; fi"
	done
	st="${st# }" && [ "$st" = "repo file" ] && $err "$mdir: repo+file"

	[ -z "$st" ] && return 0 # subrepo/subfile not defined
	chkvars "sub${st}" "sub${st}_bkup" "subhash"

	[ "$st" = "file" ] && download "$subfile" "$subfile_bkup" \
	    "$tmpgit/$1" "$subhash" && return 0
	rm -Rf "$tmpgit/$1" || $err "!rm '$mdir' '$1'"
	tmpclone "$subrepo" "$subrepo_bkup" "$tmpgit/$1" "$subhash" \
	    "$mdir/${1##*/}/patches"
}

tmpclone()
{
	livepull="n" && [ "$repofail" = "y" ] && \
	    printf "Cached clone failed; trying online.\n" 1>&2 && livepull="y"

	repofail="n"

	[ $# -lt 6 ] || rm -Rf "$3" || $err "git retry: !rm $3 ($1)"
	repodir="$XBMK_CACHE/repo/${1##*/}" && [ $# -gt 5 ] && repodir="$3"
	mkdir -p "$XBMK_CACHE/repo" || $err "!rmdir $XBMK_CACHE/repo"

	if [ "$livepull" = "y" ] && [ ! -d "$repodir" ]; then
		git clone "$1" "$repodir" || git clone $2 "$repodir" || \
		    $err "!clone $1 $2 $repodir $4 $5" #
	elif [ -d "$repodir" ] && [ $# -lt 6 ]; then
		git -C "$repodir" pull || sleep 3 || git -C "$repodir" pull \
		    || sleep 3 || git -C "$repodir" pull || :
	fi
	(
	[ $# -gt 5 ] || git clone "$repodir" "$3" || $err "!clone $repodir $3"
	git -C "$3" reset --hard "$4" || $err "!reset $1 $2 $3 $4 $5"
	git_am_patches "$3" "$5"
	) || repofail="y"

	[ "$repofail" = "y" ] && [ $# -lt 6 ] && tmpclone "$@" retry
	[ "$repofail" = "y" ] && $err "!clone $1 $2 $3 $4 $5"; :
}

git_am_patches()
{
	for p in "$2/"*; do
		[ -L "$p" ] && continue
		[ -e "$p" ] || continue
		[ -d "$p" ] && git_am_patches "$1" "$p" && continue
		[ ! -f "$p" ] || git -C "$1" am "$p" || $err "$1 $2: !am $p"
	done; :
}

nuke()
{
	e "config/${1%/}/nuke.list" f missing || while read -r nukefile; do
		rmf="src/${2%/}/$nukefile" && [ -L "$rmf" ] && continue
		e "$rmf" e missing || rm -Rf "$rmf" || $err "!rm $rmf, ${2%/}"
	done < "config/${1%/}/nuke.list"; :
}
