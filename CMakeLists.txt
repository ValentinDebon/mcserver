cmake_minimum_required(VERSION 3.14)

project(MCServer
	VERSION 0.0.1
	DESCRIPTION "Minecraft Java Edition Vanilla Servers Manager"
	HOMEPAGE_URL "https://github.com/ValentinDebon/mcserver"
	LANGUAGES C
)

#########
# Build #
#########

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

find_package(OpenSSL 1.1 REQUIRED)
find_package(CURL 7.85.0 REQUIRED)

find_path(JSON_C_INCLUDE_DIRS json-c/json.h REQUIRED)
find_library(JSON_C_LIBRARIES json-c REQUIRED)

include_directories(${OPENSSL_INCLUDE_DIR} ${CURL_INCLUDE_DIRS} ${JSON_C_INCLUDE_DIRS})

add_executable(mcserver
	src/mcserver/main.c
	src/mcserver/manifest.c
	src/mcserver/storage.c
)

target_link_libraries(mcserver PUBLIC ${OPENSSL_LIBRARIES} ${CURL_LIBRARIES} ${JSON_C_LIBRARIES})

###########
# Install #
###########

install(TARGETS mcserver RUNTIME)
install(FILES man/mcserver.1 TYPE MAN)

#############
# Packaging #
#############

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_CONTACT "Valentin Debon <valentin.debon@heylelos.org>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "java-runtime-headless, libcurl4 (>= 7.85.0), libssl3 (>= 1.1), libjson-c5")

include(CPack)
