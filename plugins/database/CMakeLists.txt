# create phony target to build everything in plugins/database folder
add_custom_target(all-plugins-database)
add_dependencies(all-plugins all-plugins-database)

set(
  IRODS_PLUGINS_DATABASE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/db_plugin.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/general_query.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/general_query_setup.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/irods_catalog_properties.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/low_level_odbc.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/mid_level_routines.cpp"
)

set(
  IRODS_PLUGINS_DATABASE_HEADERS_PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include/irods/private/irods_catalog_properties.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/irods/private/irods_zone_info.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/irods/private/low_level.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/irods/private/low_level_odbc.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/irods/private/mid_level.hpp"
)

if (NOT ODBC_LIBRARY)
  find_library(ODBC_LIBRARY odbc)
  if (ODBC_LIBRARY)
    message(STATUS "Found odbc: ${ODBC_LIBRARY}")
  else()
    message(FATAL_ERROR "odbc library not found")
  endif()
endif()

set(
  IRODS_DATABASE_PLUGINS
  postgres
  mysql
  oracle
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/icatSysTables.sql.pp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/src/icatSysTables.sql.pp"
  @ONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/mysql_functions.sql.in"
  "${CMAKE_CURRENT_BINARY_DIR}/src/mysql_functions.sql"
  @ONLY
)

foreach(plugin IN LISTS IRODS_DATABASE_PLUGINS)
  string(TOUPPER ${plugin} PLUGIN_UPPERCASE)
  set(plugin_target "irods_database_plugin-${plugin}")
  set(plugin_name "${plugin}")

  add_library(
    ${plugin_target}
    MODULE
    "${IRODS_PLUGINS_DATABASE_SOURCES}"
    "${IRODS_PLUGINS_DATABASE_HEADERS_PRIVATE}"
  )
  set_property(TARGET ${plugin_target} PROPERTY LIBRARY_OUTPUT_NAME ${plugin_name})
  target_link_libraries(
    ${plugin_target}
    PRIVATE
    irods_common
    irods_server
    irods_plugin_dependencies
    "${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_filesystem.so"
    "${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_system.so"
    "${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_regex.so"
    "${IRODS_EXTERNALS_FULLPATH_NANODBC}/lib/libnanodbc.so"
    fmt::fmt
    ${ODBC_LIBRARY}
  )
  target_include_directories(
    ${plugin_target}
    PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "${IRODS_EXTERNALS_FULLPATH_BOOST}/include"
    "${IRODS_EXTERNALS_FULLPATH_NANODBC}/include"
  )
  target_compile_definitions(
    ${plugin_target}
    PRIVATE
    ${IRODS_COMPILE_DEFINITIONS_PRIVATE}
    ENABLE_RE
    IRODS_ENABLE_SYSLOG
  )

  # TODO(#6213): don't run `cpp` like this
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/icatSysTables_${plugin}.sql"
    COMMAND cpp -E -P -D${plugin} "${CMAKE_CURRENT_BINARY_DIR}/src/icatSysTables.sql.pp" "${CMAKE_CURRENT_BINARY_DIR}/icatSysTables_${plugin}.sql"
    MAIN_DEPENDENCY "${CMAKE_CURRENT_BINARY_DIR}/src/icatSysTables.sql.pp"
  )
  add_custom_target(IRODS_PHONY_TARGET_icatSysTables_${plugin}.sql ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/icatSysTables_${plugin}.sql")
  add_dependencies(${plugin_target} IRODS_PHONY_TARGET_icatSysTables_${plugin}.sql)

  add_dependencies(all-plugins-database ${plugin_target})
  install(
    TARGETS
    ${plugin_target}
    LIBRARY
    DESTINATION "${IRODS_PLUGINS_DIRECTORY}/database"
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
  )

  install(
    FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/${plugin}/upgrade-3.3.xto4.0.0.sql"
    DESTINATION "${IRODS_HOME_DIRECTORY}/packaging"
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
  )

  install(
    FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/localhost_setup_${plugin}.input"
    DESTINATION "${IRODS_HOME_DIRECTORY}/packaging"
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
  )

  install(
    FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/icatDropSysTables.sql"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/icatSysInserts.sql"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/icatPurgeRecycleBin.sql"
    DESTINATION "${IRODS_HOME_DIRECTORY}/packaging/sql"
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
  )

  install(
    FILES
    "${CMAKE_CURRENT_BINARY_DIR}/icatSysTables_${plugin}.sql"
    DESTINATION "${IRODS_HOME_DIRECTORY}/packaging/sql"
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    RENAME icatSysTables.sql
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
  )
endforeach()

target_compile_definitions(irods_database_plugin-mysql PRIVATE MY_ICAT)
target_compile_definitions(irods_database_plugin-oracle PRIVATE ORA_ICAT)

install(
  FILES
  "${CMAKE_CURRENT_BINARY_DIR}/src/mysql_functions.sql"
  DESTINATION "${IRODS_HOME_DIRECTORY}/packaging/sql"
  COMPONENT ${IRODS_PACKAGE_COMPONENT_MYSQL_NAME}
)
