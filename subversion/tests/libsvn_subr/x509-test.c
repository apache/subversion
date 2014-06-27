/*
 * x509-test.c -- test the x509 parser functions
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

#include <string.h>
#include "svn_x509.h"
#include "svn_base64.h"
#include "svn_hash.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_string.h"

#include "../svn_test.h"

struct x509_test {
  const char *cert_name; /* name for debugging tests */
  const char *base64_cert; /* Base64 encoded DER X.509 cert */
  const char *issuer; /* Issuer in the format that the parser returns */

  /* These timesamps are in the format that svn_time_to_cstring() produces.
   * This is not the same string as the parser returns since it returns
   * the ressult of svn_time_to_human_cstring(), which is in the local
   * timezone.  So we can't store exactly what the parser will output. */
  const char *valid_from;
  const char *valid_to;
};

static struct x509_test cert_tests[] = {
  /* contains extensions and uses a sha256 algorithm */
  { "svn.apache.org",
    "MIIEtzCCA5+gAwIBAgIQWGBOrapkezd+BWVsAtmtmTANBgkqhkiG9w0BAQsFADA8"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMVGhhd3RlLCBJbmMuMRYwFAYDVQQDEw1U"
    "aGF3dGUgU1NMIENBMB4XDTE0MDQxMTAwMDAwMFoXDTE2MDQwNzIzNTk1OVowgYsx"
    "CzAJBgNVBAYTAlVTMREwDwYDVQQIEwhNYXJ5bGFuZDEUMBIGA1UEBxQLRm9yZXN0"
    "IEhpbGwxIzAhBgNVBAoUGkFwYWNoZSBTb2Z0d2FyZSBGb3VuZGF0aW9uMRcwFQYD"
    "VQQLFA5JbmZyYXN0cnVjdHVyZTEVMBMGA1UEAxQMKi5hcGFjaGUub3JnMIIBIjAN"
    "BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA+Tq4mH+stRoxe4xth8tUCgLt+P4L"
    "D/JWZz4a2IecaaAk57vIlTxEyP16fUShUfxVJnD0KV11zv2qaEUXNaA6hKd4H/oB"
    "u2OyGev+quRM+aFCjWqASkXt7fLGsIkHAwP3XwBVBpARbcXJeCjCBxqaYrQqS8LT"
    "wfPUD9eYncGlQ+ixb3Bosy7TmkWKeLsRdS90cAO/rdgQ8OI7kLT/1tr5GpF9RmXo"
    "RnVqMP+U0zGd/BNNSneg7emb7TxLzxeMKZ7QbF4MZi8RRN11spvx8/f92CiYrGGu"
    "y67VdOGPaomYc+VZ2syLwduHGK40ADrEK3+MQpsRFB0dM08j9bhpr5A44wIDAQAB"
    "o4IBYzCCAV8wFwYDVR0RBBAwDoIMKi5hcGFjaGUub3JnMAkGA1UdEwQCMAAwQgYD"
    "VR0gBDswOTA3BgpghkgBhvhFAQc2MCkwJwYIKwYBBQUHAgEWG2h0dHBzOi8vd3d3"
    "LnRoYXd0ZS5jb20vY3BzLzAOBgNVHQ8BAf8EBAMCBaAwHwYDVR0jBBgwFoAUp6KD"
    "uzRFQD381TBPErk+oQGf9tswOgYDVR0fBDMwMTAvoC2gK4YpaHR0cDovL3N2ci1v"
    "di1jcmwudGhhd3RlLmNvbS9UaGF3dGVPVi5jcmwwHQYDVR0lBBYwFAYIKwYBBQUH"
    "AwEGCCsGAQUFBwMCMGkGCCsGAQUFBwEBBF0wWzAiBggrBgEFBQcwAYYWaHR0cDov"
    "L29jc3AudGhhd3RlLmNvbTA1BggrBgEFBQcwAoYpaHR0cDovL3N2ci1vdi1haWEu"
    "dGhhd3RlLmNvbS9UaGF3dGVPVi5jZXIwDQYJKoZIhvcNAQELBQADggEBAF52BLvl"
    "x5or9/aO7+cPhxuPxwiNRgbvHdCakD7n8vzjNyct9fKp6/XxB6GQiTZ0nZPJOyIu"
    "Pi1QDLKOXvaPeLKDBilL/+mrn/ev3s/aRQSrUsieKDoQnqtmlxEHc/T3+Ni/RZob"
    "PD4GzPuNKpK3BIc0fk/95T8R1DjBSQ5/clvkzOKtcl3VffAwnHiE9TZx9js7kZwO"
    "b9nOKX8DFao3EpQcS7qn63Ibzbq5A6ry8ZNRQSIJK/xlCAWoyUd1uxnqGFnus8wb"
    "9RVZJQe8YvyytBjgbE3QjnfPOxoEJA3twupnPmH+OCTM6V3TZqpRZj/sZ5rtIQ++"
    "hI5FdJWUWVSgnSw=",
    "C=US, O=Thawte, Inc., CN=Thawte SSL CA",
    "2014-04-11T00:00:00.000000Z",
    "2016-04-07T23:59:59.000000Z" },
  { NULL }
};

