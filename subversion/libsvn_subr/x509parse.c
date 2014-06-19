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

#include "x509.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/*
 * ASN.1 DER decoding routines
 */
static int asn1_get_len(unsigned char **p, const unsigned char *end, int *len)
{
  if ((end - *p) < 1)
    return (TROPICSSL_ERR_ASN1_OUT_OF_DATA);

  if ((**p & 0x80) == 0)
    *len = *(*p)++;
  else {
    switch (**p & 0x7F) {
    case 1:
      if ((end - *p) < 2)
        return (TROPICSSL_ERR_ASN1_OUT_OF_DATA);

      *len = (*p)[1];
      (*p) += 2;
      break;

    case 2:
      if ((end - *p) < 3)
        return (TROPICSSL_ERR_ASN1_OUT_OF_DATA);

      *len = ((*p)[1] << 8) | (*p)[2];
      (*p) += 3;
      break;

    default:
      return (TROPICSSL_ERR_ASN1_INVALID_LENGTH);
      break;
    }
  }

  if (*len > (int)(end - *p))
    return (TROPICSSL_ERR_ASN1_OUT_OF_DATA);

  return (0);
}

static int asn1_get_tag(unsigned char **p,
      const unsigned char *end, int *len, int tag)
{
  if ((end - *p) < 1)
    return (TROPICSSL_ERR_ASN1_OUT_OF_DATA);

  if (**p != tag)
    return (TROPICSSL_ERR_ASN1_UNEXPECTED_TAG);

  (*p)++;

  return (asn1_get_len(p, end, len));
}

static int asn1_get_int(unsigned char **p, const unsigned char *end, int *val)
{
  int ret, len;

  if ((ret = asn1_get_tag(p, end, &len, ASN1_INTEGER)) != 0)
    return (ret);

  if (len > (int)sizeof(int) || (**p & 0x80) != 0)
    return (TROPICSSL_ERR_ASN1_INVALID_LENGTH);

  *val = 0;

  while (len-- > 0) {
    *val = (*val << 8) | **p;
    (*p)++;
  }

  return (0);
}

/*
 *  Version   ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
 */
