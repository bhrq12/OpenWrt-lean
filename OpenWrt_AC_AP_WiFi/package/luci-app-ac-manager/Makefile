#
# Copyright (C) 2026 AC+AP WiFi Management System
#
# This is free software, licensed under GPL-2.0.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-ac-manager
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=AC+AP WiFi Management System <support@example.com>

LUCI_TITLE:=LuCI support for AC Manager
LUCI_DEPENDS:=+luci-base +rpcd +rpcd-mod-ac-manager
LUCI_PKGARCH:=all
LUCI_DESCRIPTION:=LuCI web interface for AC Manager - provides visual management for AC controller including device monitoring, configuration editing, template management and audit log viewing.

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
