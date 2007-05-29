PROJECT(natpmp)

ADD_EXECUTABLE(natpmp natpmp.c die.c leases.c)

IF(NOT CMAKE_COMPILER_IS_GNUCC)
	MESSAGE(FATAL_ERROR "You need gcc to compile this program.")
ENDIF(NOT CMAKE_COMPILER_IS_GNUCC)
