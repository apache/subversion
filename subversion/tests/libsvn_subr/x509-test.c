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
  const char *subject; /* Subject in the format that the parser returns */
  const char *issuer; /* Issuer in the format that the parser returns */

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
    "C=US, O=Thawte, Inc., CN=Thawte SSL CA",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=Internet Widgits CA",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=Internet Widgits CA",
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
    "C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=Internet Widgits CA",
    "2014-07-25T18:43:28.000000Z",
    "2015-07-25T18:43:28.000000Z",
    "*.example.com, *.foo.example.com, *.bar.example.com, zig-zag.example.com",
    "3525fb617c232fdc738d736c1cbd5d97b19b51e4"
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
                             "No %s for cert '%s'", subject);

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
compare_results(struct x509_test *xt,
                svn_x509_certinfo_t *certinfo,
                apr_pool_t *pool)
{
  const char *v;

  v = svn_x509_certinfo_get_subject(certinfo);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No subject for cert '%s'", xt->subject);
  if (strcmp(v, xt->subject))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Subject didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->subject,
                             xt->subject, v);

  v = svn_x509_certinfo_get_issuer(certinfo);
  if (!v)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "No issuer for cert '%s'", xt->subject);
  if (strcmp(v, xt->issuer))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Issuer didn't match for cert '%s', "
                             "expected '%s', got '%s'", xt->subject,
                             xt->issuer, v);

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

  v = svn_checksum_to_cstring_display2(
          svn_x509_certinfo_get_digest(certinfo),
          SVN_CHECKSUM_CSTRING_LOWER,
          pool);
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
