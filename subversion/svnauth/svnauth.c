/*
 * svnauth.c:  Subversion auth creds cache administration tool
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/*** Includes. ***/

#include <apr_general.h>
#include <apr_getopt.h>
#include <apr_fnmatch.h>
#include <apr_tables.h>

#include "svn_private_config.h"

#ifdef SVN_HAVE_SERF
#include <serf.h>
#endif

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_opt.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_utf.h"
#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_auth.h"
#include "svn_sorts.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_token.h"

/* Baton for passing option/argument state to a subcommand function. */
struct svnauth_opt_state
{
  const char *config_dir;                           /* --config-dir */
  svn_boolean_t version;                            /* --version */
  svn_boolean_t help;                               /* --help */
  svn_boolean_t show_passwords;                     /* --show-passwords */
};

typedef enum svnauth__longopt_t {
  opt_config_dir = SVN_OPT_FIRST_LONGOPT_ID,
  opt_show_passwords,
  opt_version
} svnauth__longopt_t;

/** Subcommands. **/
static svn_opt_subcommand_t
  subcommand_help,
  subcommand_list,
  subcommand_delete;

/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc2_t cmd_table[] =
{
  {"help", subcommand_help, {"?", "h"}, N_
   ("usage: svnauth help [SUBCOMMAND...]\n\n"
    "Describe the usage of this program or its subcommands.\n"),
   {0} },

  {"list", subcommand_list, {0}, N_
   ("usage: svnauth list [PATTERN ...]\n"
    "\n"
    "  List cached authentication credentials.\n"
    "\n"
    "  If PATTERN is specified, only list credentials with attributes matching\n"
    "  the pattern. All attributes except passwords can be matched. If more than\n"
    "  one pattern is specified credentials are shown if their attributes match\n"
    "  all patterns. Patterns are matched case-sensitively and may contain\n"
    "  glob wildcards:\n"
    "    ?      matches any single character\n"
    "    *      matches a sequence of arbitrary characters\n"
    "    [abc]  matches any of the characters listed inside the brackets\n"
    "  Note that wildcards will usually need to be quoted or escaped on the\n"
    "  command line because many command shells will interfere by trying to\n"
    "  expand them.\n"
    "\n"
    "  If no pattern is specified, all cached credentials are shown.\n"),
   {opt_config_dir, opt_show_passwords} },

  {"delete", subcommand_delete, {"del", "remove", "rm"}, N_
   ("usage: svnauth delete PATTERN ...\n"
    "\n"
    "  Delete cached authentication credentials matching a pattern.\n"
    "\n"
    "  All credential attributes except passwords can be matched. If more than \n"
    "  one pattern is specified credentials are deleted only if their attributes\n"
    "  match all patterns. Patterns are matched case-sensitively and may contain\n"
    "  glob wildcards:\n"
    "    ?      matches any single character\n"
    "    *      matches a sequence of arbitrary characters\n"
    "    [abc]  matches any of the characters listed inside the brackets\n"
    "  Note that wildcards will usually need to be quoted or escaped on the\n"
    "  command line because many command shells will interfere by trying to\n"
    "  expand them.\n"),
   {opt_config_dir} },

  {NULL}
};

/* Option codes and descriptions.
 *
 * The entire list must be terminated with an entry of nulls.
 */
static const apr_getopt_option_t options_table[] =
  {
    {"help",          'h', 0, N_("show help on a subcommand")},

    {"config-dir",    opt_config_dir, 1,
                      N_("use auth cache in config directory ARG")},

    {"show-passwords", opt_show_passwords, 0, N_("show cached passwords")},

    {"version",       opt_version, 0, N_("show program version information")},

    {NULL}
  };

/* Parse the remaining command-line arguments from OS, returning them
   in a new array *ARGS (allocated from POOL) and optionally verifying
   that we got the expected number thereof.  If MIN_EXPECTED is not
   negative, return an error if the function would return fewer than
   MIN_EXPECTED arguments.  If MAX_EXPECTED is not negative, return an
   error if the function would return more than MAX_EXPECTED
   arguments.

   As a special case, when MIN_EXPECTED and MAX_EXPECTED are both 0,
   allow ARGS to be NULL.  */
