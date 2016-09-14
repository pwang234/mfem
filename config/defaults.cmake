# Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at the
# Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights reserved.
# See file COPYRIGHT for details.
#
# This file is part of the MFEM library. For more information and source code
# availability see http://mfem.org.
#
# MFEM is free software; you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License (as published by the Free
# Software Foundation) version 2.1 dated February 1999.

# See the file INSTALL for description of the configuration options.

# Default options. To replace these, copy this file to user.cmake and modify it.

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Build type: Debug, Release, RelWithDebInfo, or MinSizeRel." FORCE)
endif()

# MFEM options. Set to mimic the default "default.mk" file.
option(MFEM_USE_MPI "Enable MPI parallel build" OFF)
option(MFEM_USE_LIBUNWIND "Enable backtrace for errors." OFF)
option(MFEM_USE_LAPACK "Enable LAPACK usage" OFF)
option(MFEM_THREAD_SAFE "Enable thread safety" OFF)
option(MFEM_USE_OPENMP "Enable OpenMP usage" OFF)
option(MFEM_USE_MEMALLOC "Enable the internal MEMALLOC option." ON)
option(MFEM_USE_MESQUITE "Enable MESQUITE usage" OFF)
option(MFEM_USE_SUITESPARSE "Enable SuiteSparse usage" OFF)
option(MFEM_USE_SUPERLU "Enable SuperLU_DIST usage" OFF)
option(MFEM_USE_GECKO "Enable GECKO usage" OFF)
option(MFEM_USE_GNUTLS "Enable GNUTLS usage" OFF)
option(MFEM_USE_NETCDF "Enable NETCDF usage" OFF)
option(MFEM_USE_MPFR "Enable MPFR usage." OFF)

# Allow a user to disable testing, examples, and/or miniapps at
# CONFIGURE TIME if they don't want/need them (e.g. if MFEM is "just a
# dependency" and all they need is the library, building all that
# stuff adds unnecessary overhead). To match "makefile" behavior, they
# are all enabled by default.
option(MFEM_ENABLE_TESTING "Enable the ctest framework for testing" ON)
option(MFEM_ENABLE_EXAMPLES "Build all of the examples" ON)
option(MFEM_ENABLE_MINIAPPS "Build all of the miniapps" ON)

# Setting CXX/MPICXX on the command line or in user.cmake will overwrite the
# autodetected C++ compiler.
# set(CXX g++)
# set(MPICXX mpicxx)

# The *_DIR paths below will be the first place searched for the corresponding
# headers and library. If these fail, then standard cmake search is performed.
# Note: if the variables are already in the cache, they are not overwritten.
set(HYPRE_DIR "" CACHE PATH "Path to the hypre library.")
# If hypre was compiled to depend on BLAS and LAPACK:
# set(HYPRE_REQUIRED_PACKAGES "BLAS" "LAPACK" CACHE STRING
#     "Packages that HYPRE depends on.")
set(METIS_DIR "" CACHE PATH "Path to the METIS library.")
set(LIBUNWIND_DIR "" CACHE PATH "Path to Libunwind.")
set(MESQUITE_DIR "" CACHE PATH "Path to the Mesquite library.")
set(SuiteSparse_DIR "" CACHE PATH "Path to the SuiteSparse library.")
set(SuiteSparse_REQUIRED_PACKAGES "QUIET:" "SuiteSparseUMFPACK" "SuiteSparseKLU"
    "SuiteSparseAMD" "SuiteSparseBTF" "SuiteSparseCHOLMOD" "SuiteSparseCOLAMD"
    "SuiteSparseCAMD" "SuiteSparseCCOLAMD" "SuiteSparseCONFIG"
    "VERBOSE:" "BLAS" "METIS"
    CACHE STRING "Additional packages required by SuiteSparse.")
set(ParMETIS_DIR "" CACHE PATH "Path to the ParMETIS library.")
set(ParMETIS_REQUIRED_PACKAGES "METIS" CACHE STRING
    "Additional packages required by ParMETIS.")
set(SuperLUDist_DIR "" CACHE PATH "Path to the SuperLU_DIST library.")
# SuperLU_DIST may also depend on "OpenMP", depending on how it was compiled.
set(SuperLUDist_REQUIRED_PACKAGES "MPI" "BLAS" "ParMETIS" CACHE STRING
    "Additional packages required by SuperLU_DIST.")
set(GECKO_DIR "" CACHE PATH "Path to the Gecko library.")
set(GNUTLS_DIR "" CACHE PATH "Path to the GnuTLS library.")
set(NETCDF_DIR "" CACHE PATH "Path to the NetCDF library.")
set(NetCDF_REQUIRED_PACKAGES "" CACHE STRING
    "Additional packages required by NetCDF.")
set(MPFR_DIR "" CACHE PATH "Path to the MPFR library.")

set(BLAS_INCLUDE_DIRS "" CACHE STRING "Path to BLAS headers.")
set(BLAS_LIBRARIES "" CACHE STRING "The BLAS library.")
set(LAPACK_INCLUDE_DIRS "" CACHE STRING "Path to LAPACK headers.")
set(LAPACK_LIBRARIES "" CACHE STRING "The LAPACK library.")

# Some useful variables:
# set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Verbose makefiles.")
