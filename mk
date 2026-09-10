#!/usr/bin/env sh

# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

set -u -e

xrval=0

(
ispwd="true"
xbdir=""

if [ "$0" != "./mk" ]; then
	if [ "${0##*/}" != "$0" ]; then
		xbdir="`readlink -f "$0" 2>/dev/null`"
		if [ -z "$xbdir" ]; then
			xbdir="`realpath "$0" 2>/dev/null`"
		fi
		cd "${xbdir%/*}" || exit 1
	else
		ispwd="false"
	fi
fi
if [ "$ispwd" = "true" ] && [ -L "mk" ]; then

	ispwd="false"
fi
if [ "$ispwd" = "false" ]; then

	printf "You must run this in the proper work directory.\n" 1>&2
	exit 1
fi

. "include/lib.sh"
. "include/init.sh"
. "include/vendor.sh"
. "include/mrc.sh"
. "include/inject.sh"
. "include/rom.sh"
. "include/release.sh"
. "include/get.sh"
. "include/chromebook.sh"

main()
{
	cmd=""
	if [ $# -gt 0 ]; then
		cmd="$1"
		shift 1
	fi

	if ! xeq main "$cmd" \
	    version release download inject prep_mr_import; then
		return 0
	fi

	$cmd "$@"
	set -u -e

	return 1
}

main "$@" || exit 0

. "include/tree.sh"

trees "$@" || exit 0

x_ touch "$mkhelpercfg"

. "$mkhelpercfg"
$cmd
) || xrval=1

exit $xrval
