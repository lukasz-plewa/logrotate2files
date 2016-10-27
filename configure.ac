% autoscan # creates a configure.scan file
% mv configure.scan configure.ac
% edit configure.ac so that it contains at least the following lines
% (order is significant):
AC_INIT(logrotate2files, 0.1, l.plewa@adbglobal.com)
AC_CONFIG_SRCDIR(src)
AC_CONFIG_HEADER([src/logrotate2files.h])

AM_INIT_AUTOMAKE([subdir-objects])
# AM_INIT_AUTOMAKE(logrotate2files, main)

AC_FUNC_MALLOC
# AC_CHECK_FUNCS([pthread_create pthread_join])
AC_SEARCH_LIBS([pthread_create], [pthread])

# Checks for programs. 
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for libraries.
# AM_PROG_LIBTOOL

# Checks for header files.
# Checks for typedefs, structures, and 
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
