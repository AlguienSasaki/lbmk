#!/bin/sh

# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

set -u -e

eval "`printf "%s\n" "LANG LANGUAGE LC_CTYPE LC_NUMERIC LC_TIME LC_COLLATE \
    LC_MONETARY LC_MESSAGES LC_PAPER LC_NAME LC_ADDRESS LC_TELEPHONE \
    LC_MEASUREMENT LC_IDENTIFICATION LC_ALL" | \
    awk '{ for (i=1; i<=NF; i++) $i = "export " $i "=C.UTF-8;"; print }'`"

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

	"$@" || exit 1
	exit 0
fi

. "include/mk/tree.sh"

trees "$@" || exit 0

x_ touch "$mkhelpercfg"

. "$mkhelpercfg"
$cmd
) || exit 1; :
