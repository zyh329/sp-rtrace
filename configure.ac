AC_PREREQ([2.5])
AC_INIT([sp-rtrace], [1.8], [none])

AC_CONFIG_AUX_DIR([aux])

AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_CONFIG_SRCDIR([src/rtrace/sp_rtrace.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.

AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL

# set libtool versioning
AC_SUBST(VERSION_INFO, $(echo -version-info $VERSION | sed s/\\./:/))

# set up defines
AC_DEFINE(_GNU_SOURCE, 1, [Enable GNU extensions])
AC_DEFINE_UNQUOTED(BUILD_ARCH, "$(uname -m)", [Build architecture])

# Checks for libraries.


# Check for zlib availability
AC_CHECK_LIB([z], [inflate],
  [LIBS_Z="-lz"],
  [AC_MSG_ERROR([zlib library is required])],
)

# Check for dl availability
AC_CHECK_LIB([dl], [dlopen],
  [LIBS_DL="-ldl"],
  [AC_MSG_ERROR([dlopen library is required])],
)

# Check for liberty_pic
AC_CHECK_LIB([iberty_pic], [cplus_demangle],
 [LIBS_IBERTY="-liberty_pic"],
 [LIBS_IBERTY="-liberty"]
)

# Check for BFD availability
AC_CHECK_LIB([bfd], [bfd_openr],
  [LIBS_BFD="-lbfd $LIBS_DL $LIBS_Z"],
  [AC_MSG_ERROR([BFD library is required])],
  [$LIBS_DL $LIBS_Z $LIBS_IBERTY]
)

# Check for clock_gettime
AC_CHECK_LIB([rt], [clock_gettime],
  [LIBS_RT="-lrt"],
  [AC_MSG_ERROR([rt library is required])],
)

AC_SUBST(LIBS_IBERTY)
AC_SUBST(LIBS_BFD)

PKG_CHECK_MODULES([GLIB], [glib-2.0])

case $build in
  i486-*|i586-*|i686-*)
    CFLAGS="$CFLAGS -march=i486"
    ;;
  arm*)
    # libatomic-ops is required for the older armel toolchain
    AS_VERSION_COMPARE($($CC -dumpversion), "4.3.3", 
      [AC_CHECK_HEADER([atomic_ops.h], 
        AC_DEFINE(USE_LAOPS, 1, [Use libatomic-ops instead of __sync_* gcc builtins]),
        AC_MSG_ERROR([libatomic-ops is required when compiling for arm with $CC version less than 4.3.3])
        )],
    )
    ;;
  x86_64*)
    ;;
  *)
    AC_MSG_ERROR([Unsupported architecture: $build])
    ;;
esac
    
# optional debug output
AC_ARG_ENABLE(debug-info, 
  [  --enable-debug-info Enables debug output], 
  [ AS_IF(test x$enableval = xyes, [AC_DEFINE(DEBUG_INFO, 1, [Enable debug output])]) ],
)

 	
# disable c++ precompiled headers
AC_ARG_ENABLE(precomp,
AC_HELP_STRING([--disable-precomp] [Disables precompiled headers]),
[case "${enableval}" in
  yes) precomp=true ;;
  no)  precomp=false ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-precomp) ;;
esac],[precomp=true])
AM_CONDITIONAL(PRECOMP, test x$precomp = xtrue)


# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h memory.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([clock_gettime dup2 gettimeofday memmove memset mkfifo setenv strcasecmp strchr strerror])

AC_CONFIG_FILES([Makefile
		src/Makefile
		doc/Makefile
		tests/Makefile
		])

AC_OUTPUT
