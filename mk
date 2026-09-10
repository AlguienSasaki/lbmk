#!/usr/bin/env sh

# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

set -u -e

xrval=0

(
xbarg0="$0"
if [ "${xbarg0##*/}" = "$xbarg0" ]; then
	xbarg0="`command -v "$xbarg0"`"
fi

xbcddir="`readlink -f "$xbarg0" 2>/dev/null`"
if [ -z "$xbcddir" ]; then
	xbcddir="`realpath "$xbarg0" 2>/dev/null`"
fi

cd "${xbcddir%/*}" || exit 1

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