static svn_error_t *
compare_dates(const char *expected,
              const char *actual,
              const char *type,
              const char *cert_name,
              apr_pool_t *pool)
{
  apr_time_t expected_tm;
  const char *expected_human;

  if (!actual)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No %s for cert '%s'", cert_name);

  /* Jump through some hoops here since the human timestamp is in localtime
   * so we take the expected which will be in ISO-8601 and convert it to 
   * apr_time_t and then convert to a human cstring for comparison. */
  SVN_ERR(svn_time_from_cstring(&expected_tm, expected, pool));
  expected_human = svn_time_to_human_cstring(expected_tm, pool);
  if (!expected_human)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Problem converting expected %s '%s' to human "
                             "output for cert '%s'", type, expected,
                             cert_name);

  if (strcmp(expected_human, actual))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "The %s didn't match expected '%s',"
                             " got '%s' for cert '%s'", type,
                             expected_human, actual, cert_name);

  return SVN_NO_ERROR;
}

static svn_error_t *
compare_results(struct x509_test *xt,
                apr_hash_t *certinfo,
                apr_pool_t *pool)
{
  const char *v;

  v = svn_hash_gets(certinfo, SVN_X509_CERTINFO_KEY_ISSUER);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No issuer for cert '%s'", xt->cert_name);
  if (strcmp(v, xt->issuer))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Issuer didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->cert_name,
                             xt->issuer, v);

  SVN_ERR(compare_dates(xt->valid_from,
                        svn_hash_gets(certinfo,
                                      SVN_X509_CERTINFO_KEY_VALID_FROM),
                        SVN_X509_CERTINFO_KEY_VALID_FROM,
                        xt->cert_name,
                        pool));

  SVN_ERR(compare_dates(xt->valid_to,
                        svn_hash_gets(certinfo,
                                      SVN_X509_CERTINFO_KEY_VALID_TO),
                        SVN_X509_CERTINFO_KEY_VALID_TO,
                        xt->cert_name,
                        pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_x509_parse_cert(apr_pool_t *pool)
{
  struct x509_test *xt;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (xt = cert_tests; xt->base64_cert; xt++)
    {
      const svn_string_t *der_cert;
      apr_hash_t *certinfo;

      svn_pool_clear(iterpool);

      /* Convert header-less PEM to DER by undoing base64 encoding. */
      der_cert = svn_base64_decode_string(svn_string_create(xt->base64_cert,
                                                            pool),
                                          iterpool);

      SVN_ERR(svn_x509_parse_cert(&certinfo, der_cert->data, der_cert->len,
                                  iterpool, iterpool));

      SVN_ERR(compare_results(xt, certinfo, iterpool));
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_x509_parse_cert,
                   "test svn_x509_parse_cert"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
