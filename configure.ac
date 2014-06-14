AC_PREREQ([2.61])
AC_INIT([ocproxy], [0.2], [dme@dme.org], [ocproxy], [http://repo.or.cz/w/ocproxy.git])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_USE_SYSTEM_EXTENSIONS
AC_GNU_SOURCE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_CONFIG_FILES([Makefile])

# --enable-debug

AC_ARG_ENABLE(
	[debug],
	[AS_HELP_STRING([--enable-debug],[enable debugging code and output])],
	[],
	[enable_debug="no"]
)

if test "x$enable_debug" = xyes; then
	CFLAGS="$CFLAGS -O0 -ggdb"
fi

EXTRA_PC_LIBS=""

AS_COMPILER_FLAGS(WFLAGS,
        "-Wall
         -Wextra
         -Wno-missing-field-initializers
         -Wno-sign-compare
         -Wno-pointer-sign
         -Wno-unused-parameter
         -Werror=pointer-to-int-cast
         -Wdeclaration-after-statement
         -Werror-implicit-function-declaration
         -Wformat-nonliteral
         -Wformat-security
         -Winit-self
         -Wno-missing-declarations
         -Wmissing-include-dirs
         -Wnested-externs
         -Wpointer-arith
         -Wwrite-strings")
AC_SUBST(WFLAGS, [$WFLAGS])

AC_SEARCH_LIBS([event_add], [event])

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

AC_OUTPUT
