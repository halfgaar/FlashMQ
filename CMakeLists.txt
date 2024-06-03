cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0048 NEW)
include(CheckCXXCompilerFlag)
include(CMakeLists.shared)

project(FlashMQ VERSION 1.14.0 LANGUAGES CXX)

add_definitions(-DOPENSSL_API_COMPAT=0x10100000L)
add_definitions(-DFLASHMQ_VERSION=\"${PROJECT_VERSION}\")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

check_cxx_compiler_flag("-msse4.2"   COMPILER_RT_HAS_MSSE4_2_FLAG)
if (${COMPILER_RT_HAS_MSSE4_2_FLAG})
    SET(CMAKE_CXX_FLAGS "-msse4.2")
endif()

check_cxx_compiler_flag("-rdynamic" COMPILER_SUPPORTS_RDYNAMIC)
if (${COMPILER_SUPPORTS_RDYNAMIC})
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
endif()

add_compile_options(-Wall)

add_executable(flashmq
    ${FLASHMQ_HEADERS}
    ${FLASHMQ_IMPLS}

    main.cpp
    )

target_link_libraries(flashmq pthread dl ssl crypto resolv anl)

execute_process(COMMAND ../.get-os-codename-and-stamp.sh OUTPUT_VARIABLE OS_CODENAME)

install(TARGETS flashmq
  RUNTIME DESTINATION "/usr/bin/")

install(DIRECTORY DESTINATION "/var/lib/flashmq")
install(DIRECTORY DESTINATION "/var/log/flashmq")
install(FILES flashmq.conf DESTINATION "/etc/flashmq")
install(FILES debian/flashmq.service DESTINATION "/lib/systemd/system")
install(FILES man/flashmq.conf.5 DESTINATION "/usr/share/man/man5")
install(FILES man/flashmq.1 DESTINATION "/usr/share/man/man1")

SET(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/debian/conffiles;${CMAKE_CURRENT_SOURCE_DIR}/debian/preinst;${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm;${CMAKE_CURRENT_SOURCE_DIR}/debian/prerm")

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Wiebe Cazemier <wiebe@flashmq.org>")
SET(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Light-weight, high performance MQTT server capable of million+ messages per second.")
SET(CPACK_PACKAGE_HOMEPAGE_URL "https://www.flashmq.org/")
SET(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
SET(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
SET(CPACK_DEBIAN_PACKAGE_RELEASE ${OS_CODENAME})
SET(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
INCLUDE(CPack)
