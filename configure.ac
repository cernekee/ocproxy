AC_PREREQ([2.61])
AC_INIT([ocproxy], [1.60], [dme@dme.org], [ocproxy], [https://github.com/cernekee/ocproxy])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_USE_SYSTEM_EXTENSIONS
AC_GNU_SOURCE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CC_C99
AC_CANONICAL_HOST
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

# --enable-vpnns

case $host_os in
	*linux*)
		vpnns_default=yes
		;;
	*)
		vpnns_default=no
		;;
esac

AC_ARG_ENABLE(
	[vpnns],
	[AS_HELP_STRING([--enable-vpnns],[build vpnns binary (Linux only)])],
	[enable_vpnns="$enableval"],
	[enable_vpnns="$vpnns_default"]
)

if test "x$enable_vpnns" = xyes; then
	AC_CHECK_HEADERS([linux/if_tun.h sched.h], [],
			 [AC_MSG_ERROR([Missing headers for vpnns])])

fi
AM_CONDITIONAL([ENABLE_VPNNS], [test "x$enable_vpnns" = xyes])

EXTRA_PC_LIBS=""

AS_COMPILER_FLAGS(WFLAGS,
        "-Wall
         -Wextra
         -Wno-missing-field-initializers
         -Wno-sign-compare
         -Wno-pointer-sign
         -Wno-unused-parameter
         -Wno-address
         -Werror=pointer-to-int-cast
         -Wno-declaration-after-statement
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
AC_CHECK_HEADERS([event2/event.h], [],
		 [AC_MSG_ERROR([Missing development files for libevent2])])

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

AC_OUTPUT
