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
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_string.h"

#include "../svn_test.h"

struct x509_test {
  const char *base64_cert; /* Base64 encoded DER X.509 cert */
  const char *subject; /* Subject Distinguished Name */
  const char *subject_oids; /* Space separated list of oids in Subject */
  const char *issuer; /* Issuer Distinguished Name */
  const char *issuer_oids; /* Space separated list of oids in Issuer */

  /* These timesamps are in the format that svn_time_to_cstring() produces.
   * This is not the same string as the parser returns since it returns
   * the ressult of svn_time_to_human_cstring(), which is in the local
   * timezone.  So we can't store exactly what the parser will output. */
  const char *valid_from;
  const char *valid_to;
  const char *hostnames;
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
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.11 2.5.4.3",
    "C=US, O=Thawte, Inc., CN=Thawte SSL CA",
    "2.5.4.6 2.5.4.10 2.5.4.3",
    "2014-04-11T00:00:00.000000Z",
    "2016-04-07T23:59:59.000000Z",
    "*.apache.org",
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
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2014-06-27T17:31:51.000000Z",
    "2114-06-03T17:31:51.000000Z",
    NULL,
    "db3a959e145acc2741f9eeecbeabce53cc5b7362" },
  /* The subject (except for country code) is UTF-8 encoded.
   * created with openssl using utf8-yes and string_mask=utf8only */
  { "MIIDrTCCApWgAwIBAgIBATANBgkqhkiG9w0BAQUFADBFMQswCQYDVQQGEwJBVTET"
    "MBEGA1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50ZXJuZXQgV2lkZ2l0cyBQ"
    "dHkgTHRkMB4XDTE0MDcwMjE4MzYxMFoXDTE1MDcwMjE4MzYxMFowcjELMAkGA1UE"
    "BhMCR1IxFTATBgNVBAgMDM6Rz4TPhM65zrrOrjETMBEGA1UEBwwKzpHOuM6uzr3O"
    "sTEdMBsGA1UECgwUz4DOsc+BzqzOtM61zrnOs868zrExGDAWBgNVBAMMD3d3dy5l"
    "eGFtcGxlLmNvbTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMVPuQPz"
    "INjsiXl+GeiXMzXV1Bfm8vzbQnMLAFY/ZKKK4gpy58xcNrmur//Fd38naTM/DetO"
    "PEoDa+vQ48CnUWCDT3CKUA3BnrjtR3/EITC7XRcfk5lyk0IZr9RZB1WedQxK1n5E"
    "Ecz8EBrm9+1442Nmg/y1F8d/2F2CjKB+PgfOP1WWaIQcsjLsftXec+kGjc34kwbS"
    "9D9H+bRrPVcOzBZOqC+K0K7MMOxKA5mMi4b/Nlep76gTaUyonclRIADanAyaK5WG"
    "0IkEI/nxufaP3AcPksCbroWLTkPKIe97Yj6mnzNhK9TA9w5RgdBrjNyfrwUaYiYR"
    "FxVJN0VrHWSsRnECAwEAAaN7MHkwCQYDVR0TBAIwADAsBglghkgBhvhCAQ0EHxYd"
    "T3BlblNTTCBHZW5lcmF0ZWQgQ2VydGlmaWNhdGUwHQYDVR0OBBYEFNOobRTPfoWP"
    "EGgXVkHfwrqz7PVzMB8GA1UdIwQYMBaAFIV8JZkZ88X7MTQSsJ6/qF3KboHKMA0G"
    "CSqGSIb3DQEBBQUAA4IBAQAam6vJUv6kcWWrEAfdnwwRmmJ4X1Jey3Sp48G35MOE"
    "KkHtwqbtL+QU1VA2X98bEYobqZinM3e3zrlbpgbe1xoJ00MnT9CgQObXr+cum/Ql"
    "PwWXB5fK3BrNwqRMRGc9w27FevyFeybdKhc47jEKMOANrB/aziNHaq9gBtU/HZdy"
    "rm9TEaOHMy6vNrdpOZKpwXPxYqsQxMLpen9D64t/3P6hsV5FMQTaxSFhszidG44t"
    "xaU4O0BOq4x//THCWguMxzO5RxW/V8wI/rkpvhAH1wljHTusnsAZea4PpstZ7+W7"
    "43GME1DwjYdUK9HhqRNrDkiJLox4Tmegw9A7m4XLt4zu",
    "C=GR, ST=\xce\x91\xcf\x84\xcf\x84\xce\xb9\xce\xba\xce\xae, "
    "L=\xce\x91\xce\xb8\xce\xae\xce\xbd\xce\xb1, "
    "O=\xcf\x80\xce\xb1\xcf\x81\xce\xac\xce\xb4\xce\xb5\xce\xb9\xce\xb3"
    "\xce\xbc\xce\xb1, CN=www.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2014-07-02T18:36:10.000000Z",
    "2015-07-02T18:36:10.000000Z",
    "www.example.com",
    "b3b9789d8a53868f418619565f6b56af0033bdd3" },
  /* The issuer and subject (except for the country code) is
   * UnversalString encoded.  Created with a hacked version of openssl
   * using utf8=yes and string_mask=MASK:256.  In order for that to
   * output UniversalString encoded data you need to change the
   * DIRSTRING_TYPE in crypto/asn1/asn1.h to be defined as
   * B_ASN1_DIRECTORYSTRING so that UnviersalString is available to be
   * used in the DirectoryStrings.  OpenSSL by default avoids
   * this type (for the reasonable reason that it's wasteful and
   * UTF-8 can encoded everything it can in the most efficient way).
   * OU uses the mathematical monospace digits 0-9 to test characters
   * outside of the range of the Basic Multilingual Plane */
  { "MIIEnzCCA4egAwIBAgIBATANBgkqhkiG9w0BAQUFADCBqzELMAkGA1UEBhMCQVUx"
    "MTAvBgNVBAgcKAAAAFMAAABvAAAAbQAAAGUAAAAtAAAAUwAAAHQAAABhAAAAdAAA"
    "AGUxaTBnBgNVBAocYAAAAEkAAABuAAAAdAAAAGUAAAByAAAAbgAAAGUAAAB0AAAA"
    "IAAAAFcAAABpAAAAZAAAAGcAAABpAAAAdAAAAHMAAAAgAAAAUAAAAHQAAAB5AAAA"
    "IAAAAEwAAAB0AAAAZDAeFw0xNDA3MjIyMjM3MzBaFw0xNTA3MjIyMjM3MzBaMIH8"
    "MQswCQYDVQQGEwJHUjEhMB8GA1UECBwYAAADkQAAA8QAAAPEAAADuQAAA7oAAAOu"
    "MR0wGwYDVQQHHBQAAAORAAADuAAAA64AAAO9AAADsTExMC8GA1UEChwoAAADwAAA"
    "A7EAAAPBAAADrAAAA7QAAAO1AAADuQAAA7MAAAO8AAADsTExMC8GA1UECxwoAAHX"
    "9gAB1/cAAdf4AAHX+QAB1/oAAdf7AAHX/AAB1/0AAdf+AAHX/zFFMEMGA1UEAxw8"
    "AAAAdwAAAHcAAAB3AAAALgAAAGUAAAB4AAAAYQAAAG0AAABwAAAAbAAAAGUAAAAu"
    "AAAAYwAAAG8AAABtMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuYUb"
    "aNt22rsR5Qc/2zsenSvrlbvv1CwwRPNxcWTKdLl4lJEUy5YCnQXIq3qTi+eAFetQ"
    "MwUOZem6kgNdwmGvCz3lrLwOobd1D5mG9agzKLVUVj72csbNNFzHr8z/7oaHvYYs"
    "eYxW3oRm6vDYtHw5spXrxTzRIAnG6foxXFYAtDDHQpdjsofxqXO67aUmmGvE5ffX"
    "gD3dvTvjejzcjjVsLQP/HG4MQOqeIyvyyHg1E3dyOrG+3qR6RN1ZveROdvU38Udm"
    "s0KSGVX2lDLsUTQSKg5L8CLWDHqgGQWjLZQRgRiKZId/f9ubaJdLN6KfAQ3UvYAP"
    "bKL5/k2GpsPDE21X0QIDAQABo3sweTAJBgNVHRMEAjAAMCwGCWCGSAGG+EIBDQQf"
    "Fh1PcGVuU1NMIEdlbmVyYXRlZCBDZXJ0aWZpY2F0ZTAdBgNVHQ4EFgQUccHhM6C7"
    "nGMpclkG7YLIRuFueYQwHwYDVR0jBBgwFoAUz0X1b2Ok9MVVzxqxX6MgtTwSKmYw"
    "DQYJKoZIhvcNAQEFBQADggEBAEpqEa08JkPG+XBlLemnoJsnoaRuQnLZvSCoAwIt"
    "fugTE8686EigTZyYVFQ+GaI+EqVeiMjpAEhS3IMbhx5VIr61S3Nta2BG9OPjr4Xf"
    "01oUeh4egL93CpIGNwu6M1SrQv2UVAKTwahxNmNuvx6Ojx5P2tne+KJtRUiwM3dE"
    "of78/0NJD27OwjW0ruZAifF5CAR7mhy3NOMARpE2kqZk5695OF+QCahe00Y/9ulz"
    "sCjgjpCUYv87OTbBGC5XGRd/ZopTRqtBVxpEHX/fux5/wqxBawrCuQsVw1Kfw0Ur"
    "30aYWLsOsRwhiQkukjQfcMra1AHLujWaAHuLIDls1ozc8xo=",
    "C=GR, ST=\xce\x91\xcf\x84\xcf\x84\xce\xb9\xce\xba\xce\xae, "
    "L=\xce\x91\xce\xb8\xce\xae\xce\xbd\xce\xb1, "
    "O=\xcf\x80\xce\xb1\xcf\x81\xce\xac\xce\xb4\xce\xb5\xce\xb9\xce\xb3"
    "\xce\xbc\xce\xb1, "
    "OU=\xf0\x9d\x9f\xb6\xf0\x9d\x9f\xb7\xf0\x9d\x9f\xb8\xf0\x9d\x9f\xb9"
    "\xf0\x9d\x9f\xba\xf0\x9d\x9f\xbb\xf0\x9d\x9f\xbc\xf0\x9d\x9f\xbd"
    "\xf0\x9d\x9f\xbe\xf0\x9d\x9f\xbf, "
    "CN=www.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.11 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2014-07-22T22:37:30.000000Z",
    "2015-07-22T22:37:30.000000Z",
    "www.example.com",
    "cfa15310189cf89f1dadc9c989db46f287fff7a7"
  },
  /* The issuer and subject (except for the country code) is BMPString
   * encoded.  Created with openssl using utf8-yes and string_mask=MASK:2048.
   */
  { "MIID3zCCAsegAwIBAgIBATANBgkqhkiG9w0BAQUFADBnMQswCQYDVQQGEwJBVTEd"
    "MBsGA1UECB4UAFMAbwBtAGUALQBTAHQAYQB0AGUxOTA3BgNVBAoeMABJAG4AdABl"
    "AHIAbgBlAHQAIABXAGkAZABnAGkAdABzACAAUAB0AHkAIABMAHQAZDAeFw0xNDA3"
    "MjIyMzAyMDlaFw0xNTA3MjIyMzAyMDlaMIGBMQswCQYDVQQGEwJHUjEVMBMGA1UE"
    "CB4MA5EDxAPEA7kDugOuMRMwEQYDVQQHHgoDkQO4A64DvQOxMR0wGwYDVQQKHhQD"
    "wAOxA8EDrAO0A7UDuQOzA7wDsTEnMCUGA1UEAx4eAHcAdwB3AC4AZQB4AGEAbQBw"
    "AGwAZQAuAGMAbwBtMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAqzof"
    "mf9YANAl2I5AcUjfAAJhqc2BL6z6k0J9bWyDL7DZf6AJtD5stRjs8cgiSGfJt9Cg"
    "YQ0Cvnwz9ztNVXLliMmiJ4V0HzG80GI6SBK0PoCVbddUV/PN7REgPNjTwMYlys5w"
    "Yt/GR8OJJV+eb02rpAfVigDlh7CFjY/uKMs2ThPi+yQb2V6qxLk3ZKIHh5IbKQjt"
    "zIX/W1t+hiBjojnuOmhAoEefZ583k7amR5GBZO4GS5Qfj+4kjL5xiwB3bjTC8pnV"
    "Iv4+mN2F6xKW/9IOWZtdySDADaU2ioyuMDzzjp5N5Nt0ZGhrEG2cDC3CatZaV4U7"
    "9yBbi6kzlo3fCbCOlQIDAQABo3sweTAJBgNVHRMEAjAAMCwGCWCGSAGG+EIBDQQf"
    "Fh1PcGVuU1NMIEdlbmVyYXRlZCBDZXJ0aWZpY2F0ZTAdBgNVHQ4EFgQUNvwKR1v/"
    "R0FQU1WnzqT3brNxaQQwHwYDVR0jBBgwFoAUSM/JbJVWuYFp+awSOEXZcKn1ddQw"
    "DQYJKoZIhvcNAQEFBQADggEBABna/SiYMBJvbnI+lj7j8ddSFihaFheqtouxOB2d"
    "tiVz5mcc5KsAFlkrxt7YcYB7SEc+K28nqGb3bfbZ18JayRBY3JS/h4WGu4eL5XkX"
    "rceWUy60zF7DHs6p8E8HZVF1CdCC/LXr2BAdYTc/y1f37bLKVFF4mMJMP4b8/nSL"
    "z8+oOO9CxaEjzRoCawf2+jaajXTSTDXBgIx1t6bJMAS6S6RKPaCketyAmpsOZVBS"
    "VtBVfVIOB2zFqs6iqkXtdiOXWlZ0DBQRX0G1VD5G80RlZXs0yEfufCwLUl/TyOhM"
    "WisUSEOzd4RlbsBj30JQkVG9+jXb2KChPkiMpg0tFi8HU3s=",
    "C=GR, ST=\xce\x91\xcf\x84\xcf\x84\xce\xb9\xce\xba\xce\xae, "
    "L=\xce\x91\xce\xb8\xce\xae\xce\xbd\xce\xb1, "
    "O=\xcf\x80\xce\xb1\xcf\x81\xce\xac\xce\xb4\xce\xb5\xce\xb9\xce\xb3"
    "\xce\xbc\xce\xb1, CN=www.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2014-07-22T23:02:09.000000Z",
    "2015-07-22T23:02:09.000000Z",
    "www.example.com",
    "6e2cd969350979d3741b9abb66c71159a94ff971"
  },
  /* The issuer and subject (except for the country code) is T61String
   * (aka TeletexString) encoded.  Created with openssl using utf8=yes
   * and string_mask=MASK:4.  Note that the example chosen specifically
   * includes the Norwegian OE (slashed O) to highlight that this is
   * being treated as ISO-8859-1 despite what the X.509 says.
   * See the following for the horrible details on
   * this encoding: https://www.cs.auckland.ac.nz/~pgut001/pubs/x509guide.txt
   */
  { "MIIDnTCCAoWgAwIBAgIBATANBgkqhkiG9w0BAQUFADBFMQswCQYDVQQGEwJBVTET"
    "MBEGA1UECBQKU29tZS1TdGF0ZTEhMB8GA1UEChQYSW50ZXJuZXQgV2lkZ2l0cyBQ"
    "dHkgTHRkMB4XDTE0MDcyMjIzNDQxOFoXDTE1MDcyMjIzNDQxOFowYjELMAkGA1UE"
    "BhMCTk8xGDAWBgNVBAgUD034cmUgb2cgUm9tc2RhbDEQMA4GA1UEBxQHxWxlc3Vu"
    "ZDENMAsGA1UEChQEZPhtZTEYMBYGA1UEAxQPd3d3LmV4YW1wbGUuY29tMIIBIjAN"
    "BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAz8uD5f2KRXvB//mKOpCXM3h/MOjK"
    "xUgC4TIHi3BmnYR0IDElMPJrC263/eU0hKycyegyMjXkwIN5eEx4/Nl///RrzJBQ"
    "+uXKfEJ4hTJ5x1uUYxhmtq4djZFxfjFH5yobT/LRDkEw9b/+NiRb30P+WrxhrAKW"
    "7GRsE2pIdPdbM2IB5v/wORB4TK0kLYkmeEPWNJd63SmX4BEC6dRAaMxLIXKn75r5"
    "GhMHKbUdt2Yy+5s0JlN9hMWqhnavCmGquzl7y/1E1OOUIm0jhL0sJn6wVTc+UO+Q"
    "7u/w0xf38J8SU7lW6zbcQyYaSIQCMikgpprUSXdQZZUZGmHS7Gis39SiLwIDAQAB"
    "o3sweTAJBgNVHRMEAjAAMCwGCWCGSAGG+EIBDQQfFh1PcGVuU1NMIEdlbmVyYXRl"
    "ZCBDZXJ0aWZpY2F0ZTAdBgNVHQ4EFgQUQa2QLy+4QUH8hKNdR2LcvDKYImcwHwYD"
    "VR0jBBgwFoAUpX6YP04yWqNiziUM7h0KgrRHMF4wDQYJKoZIhvcNAQEFBQADggEB"
    "AElYUTQp5MOQk+ykIV0MHTw9OsEvLc1ZDmChls5WKYAu6KWgBbcjcTlkTpDlydrO"
    "6JFxvCCg0K13dYOI3K/O9icGRauIrxrJOTtaIMryj7F51C52TOVPzkjL05eZTh+q"
    "MmP3KI3uYSpXI6D6RI6hOKIRnFiUOQuXW3I8Z7s03KScBc9PSsVrMBLBz/Vpklaf"
    "Tv/3jVBVIZwCW67SnFQ+vqEzaM4Ns2TBodlVqB1w0enPpow8bNnUwElLQJx3GXnl"
    "z0JTpA6AwIRCF8n+VJgNN218fo2t2vvDDW/cZ+XMXzGNVhAqQ1F8B36esxy3P8+o"
    "Bcwx241dxeGSYFHerqrTJIU=",
    "C=NO, ST=M\xc3\xb8re og Romsdal, L=\xc3\x85lesund, O=d\xc3\xb8me, "
    "CN=www.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2014-07-22T23:44:18.000000Z",
    "2015-07-22T23:44:18.000000Z",
    "www.example.com",
    "787d1577ae77b79649d8f99cf4ed58a332dc48da"
  },
  /* Certificate with several Subject Alt Name dNSNames.  Note that
   * the CommonName is not duplicated in the Subject Alt Name to
   * test that the Common Name is excluded when Subject Alt Name
   * exists. */
  { "MIIEMTCCAxmgAwIBAgIBATANBgkqhkiG9w0BAQUFADBjMQswCQYDVQQGEwJBVTET"
    "MBEGA1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50ZXJuZXQgV2lkZ2l0cyBQ"
    "dHkgTHRkMRwwGgYDVQQDExNJbnRlcm5ldCBXaWRnaXRzIENBMB4XDTE0MDcyNTE3"
    "NDEwNFoXDTE1MDcyNTE3NDEwNFowdDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCldh"
    "c2hpbmd0b24xEzARBgNVBAcTCk5vcnRoIEJlbmQxITAfBgNVBAoTGEludGVybmV0"
    "IFdpZGdpdHMgUHR5IEx0ZDEYMBYGA1UEAxMPd3d3LmV4YW1wbGUuY29tMIIBIjAN"
    "BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAxlryoK6hMhGI/UlHi7v1m+Z3tCvg"
    "ZG1twDFNvBACpFVbJtC/v+fiy1eG7ooZ1PsdCINQ1iXLh1igevlw/4w6iTDpeSZg"
    "OCPYqK6ejnS0bKtSB4TuP8yiQtqwaVz4yPP88lXuQJDRJzgaAR0VAhooLgEpl1z1"
    "n9wQO15AW5swzpKcEOi4n6Zmf1t7oxOt9awAOhkL1FfFwkpbiK9yQv3TPVo+xzbx"
    "BJxwx55RY8Dpiu0kuiTYWsd02pocb0uIqd7a5B4y05PhJseqwyX0Mw57HBBnbru1"
    "lCetP4PkoM2gf7Uoj9e61nmM1mustKTIPvh7tZHWW3UW9JxAFG+6FkKDewIDAQAB"
    "o4HeMIHbMAkGA1UdEwQCMAAwLAYJYIZIAYb4QgENBB8WHU9wZW5TU0wgR2VuZXJh"
    "dGVkIENlcnRpZmljYXRlMB0GA1UdDgQWBBQ4A9k8VwI0wv7u5rB4+1D9cuHiqTAf"
    "BgNVHSMEGDAWgBS6O+MdRDDrD715AXdrnuNZ7wDSyjALBgNVHQ8EBAMCBeAwUwYD"
    "VR0RBEwwSoINKi5leGFtcGxlLmNvbYIRKi5mb28uZXhhbXBsZS5jb22CESouYmFy"
    "LmV4YW1wbGUuY29tghN6aWctemFnLmV4YW1wbGUuY29tMA0GCSqGSIb3DQEBBQUA"
    "A4IBAQAf4IrSOL741IUkyFQrDdof39Cp87VdNEo4Bl8fUSuCjqZONxJfiAFx7GcB"
    "Cd7h7Toe6CYCeQLHSEXQ1S1eWYLIq0ZoP3Q/huJdoH7yskDyC5Faexph0obKM5hj"
    "+EYGW2W/UYBzEZai+eePBovARDlupiMaTJGvtdU/AcgMhXCoGNK6egesXoiNgfFh"
    "h+lXUNWUWm2gZlKwRJff8tkR7bIG7MGzyL6Rqav2/tQdbFVXN5AFPdYPFLf0Vo5m"
    "eGYM87TILfSo7n7Kh0aZovwcuF/vPUWRJl3B1HaPt9k6DhcFyAji0SJyZWyM4v88"
    "GSq5Dk8dnTdL2otToll+r4IqFLlp",
    "C=US, ST=Washington, L=North Bend, O=Internet Widgits Pty Ltd, "
    "CN=www.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=Internet Widgits CA",
    "2.5.4.6 2.5.4.8 2.5.4.10 2.5.4.3",
    "2014-07-25T17:41:04.000000Z",
    "2015-07-25T17:41:04.000000Z",
    "*.example.com, *.foo.example.com, *.bar.example.com, zig-zag.example.com",
    "9c365d27b7b6cc438576a8e465685ea7a4f61129"
  },
  /* This is a CA cert that has a Common Name that doesn't look like
   * a hostname.  Make sure that the hostnames field remains blank for it. */
  { "MIIEEjCCAvqgAwIBAgIJAKJarRWbvbCjMA0GCSqGSIb3DQEBBQUAMGMxCzAJBgNV"
    "BAYTAkFVMRMwEQYDVQQIEwpTb21lLVN0YXRlMSEwHwYDVQQKExhJbnRlcm5ldCBX"
    "aWRnaXRzIFB0eSBMdGQxHDAaBgNVBAMTE0ludGVybmV0IFdpZGdpdHMgQ0EwHhcN"
    "MTQwNzI1MTc0MTAzWhcNMjQwNzIyMTc0MTAzWjBjMQswCQYDVQQGEwJBVTETMBEG"
    "A1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50ZXJuZXQgV2lkZ2l0cyBQdHkg"
    "THRkMRwwGgYDVQQDExNJbnRlcm5ldCBXaWRnaXRzIENBMIIBIjANBgkqhkiG9w0B"
    "AQEFAAOCAQ8AMIIBCgKCAQEAv0f0TAiE13WHaFv8j6M9uuniO40+Aj8cuhZtJ1GC"
    "GI/mW56wq2BJrP6N4+jyxYbZ/13S3ypPu+N087Nc/4xaPtUD/eKqMlU+o8gHM/Lf"
    "BEs2dUuBsvkNM0KoC04NPNTOYDnfHOrzx8iHhqlDedwmP8FeQn3rNS8k4qDyJpG3"
    "Ay8ICz5mB07Cy6NISohTxMtatfW5yKmhnhiS92X42QAEgI1pGB7jJl1g3u+KY1Bf"
    "/10kcramYSYIM1uB7XHQjZI4bhEhQwuIWePMOSCOykdmbemM3ijF9f531Olq+0Nz"
    "t7lA1b/aW4PGGJsZ6uIIjKMaX4npP+HHUaNGVssgTnTehQIDAQABo4HIMIHFMB0G"
    "A1UdDgQWBBS6O+MdRDDrD715AXdrnuNZ7wDSyjCBlQYDVR0jBIGNMIGKgBS6O+Md"
    "RDDrD715AXdrnuNZ7wDSyqFnpGUwYzELMAkGA1UEBhMCQVUxEzARBgNVBAgTClNv"
    "bWUtU3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDEcMBoG"
    "A1UEAxMTSW50ZXJuZXQgV2lkZ2l0cyBDQYIJAKJarRWbvbCjMAwGA1UdEwQFMAMB"
    "Af8wDQYJKoZIhvcNAQEFBQADggEBAI442H8CpePFvOtdvcosu2N8juJrzACuayDI"
    "Ze32EtHFN611azduqkWBgMJ3Fv74o0A7u5Gl8A7RZnfBTMX7cvpfHvWefau0xqgm"
    "Mn8CcTUGel0qudCCMe+kPppmkgNaZFvawSqcAA/u2yni2yx8BakYYDZzyfmEf9dm"
    "hZi5SmxFFba5UhNKOye0GKctT13s/7EgfFNyVhZA7hWU26Xm88QnGnN/qxJdpq+e"
    "+Glctn9tyke4b1VZ2Yr+R4OktrId44ZQcRD44+88v5ThP8DQsvkXcjREMFAIPkvG"
    "CEDOIem4l9KFfnsHn8/4KvoBRkmCkGaSwOwUdUG+jIjBpY/82kM=",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=Internet Widgits CA",
    "2.5.4.6 2.5.4.8 2.5.4.10 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=Internet Widgits CA",
    "2.5.4.6 2.5.4.8 2.5.4.10 2.5.4.3",
    "2014-07-25T17:41:03.000000Z",
    "2024-07-22T17:41:03.000000Z",
    NULL,
    "b9decce236aa1da07b2bf088160bffe1469b9a4a"
  },
  /* Cert with a IP SAN entry.  Make sure we properly skip them. */
  { "MIIENjCCAx6gAwIBAgIBATANBgkqhkiG9w0BAQUFADBjMQswCQYDVQQGEwJBVTET"
    "MBEGA1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50ZXJuZXQgV2lkZ2l0cyBQ"
    "dHkgTHRkMRwwGgYDVQQDExNJbnRlcm5ldCBXaWRnaXRzIENBMB4XDTE0MDcyNTE4"
    "NDMyOFoXDTE1MDcyNTE4NDMyOFowczELMAkGA1UEBhMCVVMxEzARBgNVBAgTCldh"
    "c2hpbmd0b24xEzARBgNVBAcTCk5vcnRoIEJlbmQxITAfBgNVBAoTGEludGVybmV0"
    "IFdpZGdpdHMgUHR5IEx0ZDEXMBUGA1UEAxMOaXAuZXhhbXBsZS5jb20wggEiMA0G"
    "CSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDXKkSxg89tu5/n+lIC8ajj1T9vsO5B"
    "nRH5Sne7UPc6pGMTNFi1MOVjdDWkmuCUzoI+HKLDc69/4V5RU12N1QNgsgcOzCSo"
    "qgxa+dQk2s1shz1zhyaHkpdeMZU3/p9D4v+nRGAdYifwl/VOTwjWWucNzHDBwvb6"
    "+Wm4pXE94Y5p8fY/lZi7VgtxdoPdSHGkIAps8psZGPjqKpLEjnLMp1n0v9cZhBF6"
    "OoMUZpQuwcjT8vMQppgIWhZFLiH2jn7FTYWZyB0Dh9nMd097NQA87VtVfNc+g0oY"
    "qLe3YldJgvVfyeSLhnyv68fBfGcTj310pNrGeE/m4tyxupiUT8BitfxPAgMBAAGj"
    "geQwgeEwCQYDVR0TBAIwADAsBglghkgBhvhCAQ0EHxYdT3BlblNTTCBHZW5lcmF0"
    "ZWQgQ2VydGlmaWNhdGUwHQYDVR0OBBYEFI09JZlhKV44Z+I5d58V/ZDqQ7yZMB8G"
    "A1UdIwQYMBaAFDjQVnIU9pQI1nM8jjmxYiicMTdGMAsGA1UdDwQEAwIF4DBZBgNV"
    "HREEUjBQgg0qLmV4YW1wbGUuY29tghEqLmZvby5leGFtcGxlLmNvbYcEfwAAAYIR"
    "Ki5iYXIuZXhhbXBsZS5jb22CE3ppZy16YWcuZXhhbXBsZS5jb20wDQYJKoZIhvcN"
    "AQEFBQADggEBAEK+XIGwavf+5Ht44ifHrGog0CDr4ESg7wFjzk+BJwYDtIPp9b8A"
    "EG8qbfmOS+2trG3zc74baf2rmrfn0YGZ/GV826NMTaf7YU1/tJQTo+RX9g3aHg6f"
    "pUBfIyAV8ELq84sgwd1PIgleVgIiDrz+a0UZ05Z5S+GbR2pwNH6+fO0O5E9clt2a"
    "Cute1UMBqAMGKiFaP8HD6SUFTdTKZNxHtQzYmmuvoC1nzVatMFdkTuQgSQ/uNlzg"
    "+yUFoufMZhs3gPx9PfXGOQ7f3nKE+WCK4KNGv+OILYsk4zUjMznfAwBRs9PyITN2"
    "BKe64WsF6ZxTq3zLVGy5I8LpbtlvSmAaBp4=",
    "C=US, ST=Washington, L=North Bend, O=Internet Widgits Pty Ltd, "
    "CN=ip.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=Internet Widgits CA",
    "2.5.4.6 2.5.4.8 2.5.4.10 2.5.4.3",
    "2014-07-25T18:43:28.000000Z",
    "2015-07-25T18:43:28.000000Z",
    "*.example.com, *.foo.example.com, *.bar.example.com, zig-zag.example.com",
    "3525fb617c232fdc738d736c1cbd5d97b19b51e4"
  },
  /* Cert with the signature algorithm OID set to sha1WithRSA instead of
   * sha1WithRSAEncryption.  Both have the same meaning but the sha1WithRSA
   * doesn't seem to be used anymore and is shorter */
  { "MIIDgDCCAmygAwIBAgIBATAJBgUrDgMCHQUAMEUxCzAJBgNVBAYTAkFVMRMwEQYD"
    "VQQIFApTb21lLVN0YXRlMSEwHwYDVQQKFBhJbnRlcm5ldCBXaWRnaXRzIFB0eSBM"
    "dGQwHhcNMTQwODE4MDk1OTQ1WhcNMTUwODE4MDk1OTQ1WjBNMQswCQYDVQQGEwJV"
    "SzEQMA4GA1UECBQHRW5nbGFuZDESMBAGA1UEBxQJU2hlZmZpZWxkMRgwFgYDVQQD"
    "FA93d3cuZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIB"
    "AQCkvtieKg33RSzhn5JMDPPRlDS8Q16CN96A4lLI9YrJCy33z46PrbR2mq2hOz5l"
    "MdgbAaRF0MUGhcKv4msJ0bsWhkybaSBAVgnoC7ObQWPNF7ppMzUjeDAlUBXNfheR"
    "ZcgcgGWqUkoB1uUMhvmVuPrzvxn+WCwyoP6zQCviYLsR8AygGQgdhV6c9wJ/x9HS"
    "MRUvUOeo7SCmx9GK5Hc11QV2K3rwKXABeAxXNzbyQe7hFfQYCI2SB5s3bEnhIvg7"
    "BG0BQmoprHjXWBftc0+msKQTFw7+jZ21NsfwGoPonuVsCOJjJ51jp2oKqk3b1GGc"
    "DEmmMQ0JtqfHO5a7JACBaHbTAgMBAAGjezB5MAkGA1UdEwQCMAAwLAYJYIZIAYb4"
    "QgENBB8WHU9wZW5TU0wgR2VuZXJhdGVkIENlcnRpZmljYXRlMB0GA1UdDgQWBBSo"
    "jICtcIgZL6OCCB5BJ5PGf1UIyTAfBgNVHSMEGDAWgBT5KQMLMylrXSQvhMtONHZc"
    "22Jm9TAJBgUrDgMCHQUAA4IBAQCvCJ4i2kRzSRhnlDxd0UbQtytVIJFFJlfREPTM"
    "j8+VqqtCVyPSX8T5NU+HCiEmhVrTlm/W0i8ygJXr8izyIMGRqbyhn2M9b8hAY6Jl"
    "0edztu/FV/YHsJbPznWkXWpMMaXDEX4wI329f5odccIbB5VSaaoAdKZ6Ne4nf6oV"
    "95KRFWkXoYjm24TnpALsNnK1Kjjed6h5ApB+IANOpXYFbGcsfbuKhWbFd2nd6t5U"
    "NpUcv4H9Tgdl6KgrfsbQtAeouWCgoiNzrul8FOaQTdJLZfCsjuE+IkGpM+DX8PiF"
    "5M41EqkSKia8sChFIln+lkRY41OWP9uQ1VXCfdRIzOnXWh9U",
    "C=UK, ST=England, L=Sheffield, CN=www.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2014-08-18T09:59:45.000000Z",
    "2015-08-18T09:59:45.000000Z",
    "www.example.com",
    "0e0869961d508b13bb22aa8da675b2e9951c0e70"
  },
  /* X.509 v1 certificate, we used to crash on these prior to r1619861. */
  { "MIIDDTCCAfUCAQEwDQYJKoZIhvcNAQEFBQAwRTELMAkGA1UEBhMCQVUxEzARBgNV"
    "BAgTClNvbWUtU3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0"
    "ZDAeFw0xNTAxMTkyMjEyNDhaFw0xNjAxMTkyMjEyNDhaMFQxCzAJBgNVBAYTAlVT"
    "MRMwEQYDVQQIEwpXYXNoaW5ndG9uMRMwEQYDVQQHEwpOb3J0aCBCZW5kMRswGQYD"
    "VQQDExJ4NTA5djEuZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAw"
    "ggEKAoIBAQDniW3DmGGtA0MoYqE9H55/RmjtTJD2WVmM/STEsw+RW74UGsZ62qfi"
    "ADedl4ukZYKlk3TwJrGEwDBKOMWHuzCYVxhclyHkHwX7QqamvZRgaOonEu82KHuE"
    "dZo4FhOWDC9D0yS4RFbfqvSu/JG19FYsnRQn1RPFYji6jG9TRwavplVBiMhR68kc"
    "8HTW1Wu7uJ5SV0UtTicFes8MGek3+zWceGt+Egwd2UlIYXwTPzB5m7UPuufEdvFL"
    "ED3pusVatohFzjCbYsuJIR5ppYd49uTxPWGvRidJ2C8GbDf9PCgDduS0Gz91Txnw"
    "h+WiVYCQ6SxAJWp/xeZWE71k88N0vJEzAgMBAAEwDQYJKoZIhvcNAQEFBQADggEB"
    "ABoBaObsHnIrkd3RvvGb5q7fnEfiT1DXsufS3ypf4Z8IST/z+NeaUaiRN1oLcvDz"
    "qC7ygTYZ2BZoEw3ReCGqQWT4iYET+lH8DM+U5val3gVlSWqx1jj/wiV1OAxQsakM"
    "BnmNs/MDshiv54irvSlqnxEp2o/BU/vMrN656C5DJkZpYoMpIWxdFnd+bzNzuN1k"
    "pJfTjzWlGckKfdblNPOfdtccTqtQ5d4mWtYNJ8DfL5rRRwCuzXvZtbVHKxqkXaXr"
    "CYUfFUobapgPfvvMc1QcDY+2nvhC2ij+HAPIHgZPuzJsjZRC1zwg074cfgjZbgbm"
    "R0HVF486p3vS8HFv4lndRZA=",
    "C=US, ST=Washington, L=North Bend, CN=x509v1.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2015-01-19T22:12:48.000000Z",
    "2016-01-19T22:12:48.000000Z",
    "x509v1.example.com",
    "5730dd65a7f77fdf0dfd90e5a53119f38854af29"
  },
  /* X.509 v1 certificate with an X.509 v3 Subject Alternative Name
   * extension.  Although these are ill-formed per RFC 5280 s. 4.1, we
   * suspect that they could exist in the real world.  Make sure we do
   * not error out, and that we pick up SAN (b.example.com) from the
   * extension. */
  { "MIIDLzCCAhcCAQ8wDQYJKoZIhvcNAQEFBQAwKzEpMCcGA1UEAwwgSW50ZXJuZXQg"
    "V2lkZ2l0cyBJbnRlcm1lZGlhdGUgQ0EwHhcNMTUwMTI5MDAzMzU1WhcNMTYwMTI5"
    "MDAzMzU1WjByMQswCQYDVQQGEwJVUzETMBEGA1UECAwKV2FzaGluZ3RvbjETMBEG"
    "A1UEBwwKTm9ydGggQmVuZDEhMB8GA1UECgwYSW50ZXJuZXQgV2lkZ2l0cyBQdHkg"
    "THRkMRYwFAYDVQQDDA1hLmV4YW1wbGUuY29tMIIBIjANBgkqhkiG9w0BAQEFAAOC"
    "AQ8AMIIBCgKCAQEAs0hj2xPRQZpecqk0Ih1l4juAuQZeSgv3yD/VtSq/9sTBH6iA"
    "4XjJQcHROYxYaK0QS/qlCjpl+Q3mOaVIu+59TLy3T2YVgqMYmgB453ntuJPkdF1C"
    "fJ2j19YAQZHHdOFaP1G+auBwjmHns3+MkG4s7EPuJP7TBCcSFlOmz5D4GUui3NVG"
    "LBYUog1ZhF4oe/7d4jc2Cn8uypNT/Hc1ViIlCT4rFoAirv9Uob+4zjQ3Z18I1Ql1"
    "t8oszVCj3kKDboEty2RduwPLx/2ztWYBCvFhd49JGdi/nzMi+j2d5HCI3V8W06pN"
    "mvrVU4G0ImVRa8wpmQCSm2Tp0s42FAVHWw8yMwIDAQABoxwwGjAYBgNVHREEETAP"
    "gg1iLmV4YW1wbGUuY29tMA0GCSqGSIb3DQEBBQUAA4IBAQDI/n0NYakuRP/485/A"
    "dan71qBy3sljjOreq71IfBdtq+GEjCL1B0TD0V338LXki9NicCLeD/MWfceDjV0u"
    "AjPTxaZEn/NWqXo0mpNC535Y6G46mIHYDGC8JyvCJjaXF+GVstNt6lXzZp2Yn3Si"
    "K57uVb+zz5zAGSO982I2HACZPnF/oAtp7bwxzwvBsLqSLw3hh0ATVPp6ktE+WMoI"
    "X75CVcDmU0zjXqzKiFPKeTVjQG6YxgvplMaag/iNngkgEhX4PIrxdIEsHf8l9ogC"
    "dz51MFxetsC4D2KRq8IblF9i+9r3hlv+Dbf9ovYe9Hu0usloSinImoWOw42iWWmP"
    "vT4l",
    "C=US, ST=Washington, L=North Bend, O=Internet Widgits Pty Ltd, "
    "CN=a.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.10 2.5.4.3",
    "CN=Internet Widgits Intermediate CA",
    "2.5.4.3",
    "2015-01-29T00:33:55.000000Z",
    "2016-01-29T00:33:55.000000Z",
    "b.example.com",
    "47fa5c76fee6e21e37def6da3746bba84a5a09bf"
  },
  /* X.509 certificate with multiple Relative Distinguished Names
   * Borrowed form the Chromium test suite see thier bug here
   * https://code.google.com/p/chromium/issues/detail?id=101009
   */
  { "MIICsDCCAhmgAwIBAgIJAO9sL1fZ/VoPMA0GCSqGSIb3DQEBBQUAMHExbzAJBgNV"
    "BAYTAlVTMA8GA1UECgwIQ2hyb21pdW0wFgYKCZImiZPyLGQBGRYIQ2hyb21pdW0w"
    "GgYDVQQDDBNNdWx0aXZhbHVlIFJETiBUZXN0MB0GA1UECwwWQ2hyb21pdW0gbmV0"
    "X3VuaXR0ZXN0czAeFw0xMTEyMDIwMzQ3MzlaFw0xMjAxMDEwMzQ3MzlaMHExbzAJ"
    "BgNVBAYTAlVTMA8GA1UECgwIQ2hyb21pdW0wFgYKCZImiZPyLGQBGRYIQ2hyb21p"
    "dW0wGgYDVQQDDBNNdWx0aXZhbHVlIFJETiBUZXN0MB0GA1UECwwWQ2hyb21pdW0g"
    "bmV0X3VuaXR0ZXN0czCBnzANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEAnSMQ7YeC"
    "sOuk+0n128F7TfDtG/X48sG10oTe65SC8N6LBLfo7YYiQZlWVHEzjsFpaiv0dx4k"
    "cIFbVghXAky/r5qgM1XiAGuzzFw7R27cBTC9DPlRwHArP3CiEKO3iz8i+qu9x0il"
    "/9N70LcSSAu/kGLxikDbHRoM9d2SKhy2LGsCAwEAAaNQME4wHQYDVR0OBBYEFI1e"
    "cfoqc7qfjmMyHF2rh9CrR6u3MB8GA1UdIwQYMBaAFI1ecfoqc7qfjmMyHF2rh9Cr"
    "R6u3MAwGA1UdEwQFMAMBAf8wDQYJKoZIhvcNAQEFBQADgYEAGKwN01A47nxVHOkw"
    "wFdbT8t9FFkY3pIg5meoqO3aATNaSEzkZoUljWtWgWfzr+n4ElwZBxeYv9cPurVk"
    "a+wXygzWzsOzCUMKBI/aS8ijRervyvh6LpGojPGn1HttnXNLmhy+BLECs7cq6f0Z"
    "hvImrEWhD5uZGlOxaZk+bFEjQHA=",
    "C=US, O=Chromium, 0.9.2342.19200300.100.1.25=Chromium, "
    "CN=Multivalue RDN Test, OU=Chromium net_unittests",
    "2.5.4.6 2.5.4.10 0.9.2342.19200300.100.1.25 2.5.4.3 2.5.4.11",
    "C=US, O=Chromium, 0.9.2342.19200300.100.1.25=Chromium, "
    "CN=Multivalue RDN Test, OU=Chromium net_unittests",
    "2.5.4.6 2.5.4.10 0.9.2342.19200300.100.1.25 2.5.4.3 2.5.4.11",
    "2011-12-02T03:47:39.000000Z",
    "2012-01-01T03:47:39.000000Z",
    NULL,
    "99302ca2824f585a117bb41302a388daa0519765"
  },
  /* certificate with subject that includes an attribute that has an
   * object id that has leading zeros.  This isn't technically legal
   * but a simplistic parser might parser it the same as an object
   * id that doesn't have a leading zero.  In this case the object id
   * with a leading zero could parse to the same object id as the
   * Common Name.  Make sure we don't treat it as such. */
  { "MIIDDjCCAfYCAQEwDQYJKoZIhvcNAQEFBQAwRTELMAkGA1UEBhMCQVUxEzARBgNV"
    "BAgTClNvbWUtU3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0"
    "ZDAeFw0xNTAxMjcwNzQ5MDhaFw0xNjAxMjcwNzQ5MDhaMFUxCzAJBgNVBAYTAlVT"
    "MRMwEQYDVQQIEwpXYXNoaW5ndG9uMRMwEQYDVQQHEwpOb3J0aCBCZW5kMRwwGgYE"
    "VQSAAxMSbm90YWNuLmV4YW1wbGUuY29tMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A"
    "MIIBCgKCAQEAvXCJv0gr9d3GNYiukPrbse0FdXmuBx2mPf665WyZVHk9JiPnDcb2"
    "ng8gHLgJe8izou6I0vN2iJgy91rUPvX9zA3qVhml+cboVY2jHCPWo/v5PQsXAgLV"
    "5gVjp2POn3N0O1xcS1yNe249LkP0Di3kAMp5gkzdprm3fD3JDW1Q+ocQylnbjzG0"
    "FtNQSUJLITvPXjR7ny46Fci2mv8scHOvlEXTK5/2RoBaoK2jWQimqGfFj1sr1vqZ"
    "Wcb6NAdZso64Xg1V6CWX8zymlA7gAhTQWveq+ovUWcXpmR8aj9pYNuy0aZW3BANz"
    "N6L0G7OZiVUvvzpfnn0V3Z/sR/iQs7q3nQIDAQABMA0GCSqGSIb3DQEBBQUAA4IB"
    "AQACZwruCiesCRkT08AtHl0WQnQui58e9/7En+iqxNQO6+fx84SfWGcUFYZtvzdO"
    "KkHNTs06km+471OjLSDcotRkdqO1JxQCkNxbrPat7T6FrO9n2JFivx6eijRqK/jB"
    "cBYW92dK4BfXU4+FyeB2OIpyPjuqLU2j7S5p7qNU50i/1J7Qt669nXeaPINIfZdW"
    "sDjjWkFR1VOgXS/zeu/GOxlQFmmcde+X/qkFI+L352VX7Ktf95j4ms4vG2yZgNfe"
    "jbNb9a7LMcqlop/PlX5WBGv8GGKUNZO0LvukFYOULf1oL8VQsN0x/gRHGC7m9kVM"
    "3hojWZDXAY4mYqdBCRX7/gkt",
    "C=US, ST=Washington, L=North Bend, 2.5.4.03=notacn.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.03",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2015-01-27T07:49:08.000000Z",
    "2016-01-27T07:49:08.000000Z",
    NULL,
    "6f24b834ba00fb4ef863df63b8fbeddab25e4838"
  },
  /* certificate with subject that includes an attribute that has an
   * object id that has an overflow such that it calculates to
   * the same object id as the Common Name (2.5.4.3).  OpenSSL
   * with its bignum support shows this as 2.5.4.2361183241434822606851.
   * It would be wrong to display this as a Common Name to the user. */
  { "MIIDGTCCAgECAQEwDQYJKoZIhvcNAQEFBQAwRTELMAkGA1UEBhMCQVUxEzARBgNV"
    "BAgTClNvbWUtU3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0"
    "ZDAeFw0xNTAxMjcwODMxNDNaFw0xNjAxMjcwODMxNDNaMGAxCzAJBgNVBAYTAlVT"
    "MRMwEQYDVQQIEwpXYXNoaW5ndG9uMRMwEQYDVQQHEwpOb3J0aCBCZW5kMScwJQYN"
    "VQSCgICAgICAgICAAxMUb3ZlcmZsb3cuZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3"
    "DQEBAQUAA4IBDwAwggEKAoIBAQDHL1e8zSPyRND3tI42Vqca2FoCiWn881Czv2ct"
    "tGFwyjUM8R1yHXEP+doS9KN9L29xRWZRxyCQ18S+QbjNQCh6Ay22qnkBu0uPdVB6"
    "iIVKiW9RzU8dZSFMnveUZYLloG12kK++ooJGIstTJwkI8Naw1X1D29gZaY9oSKAc"
    "Gs5c92po61RoetB744dUfUbAXi8eEd4ShdsdnCoswpEI4WTLdYLZ/cH/sU1a5Djm"
    "cAfEBzZSOseEQSG7Fa/HvHyW+jDNnKG2r73M45TDcXAunSFcAYl1ioBaRwwdcTbK"
    "SMGORThIX5UwpJDZI5sTVmTTRuCjbMxXXki/g9fTYD6mlaavAgMBAAEwDQYJKoZI"
    "hvcNAQEFBQADggEBABvZSzFniMK4lqJcubzzk410NqZQEDBxdNZTNGrQYIDV8fDU"
    "LLoQ2/2Y6kOQbx8r3RNcaJ6JtJeVqAq05It9oR5lMJFA2r0YMl4eB2V6o35+eaKY"
    "FXrJzwx0rki2mX+iKsgRbJTv6mFb4I7vny404WKHNgYIfB8Z5jgbwWgrXH9M6BMb"
    "FL9gZHMmU+6uqvCPYeIIZaAjT4J4E9322gpcumI9KGVApmbQhi5lC1hBh+eUprG7"
    "4Brl9GeCLSTnTTf4GHIpqaUsKMtJ1sN/KJGwEB7Z4aszr80P5/sjHXOyqJ78tx46"
    "pwH7/Fx0pM7nZjJVGvcxGBBOMeKy/o2QUVvEYPU=",
    "C=US, ST=Washington, L=North Bend, \?\?=overflow.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 \?\?",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2015-01-27T08:31:43.000000Z",
    "2016-01-27T08:31:43.000000Z",
    NULL,
    "c1f063daf23e402fe58bab1a3fa2ba05c1106158"
  },
  /* certificate with multiple common names, make sure this behaves
   * the same way as serf. */
  { "MIIDJjCCAg4CAQEwDQYJKoZIhvcNAQEFBQAwRTELMAkGA1UEBhMCQVUxEzARBgNV"
    "BAgTClNvbWUtU3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0"
    "ZDAeFw0xNTAxMjExNzUwMDZaFw0xNjAxMjExNzUwMDZaMG0xCzAJBgNVBAYTAlVT"
    "MRMwEQYDVQQIEwpXYXNoaW5ndG9uMRMwEQYDVQQHEwpOb3J0aCBCZW5kMRkwFwYD"
    "VQQDExBnb29kLmV4YW1wbGUuY29tMRkwFwYDVQQDExBldmlsLmV4YW1wbGUuY29t"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA5pfrXkiiDGCWSYhMQNHJ"
    "gNBLEBNcFzsGpW8i6rMKVephwG7p4VqIvc0pSsmpD9IYuIxxq/2E2cziaTWyqCBp"
    "hKKipqt8eMcu6u45LduHGiCcnN7rHORbQZTdvwzTmiVN1eI1oCVejB4zgHNkHUko"
    "DyaALCHGRz8l7Qq6hSbiOnhH1qlscIIEsgQEyDlMZpbsWVTQKPxluhtgqVEn7wPN"
    "qScrf2evq050NuNYYFzCmuqOGKq2gKbD/BlUqCNmEM2JPg/bdcAQxFCf0HcvDiS9"
    "e29suMKWZAzJkbzrWhlDMG1Xt5c7dd82PcGwnL//Q7muE57luCw38Gp2vQQ3/Uki"
    "vQIDAQABMA0GCSqGSIb3DQEBBQUAA4IBAQBry9wfxYia/dCSKvDXOBKUgWFQtI8j"
    "7vYHuouTvIb5m6b62kiUdtuaVKi3jnUbHUFohOi/6o+HIwbXSgz5CbiLjgUvONBU"
    "BLekaguIYX9tTmg+vhWchcmVMHufj6HdQkzWtyojSQD9GjHGInNDG102KlN1cdL8"
    "jGTrru4vnef+xA24EvYPdcS2+H2yYH0THL3JPKo1GtO4NCEGWQbS6Ygwcy+BQpbU"
    "TBIWhlbleuCalB8qhWyijcHeszT7mFR0CarEaSLeZj6FaQpZB636iHuELmxcgiFw"
    "j3r3QZyAMEGvPPBPKYSTgmol31pX9LYvuFGA9ADQ2in/n9WdMfYzFzOn",
    "C=US, ST=Washington, L=North Bend, "
    "CN=good.example.com, CN=evil.example.com",
    "2.5.4.6 2.5.4.8 2.5.4.7 2.5.4.3 2.5.4.3",
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
    "2.5.4.6 2.5.4.8 2.5.4.10",
    "2015-01-21T17:50:06.000000Z",
    "2016-01-21T17:50:06.000000Z",
    "good.example.com",
    "9693f17e59205f41ca2e14450d151b945651b2d7"
  },
  /* Signed using RSASSA-PSS algorithm with algorithm parameters */
  {
    "MIICsjCCAWkCCQDHslXYA8hCxTA+BgkqhkiG9w0BAQowMaANMAsGCWCGSAFlAwQC"
    "AaEaMBgGCSqGSIb3DQEBCDALBglghkgBZQMEAgGiBAICAN4wKjEUMBIGA1UECgwL"
    "TXkgTG9jYWwgQ0ExEjAQBgNVBAMMCWxvY2FsaG9zdDAeFw0xODAyMDIxNjQ4MzVa"
    "Fw0xODAyMDMxNjQ4MzVaMC4xGDAWBgNVBAoMD015IExvY2FsIFNlcnZlcjESMBAG"
    "A1UEAwwJbG9jYWxob3N0MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCues61"
    "JXXpLQI5yeg4aCLWRfvnJY7wnuU6FSA++3wwCJREx1/7ebnP9RRRqqKM+ZeeFMC+"
    "UlJE3ft2tJTDOVk9j6qjvKrJUKM1YkIe0lARxs4RtZKDGfOdBhw/+iD+6fZzhL0n"
    "+w+dIJGzl6ADWsE/x9yjDTkdgbtxHrx/76K0KQIDAQABMD4GCSqGSIb3DQEBCjAx"
    "oA0wCwYJYIZIAWUDBAIBoRowGAYJKoZIhvcNAQEIMAsGCWCGSAFlAwQCAaIEAgIA"
    "3gOCAQEABYRAijCSGyFdSuUYALUnNzPylqYXlW+dMKPywlUrFEhKnvS+FD9twerI"
    "8kT4MDW6XvhScmL1MCDPNAkFY92UqaUrgT80oyrbpuakVrxFSS1i28xy8+kXAWYq"
    "RNQVaME1NqnATYF0ZMD5xQK4rpa76gvWj3K8Lt++9EjjbkNiirIIMQEOxh1lwnDQ"
    "81q1Rk6iujlnVDGHDQ+w8reE6fKfSWfv1EaQRcjNKCuzrW8WNN387G2byvwaaKeL"
    "M7lV7wiV6PwrTNTZzVG3cWKDOEP1mGE7gyMu66siLECo8U95+ahK7O6vfeT3m3gv"
    "7kzWNYozAQtBSC7b0WqWbVrzWI4HSg==",
    "O=My Local Server, CN=localhost",
    "2.5.4.10 2.5.4.3",
    "O=My Local CA, CN=localhost",
    "2.5.4.10 2.5.4.3",
    "2018-02-02T16:48:35.000000Z ",
    "2018-02-03T16:48:35.000000Z ",
    "localhost",
    "25ab5a059acfc793fc0d3734d426794a4ca7b631"
  },
  { NULL }
};

