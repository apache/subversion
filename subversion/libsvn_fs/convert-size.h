/* convert-size.h : interface to ascii-to-size and vice-versa conversions
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_CONVERT_SIZE_H
#define SVN_LIBSVN_FS_CONVERT_SIZE_H

#include "apr.h"

/* Return the value of the string of digits at DATA as an ASCII
   decimal number.  The string is at most LEN bytes long.  The value
   of the number is at most MAX.  Set *END to the address of the first
   byte after the number, or zero if an error occurred while
   converting the number (overflow, for example).

   We would like to use strtoul, but that family of functions is
   locale-dependent, whereas we're trying to parse data in a
   local-independent format.  */

apr_size_t svn_fs__getsize (const char *data, apr_size_t len,
                            const char **endptr, apr_size_t max);


/* Store the ASCII decimal representation of VALUE at DATA.  Return
   the length of the representation if all goes well; return zero if
   the result doesn't fit in LEN bytes.  */
int svn_fs__putsize (char *data, apr_size_t len, apr_size_t value);


#endif /* SVN_LIBSVN_FS_CONVERT_SIZE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