static svn_error_t *
parse_args(apr_array_header_t **args,
           apr_getopt_t *os,
           int min_expected,
           int max_expected,
           apr_pool_t *pool)
{
  int num_args = os ? (os->argc - os->ind) : 0;

  if (min_expected || max_expected)
    SVN_ERR_ASSERT(args);

  if ((min_expected >= 0) && (num_args < min_expected))
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if ((max_expected >= 0) && (num_args > max_expected))
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                            _("Too many arguments provided"));
  if (args)
    {
      *args = apr_array_make(pool, num_args, sizeof(const char *));

      if (num_args)
        while (os->ind < os->argc)
          APR_ARRAY_PUSH(*args, const char *) =
            apr_pstrdup(pool, os->argv[os->ind++]);
    }

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_help(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnauth_opt_state *opt_state = baton;
  const char *header =
    _("general usage: svnauth SUBCOMMAND [ARGS & OPTIONS ...]\n"
      "Subversion authentication credentials management tool.\n"
      "Type 'svnauth help <subcommand>' for help on a specific subcommand.\n"
      "Type 'svnauth --version' to see the program version and available\n"
      "authentication credential caches.\n"
      "\n"
      "Available subcommands:\n");
  const char *footer = NULL;

  if (opt_state && opt_state->version)
    {
      const char *config_path;

      SVN_ERR(svn_config_get_user_config_path(&config_path,
                                              opt_state->config_dir, NULL,
                                              pool));
      footer = _("Available authentication credential caches:\n");

      /*
       * ### There is no API to query available providers at run time.
       */
#if (defined(WIN32) && !defined(__MINGW32__))
      footer = apr_psprintf(pool, _("%s  Wincrypt cache in %s\n"),
                            footer, svn_dirent_local_style(config_path, pool));
#elif !defined(SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE)
      footer = apr_psprintf(pool, _("%s  Plaintext cache in %s\n"),
                            footer, svn_dirent_local_style(config_path, pool));
#endif
#ifdef SVN_HAVE_GNOME_KEYRING
      footer = apr_pstrcat(pool, footer, "  Gnome Keyring\n", NULL);
#endif
#ifdef SVN_HAVE_GPG_AGENT
      footer = apr_pstrcat(pool, footer, "  GPG-Agent\n", NULL);
#endif
#ifdef SVN_HAVE_KEYCHAIN_SERVICES
      footer = apr_pstrcat(pool, footer, "  Mac OS X Keychain\n", NULL);
#endif
#ifdef SVN_HAVE_KWALLET
      footer = apr_pstrcat(pool, footer, "  KWallet (KDE)\n", NULL);
#endif
    }

  SVN_ERR(svn_opt_print_help4(os, "svnauth",
                              opt_state ? opt_state->version : FALSE,
                              FALSE, FALSE, footer,
                              header, cmd_table, options_table, NULL, NULL,
                              pool));

  return SVN_NO_ERROR;
}

/* The separator between credentials . */
#define SEP_STRING \
  "------------------------------------------------------------------------\n"

#ifdef SVN_HAVE_SERF
/* Because APR hash order is unstable we use a token map of keys
 * to ensure values are always presented in the same order. */
typedef enum svnauth__cert_info_keys {
  svnauth__cert_key_cn,
  svnauth__cert_key_e,
  svnauth__cert_key_ou,
  svnauth__cert_key_o,
  svnauth__cert_key_l,
  svnauth__cert_key_st,
  svnauth__cert_key_c,
  svnauth__cert_key_sha1,
  svnauth__cert_key_not_before,
  svnauth__cert_key_not_after,
} svnauth__cert_info_keys;

static svn_token_map_t cert_info_key_map[] = {
    { "CN",         svnauth__cert_key_cn },
    { "E",          svnauth__cert_key_e },
    { "OU",         svnauth__cert_key_ou },
    { "O",          svnauth__cert_key_o },
    { "L",          svnauth__cert_key_l },
    { "ST",         svnauth__cert_key_st },
    { "C",          svnauth__cert_key_c },
    { "sha1",       svnauth__cert_key_sha1 },
    { "notBefore",  svnauth__cert_key_not_before },
    { "notAfter",   svnauth__cert_key_not_after }
};

