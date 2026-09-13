# SPDX-License-Identifier: GPL-3.0-or-later

. "include/lib.sh"
. "include/env/init.sh"
. "include/env/get.sh"

. "include/fw/vendor.sh"
. "include/fw/mrc.sh"
. "include/fw/inject.sh"
. "include/fw/rom.sh"

. "include/mk/release.sh"

# tree.sh is included from the main mk script

xbcommands=" \
version \
release \
download \
inject \
"