static svn_error_t *
compare_dates(const char *expected,
              apr_time_t actual,
              const char *type,
              const char *subject,
              apr_pool_t *pool)
{
  apr_time_t expected_tm;

  if (!actual)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No %s for cert '%s'", type, subject);

  SVN_ERR(svn_time_from_cstring(&expected_tm, expected, pool));
  if (!expected_tm)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Problem converting expected %s '%s' to text "
                             "output for cert '%s'", type, expected,
                             subject);

  if (expected_tm != actual)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "The %s didn't match expected '%s',"
                             " got '%s' for cert '%s'",
                             type, expected,
                             svn_time_to_cstring(actual, pool),
                             subject);

  return SVN_NO_ERROR;
}

static svn_error_t *
compare_hostnames(const char *expected,
                  const apr_array_header_t *actual,
                  const char *subject,
                  apr_pool_t *pool)
{

  int i;
  svn_stringbuf_t *buf;

  if (!actual)
    {
      if (expected)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "The hostnames didn't match expected '%s',"
                                 " got NULL for cert '%s'",
                                 expected, subject);
      return SVN_NO_ERROR;
    }

  buf = svn_stringbuf_create_empty(pool);
  for (i = 0; i < actual->nelts; ++i)
    {
      const char *hostname = APR_ARRAY_IDX(actual, i, const char*);
      if (i > 0)
        svn_stringbuf_appendbytes(buf, ", ", 2);
      svn_stringbuf_appendbytes(buf, hostname, strlen(hostname));
    }

  if (strcmp(expected, buf->data))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "The hostnames didn't match expected '%s',"
                             " got '%s' for cert '%s'",
                             expected, buf->data, subject);
  return SVN_NO_ERROR;
}