/* Show information stored in CERT_INFO.
 * Assume all hash table keys occur in the above key map. */
static svn_error_t *
show_cert_info(apr_hash_t *cert_info,
               apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < sizeof(cert_info_key_map) / sizeof(svn_token_map_t); i++)
    {
      const char *key = cert_info_key_map[i].str;
      const char *value = svn_hash_gets(cert_info, key);

      if (value)
        {
          int token;

          token = svn_token__from_word(cert_info_key_map, key);
          switch (token)
            {
              case svnauth__cert_key_cn:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Common Name: %s\n"), value));
                break;
              case svnauth__cert_key_e:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Email Address: %s\n"), value));
                break;
              case svnauth__cert_key_o:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Organization Name: %s\n"),
                                           value));
                break;
              case svnauth__cert_key_ou:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Organizational Unit: %s\n"),
                                           value));
                break;
              case svnauth__cert_key_l:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Locality: %s\n"), value));
                break;
              case svnauth__cert_key_st:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  State or Province: %s\n"),
                                           value));
                break;
              case svnauth__cert_key_c:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Country: %s\n"), value));
                break;
              case svnauth__cert_key_sha1:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  SHA1 Fingerprint: %s\n"),
                                           value));
                break;
              case svnauth__cert_key_not_before:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Valid as of: %s\n"), value));
                break;
              case svnauth__cert_key_not_after:
                SVN_ERR(svn_cmdline_printf(scratch_pool,
                                           _("  Valid until: %s\n"), value));
                break;
              case SVN_TOKEN_UNKNOWN:
              default:
#ifdef SVN_DEBUG
                SVN_ERR_MALFUNCTION();
#endif
                break;
            }
        }
    }

  return SVN_NO_ERROR;
}

#define MAX_CERT_LINE_LEN 78

/* Break ASCII_CERT into lines of at most MAX_CERT_LINE_LEN characters.
 * Otherwise, OpenSSL won't parse it due to the way it is invoked by serf. */
static const char *
split_ascii_cert(const char *ascii_cert,
                 apr_pool_t *result_pool)
{
  apr_array_header_t *lines;
  int i;
  apr_size_t cert_len, nlines;
  const char *p;
  svn_stringbuf_t *line;

  p = ascii_cert;
  cert_len = strlen(ascii_cert);
  nlines = cert_len / MAX_CERT_LINE_LEN;
  lines = apr_array_make(result_pool, 22, sizeof(const char *));
  for (i = 0; i < nlines; i++)
    {
      line = svn_stringbuf_create_ensure(MAX_CERT_LINE_LEN, result_pool);
      svn_stringbuf_appendbytes(line, p, MAX_CERT_LINE_LEN);
      p += MAX_CERT_LINE_LEN;
      APR_ARRAY_PUSH(lines, const char *) = line->data;
    }
  if (*p)
    {
      line = svn_stringbuf_create_ensure(MAX_CERT_LINE_LEN, result_pool);
      while (*p)
        svn_stringbuf_appendbyte(line, *p++);
      APR_ARRAY_PUSH(lines, const char *) = line->data;
    }

  return svn_cstring_join(lines, "\n", result_pool);
}
#endif /* SVN_HAVE_SERF */

#ifdef SVN_HAVE_SERF
static svn_error_t *
load_cert(serf_ssl_certificate_t **cert,
          const char *ascii_cert,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  apr_file_t *pem_file;
  const char *pem_path;
  const char *pem;
  apr_size_t pem_len;
  apr_size_t written;
  apr_status_t status;

  SVN_ERR(svn_io_open_unique_file3(&pem_file, &pem_path, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));
  pem = apr_psprintf(scratch_pool, "%s%s%s",
                     "-----BEGIN CERTIFICATE-----\n",
                     split_ascii_cert(ascii_cert, scratch_pool),
                     "-----END CERTIFICATE-----\n");
  pem_len = strlen(pem);
  SVN_ERR(svn_io_file_write_full(pem_file, pem, pem_len, &written,
                                 scratch_pool));
  if (written != pem_len)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("Base64-encoded certificate: %s\n"),
                                 ascii_cert));
      return SVN_NO_ERROR;
    }
  SVN_ERR(svn_io_file_flush_to_disk(pem_file, scratch_pool));

  status = serf_ssl_load_cert_file(cert, pem_path, result_pool);
  if (status)
    {
      svn_error_t *err;
      
      err = svn_error_wrap_apr(status, _("serf error: %s"),
                               serf_error_string(status));
      svn_handle_warning2(stderr, err, "svnauth: ");
      svn_error_clear(err);

      *cert = NULL;
      return SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}
