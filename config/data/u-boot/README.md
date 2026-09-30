SPDX-License-Identifier: CC-0

<alpernebbi> in u-boot tree, configs/{chromebook_kevin,chromebook_bob,qemu-arm64}_defconfig iirc

14:10 <alpernebbi> git log 5bea1fade9...418570a617

this is some notes about updating the u-boot rev

rough reverse engineered interpretation of alper's process:

git show 976fc6890aee -- "config/u-boot/*/config/diffconfig"
show diffconfigs

copy config from upstream, apply these changes

./mk -l in lbmk

analyse overall diff from last lbmk config, grep upstream
git log to see what caused them - tweak if necessary and
adjust diffconfig accordingly.

also check for any changes related to rk3399 upstream that
might affect us, e.g. CONFIG_BOARD_INIT in 2026.07 that
we had to turn off in lbmk.
