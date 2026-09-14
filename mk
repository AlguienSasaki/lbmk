#!/bin/sh

# SPDX-License-Identifier: GPL-3.0-or-later

# Copyright (c) 2020-2026 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

set -u -e

eval "`printf "LANG LANGUAGE LC_CTYPE LC_NUMERIC LC_TIME LC_COLLATE \
    LC_MONETARY LC_MESSAGES LC_PAPER LC_NAME LC_ADDRESS LC_TELEPHONE \
    LC_MEASUREMENT LC_IDENTIFICATION LC_ALL\n" | \
    awk '{ for (i=1; i<=NF; i++) $i = "export " $i "=C.UTF-8;"; print }'`"

(
xbarg0="$0"
[ "${xbarg0##*/}" = "$xbarg0" ] && \
    xbarg0="`command -v "$xbarg0"`"

commandv="`readlink -f "$xbarg0" 2>/dev/null`"
[ -z "$commandv" ] && \
    commandv="`realpath "$xbarg0" 2>/dev/null`"

cd "${commandv%/*}" || exit 1

. "include/lib.sh"
. "include/env/init.sh"
. "include/env/get.sh"

. "include/fw/vendor.sh"
. "include/fw/mrc.sh"
. "include/fw/inject.sh"
. "include/fw/rom.sh"

. "include/mk/tree.sh"
. "include/mk/release.sh"

_f="${1-}"
shift 1 2>/dev/null || :

eval "$(printf "version release download inject\n" | awk '{ for (i=1; i<=NF; \
    i++) $i = "[ \""$i"\" = \"$_f\" ] && x_ "$i" \"$@\" && exit 0;"; print }')"

x_ trees "$_f" "$@"

) || exit 1; :