#endif

/* ### from libsvn_subr/ssl_server_trust_providers.c */
#define AUTHN_ASCII_CERT_KEY            "ascii_cert"
#define AUTHN_FAILURES_KEY              "failures"

/* Display the base64-encoded DER certificate ASCII_CERT. */
static svn_error_t *
show_ascii_cert(const char *ascii_cert,
                apr_pool_t *scratch_pool)
{
#ifdef SVN_HAVE_SERF
  serf_ssl_certificate_t *cert;
  apr_hash_t *cert_info;

  SVN_ERR(load_cert(&cert, ascii_cert, scratch_pool, scratch_pool));

  if (cert == NULL)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("Base64-encoded certificate: %s\n"),
                                 ascii_cert));
      return SVN_NO_ERROR;
    }

  cert_info = serf_ssl_cert_issuer(cert, scratch_pool);
  if (cert_info && apr_hash_count(cert_info) > 0)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, _("Certificate issuer:\n")));
      SVN_ERR(show_cert_info(cert_info, scratch_pool));
    }

  cert_info = serf_ssl_cert_subject(cert, scratch_pool);
  if (cert_info && apr_hash_count(cert_info) > 0)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, _("Certificate subject:\n")));
      SVN_ERR(show_cert_info(cert_info, scratch_pool));
    }

  cert_info = serf_ssl_cert_certificate(cert, scratch_pool);
  if (cert_info && apr_hash_count(cert_info) > 0)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, _("Certificate validity:\n")));
      SVN_ERR(show_cert_info(cert_info, scratch_pool));
    }
#else
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("Base64-encoded certificate: %s\n"),
                             ascii_cert));
#endif /* SVN_HAVE_SERF */

  return SVN_NO_ERROR;
}
                
static svn_error_t *
show_cert_failures(const char *failure_string,
                   apr_pool_t *scratch_pool)
{
  unsigned int failures;

  SVN_ERR(svn_cstring_atoui(&failures, failure_string));

  if (0 == (failures & (SVN_AUTH_SSL_NOTYETVALID | SVN_AUTH_SSL_EXPIRED |
                        SVN_AUTH_SSL_CNMISMATCH | SVN_AUTH_SSL_UNKNOWNCA |
                        SVN_AUTH_SSL_OTHER)))
    return SVN_NO_ERROR;

  SVN_ERR(svn_cmdline_printf(
            scratch_pool, _("Automatic certificate validity check failed "
                            "because:\n")));

  if (failures & SVN_AUTH_SSL_NOTYETVALID)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate is not yet valid.\n")));

  if (failures & SVN_AUTH_SSL_EXPIRED)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate has expired.\n")));

  if (failures & SVN_AUTH_SSL_CNMISMATCH)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate's Common Name (hostname) "
                              "does not match the remote hostname.\n")));

  if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  The certificate issuer is unknown.\n")));

  if (failures & SVN_AUTH_SSL_OTHER)
    SVN_ERR(svn_cmdline_printf(
              scratch_pool, _("  Unknown verification failure.\n")));

  return SVN_NO_ERROR;
}

/* ### from libsvn_subr/simple_providers.c */
#define AUTHN_USERNAME_KEY            "username"
#define AUTHN_PASSWORD_KEY            "password"
#define AUTHN_PASSTYPE_KEY            "passtype"

/* ### from libsvn_subr/ssl_client_cert_pw_providers.c */
#define AUTHN_PASSPHRASE_KEY            "passphrase"

struct walk_credentials_baton_t
{
  int matches;
  svn_boolean_t list;
  svn_boolean_t delete;
  svn_boolean_t show_passwords;
  apr_array_header_t *patterns;
};

static svn_boolean_t
match_pattern(const char *pattern, const char *value,
              apr_pool_t *scratch_pool)
{
  const char *p = apr_psprintf(scratch_pool, "*%s*", pattern);

  return (apr_fnmatch(p, value, 0) == APR_SUCCESS);
}

