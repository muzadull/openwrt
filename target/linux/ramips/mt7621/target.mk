#
# Copyright (C) 2009 OpenWrt.org
#

SUBTARGET:=mt7621
BOARDNAME:=MT7621 based boards
FEATURES+=nand ramdisk rtc usb minor
CPU_TYPE:=24kc
KERNELNAME:=vmlinux vmlinuz
# make Kernel/CopyImage use $LINUX_DIR/vmlinuz
IMAGES_DIR:=../../..

DEFAULT_PACKAGES += wpad-basic-mbedtls uboot-envtools kmod-crypto-hw-eip93 swconfig switch \
                    luci luci-app-adblock luci-app-https-dns-proxy luci-app-sqm luci-app-ttyd curl ethtool irqbalance \
                    kmod-br-netfilter kmod-sched kmod-sched-connmark \
	            iptables-mod-conntrack-extra iptables-mod-ipopt iptables-zz-legacy ip6tables-zz-legacy

define Target/Description
	Build firmware images for Ralink MT7621 based boards.
endef

