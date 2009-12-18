/*
 * nls.c :  Helpers for NLS programs.
 *
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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

#include <stdlib.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <apr_errno.h>

#include "svn_nls.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_path.h"

#include "svn_private_config.h"

#ifdef WIN32
/* FIXME: We're using an internal APR header here, which means we
   have to build Subversion with APR sources. This being Win32-only,
   that should be fine for now, but a better solution must be found in
   combination with issue #850. */
#include <arch/win32/apr_arch_utf8.h>
#endif


svn_error_t *
svn_nls_init(void)
{
  svn_error_t *err = SVN_NO_ERROR;

#ifdef ENABLE_NLS
  if (getenv("SVN_LOCALE_DIR"))
    {
      bindtextdomain(PACKAGE_NAME, getenv("SVN_LOCALE_DIR"));
    }
  else
    {
#ifdef WIN32
      WCHAR ucs2_path[MAX_PATH];
      char* utf8_path;
      const char* internal_path;
      apr_pool_t* pool;
      apr_status_t apr_err;
      apr_size_t inwords, outbytes, outlength;

      apr_pool_create(&pool, 0);
      /* get exe name - our locale info will be in '../share/locale' */
      inwords = GetModuleFileNameW(0, ucs2_path,
                                   sizeof(ucs2_path) / sizeof(ucs2_path[0]));
      if (! inwords)
        {
          /* We must be on a Win9x machine, so attempt to get an ANSI path,
             and convert it to Unicode. */
          CHAR ansi_path[MAX_PATH];

          if (GetModuleFileNameA(0, ansi_path, sizeof(ansi_path)))
            {
              inwords =
                MultiByteToWideChar(CP_ACP, 0, ansi_path, -1, ucs2_path,
                                    sizeof(ucs2_path) / sizeof(ucs2_path[0]));
              if (! inwords)
                {
                err =
                  svn_error_createf(APR_EINVAL, NULL,
                                    _("Can't convert string to UCS-2: '%s'"),
                                    ansi_path);
                }
            }
          else
            {
              err = svn_error_create(APR_EINVAL, NULL,
                                     _("Can't get module file name"));
            }
        }

      if (! err)
        {
          outbytes = outlength = 3 * (inwords + 1);
          utf8_path = apr_palloc(pool, outlength);
          apr_err = apr_conv_ucs2_to_utf8(ucs2_path, &inwords,
                                          utf8_path, &outbytes);
          if (!apr_err && (inwords > 0 || outbytes == 0))
            apr_err = APR_INCOMPLETE;
          if (apr_err)
            {
              err = svn_error_createf(apr_err, NULL,
                                      _("Can't convert module path "
                                        "to UTF-8 from UCS-2: '%s'"),
                                      ucs2_path);
            }
          else
            {
              utf8_path[outlength - outbytes] = '\0';
              internal_path = svn_path_internal_style(utf8_path, pool);
              /* get base path name */
              internal_path = svn_path_dirname(internal_path, pool);
              internal_path = svn_path_join(internal_path,
                                            SVN_LOCALE_RELATIVE_PATH,
                                            pool);
              bindtextdomain(PACKAGE_NAME, internal_path);
            }
        }
      svn_pool_destroy(pool);
    }
#else /* ! WIN32 */
      bindtextdomain(PACKAGE_NAME, SVN_LOCALE_DIR);
    }
#ifdef HAVE_BIND_TEXTDOMAIN_CODESET
  bind_textdomain_codeset(PACKAGE_NAME, "UTF-8");
#endif /* HAVE_BIND_TEXTDOMAIN_CODESET */
#endif /* WIN32 */
#endif /* ENABLE_NLS */

  return err;
}