#ifdef SVN_HAVE_SERF
static svn_error_t *
match_cert_info(svn_boolean_t *match,
                const char *pattern,
                apr_hash_t *cert_info,
                apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool;

  *match = FALSE;
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < sizeof(cert_info_key_map) / sizeof(svn_token_map_t); i++)
    {
      const char *key = cert_info_key_map[i].str;
      const char *value = svn_hash_gets(cert_info, key);

      svn_pool_clear(iterpool);
      if (value)
        *match = match_pattern(pattern, value, iterpool);
      if (*match)
        break;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
#endif


static svn_error_t *
match_ascii_cert(svn_boolean_t *match,
                 const char *pattern,
                 const char *ascii_cert,
                 apr_pool_t *scratch_pool)
{
#ifdef SVN_HAVE_SERF
  serf_ssl_certificate_t *cert;
  apr_hash_t *cert_info;

  *match = FALSE;

  SVN_ERR(load_cert(&cert, ascii_cert, scratch_pool, scratch_pool));

  cert_info = serf_ssl_cert_issuer(cert, scratch_pool);
  if (cert_info && apr_hash_count(cert_info) > 0)
    {
      SVN_ERR(match_cert_info(match, pattern, cert_info, scratch_pool));
      if (*match)
        return SVN_NO_ERROR;
    }

  cert_info = serf_ssl_cert_subject(cert, scratch_pool);
  if (cert_info && apr_hash_count(cert_info) > 0)
    {
      SVN_ERR(match_cert_info(match, pattern, cert_info, scratch_pool));
      if (*match)
        return SVN_NO_ERROR;
    }

  cert_info = serf_ssl_cert_certificate(cert, scratch_pool);
  if (cert_info && apr_hash_count(cert_info) > 0)
    {
      SVN_ERR(match_cert_info(match, pattern, cert_info, scratch_pool));
      if (*match)
        return SVN_NO_ERROR;
    }
#else
  *match = FALSE;
#endif

  return SVN_NO_ERROR;
}

static svn_error_t *
match_credential(svn_boolean_t *match,
                 const char *cred_kind,
                 const char *realmstring,
                 apr_array_header_t *patterns,
                 apr_array_header_t *cred_items,
                 apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  *match = FALSE;

  for (i = 0; i < patterns->nelts; i++)
    {
      const char *pattern = APR_ARRAY_IDX(patterns, i, const char *);
      int j;

      *match = match_pattern(pattern, cred_kind, iterpool);
      if (!*match)
        *match = match_pattern(pattern, realmstring, iterpool);
      if (!*match)
        {
          svn_pool_clear(iterpool);
          for (j = 0; j < cred_items->nelts; j++)
            {
              svn_sort__item_t item;
              const char *key;
              svn_string_t *value;

              item = APR_ARRAY_IDX(cred_items, j, svn_sort__item_t);
              key = item.key;
              value = item.value;
              if (strcmp(key, AUTHN_PASSWORD_KEY) == 0 ||
                  strcmp(key, AUTHN_PASSPHRASE_KEY) == 0)
                continue; /* don't match secrets */
              else if (strcmp(key, AUTHN_ASCII_CERT_KEY) == 0)
                SVN_ERR(match_ascii_cert(match, pattern, value->data,
                                         iterpool));
              else
                *match = match_pattern(pattern, value->data, iterpool);

              if (*match)
                break;
            }
        }
      if (!*match)
        break;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
list_credential(const char *cred_kind,
                const char *realmstring,
                apr_array_header_t *cred_items,
                svn_boolean_t show_passwords,
                apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_cmdline_printf(scratch_pool, SEP_STRING));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("Credential kind: %s\n"), cred_kind));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("Authentication realm: %s\n"), realmstring));

  for (i = 0; i < cred_items->nelts; i++)
    {
      svn_sort__item_t item;
      const char *key;
      svn_string_t *value;
      
      svn_pool_clear(iterpool);
      item = APR_ARRAY_IDX(cred_items, i, svn_sort__item_t);
      key = item.key;
      value = item.value;
      if (strcmp(value->data, realmstring) == 0)
        continue; /* realm string was already shown above */
      else if (strcmp(key, AUTHN_PASSWORD_KEY) == 0)
        {
          if (show_passwords)
            SVN_ERR(svn_cmdline_printf(iterpool,
                                       _("Password: %s\n"), value->data));
          else
            SVN_ERR(svn_cmdline_printf(iterpool, _("Password: [not shown]\n")));
        }
      else if (strcmp(key, AUTHN_PASSPHRASE_KEY) == 0)
        {
          if (show_passwords)
            SVN_ERR(svn_cmdline_printf(iterpool,
                                       _("Passphrase: %s\n"), value->data));
          else
            SVN_ERR(svn_cmdline_printf(iterpool,
                                       _("Passphrase: [not shown]\n")));
        }
      else if (strcmp(key, AUTHN_PASSTYPE_KEY) == 0)
        SVN_ERR(svn_cmdline_printf(iterpool, _("Password cache: %s\n"),
                                   value->data));
      else if (strcmp(key, AUTHN_USERNAME_KEY) == 0)
        SVN_ERR(svn_cmdline_printf(iterpool, _("Username: %s\n"), value->data));
      else if (strcmp(key, AUTHN_ASCII_CERT_KEY) == 0)
        SVN_ERR(show_ascii_cert(value->data, iterpool));
      else if (strcmp(key, AUTHN_FAILURES_KEY) == 0)
        SVN_ERR(show_cert_failures(value->data, iterpool));
      else
        SVN_ERR(svn_cmdline_printf(iterpool, "%s: %s\n", key, value->data));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_cmdline_printf(scratch_pool, "\n"));
  return SVN_NO_ERROR;
}