static svn_error_t *
compare_oids(const char *expected,
             const apr_array_header_t *actual,
             const char *subject,
             apr_pool_t *pool)
{
  int i;
  svn_stringbuf_t *buf;

  if (!actual)
    {
      if (expected)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "The oids didn't match expected '%s',"
                                 " got NULL for cert '%s'",
                                 expected, subject);
      return SVN_NO_ERROR;
    }

  buf = svn_stringbuf_create_empty(pool);
  for (i = 0; i < actual->nelts; ++i)
    {
      apr_size_t len;
      const svn_x509_name_attr_t *attr = APR_ARRAY_IDX(actual, i, const svn_x509_name_attr_t *);
      const void *oid = svn_x509_name_attr_get_oid(attr, &len);
      const char *oid_string = svn_x509_oid_to_string(oid, len, pool, pool);
      if (i > 0)
        svn_stringbuf_appendbyte(buf, ' ');
      if (oid_string)
        svn_stringbuf_appendcstr(buf, oid_string);
      else
        svn_stringbuf_appendcstr(buf, "??");
    }

  if (strcmp(expected, buf->data))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "The oids didn't match expected '%s',"
                             " got '%s' for cert '%s'",
                             expected, buf->data, subject);
  return SVN_NO_ERROR;

}


static svn_error_t *
compare_results(struct x509_test *xt,
                svn_x509_certinfo_t *certinfo,
                apr_pool_t *pool)
{
  const char *v;

  v = svn_x509_certinfo_get_subject(certinfo, pool);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No subject for cert '%s'", xt->subject);
  if (strcmp(v, xt->subject))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Subject didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->subject,
                             xt->subject, v);

  SVN_ERR(compare_oids(xt->subject_oids, svn_x509_certinfo_get_subject_attrs(certinfo),
                       xt->subject, pool));

  v = svn_x509_certinfo_get_issuer(certinfo, pool);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No issuer for cert '%s'", xt->subject);
  if (strcmp(v, xt->issuer))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Issuer didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->subject,
                             xt->issuer, v);

  SVN_ERR(compare_oids(xt->issuer_oids, svn_x509_certinfo_get_issuer_attrs(certinfo),
                       xt->subject, pool));

  SVN_ERR(compare_dates(xt->valid_from,
                        svn_x509_certinfo_get_valid_from(certinfo),
                        "valid-from",
                        xt->subject,
                        pool));

  SVN_ERR(compare_dates(xt->valid_to,
                        svn_x509_certinfo_get_valid_to(certinfo),
                        "valid-to",
                        xt->subject,
                        pool));

  SVN_ERR(compare_hostnames(xt->hostnames,
                            svn_x509_certinfo_get_hostnames(certinfo),
                            xt->subject,
                            pool));

  v = svn_checksum_to_cstring_display(
      svn_x509_certinfo_get_digest(certinfo), pool);
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
      svn_x509_certinfo_t *certinfo;

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

