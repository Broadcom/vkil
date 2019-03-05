# Initialize Autoconf

AC_INIT([vkil], [0.0.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign -Werror])

AC_ARG_ENABLE([drvmodel],
[  --enable-drvmodel    Turn on x 86 user space driver model],
[case "${enableval}" in
  yes) drvmodel=true ;;
  no)  drvmodel=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-drvmodel]) ;;
esac],[debug=false])
AM_CONDITIONAL([VKDRV_USERMODEL], [test x$drvmodel = xtrue])

# Checks for programs.
AM_PROG_AR
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AM_CFLAGS="-Wall -Werror"
AC_SUBST([AM_CFLAGS])

# Initialize Libtool
LT_INIT([shared])

AC_CONFIG_FILES([Makefile apps/Makefile \
                 apps/flash_util/Makefile apps/pcie_util/Makefile \
                 src/Makefile unittest/Makefile drv_model/Makefile])
AC_OUTPUT
