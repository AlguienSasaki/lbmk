# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2025 Leah Rowe <leah@libreboot.org>

# Import MrChromebox project into xbmk

# NOTE: variable naming scheme:
#  mr_ for variables/functions dealing with MrChromebox
#  mx_ for variables/functions pertaining to Libreboot setup
#	because i am a non-binary blob

spdx="# SPDX-License-Identifier: GPL-3.0-or-later"

# temporary work variables
mr_tmpdir="" # dir to clone tmp repos in

# NOTE: upstream for our purposes: https://review.coreboot.org/coreboot
mr_cbrepo="https://github.com/mrchromebox/coreboot"
mr_cbbranch="MrChromebox-2503" # branch in mrchromebox
mr_cbrev="ecd9fa6a177e00132ec214252a2b9cebbb01e25f" # relative to base
mr_cbrevbase="38f5f7c48024d9fca4b6bbd88914423c34da709c" # 25.03 upstream base
mr_cbtree="chromebook" # tree name in xbmk

# NOTE: upstream for our purposes: https://github.com/tianocore/edk2.git
mr_edk2repo="https://github.com/mrchromebox/edk2"
mr_edk2branch="uefipayload_2502" # branch in mrchromebox
mr_edk2rev="feaf6b976b7cc72a18ed364f273751c943a9e7d0" # relative to base
mr_edk2revbase="fbe0805b2091393406952e84724188f8c1941837" # 2025.02 upstream
mr_edk2tree="chromebook" # tree name in xbmk

# mxlibreboot was here
prep_mr_import()
{
	if [ -f "$xbmkpwd/CHANGELOG" ]; then
		err "Project import disabled on releases" "prep_mr_import" "$@"
	fi

	mr_tmpdir="`mktemp -d || err "can't make mrtmpdir"`" || \
	    err "can't make mrtmpdir" "prep_mr_coreboot" "$@"
	x_ remkdir "$mr_tmpdir"

	x_ prep_mx_edk2conf

	x_ prep_mr_projects

	x_ rm -Rf "$mr_tmpdir"
}

# create config/git/edk2/pkg.cfg
prep_mx_edk2conf()
{
	x_ remkdir "config/git/edk2"

	x_ prep_mr_file "config/git/edk2/pkg.cfg" \
	    "$spdx" \
	    "" \
	    "rev=\"HEAD\"" \
	    "url=\"https://codeberg.org/libreboot/edk2\"" \
	    "bkup_url=\"https://git.disroot.org/libreboot/edk2\""
}

# prep config/PROJECT/TREE/ for various projects
prep_mr_projects()
{
	x_ prep_mr "coreboot" "$mr_cbrepo" "$mr_cbbranch" "$mr_cbrev" \
	    "$mr_cbrevbase" "$mr_cbtree"
	x_ prep_mr "edk2" "$mr_edk2repo" "$mr_edk2branch" "$mr_edk2rev" \
	    "$mr_edk2revbase" "$mr_edk2tree"
}

# create config/PROJECT/TREE/target.cfg
# and config/PROJECT/TREE/patches/
prep_mr()
{
	mr_projectname="$1"
	mr_repo="$2"
	mr_branch="$3"
	mr_rev="$4"
	mr_revbase="$5"
	mr_tree="$6"

	x_ prep_mr_clone "$@"
	x_ prep_mr_patch "$@"
	x_ prep_mr_file "config/$1/$6/target.cfg" \
	    "$spdx" \
	    "" \
	    "tree=\"$6\"" \
	    "rev=\"$5\""
}

prep_mr_clone()
{
	mr_tmpclone="$mr_tmpdir/$1"

	x_ git clone "$2" "$mr_tmpclone"

	x_ git -C "$mr_tmpclone" checkout "$3"
	# we don't reset, because we format-patch between revbase..rev
}

prep_mr_patch()
{
	mr_tmpclone="$mr_tmpdir/$1"
	mx_patchdir="config/$1/$6/patches"

	x_ remkdir "$mx_patchdir"
	if [ "$4" != "$5" ]; then
		x_ git -C "$mr_tmpclone" format-patch $5..$4
		x_ mv "$mr_tmpclone"/*.patch "$mx_patchdir"
	fi
	# if no patches were created, rmdir will succeed
	rmdir "$mx_patchdir" 1>/dev/null 2>/dev/null || :
}

prep_mr_file()
{
	mr_filename="$1"
	shift 1

	x_ rm -f "$mr_filename"

	while [ $# -gt 0 ]
	do
		printf "%s\n" "$1" >> "$mr_filename" || \
		    err "Can't write '$1' to '$mr_filename'" prep_mr_file "$@"
		shift 1
	done

	printf "Created '%s'\n" "$mr_filename"
}
