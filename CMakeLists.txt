# ======================================================================
#  otsdaq-mu2e main build file
#
#  cd .../path/to/build/directory
#  source .../path/to/otsdaq-mu2e/ups/setup_for_development
#  buildtool
# ======================================================================

cmake_minimum_required (VERSION 3.19 FATAL_ERROR)

find_package(cetmodules 3.19.02 REQUIRED)

project(otsdaq-mu2e VERSION 11.04.00)

include(CetCMakeEnv)
cet_cmake_env()
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

cet_set_compiler_flags(DIAGS VIGILANT
 WERROR
 NO_UNDEFINED
 EXTRA_FLAGS -pedantic -Wno-unused-parameter
 )


string(TOUPPER ${CMAKE_BUILD_TYPE} BTYPE_UC)
if(BTYPE_UC STREQUAL DEBUG)
  add_definitions(-DDEBUGME)
	if(DEFINED ENV{USE_ASAN})
		if($ENV{USE_ASAN} GREATER 0)
			message("!!! Address Sanitizer Activated!!!")
			message("!!! Note: To run, add 'export ASAN_OPTIONS=verify_asan_link_order=0' to setup !!!")
			add_compile_options(-fsanitize=address -Wno-unused-command-line-argument)
			add_link_options(-lasan)
		endif()
	endif()
	if(DEFINED ENV{USE_TSAN})
		if($ENV{USE_TSAN} GREATER 0)
			message("!!! Thread Sanitizer Activated!!!")
			add_compile_options(-fsanitize=thread -Wno-unused-command-line-argument -Wno-tsan)
			add_link_options(-ltsan)
		endif()
	endif()
	if(DEFINED ENV{USE_UBSAN})
		if($ENV{USE_UBSAN} GREATER 0)
			message("!!! \"Undefined Behavior\" Sanitizer Activated!!!")
			add_compile_options(-fsanitize=undefined -Wno-unused-command-line-argument)
			add_link_options(-lubsan)
		endif()
	endif()
	if(DEFINED ENV{USE_GCOV})
		add_compile_options(-fprofile-arcs -ftest-coverage --coverage)
		add_link_options(--coverage)
	endif()
endif()

#exeption stack trace
if(DEFINED ENV{USE_EXCEPTIONSTACKTRACE})
	if($ENV{USE_EXCEPTIONSTACKTRACE} GREATER 0)
		message("!!! Exception StackTrace Activated!!!")
		add_definitions(-D EXCEPTIONSTACKTRACE)
		set( EXCEPTIONSTACKTRACE_LIB  artdaq-core_Utilities_ExceptionStackTrace )
		add_compile_options( -rdynamic -Wno-unused-command-line-argument )
		add_link_options( -rdynamic )
	endif()
endif()

#cet_report_compiler_flags()

find_package(otsdaq 2.06.08 REQUIRED EXPORT)
find_package(otsdaq-components 2.06.08 REQUIRED EXPORT)
find_package(artdaq-mu2e 1.05.02 REQUIRED EXPORT)
find_package(PostgreSQL 13.2 REQUIRED)

# macros for art_dictionary and simple_plugin

include(BuildPlugins)

# Code
add_subdirectory(otsdaq-mu2e)

# test programs
add_subdirectory(test)

# Useful script tools
add_subdirectory(tools)
FILE(COPY tools DESTINATION .)
INSTALL(DIRECTORY tools DESTINATION . FILE_PERMISSIONS USE_SOURCE_PERMISSIONS)

# Test Programs
#add_subdirectory(test)
#FILE(COPY test DESTINATION .)
#INSTALL(DIRECTORY test DESTINATION ${product}/${version})

# Documentation (to comment out if not needed DOXYGEN locally)
if( $ENV{OTS_DOXY} MATCHES "DOIT" )
	add_subdirectory(doc)
endif()

cet_cmake_config()