static int x509_get_version(unsigned char **p, const unsigned char *end, int *ver)
{
  int ret, len;

  if ((ret = asn1_get_tag(p, end, &len,
        ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | 0))
      != 0) {
    if (ret == TROPICSSL_ERR_ASN1_UNEXPECTED_TAG)
      return (*ver = 0);

    return (ret);
  }

  end = *p + len;

  if ((ret = asn1_get_int(p, end, ver)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_VERSION | ret);

  if (*p != end)
    return (TROPICSSL_ERR_X509_CERT_INVALID_VERSION |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);

  return (0);
}

/*
 *  CertificateSerialNumber   ::=  INTEGER
 */
static int x509_get_serial(unsigned char **p,
         const unsigned char *end, x509_buf * serial)
{
  int ret;

  if ((end - *p) < 1)
    return (TROPICSSL_ERR_X509_CERT_INVALID_SERIAL |
      TROPICSSL_ERR_ASN1_OUT_OF_DATA);

  if (**p != (ASN1_CONTEXT_SPECIFIC | ASN1_PRIMITIVE | 2) &&
      **p != ASN1_INTEGER)
    return (TROPICSSL_ERR_X509_CERT_INVALID_SERIAL |
      TROPICSSL_ERR_ASN1_UNEXPECTED_TAG);

  serial->tag = *(*p)++;

  if ((ret = asn1_get_len(p, end, &serial->len)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_SERIAL | ret);

  serial->p = *p;
  *p += serial->len;

  return (0);
}

/*
 *  AlgorithmIdentifier   ::=  SEQUENCE  {
 *     algorithm         OBJECT IDENTIFIER,
 *     parameters         ANY DEFINED BY algorithm OPTIONAL  }
 */
static int x509_get_alg(unsigned char **p, const unsigned char *end, x509_buf * alg)
{
  int ret, len;

  if ((ret = asn1_get_tag(p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_ALG | ret);

  end = *p + len;
  alg->tag = **p;

  if ((ret = asn1_get_tag(p, end, &alg->len, ASN1_OID)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_ALG | ret);

  alg->p = *p;
  *p += alg->len;

  if (*p == end)
    return (0);

  /*
   * assume the algorithm parameters must be NULL
   */
  if ((ret = asn1_get_tag(p, end, &len, ASN1_NULL)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_ALG | ret);

  if (*p != end)
    return (TROPICSSL_ERR_X509_CERT_INVALID_ALG |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);

  return (0);
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
static int x509_get_name(unsigned char **p, const unsigned char *end, x509_name * cur)
{
  int ret, len;
  const unsigned char *end2;
  x509_buf *oid;
  x509_buf *val;

  if ((ret = asn1_get_tag(p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SET)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME | ret);

  end2 = end;
  end = *p + len;

  if ((ret = asn1_get_tag(p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME | ret);

  if (*p + len != end)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);

  oid = &cur->oid;
  oid->tag = **p;

  if ((ret = asn1_get_tag(p, end, &oid->len, ASN1_OID)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME | ret);

  oid->p = *p;
  *p += oid->len;

  if ((end - *p) < 1)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME |
      TROPICSSL_ERR_ASN1_OUT_OF_DATA);

  if (**p != ASN1_BMP_STRING && **p != ASN1_UTF8_STRING &&
      **p != ASN1_T61_STRING && **p != ASN1_PRINTABLE_STRING &&
      **p != ASN1_IA5_STRING && **p != ASN1_UNIVERSAL_STRING)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME |
      TROPICSSL_ERR_ASN1_UNEXPECTED_TAG);

  val = &cur->val;
  val->tag = *(*p)++;

  if ((ret = asn1_get_len(p, end, &val->len)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME | ret);

  val->p = *p;
  *p += val->len;

  cur->next = NULL;

  if (*p != end)
    return (TROPICSSL_ERR_X509_CERT_INVALID_NAME |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);

  /*
   * recurse until end of SEQUENCE is reached
   */
  if (*p == end2)
    return (0);

  cur->next = (x509_name *) malloc(sizeof(x509_name));

  if (cur->next == NULL)
    return (1);

  return (x509_get_name(p, end2, cur->next));
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
static int x509_get_dates(unsigned char **p,
        const unsigned char *end, x509_time * from, x509_time * to)
{
  int ret, len;
  char date[64];

  if ((ret = asn1_get_tag(p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_DATE | ret);

  end = *p + len;

  /*
   * TODO: also handle GeneralizedTime
   */
  if ((ret = asn1_get_tag(p, end, &len, ASN1_UTC_TIME)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_DATE | ret);

  memset(date, 0, sizeof(date));
  memcpy(date, *p, (len < (int)sizeof(date) - 1) ?
         len : (int)sizeof(date) - 1);

  if (sscanf(date, "%2d%2d%2d%2d%2d%2d",
       &from->year, &from->mon, &from->day,
       &from->hour, &from->min, &from->sec) < 5)
    return (TROPICSSL_ERR_X509_CERT_INVALID_DATE);

  from->year += 100 * (from->year < 90);
  from->year += 1900;

  *p += len;

  if ((ret = asn1_get_tag(p, end, &len, ASN1_UTC_TIME)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_DATE | ret);

  memset(date, 0, sizeof(date));
  memcpy(date, *p, (len < (int)sizeof(date) - 1) ?
         len : (int)sizeof(date) - 1);

  if (sscanf(date, "%2d%2d%2d%2d%2d%2d",
       &to->year, &to->mon, &to->day,
       &to->hour, &to->min, &to->sec) < 5)
    return (TROPICSSL_ERR_X509_CERT_INVALID_DATE);

  to->year += 100 * (to->year < 90);
  to->year += 1900;

  *p += len;

  if (*p != end)
    return (TROPICSSL_ERR_X509_CERT_INVALID_DATE |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);

  return (0);
}

static int x509_get_sig(unsigned char **p, const unsigned char *end, x509_buf * sig)
{
  int ret, len;

  sig->tag = **p;

  if ((ret = asn1_get_tag(p, end, &len, ASN1_BIT_STRING)) != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_SIGNATURE | ret);

  if (--len < 1 || *(*p)++ != 0)
    return (TROPICSSL_ERR_X509_CERT_INVALID_SIGNATURE);

  sig->len = len;
  sig->p = *p;

  *p += len;

  return (0);
}

/*
 * X.509 v2/v3 unique identifier (not parsed)
 */
static int x509_get_uid(unsigned char **p,
      const unsigned char *end, x509_buf * uid, int n)
{
  int ret;

  if (*p == end)
    return (0);

  uid->tag = **p;

  if ((ret = asn1_get_tag(p, end, &uid->len,
        ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | n))
      != 0) {
    if (ret == TROPICSSL_ERR_ASN1_UNEXPECTED_TAG)
      return (0);

    return (ret);
  }

  uid->p = *p;
  *p += uid->len;

  return (0);
}

/*
 * Parse one certificate.
 */
int
svn_x509_parse_cert(x509_cert **cert,
                    const unsigned char *buf,
                    int buflen,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  int ret, len;
  unsigned char *p;
  const unsigned char *end;
  x509_cert *crt;

  crt = apr_pcalloc(result_pool, sizeof(*crt));

  /*
   * Copy the raw DER data.
   */
  p = (unsigned char *)malloc(len = buflen);

  if (p == NULL)
    return (1);

  memcpy(p, buf, buflen);

  buflen = 0;

  crt->raw.p = p;
  crt->raw.len = len;
  end = p + len;

  /*
   * Certificate  ::=      SEQUENCE  {
   *              tbsCertificate           TBSCertificate,
   *              signatureAlgorithm       AlgorithmIdentifier,
   *              signatureValue           BIT STRING      }
   */
  if ((ret = asn1_get_tag(&p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT);
  }

  if (len != (int)(end - p)) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);
  }

  /*
   * TBSCertificate  ::=  SEQUENCE  {
   */
  crt->tbs.p = p;

  if ((ret = asn1_get_tag(&p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT | ret);
  }

  end = p + len;
  crt->tbs.len = end - crt->tbs.p;

  /*
   * Version      ::=      INTEGER  {      v1(0), v2(1), v3(2)  }
   *
   * CertificateSerialNumber      ::=      INTEGER
   *
   * signature                    AlgorithmIdentifier
   */
  if ((ret = x509_get_version(&p, end, &crt->version)) != 0 ||
      (ret = x509_get_serial(&p, end, &crt->serial)) != 0 ||
      (ret = x509_get_alg(&p, end, &crt->sig_oid1)) != 0) {
    return (ret);
  }

  crt->version++;

  if (crt->version > 3) {
    return (TROPICSSL_ERR_X509_CERT_UNKNOWN_VERSION);
  }

  if (crt->sig_oid1.len != 9 ||
      memcmp(crt->sig_oid1.p, OID_PKCS1, 8) != 0) {
    return (TROPICSSL_ERR_X509_CERT_UNKNOWN_SIG_ALG);
  }

  if (crt->sig_oid1.p[8] < 2 || crt->sig_oid1.p[8] > 5) {
    return (TROPICSSL_ERR_X509_CERT_UNKNOWN_SIG_ALG);
  }

  /*
   * issuer                               Name
   */
  crt->issuer_raw.p = p;

  if ((ret = asn1_get_tag(&p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT | ret);
  }

  if ((ret = x509_get_name(&p, p + len, &crt->issuer)) != 0) {
    return (ret);
  }

  crt->issuer_raw.len = p - crt->issuer_raw.p;

  /*
   * Validity ::= SEQUENCE {
   *              notBefore          Time,
   *              notAfter           Time }
   *
   */
  if ((ret = x509_get_dates(&p, end, &crt->valid_from,
          &crt->valid_to)) != 0) {
    return (ret);
  }

  /*
   * subject                              Name
   */
  crt->subject_raw.p = p;

  if ((ret = asn1_get_tag(&p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT | ret);
  }

  if ((ret = x509_get_name(&p, p + len, &crt->subject)) != 0) {
    return (ret);
  }

  crt->subject_raw.len = p - crt->subject_raw.p;

  /*
   * SubjectPublicKeyInfo  ::=  SEQUENCE
   *              algorithm                        AlgorithmIdentifier,
   *              subjectPublicKey         BIT STRING      }
   */
  if ((ret = asn1_get_tag(&p, end, &len,
        ASN1_CONSTRUCTED | ASN1_SEQUENCE)) != 0) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT | ret);
  }

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
    ret = x509_get_uid(&p, end, &crt->issuer_id, 1);
    if (ret != 0) {
      return (ret);
    }
  }

  if (crt->version == 2 || crt->version == 3) {
    ret = x509_get_uid(&p, end, &crt->subject_id, 2);
    if (ret != 0) {
      return (ret);
    }
  }

  if (p != end) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);
  }

  end = crt->raw.p + crt->raw.len;

  /*
   *      signatureAlgorithm       AlgorithmIdentifier,
   *      signatureValue           BIT STRING
   */
  if ((ret = x509_get_alg(&p, end, &crt->sig_oid2)) != 0) {
    return (ret);
  }

  if (memcmp(crt->sig_oid1.p, crt->sig_oid2.p, 9) != 0) {
    return (TROPICSSL_ERR_X509_CERT_SIG_MISMATCH);
  }

  if ((ret = x509_get_sig(&p, end, &crt->sig)) != 0) {
    return (ret);
  }

  if (p != end) {
    return (TROPICSSL_ERR_X509_CERT_INVALID_FORMAT |
      TROPICSSL_ERR_ASN1_LENGTH_MISMATCH);
  }

  return (0);
}
