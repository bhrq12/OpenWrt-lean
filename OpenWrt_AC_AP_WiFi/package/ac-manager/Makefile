include $(TOPDIR)/rules.mk

PKG_NAME:=ac-manager
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=AC+AP WiFi Management System <support@example.com>
PKG_DESCRIPTION:=AC controller module for OpenWrt AC+AP WiFi Unified Management System

PKG_LICENSE:=GPL-2.0
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk

define Package/ac-manager
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=WiFi
	TITLE:=AC Controller for WiFi Management
	URL:=https://github.com/bhrq12/ac-ap-wifi-manager
	DEPENDS:=+libsqlite3 +libopenssl +libwebsockets +libhiredis +cJSON +libuuid
	MAINTAINER:=$(PKG_MAINTAINER)
endef

define Package/ac-manager/description
	AC (Access Controller) module for OpenWrt AC+AP WiFi Unified Management System.
	Provides centralized management for multiple AP devices including:
	- Device registration and authentication
	- Configuration template management
	- Real-time device status monitoring
	- WebSocket-based device communication
	- Audit logging and access control
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
		LDFLAGS="$(TARGET_LDFLAGS) -lsqlite3 -lssl -lcrypto -lwebsockets -lhiredis -lcjson -luuid -lpthread" \
		-j$(PARALLEL_JOBS)
endef

define Package/ac-manager/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ac-manager.bin $(1)/usr/sbin/ac-manager

	$(INSTALL_DIR) $(1)/etc/ac-manager
	$(INSTALL_CONF) ./config/ac-manager.conf $(1)/etc/ac-manager/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./scripts/ac-manager.init $(1)/etc/init.d/ac-manager

endef

define Package/ac-manager/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	/etc/init.d/ac-manager enable
fi
exit 0
endef

define Package/ac-manager/postrm
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	/etc/init.d/ac-manager stop
	/etc/init.d/ac-manager disable
fi
exit 0
endef

$(eval $(call BuildPackage,ac-manager))