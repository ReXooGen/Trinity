# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-src")
  file(MAKE_DIRECTORY "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-src")
endif()
file(MAKE_DIRECTORY
  "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-build"
  "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-subbuild/minhook-populate-prefix"
  "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-subbuild/minhook-populate-prefix/tmp"
  "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-subbuild/minhook-populate-prefix/src/minhook-populate-stamp"
  "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-subbuild/minhook-populate-prefix/src"
  "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-subbuild/minhook-populate-prefix/src/minhook-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-subbuild/minhook-populate-prefix/src/minhook-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/GABUT/apalah/Trinity-Legacy/build-dlc/_deps/minhook-subbuild/minhook-populate-prefix/src/minhook-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
