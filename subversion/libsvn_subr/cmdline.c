/*
 * cmdline.c :  Helpers for command-line programs.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#include <stdlib.h>             /* for atexit() */
#include <locale.h>             /* for setlocale() */

#include <apr_errno.h>          /* for apr_strerror */
#include <apr_general.h>        /* for apr_initialize/apr_terminate */

#include <svn_pools.h>
#include "svn_private_config.h" /* for SVN_WIN32 */


int
svn_cmdline_init (const char *progname, FILE *error_stream)
{
  apr_status_t status;

#ifdef SVN_WIN32
  /* Force the Windows console to use the same multibyte character set
     that the app uses internally. */

  /* FIXME: Win9x/Me don't support the SetConsoleCP funcitons. As a
     temporary measure, we skip setting the console code page on those
     systems. */
  OSVERSIONINFO os_version;
  os_version.dwOSVersionInfoSize = sizeof(os_version);
  if (!GetVersionEx(&os_version)
      || os_version.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
  {
    UINT codepage = GetACP();
    if (!SetConsoleCP(codepage))
      {
        if (error_stream)
          fprintf(error_stream,
                  "%s: error: cannot set console input codepage (code %lu)\n",
                  progname, (unsigned long) GetLastError());
        return EXIT_FAILURE;
      }

    if (!SetConsoleOutputCP(codepage))
      {
        if (error_stream)
          fprintf(error_stream,
                  "%s: error: cannot set console output codepage (code %lu)\n",
                  progname, (unsigned long) GetLastError());
        return EXIT_FAILURE;
      }
  }
#endif /* SVN_WIN32 */

  /* C programs default to the "C" locale. But because svn is supposed
     to be i18n-aware, it should inherit the default locale of its
     environment.  */
  if (!setlocale(LC_CTYPE, ""))
    {
      if (error_stream)
        fprintf(error_stream,
                "%s: error: cannot set the locale\n",
                progname);
      return EXIT_FAILURE;
    }

  /* Initialize the APR subsystem, and register an atexit() function
     to Uninitialize that subsystem at program exit. */
  status = apr_initialize();
  if (status)
    {
      if (error_stream)
        {
          char buf[1024];
          apr_strerror(status, buf, sizeof(buf) - 1);
          fprintf(error_stream,
                  "%s: error: cannot initialize APR: %s\n",
                  progname, buf);
        }
      return EXIT_FAILURE;
    }

  if (0 > atexit(apr_terminate))
    {
      if (error_stream)
        fprintf(error_stream,
                "%s: error: atexit registration failed\n",
                progname);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
