/*
 * cmdline.c :  Helpers for command-line programs.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <apr_errno.h>          /* for apr_strerror */
#include <apr_general.h>        /* for apr_initialize/apr_terminate */
#include <apr_strings.h>        /* for apr_snprintf */

#include "svn_cmdline.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "utf_impl.h"

#include "svn_private_config.h"

#ifdef WIN32
/* FIXME: We're using an internal APR header here, which means we
   have to build Subversion with APR sources. This being Win32-only,
   that should be fine for now, but a better solution must be found in
   combination with issue #850. */
#include "arch/win32/apr_arch_utf8.h"

#include <mbctype.h>
#endif

#define SVN_UTF_CONTOU_XLATE_HANDLE "svn-utf-contou-xlate-handle"
#define SVN_UTF_UTOCON_XLATE_HANDLE "svn-utf-utocon-xlate-handle"

/* The stdin encoding. If null, it's the same as the native encoding. */
static const char *input_encoding = NULL;

/* The stdout encoding. If null, it's the same as the native encoding. */
static const char *output_encoding = NULL;


int
svn_cmdline_init (const char *progname, FILE *error_stream)
{
  apr_status_t status;
  apr_pool_t *pool;

#ifndef WIN32
  {
    struct stat st;

    /* The following makes sure that file descriptors 0 (stdin), 1
       (stdout) and 2 (stderr) will not be "reused", because if
       e.g. file descriptor 2 would be reused when opening a file, a
       write to stderr would write to that file and most likely
       corrupt it. */
    if ((fstat (0, &st) == -1 && open ("/dev/null", O_RDONLY) == -1) ||
        (fstat (1, &st) == -1 && open ("/dev/null", O_WRONLY) == -1) ||
        (fstat (2, &st) == -1 && open ("/dev/null", O_WRONLY) == -1))
      {
        fprintf(error_stream, "%s: error: cannot open '/dev/null'\n",
                progname);
        return EXIT_FAILURE;
      }
  }
#endif

#ifdef WIN32
  /* Initialize the input and output encodings. */
  {
    static char input_encoding_buffer[16];
    static char output_encoding_buffer[16];

    apr_snprintf(input_encoding_buffer, sizeof input_encoding_buffer,
                 "CP%u", (unsigned) GetConsoleCP());
    input_encoding = input_encoding_buffer;

    apr_snprintf(output_encoding_buffer, sizeof output_encoding_buffer,
                 "CP%u", (unsigned) GetConsoleOutputCP());
    output_encoding = output_encoding_buffer;
  }
#endif /* WIN32 */

  /* C programs default to the "C" locale. But because svn is supposed
     to be i18n-aware, it should inherit the default locale of its
     environment.  */
  if (!setlocale(LC_ALL, ""))
    {
      if (error_stream)
        {
          const char *env_vars[] = { "LC_ALL", "LC_CTYPE", "LANG", NULL };
          const char **env_var = &env_vars[0], *env_val = NULL;
          while (*env_var)
            {
              env_val = getenv(*env_var);
              if (env_val && env_val[0])
                break;
              ++env_var;
            }

          if (!*env_var)
            {
              /* Unlikely. Can setlocale fail if no env vars are set? */
              --env_var;
              env_val = "not set";
            }

          fprintf(error_stream,
                  "%s: error: cannot set LC_ALL locale\n"
                  "%s: error: environment variable %s is %s\n"
                  "%s: error: please check that your locale name is correct\n",
                  progname, progname, *env_var, env_val, progname);
        }
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

  /* Create a pool for use by the UTF-8 routines.  It will be cleaned
     up by APR at exit time. */
  pool = svn_pool_create (NULL);
  svn_utf_initialize (pool);

#ifdef ENABLE_NLS
#ifdef WIN32
  {
    WCHAR ucs2_path[MAX_PATH];
    char* utf8_path;
    const char* internal_path;
    apr_pool_t* pool;
    apr_status_t apr_err;
    apr_size_t inwords, outbytes, outlength;
    
    apr_pool_create (&pool, 0);
    /* get exe name - our locale info will be in '../share/locale' */
    inwords = GetModuleFileNameW (0, ucs2_path,
                                  sizeof (ucs2_path) / sizeof(ucs2_path[0]));
    if (! inwords)
      {
        /* We must be on a Win9x machine, so attempt to get an ANSI path,
           and convert it to Unicode. */
        CHAR ansi_path[MAX_PATH];

        if (! GetModuleFileNameA (0, ansi_path, sizeof (ansi_path)))
          goto utf8_error;

        inwords = 
          MultiByteToWideChar (CP_ACP, 0, ansi_path, -1, ucs2_path,
                               sizeof (ucs2_path) / sizeof (ucs2_path[0]));
        if (! inwords)
          goto utf8_error;
      }

    outbytes = outlength = 3 * (inwords + 1);
    utf8_path = apr_palloc (pool, outlength);
    apr_err = apr_conv_ucs2_to_utf8 (ucs2_path, &inwords,
                                     utf8_path, &outbytes);
    if (!apr_err && (inwords > 0 || outbytes == 0))
      apr_err = APR_INCOMPLETE;
    if (apr_err)
      goto utf8_error;

    utf8_path[outlength - outbytes] = '\0';
    internal_path = svn_path_internal_style (utf8_path, pool);
    /* get base path name */
    internal_path = svn_path_dirname (internal_path, pool);
    internal_path = svn_path_join (internal_path, SVN_LOCALE_RELATIVE_PATH,
                                   pool);
    bindtextdomain (PACKAGE_NAME, internal_path);    
    apr_pool_destroy (pool);
  }
#else
  bindtextdomain(PACKAGE_NAME, SVN_LOCALE_DIR);
#ifdef HAVE_BIND_TEXTDOMAIN_CODESET
  bind_textdomain_codeset(PACKAGE_NAME, "UTF-8");
#endif
#endif
  textdomain(PACKAGE_NAME);
#endif

  return EXIT_SUCCESS;

 utf8_error:
  if (error_stream)
    fprintf (error_stream, "Can't convert module path to UTF-8");
  return EXIT_FAILURE;
}


svn_error_t *
svn_cmdline_cstring_from_utf8 (const char **dest,
                               const char *src,
                               apr_pool_t *pool)
{
  if (output_encoding == NULL)
    return svn_utf_cstring_from_utf8(dest, src, pool);
  else
    return svn_utf_cstring_from_utf8_ex(dest, src, output_encoding,
                                        SVN_UTF_UTOCON_XLATE_HANDLE, pool);
}


const char *
svn_cmdline_cstring_from_utf8_fuzzy (const char *src,
                                     apr_pool_t *pool)
{
  return svn_utf__cstring_from_utf8_fuzzy (src, pool,
                                           svn_cmdline_cstring_from_utf8);
}


svn_error_t *
svn_cmdline_cstring_to_utf8 (const char **dest,
                             const char *src,
                             apr_pool_t *pool)
{
  if (input_encoding == NULL)
    return svn_utf_cstring_to_utf8(dest, src, pool);
  else
    return svn_utf_cstring_to_utf8_ex(dest, src, input_encoding,
                                      SVN_UTF_CONTOU_XLATE_HANDLE, pool);
}


svn_error_t *
svn_cmdline_path_local_style_from_utf8 (const char **dest,
                                        const char *src,
                                        apr_pool_t *pool)
{
  return svn_cmdline_cstring_from_utf8 (dest,
                                        svn_path_local_style (src, pool),
                                        pool);
}

svn_error_t *
svn_cmdline_printf (apr_pool_t *pool, const char *fmt, ...)
{
  const char *message;
  va_list ap;

  /* A note about encoding issues:
   * APR uses the execution character set, but here we give it UTF-8 strings,
   * both the fmt argument and any other string arguments.  Since apr_pvsprintf
   * only cares about and produces ASCII characters, this works under the
   * assumption that all supported platforms use an execution character set
   * with ASCII as a subset.
   */

  va_start (ap, fmt);
  message = apr_pvsprintf (pool, fmt, ap);
  va_end (ap);

  return svn_cmdline_fputs(message, stdout, pool);
}

svn_error_t *
svn_cmdline_fprintf (FILE *stream, apr_pool_t *pool, const char *fmt, ...)
{
  const char *message;
  va_list ap;

  /* See svn_cmdline_printf () for a note about character encoding issues. */

  va_start (ap, fmt);
  message = apr_pvsprintf (pool, fmt, ap);
  va_end (ap);

  return svn_cmdline_fputs(message, stream, pool);
}

svn_error_t *
svn_cmdline_fputs (const char *string, FILE* stream, apr_pool_t *pool)
{
  svn_error_t *err;
  const char *out;

  err = svn_cmdline_cstring_from_utf8 (&out, string, pool);

  if (err)
    {
      svn_error_clear (err);
      out = svn_cmdline_cstring_from_utf8_fuzzy (string, pool);
    }

  /* On POSIX systems, errno will be set on an error in fputs, but this might
     not be the case on other platforms.  We reset errno and only
     use it if it was set by the below fputs call.  Else, we just return
     a generic error. */
  errno = 0;

  if (fputs (out, stream) == EOF)
    {
      if (errno)
        return svn_error_wrap_apr (errno, _("Write error"));
      else
        return svn_error_create
          (SVN_ERR_IO_WRITE_ERROR, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cmdline_fflush (FILE *stream)
{
  /* See comment in svn_cmdline_fputs about use of errno and stdio. */
  errno = 0;
  if (fflush (stream) == EOF)
    {
      if (errno)
        return svn_error_wrap_apr (errno, _("Write error"));
      else
        return svn_error_create (SVN_ERR_IO_WRITE_ERROR, NULL, NULL);
    }

  return SVN_NO_ERROR;
}
