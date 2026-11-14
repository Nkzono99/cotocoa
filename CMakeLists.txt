cmake_minimum_required(VERSION 3.16)
project(cotocoa VERSION 0.1.0 LANGUAGES C Fortran)
include(GNUInstallDirs)
find_package(MPI REQUIRED COMPONENTS C Fortran)

add_library(cotocoa SHARED src/ctca.c src/ctca_f.c src/cotocoa.c)
target_compile_features(cotocoa PRIVATE c_std_11)
target_include_directories(cotocoa PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cotocoa PUBLIC MPI::MPI_C)
set_target_properties(cotocoa PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)

add_library(cotocoa_fortran src/cotocoa_mod.f90 src/ctca_mod.f90 src/ctca_int8_mod.f90)
set_target_properties(cotocoa_fortran PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules)
target_include_directories(cotocoa_fortran PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/modules>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cotocoa>)
target_link_libraries(cotocoa_fortran PUBLIC cotocoa MPI::MPI_Fortran)

add_executable(cotocoa-coupler app/cotocoa_coupler.c)
target_link_libraries(cotocoa-coupler PRIVATE cotocoa)
add_executable(snapshot_c examples/snapshot/c.c)
target_link_libraries(snapshot_c PRIVATE cotocoa)
add_executable(snapshot_fortran examples/snapshot/fortran.f90)
target_link_libraries(snapshot_fortran PRIVATE cotocoa_fortran)
install(TARGETS cotocoa cotocoa_fortran cotocoa-coupler
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/cotocoa.h include/ctca.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cotocoa FILES_MATCHING PATTERN "*.mod")

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
