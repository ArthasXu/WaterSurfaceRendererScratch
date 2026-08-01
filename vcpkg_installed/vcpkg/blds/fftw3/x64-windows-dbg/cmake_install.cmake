# Install script for directory: D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/src/fftw-3-1dc30b866b.clean

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/pkgs/fftw3_x64-windows/debug")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "OFF")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/fftw3l.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/fftw3l.dll")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/src/fftw-3-1dc30b866b.clean/api/fftw3.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/src/fftw-3-1dc30b866b.clean/api/fftw3.f"
    "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/src/fftw-3-1dc30b866b.clean/api/fftw3l.f03"
    "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/src/fftw-3-1dc30b866b.clean/api/fftw3q.f03"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/fftw3.f03")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Development" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/fftw3l.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Development" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fftw3l" TYPE FILE FILES
    "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/FFTW3lConfig.cmake"
    "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/FFTW3lConfigVersion.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Development" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fftw3l/FFTW3LibraryDepends.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fftw3l/FFTW3LibraryDepends.cmake"
         "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/CMakeFiles/Export/0e80b2a5f38dfbbc3217155721c8592c/FFTW3LibraryDepends.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fftw3l/FFTW3LibraryDepends-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fftw3l/FFTW3LibraryDepends.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fftw3l" TYPE FILE FILES "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/CMakeFiles/Export/0e80b2a5f38dfbbc3217155721c8592c/FFTW3LibraryDepends.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fftw3l" TYPE FILE FILES "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/CMakeFiles/Export/0e80b2a5f38dfbbc3217155721c8592c/FFTW3LibraryDepends-debug.cmake")
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/WaterSurfaceRendererScratch/vcpkg_installed/vcpkg/blds/fftw3/x64-windows-dbg/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
