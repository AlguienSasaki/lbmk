#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020-2025 Leah Rowe <leah@libreboot.org>
# Copyright (c) 2022 Caleb La Grange <thonkpeasant@protonmail.com>

set -u -e

if [ "./${0##*/}" != "${0}" ] || [ ! -f "mk" ] || [ -L "mk" ]; then
	printf "You must run this in the proper work directory.\n" 1>&2
	exit 1
fi

. "include/lib.sh"
. "include/init.sh"
. "include/vendor.sh"
. "include/inject.sh"
. "include/mrc.sh"
. "include/rom.sh"
. "include/release.sh"

main()
{
	cmd="" && [ $# -gt 0 ] && cmd="$1" && shift 1

	case "$cmd" in
	version) printf "%s\nWebsite: %s\n" "$relname" "$projectsite" ;;
	release|download|inject) $cmd "$@" ;;
	-*) return 0 ;;
	*) err "bad command" ;;
	esac
	set -u -e # some commands disable them. turn them on!
	return 1
}

main "$@" || exit 0

. "include/git.sh"
. "include/tree.sh"

trees "$@" || exit 0
. "$mkhelpercfg"
$cmd
