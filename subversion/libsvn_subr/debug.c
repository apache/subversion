/*
 * debug.c :  small functions to help SVN developers
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* These functions are only available to SVN developers.  */
#ifdef SVN_DEBUG

#include <stdarg.h>

#include "svn_types.h"

#include "private/svn_debug.h"


/* This will be tweaked by the preamble code.  */
static FILE * volatile debug_output = NULL;


static svn_boolean_t
quiet_mode(void)
{
  return getenv("SVN_DBG_QUIET") != NULL;
}


void
svn_dbg__preamble(const char *file, long line, FILE *output)
{
  debug_output = output;

  if (output != NULL && !quiet_mode())
    {
      /* Quick and dirty basename() code.  */
      const char *slash = strrchr(file, '/');

      if (slash == NULL)
        slash = strrchr(file, '\\');
      if (slash == NULL)
        slash = file;
      else
        ++slash;

      fprintf(output, "DBG: %s:%4ld: ", slash, line);
    }
}


void
svn_dbg__printf(const char *fmt, ...)
{
  FILE *output = debug_output;
  va_list ap;

  if (output == NULL || quiet_mode())
    return;

  va_start(ap, fmt);
  (void) vfprintf(output, fmt, ap);
  va_end(ap);
}


#endif /* SVN_DEBUG */
