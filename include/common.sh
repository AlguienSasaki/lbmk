# SPDX-License-Identifier: GPL-3.0-or-later

. "include/lib.sh"
. "include/env/init.sh"
. "include/fw/vendor.sh"
. "include/fw/mrc.sh"
. "include/fw/inject.sh"
. "include/fw/rom.sh"
. "include/mk/release.sh"
. "include/env/get.sh"

xbcommands=" \
version \
release \
download \
inject \
"
