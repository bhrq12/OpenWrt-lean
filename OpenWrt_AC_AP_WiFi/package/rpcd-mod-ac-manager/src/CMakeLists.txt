cmake_minimum_required(VERSION 3.10)

PROJECT(rpcd-mod-ac-manager C)

ADD_DEFINITIONS(-Os -Wall -Werror --std=gnu99 -g3 -Wmissing-declarations)

SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

# Statically link sqlite3 to avoid runtime dependency on libsqlite3.so
# (sqlite3 package not available as feed in minimal SDK)
ADD_LIBRARY(rpcd-mod-ac-manager MODULE ac-manager.c)
TARGET_LINK_LIBRARIES(rpcd-mod-ac-manager ubox ubus dl)
TARGET_LINK_LIBRARIES(rpcd-mod-ac-manager -Wl,-Bstatic sqlite3 -Wl,-Bdynamic)
SET_TARGET_PROPERTIES(rpcd-mod-ac-manager PROPERTIES OUTPUT_NAME ac-manager PREFIX "")

INSTALL(TARGETS rpcd-mod-ac-manager LIBRARY DESTINATION lib)
