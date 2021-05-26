cmake_minimum_required(VERSION 3.14)

project(MCServer LANGUAGES C)

#########
# Build #
#########

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

find_package(OpenSSL 1.1 REQUIRED)
find_package(CURL 7.64.1 REQUIRED)

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

