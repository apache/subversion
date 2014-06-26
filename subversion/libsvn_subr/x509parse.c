/*
 *  X.509 certificate and private key decoding
 *
 *  Based on XySSL: Copyright (C) 2006-2008   Christophe Devine
 *
 *  Copyright (C) 2009  Paul Bakker <polarssl_maintainer at polarssl dot org>
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *    * Neither the names of PolarSSL or XySSL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *  The ITU-T X.509 standard defines a certificat format for PKI.
 *
 *  http://www.ietf.org/rfc/rfc2459.txt
 *  http://www.ietf.org/rfc/rfc3279.txt
 *
 *  ftp://ftp.rsasecurity.com/pub/pkcs/ascii/pkcs-1v2.asc
 *
 *  http://www.itu.int/ITU-T/studygroups/com17/languages/X.680-0207.pdf
 *  http://www.itu.int/ITU-T/studygroups/com17/languages/X.690-0207.pdf
 */

#include <apr_pools.h>
#include <svn_hash.h>

#include "x509.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/*
 * ASN.1 DER decoding routines
 */
static svn_error_t *
asn1_get_len(const unsigned char **p, const unsigned char *end, int *len)
{
  if ((end - *p) < 1)
    return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

  if ((**p & 0x80) == 0)
    *len = *(*p)++;
  else {
    switch (**p & 0x7F) {
    case 1:
      if ((end - *p) < 2)
        return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

      *len = (*p)[1];
      (*p) += 2;
      break;

    case 2:
      if ((end - *p) < 3)
        return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

      *len = ((*p)[1] << 8) | (*p)[2];
      (*p) += 3;
      break;

    default:
      return svn_error_create(SVN_ERR_ASN1_INVALID_LENGTH, NULL, NULL);
      break;
    }
  }

  if (*len > (int)(end - *p))
    return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
asn1_get_tag(const unsigned char **p,
             const unsigned char *end, int *len, int tag)
{
  if ((end - *p) < 1)
    return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

  if (**p != tag)
    return svn_error_create(SVN_ERR_ASN1_UNEXPECTED_TAG, NULL, NULL);

  (*p)++;

  return svn_error_trace(asn1_get_len(p, end, len));
}

static svn_error_t *
asn1_get_int(const unsigned char **p, const unsigned char *end, int *val)
{
  int len;

  SVN_ERR(asn1_get_tag(p, end, &len, ASN1_INTEGER));

  if (len > (int)sizeof(int) || (**p & 0x80) != 0)
    return svn_error_create(SVN_ERR_ASN1_INVALID_LENGTH, NULL, NULL);

  *val = 0;

  while (len-- > 0) {
    *val = (*val << 8) | **p;
    (*p)++;
  }

  return SVN_NO_ERROR;
}

/*
 *  Version   ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
 */
static svn_error_t *
x509_get_version(const unsigned char **p, const unsigned char *end, int *ver)
{
  svn_error_t *err;
  int len;

  err = asn1_get_tag(p, end, &len,
                     ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | 0);
  if (err)
    {
      if (err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
        {
          svn_error_clear(err);
          *ver = 0;
          return SVN_NO_ERROR;
        }

      return svn_error_trace(err);
    }

  end = *p + len;

  err = asn1_get_int(p, end, ver);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_VERSION, err, NULL);

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_VERSION, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
    }

  return SVN_NO_ERROR;
}

/*
 *  CertificateSerialNumber   ::=  INTEGER
 */
static svn_error_t *
x509_get_serial(const unsigned char **p,
                const unsigned char *end, x509_buf * serial)
{
  svn_error_t *err;

  if ((end - *p) < 1)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_SERIAL, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, err, NULL);
    }

  if (**p != (ASN1_CONTEXT_SPECIFIC | ASN1_PRIMITIVE | 2) &&
      **p != ASN1_INTEGER)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_SERIAL, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_UNEXPECTED_TAG, err, NULL);
    }

  serial->tag = *(*p)++;

  err = asn1_get_len(p, end, &serial->len);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_SERIAL, err, NULL);

  serial->p = *p;
  *p += serial->len;

  return SVN_NO_ERROR;
}

/*
 *  AlgorithmIdentifier   ::=  SEQUENCE  {
 *     algorithm         OBJECT IDENTIFIER,
 *     parameters         ANY DEFINED BY algorithm OPTIONAL  }
 */
