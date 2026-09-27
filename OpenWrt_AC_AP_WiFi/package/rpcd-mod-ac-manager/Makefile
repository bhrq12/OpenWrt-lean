#
# Copyright (C) 2026 AC+AP WiFi Management System
#
# This is free software, licensed under GPL-2.0.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=rpcd-mod-ac-manager
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=AC+AP WiFi Management System <support@example.com>

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

TARGET_CFLAGS += \
	-I$(STAGING_DIR)/usr/include

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/rpcd-mod-ac-manager
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WiFi
  TITLE:=rpcd ubus plugin for AC Manager
  DEPENDS:=+rpcd +libubox +libubus
  MAINTAINER:=$(PKG_MAINTAINER)
endef

define Package/rpcd-mod-ac-manager/description
  rpcd ubus bridge plugin for AC Manager. Provides read-only ubus
  interfaces for device list, config templates, audit logs and service status.
endef

define Package/rpcd-mod-ac-manager/install
	$(INSTALL_DIR) $(1)/usr/lib/rpcd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ac-manager.so $(1)/usr/lib/rpcd/
endef

define Package/rpcd-mod-ac-manager/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] || /etc/init.d/rpcd reload 2>/dev/null
endef

$(eval $(call BuildPackage,rpcd-mod-ac-manager))
