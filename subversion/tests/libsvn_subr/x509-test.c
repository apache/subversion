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
  const char *base64_cert; /* Base64 encoded DER X.509 cert */
  const char *subject; /* Subject in the format that the parser returns */
  const char *issuer; /* Issuer in the format that the parser returns */

  /* These timesamps are in the format that svn_time_to_cstring() produces.
   * This is not the same string as the parser returns since it returns
   * the ressult of svn_time_to_human_cstring(), which is in the local
   * timezone.  So we can't store exactly what the parser will output. */
  const char *valid_from;
  const char *valid_to;
  const char *sha1_digest;
};

static struct x509_test cert_tests[] = {
  /* contains extensions and uses a sha256 algorithm */
  { "MIIEtzCCA5+gAwIBAgIQWGBOrapkezd+BWVsAtmtmTANBgkqhkiG9w0BAQsFADA8"
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
    "C=US, ST=Maryland, L=Forest Hill, O=Apache Software Foundation, "
    "OU=Infrastructure, CN=*.apache.org",
    "C=US, O=Thawte, Inc., CN=Thawte SSL CA",
    "2014-04-11T00:00:00.000000Z",
    "2016-04-07T23:59:59.000000Z",
    "151d8ad1e1bac21466bc2836ba80b5fcf872f37c" },
  /* the expiration is after 2049 so the expiration is in the
   * generalized format, while the start date is still in the UTC
   * format. Note this is actually a CA cert but that really doesn't
   * matter here. */
  { "MIIDtzCCAp+gAwIBAgIJAJKX85dqh3RvMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV"
    "BAYTAkFVMRMwEQYDVQQIEwpTb21lLVN0YXRlMSEwHwYDVQQKExhJbnRlcm5ldCBX"
    "aWRnaXRzIFB0eSBMdGQwIBcNMTQwNjI3MTczMTUxWhgPMjExNDA2MDMxNzMxNTFa"
    "MEUxCzAJBgNVBAYTAkFVMRMwEQYDVQQIEwpTb21lLVN0YXRlMSEwHwYDVQQKExhJ"
    "bnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAw"
    "ggEKAoIBAQDaa4gwNBB6vgWrlOIEMdzvD06zmmiocEt6UnTHtmAcfrBuDnKrBwEh"
    "f5JxneL16XIuKwK6n/4omBtem/PPjjpOLM9PMQuoO0cpQ0UGFnfpmko6PSQoqRHl"
    "qTbDGv4usn7qdZV+FKz/B9CMonRSzWHMz5YPmqfob6BqaaJY/qJEzHJA24bm4jPH"
    "IsaVCInEGpqAUpejwBzNujfbLibBNrVX7K846zk+tnsNR90kP5h3IRP3SdWVywKC"
    "AMN2izzhmaDhuPzaTBobovr+ySJShmX6gdB5PpWkm6rcBl6RJ+tM0ZBSJjQvkYp4"
    "seV+rcXFgpJP/aQL3vhDON32tjWh3A2JAgMBAAGjgacwgaQwHQYDVR0OBBYEFF+N"
    "7TyDI8THpAbx1pfzFFtl5z4iMHUGA1UdIwRuMGyAFF+N7TyDI8THpAbx1pfzFFtl"
    "5z4ioUmkRzBFMQswCQYDVQQGEwJBVTETMBEGA1UECBMKU29tZS1TdGF0ZTEhMB8G"
    "A1UEChMYSW50ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkggkAkpfzl2qHdG8wDAYDVR0T"
    "BAUwAwEB/zANBgkqhkiG9w0BAQUFAAOCAQEAo4t9fYe2I+XIQn8i/KI9UFEE9fue"
    "w6rQMnf9yyd8nwL+IcV84hvyNrq0+7SptUBMq3rsEf5UIBIBI4Oa614mJ/Kt976O"
    "S7Sa1IPH7j+zb/jqH/xGskEVi25dZz7psFCmi7Hm9dnVz9YKa2yLW6R2KZcTVxCx"
    "SSdDRlD7SonsYeq2fGrAo7Y9xfZsiJ2ZbJ18kHs2coMWuhgSrN9jrML6mb5B+k22"
    "/rgsCJgFsBDPBYR3ju0Ahqg7v6kwg9O2PJzyb4ljsw8oI0sCwHTZW5I5FMq2D9g6"
    "hj80N2fhS9QWoLyeKoMTNB2Do6VaNrLrCJiscZWrsnM1f+XBqV8hMuHX8A==",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2014-06-27T17:31:51.000000Z",
    "2114-06-03T17:31:51.000000Z",
    "db3a959e145acc2741f9eeecbeabce53cc5b7362" },
  { NULL }
};

static svn_error_t *
compare_dates(const char *expected,
              const char *actual,
              const char *type,
              const char *subject,
              apr_pool_t *pool)
{
  apr_time_t expected_tm;
  const char *expected_human;

  if (!actual)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No %s for cert '%s'", subject);

  /* Jump through some hoops here since the human timestamp is in localtime
   * so we take the expected which will be in ISO-8601 and convert it to 
   * apr_time_t and then convert to a human cstring for comparison. */
  SVN_ERR(svn_time_from_cstring(&expected_tm, expected, pool));
  expected_human = svn_time_to_human_cstring(expected_tm, pool);
  if (!expected_human)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Problem converting expected %s '%s' to human "
                             "output for cert '%s'", type, expected,
                             subject);

  if (strcmp(expected_human, actual))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "The %s didn't match expected '%s',"
                             " got '%s' for cert '%s'", type,
                             expected_human, actual, subject);

  return SVN_NO_ERROR;
}

static svn_error_t *
compare_results(struct x509_test *xt,
                apr_hash_t *certinfo,
                apr_pool_t *pool)
{
  const char *v;

  v = svn_hash_gets(certinfo, SVN_X509_CERTINFO_KEY_SUBJECT);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No subject for cert '%s'", xt->subject);
  if (strcmp(v, xt->subject))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Subject didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->subject,
                             xt->subject, v);

  v = svn_hash_gets(certinfo, SVN_X509_CERTINFO_KEY_ISSUER);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No issuer for cert '%s'", xt->subject);
  if (strcmp(v, xt->issuer))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Issuer didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->subject,
                             xt->issuer, v);

  SVN_ERR(compare_dates(xt->valid_from,
                        svn_hash_gets(certinfo,
                                      SVN_X509_CERTINFO_KEY_VALID_FROM),
                        SVN_X509_CERTINFO_KEY_VALID_FROM,
                        xt->subject,
                        pool));

  SVN_ERR(compare_dates(xt->valid_to,
                        svn_hash_gets(certinfo,
                                      SVN_X509_CERTINFO_KEY_VALID_TO),
                        SVN_X509_CERTINFO_KEY_VALID_TO,
                        xt->subject,
                        pool));

  v = svn_hash_gets(certinfo, SVN_X509_CERTINFO_KEY_SHA1_DIGEST);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No SHA1 digest for cert '%s'", xt->subject);
  if (strcmp(v, xt->sha1_digest))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "SHA1 digest didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->subject,
                             xt->sha1_digest, v);


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