static svn_error_t *
x509_get_alg(const unsigned char **p, const unsigned char *end, x509_buf * alg)
{
  svn_error_t *err;
  int len;

  err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);

  end = *p + len;
  alg->tag = **p;

  err = asn1_get_tag(p, end, &alg->len, ASN1_OID);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);

  alg->p = *p;
  *p += alg->len;

  if (*p == end)
    return SVN_NO_ERROR;

  /*
   * assume the algorithm parameters must be NULL
   */
  err = asn1_get_tag(p, end, &len, ASN1_NULL);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
    }

  return SVN_NO_ERROR;
}

/*
 *  RelativeDistinguishedName ::=
 *    SET OF AttributeTypeAndValue
 *
 *  AttributeTypeAndValue ::= SEQUENCE {
 *    type     AttributeType,
 *    value     AttributeValue }
 *
 *  AttributeType ::= OBJECT IDENTIFIER
 *
 *  AttributeValue ::= ANY DEFINED BY AttributeType
 */
static svn_error_t *
x509_get_name(const unsigned char **p, const unsigned char *end, x509_name * cur)
{
  svn_error_t *err;
  int len;
  const unsigned char *end2;
  x509_buf *oid;
  x509_buf *val;

  err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SET);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  end2 = end;
  end = *p + len;

  err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  if (*p + len != end)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
    }

  oid = &cur->oid;
  oid->tag = **p;

  err = asn1_get_tag(p, end, &oid->len, ASN1_OID);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  oid->p = *p;
  *p += oid->len;

  if ((end - *p) < 1)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, err, NULL);
    }

  if (**p != ASN1_BMP_STRING && **p != ASN1_UTF8_STRING &&
      **p != ASN1_T61_STRING && **p != ASN1_PRINTABLE_STRING &&
      **p != ASN1_IA5_STRING && **p != ASN1_UNIVERSAL_STRING)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_UNEXPECTED_TAG, err, NULL);
    }

  val = &cur->val;
  val->tag = *(*p)++;

  err = asn1_get_len(p, end, &val->len);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  val->p = *p;
  *p += val->len;

  cur->next = NULL;

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
    }

  /*
   * recurse until end of SEQUENCE is reached
   */
  if (*p == end2)
    return SVN_NO_ERROR;

  cur->next = (x509_name *) malloc(sizeof(x509_name));

  if (cur->next == NULL)
    return SVN_NO_ERROR;

  return svn_error_trace(x509_get_name(p, end2, cur->next));
}

/*
 *  Validity ::= SEQUENCE {
 *     notBefore    Time,
 *     notAfter    Time }
 *
 *  Time ::= CHOICE {
 *     utcTime    UTCTime,
 *     generalTime  GeneralizedTime }
 */
static svn_error_t *
x509_get_dates(const unsigned char **p,
               const unsigned char *end, x509_time * from, x509_time * to)
{
  svn_error_t *err;
  int len;
  char date[64];

  err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, err, NULL);

  end = *p + len;

  /*
   * TODO: also handle GeneralizedTime
   */
  err = asn1_get_tag(p, end, &len, ASN1_UTC_TIME);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, err, NULL);

  memset(date, 0, sizeof(date));
  memcpy(date, *p, (len < (int)sizeof(date) - 1) ?
         len : (int)sizeof(date) - 1);

  if (sscanf(date, "%2d%2d%2d%2d%2d%2d",
       &from->year, &from->mon, &from->day,
       &from->hour, &from->min, &from->sec) < 5)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);

  from->year += 100 * (from->year < 90);
  from->year += 1900;

  *p += len;

  err = asn1_get_tag(p, end, &len, ASN1_UTC_TIME);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, err, NULL);

  memset(date, 0, sizeof(date));
  memcpy(date, *p, (len < (int)sizeof(date) - 1) ?
         len : (int)sizeof(date) - 1);

  if (sscanf(date, "%2d%2d%2d%2d%2d%2d",
       &to->year, &to->mon, &to->day,
       &to->hour, &to->min, &to->sec) < 5)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);

  to->year += 100 * (to->year < 90);
  to->year += 1900;

  *p += len;

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
x509_get_sig(const unsigned char **p, const unsigned char *end, x509_buf * sig)
{
  svn_error_t *err;
  int len;

  sig->tag = **p;

  err = asn1_get_tag(p, end, &len, ASN1_BIT_STRING);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_SIGNATURE, err, NULL);

  if (--len < 1 || *(*p)++ != 0)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_SIGNATURE, NULL, NULL);

  sig->len = len;
  sig->p = *p;

  *p += len;

  return SVN_NO_ERROR;
}

