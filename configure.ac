# Initialize Autoconf
AC_INIT([vkil], [0.0.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign -Werror])

# Checks for programs.
AM_PROG_AR
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AM_CFLAGS="-Wall -Werror"
AC_SUBST([AM_CFLAGS])

# Initialize Libtool
LT_INIT([shared])

AC_CONFIG_FILES([Makefile apps/Makefile apps/flash_util/Makefile src/Makefile unittest/Makefile])
AC_OUTPUT
