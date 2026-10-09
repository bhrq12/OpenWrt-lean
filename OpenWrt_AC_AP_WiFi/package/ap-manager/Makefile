include $(TOPDIR)/rules.mk

PKG_NAME:=ap-manager
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=AC+AP WiFi Management System <support@example.com>
PKG_DESCRIPTION:=AP agent module for OpenWrt AC+AP WiFi Unified Management System

PKG_LICENSE:=GPL-2.0
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk

define Package/ap-manager
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=WiFi
	TITLE:=AP Agent for WiFi Management
	URL:=https://github.com/bhrq12/ac-ap-wifi-manager
	DEPENDS:=+libopenssl +libwebsockets +cJSON +libubus +libuci
	MAINTAINER:=$(PKG_MAINTAINER)
endef

define Package/ap-manager/description
	AP (Access Point) agent module for OpenWrt AC+AP WiFi Unified Management System.
	Runs on each AP device and provides:
	- Secure WebSocket connection to AC controller
	- Configuration synchronization and application
	- Device status reporting (CPU, memory, client count)
	- Automatic reconnection on failure
	- Configuration rollback support
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		$(TARGET_CONFIGURE_OPTS) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -Wall -Wextra" \
		LDFLAGS="$(TARGET_LDFLAGS) -lssl -lcrypto -lwebsockets -lcjson -lubus -luci -lpthread" \
		-j$(PARALLEL_JOBS)
endef

define Package/ap-manager/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ap-manager.bin $(1)/usr/sbin/ap-manager

	$(INSTALL_DIR) $(1)/etc/ap-manager
	$(INSTALL_CONF) ./config/ap-manager.conf $(1)/etc/ap-manager/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./scripts/ap-manager.init $(1)/etc/init.d/ap-manager

endef

define Package/ap-manager/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	/etc/init.d/ap-manager enable
fi
exit 0
endef

define Package/ap-manager/postrm
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	/etc/init.d/ap-manager stop
	/etc/init.d/ap-manager disable
fi
exit 0
endef

$(eval $(call BuildPackage,ap-manager))