/*
 * X.509 v2/v3 unique identifier (not parsed)
 */
static svn_error_t *
x509_get_uid(const unsigned char **p,
             const unsigned char *end, x509_buf * uid, int n)
{
  svn_error_t *err;

  if (*p == end)
    return SVN_NO_ERROR;

  uid->tag = **p;

  err = asn1_get_tag(p, end, &uid->len,
        ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | n);
  if (err)
    {
      if (err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }

      return svn_error_trace(err);
    }

  uid->p = *p;
  *p += uid->len;

  return SVN_NO_ERROR;
}

/*
 * X.509 v3 extensions (not parsed)
 */
static svn_error_t *
x509_skip_ext(const unsigned char **p,
             const unsigned char *end)
{
  svn_error_t *err;
  int len;

  if (*p == *end)
    return SVN_NO_ERROR;

  err = asn1_get_tag(p, end, &len,
                     ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | 3);
  if (err)
    {
      if (err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }

      return svn_error_trace(err);
    }

  /* Skip extensions */
  *p += len;

  return SVN_NO_ERROR;
}

/*
 * Store the name in printable form into buf; no more
 * than (end - buf) characters will be written
 */
static int x509parse_dn_gets(char *buf, const char *end, const x509_name * dn)
{
  int i;
  unsigned char c;
  const x509_name *name;
  char s[128], *p;

  memset(s, 0, sizeof(s));

  name = dn;
  p = buf;

  while (name != NULL) {
    if (name != dn)
      p += snprintf(p, end - p, ", ");

    if (memcmp(name->oid.p, OID_X520, 2) == 0) {
      switch (name->oid.p[2]) {
      case X520_COMMON_NAME:
        p += snprintf(p, end - p, "CN=");
        break;

      case X520_COUNTRY:
        p += snprintf(p, end - p, "C=");
        break;

      case X520_LOCALITY:
        p += snprintf(p, end - p, "L=");
        break;

      case X520_STATE:
        p += snprintf(p, end - p, "ST=");
        break;

      case X520_ORGANIZATION:
        p += snprintf(p, end - p, "O=");
        break;

      case X520_ORG_UNIT:
        p += snprintf(p, end - p, "OU=");
        break;

      default:
        p += snprintf(p, end - p, "0x%02X=",
                name->oid.p[2]);
        break;
      }
    } else if (memcmp(name->oid.p, OID_PKCS9, 8) == 0) {
      switch (name->oid.p[8]) {
      case PKCS9_EMAIL:
        p += snprintf(p, end - p, "emailAddress=");
        break;

      default:
        p += snprintf(p, end - p, "0x%02X=",
                name->oid.p[8]);
        break;
      }
    } else
      p += snprintf(p, end - p, "\?\?=");

    for (i = 0; i < name->val.len; i++) {
      if (i >= (int)sizeof(s) - 1)
        break;

      c = name->val.p[i];
      if (c < 32 || c == 127 || (c > 128 && c < 160))
        s[i] = '?';
      else
        s[i] = c;
    }
    s[i] = '\0';
    p += snprintf(p, end - p, "%s", s);
    name = name->next;
  }

  return (p - buf);
}

/*
 * Parse one certificate.
 */
