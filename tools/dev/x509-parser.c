/* x509-parser.c -- print human readable info from an X.509 certificate
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

#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "svn_base64.h"
#include "svn_x509.h"
#include "svn_time.h"

#include "svn_private_config.h"

#define PEM_BEGIN_CERT "-----BEGIN CERTIFICATE-----"
#define PEM_END_CERT "-----END CERTIFICATE-----"

static svn_error_t *
show_cert(const svn_string_t *der_cert, apr_pool_t *scratch_pool)
{
  svn_x509_certinfo_t *certinfo;
  const apr_array_header_t *hostnames;

  SVN_ERR(svn_x509_parse_cert(&certinfo, der_cert->data, der_cert->len,
                            scratch_pool, scratch_pool));

  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Subject: %s\n"),
                             svn_x509_certinfo_get_subject(certinfo, scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Valid from: %s\n"),
                             svn_time_to_human_cstring(
                                 svn_x509_certinfo_get_valid_from(certinfo),
                                 scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Valid until: %s\n"),
                             svn_time_to_human_cstring(
                                 svn_x509_certinfo_get_valid_to(certinfo),
                                 scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Issuer: %s\n"),
                             svn_x509_certinfo_get_issuer(certinfo, scratch_pool)));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("Fingerprint: %s\n"),
                             svn_checksum_to_cstring_display(
                                 svn_x509_certinfo_get_digest(certinfo),
                                 scratch_pool)));

  hostnames = svn_x509_certinfo_get_hostnames(certinfo);
  if (hostnames && !apr_is_empty_array(hostnames))
    {
      int i;
      svn_stringbuf_t *buf = svn_stringbuf_create_empty(scratch_pool);
      for (i = 0; i < hostnames->nelts; ++i)
        {
          const char *hostname = APR_ARRAY_IDX(hostnames, i, const char*);
          if (i > 0)
            svn_stringbuf_appendbytes(buf, ", ", 2);
          svn_stringbuf_appendbytes(buf, hostname, strlen(hostname));
        }
      SVN_ERR(svn_cmdline_printf(scratch_pool, _("Hostnames: %s\n"),
                                 buf->data));
    }

  return SVN_NO_ERROR;
}

static svn_boolean_t
is_der_cert(const svn_string_t *raw)
{
  /* really simplistic fingerprinting of a DER.  By definition it must
   * start with an ASN.1 tag of a constructed (0x20) sequence (0x10).
   * It's somewhat unfortunate that 0x30 happens to also come out to the
   * ASCII for '0' which may mean this will create false positives. */
  return raw->data[0] == 0x30 ? TRUE : FALSE;
}

static svn_error_t *
get_der_cert_from_stream(const svn_string_t **der_cert, svn_stream_t *in,
                         apr_pool_t *pool)
{
  svn_string_t *raw;
  SVN_ERR(svn_string_from_stream2(&raw, in, SVN__STREAM_CHUNK_SIZE,
                                  pool));

  *der_cert = NULL;

  /* look for a DER cert */
  if (is_der_cert(raw))
    {
      *der_cert = raw;
      return SVN_NO_ERROR;
    }
  else
    {
      const svn_string_t *base64_decoded;
      const char *start, *end;

      /* Try decoding as base64 without headers */
      base64_decoded = svn_base64_decode_string(raw, pool);
      if (base64_decoded && is_der_cert(base64_decoded))
        {
          *der_cert = base64_decoded;
          return SVN_NO_ERROR;
        }

      /* Try decoding as a PEM with begining and ending headers. */
      start = strstr(raw->data, PEM_BEGIN_CERT);
      end = strstr(raw->data, PEM_END_CERT);
      if (start && end && end > start)
        {
          svn_string_t *encoded;

          start += sizeof(PEM_BEGIN_CERT) - 1;
          end -= 1;
          encoded = svn_string_ncreate(start, end - start, pool);
          base64_decoded = svn_base64_decode_string(encoded, pool);
          if (is_der_cert(base64_decoded))
            {
              *der_cert = base64_decoded;
              return SVN_NO_ERROR;
            }
         }
    }

  return svn_error_create(SVN_ERR_X509_CERT_INVALID_PEM, NULL,
                          _("Couldn't find certificate in input data"));
}

int main (int argc, const char *argv[])
{
  apr_pool_t *pool = NULL;
  svn_error_t *err;
  svn_stream_t *in;

  apr_initialize();
  atexit(apr_terminate);

  pool = svn_pool_create(NULL);

  if (argc == 2)
    {
      const char *target = svn_dirent_canonicalize(argv[1], pool);
      err = svn_stream_open_readonly(&in, target, pool, pool);
    }
  else if (argc == 1)
    {
      err = svn_stream_for_stdin2(&in, TRUE, pool);
    }
  else
    err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL, _("Too many arguments"));

  if (!err)
    {
      const svn_string_t *der_cert;
      err = get_der_cert_from_stream(&der_cert, in, pool);
      if (!err)
        err = show_cert(der_cert, pool);
    }

  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "x509-parser: ");

  return 0;
}
