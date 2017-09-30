
cmake_minimum_required ( VERSION 2.8.12 )

# project
project ( wilton_pdf CXX )

# dependencies
staticlib_add_subdirectory ( ${STATICLIB_DEPS}/external_libpng )
staticlib_add_subdirectory ( ${STATICLIB_DEPS}/external_hpdf )

set ( ${PROJECT_NAME}_DEPS
        staticlib_config
        staticlib_support
        staticlib_io
        staticlib_ranges
        staticlib_utils
        staticlib_json
        staticlib_tinydir
        hpdf )
staticlib_pkg_check_modules ( ${PROJECT_NAME}_DEPS_PC REQUIRED ${PROJECT_NAME}_DEPS )

# library
set ( ${PROJECT_NAME}_PLATFORM_SRC ) 
set ( ${PROJECT_NAME}_RESFILE )
set ( ${PROJECT_NAME}_DEFFILE )
if ( ${CMAKE_SYSTEM_NAME} STREQUAL "Windows" )
    configure_file ( ${WILTON_DIR}/resources/buildres/wilton_module.rc
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc )
    set ( ${PROJECT_NAME}_RESFILE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.rc )
    set ( ${PROJECT_NAME}_DEFFILE ${CMAKE_CURRENT_LIST_DIR}/resources/${PROJECT_NAME}.def )
endif ( )

add_library ( ${PROJECT_NAME} SHARED
        ${CMAKE_CURRENT_LIST_DIR}/src/wiltoncall_pdf.cpp
        ${${PROJECT_NAME}_RESFILE}
        ${${PROJECT_NAME}_DEFFILE} )
        
target_link_libraries ( ${PROJECT_NAME} PRIVATE
        wilton_core
        ${${PROJECT_NAME}_DEPS_PC_LIBRARIES} )

target_include_directories ( ${PROJECT_NAME} BEFORE PRIVATE 
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${WILTON_DIR}/core/include
        ${${PROJECT_NAME}_DEPS_PC_INCLUDE_DIRS} )
        
target_compile_options ( ${PROJECT_NAME} PRIVATE ${${PROJECT_NAME}_DEPS_PC_CFLAGS_OTHER} )

if ( ${CMAKE_SYSTEM_NAME} STREQUAL "Windows" )
    target_link_libraries ( ${PROJECT_NAME} PRIVATE wtsapi32 )
    set_property ( TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "/manifest:no" )
endif ( )

# pkg-config
set ( ${PROJECT_NAME}_PC_CFLAGS "-I${CMAKE_CURRENT_LIST_DIR}/include" )
set ( ${PROJECT_NAME}_PC_LIBS "-L${CMAKE_LIBRARY_OUTPUT_DIRECTORY} -l${PROJECT_NAME}" )
staticlib_list_to_string ( ${PROJECT_NAME}_PC_REQUIRES_PRIVATE "" ${PROJECT_NAME}_DEPS )
configure_file ( ${WILTON_DIR}/resources/buildres/pkg-config.in 
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/pkgconfig/${PROJECT_NAME}.pc )