svn_error_t *
svn_x509_parse_cert(apr_hash_t **certinfo,
                    const char *buf,
                    int buflen,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  int len;
  const unsigned char *p;
  const unsigned char *end;
  x509_cert *crt;
  char name[1024];

  crt = apr_pcalloc(scratch_pool, sizeof(*crt));
  p = (const unsigned char *)buf;
  len = buflen;
  end = p + len;

  /*
   * Certificate  ::=      SEQUENCE  {
   *              tbsCertificate           TBSCertificate,
   *              signatureAlgorithm       AlgorithmIdentifier,
   *              signatureValue           BIT STRING      }
   */
  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, NULL, NULL);

  if (len != (int)(end - p))
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
    }

  /*
   * TBSCertificate  ::=  SEQUENCE  {
   */
  crt->tbs.p = p;

  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  end = p + len;
  crt->tbs.len = end - crt->tbs.p;

  /*
   * Version      ::=      INTEGER  {      v1(0), v2(1), v3(2)  }
   *
   * CertificateSerialNumber      ::=      INTEGER
   *
   * signature                    AlgorithmIdentifier
   */
  SVN_ERR(x509_get_version(&p, end, &crt->version));
  SVN_ERR(x509_get_serial(&p, end, &crt->serial));
  SVN_ERR(x509_get_alg(&p, end, &crt->sig_oid1));

  crt->version++;

  if (crt->version > 3) {
    return svn_error_create(SVN_ERR_X509_CERT_UNKNOWN_VERSION, NULL, NULL);
  }

  if (crt->sig_oid1.len != 9 ||
      memcmp(crt->sig_oid1.p, OID_PKCS1, 8) != 0) {
    return svn_error_create(SVN_ERR_X509_CERT_UNKNOWN_SIG_ALG, NULL, NULL);
  }

  if (crt->sig_oid1.p[8] < 2 || crt->sig_oid1.p[8] > 5) {
    return svn_error_create(SVN_ERR_X509_CERT_UNKNOWN_SIG_ALG, NULL, NULL);
  }

  /*
   * issuer                               Name
   */
  crt->issuer_raw.p = p;

  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  SVN_ERR(x509_get_name(&p, p + len, &crt->issuer));

  crt->issuer_raw.len = p - crt->issuer_raw.p;

  /*
   * Validity ::= SEQUENCE {
   *              notBefore          Time,
   *              notAfter           Time }
   *
   */
  SVN_ERR(x509_get_dates(&p, end, &crt->valid_from, &crt->valid_to));

  /*
   * subject                              Name
   */
  crt->subject_raw.p = p;

  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  SVN_ERR(x509_get_name(&p, p + len, &crt->subject));

  crt->subject_raw.len = p - crt->subject_raw.p;

  /*
   * SubjectPublicKeyInfo  ::=  SEQUENCE
   *              algorithm                        AlgorithmIdentifier,
   *              subjectPublicKey         BIT STRING      }
   */
  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  /* Skip pubkey. */
  p += len;

  /*
   *      issuerUniqueID  [1]      IMPLICIT UniqueIdentifier OPTIONAL,
   *                                               -- If present, version shall be v2 or v3
   *      subjectUniqueID [2]      IMPLICIT UniqueIdentifier OPTIONAL,
   *                                               -- If present, version shall be v2 or v3
   *      extensions              [3]      EXPLICIT Extensions OPTIONAL
   *                                               -- If present, version shall be v3
   */
  if (crt->version == 2 || crt->version == 3) {
    SVN_ERR(x509_get_uid(&p, end, &crt->issuer_id, 1));
  }

  if (crt->version == 2 || crt->version == 3) {
    SVN_ERR(x509_get_uid(&p, end, &crt->subject_id, 2));
  }

  if (crt->version == 3) {
    SVN_ERR(x509_skip_ext(&p, end));
  }

  if (p != end) {
    err = svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, NULL, NULL);
    return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
  }

  end = buf + buflen;

  /*
   *      signatureAlgorithm       AlgorithmIdentifier,
   *      signatureValue           BIT STRING
   */
  SVN_ERR(x509_get_alg(&p, end, &crt->sig_oid2));

  if (memcmp(crt->sig_oid1.p, crt->sig_oid2.p, 9) != 0) {
    return svn_error_create(SVN_ERR_X509_CERT_SIG_MISMATCH, NULL, NULL);
  }

  SVN_ERR(x509_get_sig(&p, end, &crt->sig));

  if (p != end)
    {
      err = svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, NULL, NULL);
      return svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, err, NULL);
    }

  *certinfo = apr_hash_make(result_pool);

  x509parse_dn_gets(name, name + sizeof(name), &crt->issuer);
  svn_hash_sets(*certinfo, SVN_X509_CERTINFO_KEY_ISSUER,
                apr_pstrdup(result_pool, name));

  svn_hash_sets(*certinfo, SVN_X509_CERTINFO_KEY_VALID_FROM,
                apr_psprintf(result_pool,
                             "%4d/%02d/%02d %02d:%02d:%02d",
                             crt->valid_from.year,
                             crt->valid_from.mon,
                             crt->valid_from.day,
                             crt->valid_from.hour,
                             crt->valid_from.min,
                             crt->valid_from.sec));

  svn_hash_sets(*certinfo, SVN_X509_CERTINFO_KEY_VALID_TO,
                apr_psprintf(result_pool,
                             "%4d/%02d/%02d %02d:%02d:%02d",
                             crt->valid_to.year,
                             crt->valid_to.mon,
                             crt->valid_to.day,
                             crt->valid_to.hour,
                             crt->valid_to.min,
                             crt->valid_to.sec));
  return SVN_NO_ERROR;
}

