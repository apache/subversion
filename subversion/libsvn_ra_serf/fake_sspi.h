/*
 * fake_sspi.h : Fake header to stand in for Windows' sspi.h header
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
