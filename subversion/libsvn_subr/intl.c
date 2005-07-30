/* intl.c: Internationalization and localization for Subversion.
 *
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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

#include <apr_errno.h>
#include <apr_mmap.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <assert.h>
#include <apr_hash.h>
#include <ctype.h>
#include <stdlib.h>

#include <locale.h>  /* for dgettext */
#include <libintl.h> /* for dgettext */

#include "svn_intl.h"
#include "svn_error.h"
#include "svn_private_config.h" /* for SVN_LOCALE_DIR */



/* gettext msgid used to request the current locale using gettext. */
#define SVN_CLIENT_MESSAGE_LOCALE N_("Client requests untranslated messages")

static apr_hash_t *cache;
static apr_pool_t *private_pool = NULL;
#if APR_HAS_THREADS
static apr_thread_mutex_t *cache_lock;
#endif

#if !APR_HAS_MMAP
#error This needs mmap support
#endif

typedef struct
{
  char domain[32];
  char locale[16];
} message_table_key_t;

/* Null out a few statics to allow for the possibility of graceful
   re-initialization. */
static apr_status_t
svn_intl_terminate (void *ignored)
{
  cache = NULL;
  /* ### Does an apr_thread_mutex_t need to be explicitly released? */
  cache_lock = NULL;
  private_pool = NULL;
  return APR_SUCCESS;
}

