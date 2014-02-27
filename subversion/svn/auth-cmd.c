/*
 * auth-cmd.c:  Subversion auth creds cache administration
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

/* Don't enable SSL cert pretty-printing on Windows yet because of a
   known issue in serf. See serf's r2314. Once this fix is part of a
   serf release, we'll want a SERF_VERSION_AT_LEAST() check here. */
#ifndef WIN32
#define SVN_AUTH_PRETTY_PRINT_SSL_CERTS
#endif
#else /* !SVN_HAVE_SERF */
#undef SVN_AUTH_PRETTY_PRINT_SSL_CERTS
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
#include "private/svn_sorts_private.h"

#include "cl.h"

/* The separator between credentials . */
#define SEP_STRING \
  "------------------------------------------------------------------------\n"

#ifdef SVN_AUTH_PRETTY_PRINT_SSL_CERTS
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
      svn_handle_warning2(stderr, err, "svn: ");
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
#ifdef SVN_AUTH_PRETTY_PRINT_SSL_CERTS
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
#endif /* SVN_AUTH_PRETTY_PRINT_SSL_CERTS */

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

#ifdef SVN_AUTH_PRETTY_PRINT_SSL_CERTS
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
#ifdef SVN_AUTH_PRETTY_PRINT_SSL_CERTS
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
svn_error_t *
svn_cl__auth(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *config_path;
  struct walk_credentials_baton_t b;

  b.matches = 0;
  b.show_passwords = opt_state->show_passwords;
  b.list = !opt_state->remove;
  b.delete = opt_state->remove;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&b.patterns, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      pool));

  SVN_ERR(svn_config_get_user_config_path(&config_path,
                                          opt_state->config_dir, NULL,
                                          pool));

  if (b.delete && b.patterns->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  SVN_ERR(svn_config_walk_auth_data(config_path, walk_credentials, &b, pool));

  if (b.list)
    {
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
                      _("Credentials cache in '%s' contains %d credentials\n"),
                      svn_dirent_local_style(config_path, pool), b.matches));
          else
            SVN_ERR(svn_cmdline_printf(pool,
                      _("Credentials cache in '%s' contains %d matching "
                        "credentials\n"),
                      svn_dirent_local_style(config_path, pool), b.matches));
        }

    }

  if (b.delete)
    {
      if (b.matches == 0)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, 0,
                                 _("Credentials cache in '%s' contains "
                                   "no matching credentials"),
                                 svn_dirent_local_style(config_path, pool));
      else
        SVN_ERR(svn_cmdline_printf(pool, _("Deleted %d matching credentials "
                                   "from '%s'\n"), b.matches,
                                   svn_dirent_local_style(config_path, pool)));
    }

  return SVN_NO_ERROR;
}