struct x509_broken {
  const char *base64_cert;
  apr_status_t apr_err;
};
static struct x509_broken broken_cert_tests[] = {
  /* Invalid zero-length name that caused a SEGV, found using AFL. */
  {
    "MIIDDTCCAfUCAQEwDQYJKoZIhvcNAQEFBQAwRTEAMAkGA1UEBhMCQVUxEzARBgNV"
    "BAgTClNvbWUtU3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0"
    "ZDAeFw0xNTAxMTkyMjEyNDhaFw0xNjAxMTkyMjEyNDhaMFQxCzAJBgNVBAYTAlVT"
    "MRMwEQYDVQQIEwpXYXNoaW5ndG9uMRMwEQYDVQQHEwpOb3J0aCBCZW5kMRswGQYD"
    "VQQDExJ4NTA5djEuZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAw"
    "ggEKAoIBAQDniW3DmGGtA0MoYqE9H55/RmjtTJD2WVmM/STEsw+RW74UGsZ62qfi"
    "ADedl4ukZYKlk3TwJrGEwDBKOMWHuzCYVxhclyHkHwX7QqamvZRgaOonEu82KHuE"
    "dZo4FhOWDC9D0yS4RFbfqvSu/JG19FYsnRQn1RPFYji6jG9TRwavplVBiMhR68kc"
    "8HTW1Wu7uJ5SV0UtTicFes8MGek3+zWceGt+Egwd2UlIYXwTPzB5m7UPuufEdvFL"
    "ED3pusVatohFzjCbYsuJIR5ppYd49uTxPWGvRidJ2C8GbDf9PCgDduS0Gz91Txnw"
    "h+WiVYCQ6SxAJWp/xeZWE71k88N0vJEzAgMBAAEwDQYJKoZIhvcNAQEFBQADggEB"
    "ABoBaObsHnIrkd3RvvGb5q7fnEfiT1DXsufS3ypf4Z8IST/z+NeaUaiRN1oLcvDz"
    "qC7ygTYZ2BZoEw3ReCGqQWT4iYET+lH8DM+U5val3gVlSWqx1jj/wiV1OAxQsakM"
    "BnmNs/MDshiv54irvSlqnxEp2o/BU/vMrN656C5DJkZpYoMpIWxdFnd+bzNzuN1k"
    "pJfTjzWlGckKfdblNPOfdtccTqtQ5d4mWtYNJ8DfL5rRRwCuzXvZtbVHKxqkXaXr"
    "CYUfFUobapgPfvvMc1QcDY+2nvhC2ij+HAPIHgZPuzJsjZRC1zwg074cfgjZbgbm"
    "R0HVF486p3vS8HFv4lndRZA=",
    SVN_ERR_X509_CERT_INVALID_NAME,
  },
 { NULL }
};

static svn_error_t *
test_x509_parse_cert_broken(apr_pool_t *pool)
{
  struct x509_broken *xt;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (xt = broken_cert_tests; xt->base64_cert; xt++)
    {
      const svn_string_t *der_cert;
      svn_x509_certinfo_t *certinfo;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      /* Convert header-less PEM to DER by undoing base64 encoding. */
      der_cert = svn_base64_decode_string(svn_string_create(xt->base64_cert,
                                                            pool),
                                          iterpool);

      err = svn_x509_parse_cert(&certinfo, der_cert->data, der_cert->len,
                                iterpool, iterpool);
      if (!err)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected parse error E%d got SUCCESS",
                                 xt->apr_err);
      if (err && err->apr_err != xt->apr_err)
        return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                                 "Expected parse error E%d got E%d",
                                 xt->apr_err, err->apr_err);
      svn_error_clear(err);
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
    SVN_TEST_PASS2(test_x509_parse_cert_broken,
                   "test broken certs"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