svn_error_t *
svn_intl_initialize (apr_pool_t *parent_pool)
{
  /* ### Fix race condition in initialization of private_pool */
  if (private_pool == NULL)
    {
      apr_status_t st = apr_pool_create (&private_pool, parent_pool);
      if (st == APR_SUCCESS)
        {
           apr_pool_cleanup_register(private_pool, NULL,
                                     svn_intl_terminate, NULL);

          cache = apr_hash_make (private_pool);
#if APR_HAS_THREADS
          st = apr_thread_mutex_create (&cache_lock, APR_THREAD_MUTEX_DEFAULT,
                                        private_pool);
          if (st != APR_SUCCESS)
            apr_pool_destroy (private_pool);
#endif
        }
      if (st != APR_SUCCESS)
        {
          return svn_error_create(st, NULL, "Failed to initialize "
                                  "localiztion module");
        }

      /* C programs default to the "C" locale.  However, because SVN
         is supposed to be I18N-aware, it inherits the default locale
         of its environment. */
      if (setlocale(LC_ALL, "") == NULL)
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

          /* ### FIXME: Add meaningful error code. */
          return svn_error_createf(-1, NULL, "cannot set LC_ALL locale\n"
                                   "environment variable '%s' is '%s'\n"
                                   "please check that your locale name "
                                   "is correct", *env_var, env_val);
        }

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
          inwords = GetModuleFileNameW (0, ucs2_path, sizeof (ucs2_path) /
                                        sizeof (ucs2_path[0]));
          if (! inwords)
            {
              /* We must be on a Win9x machine, so attempt to get an ANSI path,
                 and convert it to Unicode. */
              CHAR ansi_path[MAX_PATH];

              if (! GetModuleFileNameA (0, ansi_path, sizeof (ansi_path)))
                goto utf8_error;

              inwords = 
                MultiByteToWideChar (CP_ACP, 0, ansi_path, -1, ucs2_path,
                                     sizeof (ucs2_path) /
                                     sizeof (ucs2_path[0]));
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
            {
            utf8_error:
              if (error_stream)
                fprintf (error_stream, "Can't convert module path to UTF-8");
              return EXIT_FAILURE;
            }

          utf8_path[outlength - outbytes] = '\0';
          internal_path = svn_path_internal_style (utf8_path, pool);
          /* get base path name */
          internal_path = svn_path_dirname (internal_path, pool);
          internal_path = svn_path_join (internal_path,
                                         SVN_LOCALE_RELATIVE_PATH,
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
    }

  return SVN_NO_ERROR;
}

/* The value returned by svn_intl_get_locale_prefs() when no locale
   preferences are found. */
static const char *NO_LOCALE_PREFS[] = { NULL };

char **
svn_intl_get_locale_prefs (void *context, apr_pool_t *pool)
{
  char **prefs = NO_LOCALE_PREFS;
  if (context != NULL)
    {
      /* Look for context-specific locale preferences. */
      /* ### TODO */
    }

  /* With no contextual locale, fall back to the system locale. */
  if (prefs == NO_LOCALE_PREFS)
    {
      /* xgettext: Set this to the ISO-639 two-letter language code
         and -- optionally -- the ISO-3166 country code for this .po
         file (e.g. en-US, sv-SE, etc.). */
      const char *locale = dgettext(PACKAGE_NAME, SVN_CLIENT_MESSAGE_LOCALE);

      /* The bundle could be missing the "translation", or we could be
         missing a bundle for the locale. */
      if (apr_strnatcmp(locale, SVN_CLIENT_MESSAGE_LOCALE) != 0)
        {
          prefs = apr_pcalloc(pool, 2 * sizeof(char *));
          prefs[0] = locale;
        }
    }
  return prefs;
}

void
svn_intl_set_locale_prefs (void *context, char **locale_prefs)
{
  /* ### TODO: Save locale information to thread-local storage. */


  /* ### LATER: Should we save the locale information to the context
     ### instead?  For instance, if context was an apr_pool_t, we
     ### could use its userdata field.

     ((apr_pool_t *) context)->userdata
  */
}

typedef struct
{
  apr_uint32_t len;
  apr_uint32_t offset;
} message_entry_t;

typedef struct
{
  apr_mmap_t *map;
  apr_uint32_t num_strings;
  message_entry_t *original;
  message_entry_t *translated;
} message_table_t;

static apr_status_t
message_table_open (message_table_t **mt, const char *domain,
                    const char *locale, apr_pool_t * pool)
{
  char fn[200];
  const char *header;
  apr_finfo_t finfo;
  apr_file_t *file;
  const void *mo;
  apr_uint32_t *mem;
  apr_status_t st;

  /* ### The .mo format is not standard across gettext implementations
     ### -- it's specific to GNU gettext.  A more portable
     ### implementation is necessary! */

  /* Follows macro usage from libsvn_subr/cmdline.c:svn_cmdline_init() */
  apr_snprintf (fn, sizeof (fn),
                SVN_LOCALE_DIR "/%s/LC_MESSAGES/%s.mo",
                locale, domain);
  *mt = (message_table_t *) apr_palloc (pool, sizeof (**mt));
#if 0
  printf("Looking for localization bundle '%s'...\n", fn);
#endif
  st = apr_stat (&finfo, fn, APR_FINFO_SIZE, pool);
  if (st)
    return st;
  st =
    apr_file_open (&file, fn, APR_READ | APR_BINARY | APR_SHARELOCK,
		   APR_OS_DEFAULT, pool);
  if (st)
    return st;
  st = apr_file_lock (file, APR_FLOCK_SHARED);
  if (st)
    return st;
  st =
    apr_mmap_create (&((*mt)->map), file, 0, finfo.size, APR_MMAP_READ, pool);
  if (st)
    return st;
  apr_mmap_offset ((void **) &mo, (*mt)->map, 0);
  mem = (apr_uint32_t *) mo;
  /* ### What does this big numeric value represent? */
  assert (mem[0] == 0x950412de && mem[1] == 0);
  (*mt)->num_strings = mem[2];

  apr_mmap_offset ((void **) &((*mt)->original), (*mt)->map, mem[3]);
  apr_mmap_offset ((void **) &((*mt)->translated), (*mt)->map, mem[4]);

  header = (char *) (mem + (*mt)->translated[0].offset);

#if 0
  printf ("%d strings\n", (*mt)->num_strings);

  for (i = 0; i < (*mt)->num_strings; i++)
    {
      printf ("%.*s -> %.*s\n-----------------\n", (*mt)->original[i].len,
	      (char *) (mo + (*mt)->original[i].offset),
	      (*mt)->translated[i].len,
	      (char *) (mo + (*mt)->translated[i].offset));
    }
#endif
  return 0;
}

static const char *
message_table_gettext (message_table_t *mt, const char *msgid)
{
  apr_uint32_t a = 0;
  apr_uint32_t b = mt->num_strings - 1;

  while (1)
    {
      void *addr;
      const char *str;
      int i;
      apr_uint32_t m;

      m = (a + b) / 2;
      apr_mmap_offset (&addr, mt->map, mt->original[m].offset);
      str = addr;
      i = apr_strnatcmp (msgid, str);
      if (i == 0)
        {
          apr_mmap_offset (&addr, mt->map, mt->translated[m].offset);
          return addr;
        }
      if (a == b)
        return msgid;

      if (i < 0)
        b = m - 1;
      else
        a = m + 1;
    }
}

/* ### Especially with the looming possibility of httpd moving to a
   ### model where a single HTTP request might one day be serviceable
   ### by multiple threads, the black magic of thread-local storage is
   ### frowned upon.  Instead, it's been suggested that a context
   ### parameter (probably apr_pool_t.userdata) be used to indicate
   ### language preferences, and its contents used to differentiate
   ### between per-client session preferences (server-side) and global
   ### user preferences (client-side). */
const char *
svn_intl_dgettext (const char *domain, const char *msgid)
{
  const char *locale;
  /* See http://apr.apache.org/docs/apr/group__apr__thread__proc.html */
  apr_threadkey_t *key;
  /* ### use sub-pool? */
  apr_threadkey_private_create (&key, NULL /* ### */, private_pool);
  apr_threadkey_private_get (&locale, key);
  apr_threadkey_private_delete (key);

  if (locale == NULL)
    {
      /* ### A shortcut to avoid dealing with locale-related env vars,
         ### GetThreadLocale(), etc.  Ideally, we'd used only one
         ### gettext-like implementation which suites our purposes. */
      return dgettext(domain, msgid);
    }
  else
    {
      return svn_intl_dlgettext(domain, locale, msgid);
    }
}

const char *
svn_intl_dlgettext (const char *domain, const char *locale, const char *msgid)
{
  message_table_t *msg_tbl;
  message_table_key_t key;
  apr_status_t st;

  memset (&key, 0, sizeof (key));
  apr_cpystrn (key.domain, domain, sizeof (key.domain));
  apr_cpystrn (key.locale, locale, sizeof (key.locale));
#if APR_HAS_THREADS
  st = apr_thread_mutex_lock (cache_lock);
  if (st != APR_SUCCESS)
    return msgid;
#endif
  msg_tbl = (message_table_t *) apr_hash_get (cache, &key, sizeof (key));
  if (!msg_tbl)
    {
      st = message_table_open (&msg_tbl, domain, locale, private_pool);
      if (st == APR_SUCCESS)
        {
          apr_hash_set (cache, apr_pmemdup (private_pool, &key, sizeof (key)),
                        sizeof (key), msg_tbl);
        }
      else
        msg_tbl = NULL;
    }
#if APR_HAS_THREADS
  apr_thread_mutex_unlock (cache_lock);
#endif
  if (!msg_tbl)
    return msgid;
  return message_table_gettext (msg_tbl, msgid);
}