/* This implements `svn_config_auth_walk_func_t` */
static svn_error_t *
walk_credentials(svn_boolean_t *delete_cred,
                 void *baton,
                 const char *cred_kind,
                 const char *realmstring,
                 apr_hash_t *cred_hash,
                 apr_pool_t *scratch_pool)
{
  struct walk_credentials_baton_t *b = baton;
  apr_array_header_t *sorted_cred_items;

  *delete_cred = FALSE;

  sorted_cred_items = svn_sort__hash(cred_hash,
                                     svn_sort_compare_items_lexically,
                                     scratch_pool);
  if (b->patterns->nelts > 0)
    {
      svn_boolean_t match;

      SVN_ERR(match_credential(&match, cred_kind, realmstring,
                               b->patterns, sorted_cred_items,
                               scratch_pool));
      if (!match)
        return SVN_NO_ERROR;
    }

  b->matches++;

  if (b->list)
    SVN_ERR(list_credential(cred_kind, realmstring, sorted_cred_items,
                            b->show_passwords, scratch_pool));
  if (b->delete)
    {
      *delete_cred = TRUE;
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("Deleting %s credential for realm '%s'\n"),
                                 cred_kind, realmstring));
    }

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_list(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnauth_opt_state *opt_state = baton;
  const char *config_path;
  struct walk_credentials_baton_t b;

  b.matches = 0;
  b.show_passwords = opt_state->show_passwords;
  b.list = TRUE;
  b.delete = FALSE;
  SVN_ERR(parse_args(&b.patterns, os, 0, -1, pool));

  SVN_ERR(svn_config_get_user_config_path(&config_path,
                                          opt_state->config_dir, NULL,
                                          pool));

  SVN_ERR(svn_config_walk_auth_data(config_path, walk_credentials, &b,
                                    pool));

  if (b.matches == 0)
    {
      if (b.patterns->nelts == 0)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("Credentials cache in '%s' is empty\n"),
                                   svn_dirent_local_style(config_path, pool)));
      else 
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, 0,
                                 _("Credentials cache in '%s' contains "
                                   "no matching credentials"),
                                 svn_dirent_local_style(config_path, pool));
    }
  else
    {
      if (b.patterns->nelts == 0)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("Credentials cache in '%s' contains %d "
                                     "credentials\n"),
                                   svn_dirent_local_style(config_path, pool),
                                   b.matches));
      else
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("Credentials cache in '%s' contains %d "
                                     "matching credentials\n"),
                                   svn_dirent_local_style(config_path, pool),
                                   b.matches));
    }
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_delete(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnauth_opt_state *opt_state = baton;
  const char *config_path;
  struct walk_credentials_baton_t b;

  b.matches = 0;
  b.show_passwords = opt_state->show_passwords;
  b.list = FALSE;
  b.delete = TRUE;
  SVN_ERR(parse_args(&b.patterns, os, 1, -1, pool));

  SVN_ERR(svn_config_get_user_config_path(&config_path,
                                          opt_state->config_dir, NULL,
                                          pool));

  SVN_ERR(svn_config_walk_auth_data(config_path, walk_credentials, &b, pool));

  if (b.matches == 0)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, 0,
                             _("Credentials cache in '%s' contains "
                               "no matching credentials"),
                             svn_dirent_local_style(config_path, pool));
  else
    SVN_ERR(svn_cmdline_printf(pool, _("Deleted %d matching credentials "
                               "from '%s'\n"), b.matches,
                               svn_dirent_local_style(config_path, pool)));

  return SVN_NO_ERROR;
}


