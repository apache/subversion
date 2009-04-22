/*
 * fake_sspi.h : Fake header to stand in for Windows' sspi.h header
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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
#ifndef SVN_LIBSVN_RA_SERF_FAKE_SSPI_H
#define SVN_LIBSVN_RA_SERF_FAKE_SSPI_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct {
    int dwLower;
    int dwUpper;
} CtxtHandle;

typedef struct {
  int (*QuerySecurityPackageInfo)(const void *,const void *);
  void (*FreeContextBuffer)(const void *);
  int (*AcquireCredentialsHandle)(const void *, const void *, int,
                                  const void *, const void *,
                                  const void *, const void *,
                                  const void *, const void *);
  void (*CompleteAuthToken)(const void *, const void *);
  int (*InitializeSecurityContext)(const void *, const void *,
                                   const void *, int, int, int, const void *,
                                   int, const void *, const void *,
                                   const void *, const void *);
} * PSecurityFunctionTable;

typedef int SECURITY_STATUS, DWORD, TimeStamp;

#define SEC_E_OK 0
#define ISC_REQ_REPLAY_DETECT 1
#define ISC_REQ_SEQUENCE_DETECT 2
#define ISC_REQ_CONFIDENTIALITY 3
#define ISC_REQ_DELEGATE 4
#define SECURITY_NATIVE_DREP 5
#define SEC_I_COMPLETE_NEEDED 6
#define SEC_I_COMPLETE_AND_CONTINUE 7
#define SEC_I_CONTINUE_NEEDED 8
#define SECBUFFER_TOKEN 9
#define SECBUFFER_VERSION 10
#define SECPKG_CRED_OUTBOUND 11

PSecurityFunctionTable InitSecurityInterface(void);

typedef struct {
  int cbMaxToken;
} SecPkgInfo;

typedef struct {
  int BufferType;
  int cbBuffer;
  const void *pvBuffer;
} SecBuffer;

typedef struct {
  int cBuffers;
  int ulVersion;
  const void *pBuffers;
} SecBufferDesc;

typedef void *CredHandle;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_SERF_FAKE_SSPI_H */
