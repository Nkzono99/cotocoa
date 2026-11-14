cmake_minimum_required(VERSION 3.16)
project(cotocoa VERSION 0.3.0 LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(COTOCOA_ENABLE_FORTRAN "Build Fortran bindings and examples" ON)
find_package(MPI REQUIRED COMPONENTS C)
if(COTOCOA_ENABLE_FORTRAN)
    enable_language(Fortran)
    find_package(MPI REQUIRED COMPONENTS Fortran)
endif()

add_library(cotocoa SHARED src/ctca.c src/ctca_f.c src/cotocoa.c src/cotocoa_plan.c src/cotocoa_assembly.c)
target_compile_features(cotocoa PRIVATE c_std_11)
target_include_directories(cotocoa PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cotocoa PUBLIC MPI::MPI_C)
set_target_properties(cotocoa PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 2)

if(COTOCOA_ENABLE_FORTRAN)
add_library(cotocoa_fortran src/cotocoa_mod.f90 src/ctca_mod.f90 src/ctca_int8_mod.f90)
set_target_properties(cotocoa_fortran PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules)
target_include_directories(cotocoa_fortran PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/modules>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cotocoa>)
target_include_directories(cotocoa_fortran PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cotocoa_fortran PUBLIC cotocoa MPI::MPI_Fortran)
install(TARGETS cotocoa_fortran EXPORT cotocoaTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cotocoa FILES_MATCHING PATTERN "*.mod")
endif()

add_executable(cotocoa-coupler app/cotocoa_coupler.c)
target_link_libraries(cotocoa-coupler PRIVATE cotocoa)
add_executable(cotocoa-probe app/cotocoa_probe.c)
target_include_directories(cotocoa-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cotocoa-probe PRIVATE cotocoa)
add_executable(snapshot_c examples/snapshot/c.c)
target_link_libraries(snapshot_c PRIVATE cotocoa)
add_executable(task_c examples/tasks/c.c)
target_link_libraries(task_c PRIVATE cotocoa)
if(COTOCOA_ENABLE_FORTRAN)
add_executable(snapshot_fortran examples/snapshot/fortran.f90)
target_link_libraries(snapshot_fortran PRIVATE cotocoa_fortran)
add_executable(task_fortran examples/tasks/fortran.f90)
target_link_libraries(task_fortran PRIVATE cotocoa_fortran)
endif()
set_target_properties(cotocoa-coupler cotocoa-probe PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS cotocoa cotocoa-coupler cotocoa-probe EXPORT cotocoaTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/cotocoa.h include/ctca.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(cmake/cotocoaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cotocoaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cotocoa)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/cotocoaConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT cotocoaTargets NAMESPACE cotocoa:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cotocoa)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cotocoaConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cotocoaConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cotocoa)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