/* Report and clear the error ERR, and return EXIT_FAILURE. */
#define EXIT_ERROR(err)                                                 \
  svn_cmdline_handle_exit_error(err, NULL, "svnauth: ")

/* A redefinition of the public SVN_INT_ERR macro, that suppresses the
 * error message if it is SVN_ERR_IO_PIPE_WRITE_ERROR, amd with the
 * program name 'svnauth' instead of 'svn'. */
#undef SVN_INT_ERR
#define SVN_INT_ERR(expr)                                        \
  do {                                                           \
    svn_error_t *svn_err__temp = (expr);                         \
    if (svn_err__temp)                                           \
      return EXIT_ERROR(svn_err__temp);                          \
  } while (0)


static int
sub_main(int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  struct svnauth_opt_state opt_state = { 0 };
  apr_getopt_t *os;

  if (argc <= 1)
    {
      SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
      return EXIT_FAILURE;
    }

  /* Parse options. */
  SVN_INT_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));
  os->interleave = 1;

  while (1)
    {
      int opt_id;
      const char *opt_arg;
      const char *utf8_opt_arg;
      apr_status_t apr_err;

      /* Parse the next option. */
      apr_err = apr_getopt_long(os, options_table, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
          return EXIT_FAILURE;
        }

      switch (opt_id) {
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case opt_config_dir:
        SVN_INT_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.config_dir = svn_dirent_internal_style(utf8_opt_arg, pool);
        break;
      case opt_show_passwords:
        opt_state.show_passwords = TRUE;
        break;
      case opt_version:
        opt_state.version = TRUE;
        break;
      default:
        {
          SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
          return EXIT_FAILURE;
        }
      }
    }

  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(cmd_table, "help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_state.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", subcommand_help, {0}, "",
                  {opt_version,  /* must accept its own option */
                   'q',  /* --quiet */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                        _("subcommand argument required\n")));
              SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
              return EXIT_FAILURE;
            }
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_opt_get_canonical_subcommand2(cmd_table, first_arg);
          if (subcommand == NULL)
            {
              const char *first_arg_utf8;
              SVN_INT_ERR(svn_utf_cstring_to_utf8(&first_arg_utf8,
                                                  first_arg, pool));
              svn_error_clear(
                svn_cmdline_fprintf(stderr, pool,
                                    _("Unknown subcommand: '%s'\n"),
                                    first_arg_utf8));
              SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
              return EXIT_FAILURE;
            }
        }
    }

  SVN_INT_ERR(svn_config_ensure(opt_state.config_dir, pool));

  /* Run the subcommand. */
  err = (*subcommand->cmd_func)(os, &opt_state, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(err,
                                     _("Try 'svnauth help' for more info"));
        }
      return EXIT_ERROR(err);
    }
  else
    {
      /* Ensure that everything is written to stdout, so the user will
         see any print errors. */
      err = svn_cmdline_fflush(stdout);
      if (err)
        {
          return EXIT_ERROR(err);
        }
      return EXIT_SUCCESS;
    }

  return EXIT_SUCCESS;
}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code;

  /* Initialize the app. */
  if (svn_cmdline_init("svnauth", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  exit_code = sub_main(argc, argv, pool);

  svn_pool_destroy(pool);
  return exit_code;
}
