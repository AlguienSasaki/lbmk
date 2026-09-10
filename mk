#!/bin/sh

# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

set -u -e

xrval=0

(
xbarg0="$0"
[ "${xbarg0##*/}" = "$xbarg0" ] && \
    xbarg0="`command -v "$xbarg0"`"

commandv="`readlink -f "$xbarg0" 2>/dev/null`"
[ -z "$commandv" ] && \
    commandv="`realpath "$xbarg0" 2>/dev/null`"

cd "${commandv%/*}" || exit 1

. "include/common.sh"

if xeq main "${1-}" \
    $xbcommands; then

	"$@"
	exit 0
fi

. "include/tree.sh"

trees "$@" || exit 0

x_ touch "$mkhelpercfg"

. "$mkhelpercfg"
$cmd
) || xrval=1

exit $xrval
