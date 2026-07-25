/**
 * @copyright
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
 * @endcopyright
 *
 * @file ClientContext.cpp
 * @brief Implementation of the class ClientContext
 */

#include "svn_client.h"
#include "private/svn_wc_private.h"

#include "ClientContext.h"
#include "JNIUtil.h"
#include "JNICriticalSection.h"

#include "Prompter.h"
#include "CreateJ.h"
#include "EnumMapper.h"
#include "CommitMessage.h"

#include "svn_private_config.h"

ClientContext::ClientContext(jobject jsvnclient, SVN::Pool &pool)
    : OperationContext(pool)
{
    static jfieldID ctxFieldID = 0;
    attachJavaObject(jsvnclient, JAVAHL_ARG("/SVNClient$ClientContext;"), "clientContext", &ctxFieldID);

    SVN_JNI_ERR(svn_client_create_context2(&m_context, NULL,
                                           pool.getPool()),
                );

    /* Clear the wc_ctx as we don't want to maintain this unconditionally
       for compatibility reasons */
    SVN_JNI_ERR(svn_wc_context_destroy(m_context->wc_ctx),
                );
    m_context->wc_ctx = NULL;

    /* None of the following members change during the lifetime of
       this object. */
    m_context->notify_func = NULL;
    m_context->notify_baton = NULL;
    m_context->log_msg_func3 = CommitMessage::callback;
    m_context->log_msg_baton3 = NULL;
    m_context->cancel_func = checkCancel;
    m_context->cancel_baton = this;
    m_context->notify_func2= notify;
    m_context->notify_baton2 = m_jctx;
    m_context->progress_func = progress;
    m_context->progress_baton = m_jctx;
    m_context->conflict_func2 = resolve;
    m_context->conflict_baton2 = m_jctx;

    m_context->client_name = getClientName();

    if (m_jtunnelcb)
      {
        m_context->check_tunnel_func = checkTunnel;
        m_context->open_tunnel_func = openTunnel;
        m_context->tunnel_baton = m_jtunnelcb;
      }
}

ClientContext::~ClientContext()
{
}

void ClientContext::setTunnelCallback(jobject jtunnelcb)
{
  OperationContext::setTunnelCallback(jtunnelcb);
  if (m_jtunnelcb)
    {
      m_context->check_tunnel_func = checkTunnel;
      m_context->open_tunnel_func = openTunnel;
      m_context->tunnel_baton = m_jtunnelcb;
    }
  else
    {
      m_context->check_tunnel_func = NULL;
      m_context->open_tunnel_func = NULL;
      m_context->tunnel_baton = NULL;
    }
}

/* Helper function to make sure that we don't keep dangling pointers in ctx.
   Note that this function might be called multiple times if getContext()
   is called on the same pool.

   The use of this function assumes a proper subpool behavior by its user,
   (read: SVNClient) usually per request.
 */
extern "C" {

struct clearctx_baton_t
{
  svn_client_ctx_t *ctx;
  svn_client_ctx_t *backup;
};

static apr_status_t clear_ctx_ptrs(void *ptr)
{
    clearctx_baton_t *bt = reinterpret_cast<clearctx_baton_t*>(ptr);

    /* Reset all values to those before overwriting by getContext. */
    *bt->ctx = *bt->backup;

    return APR_SUCCESS;
}

};

svn_client_ctx_t *
ClientContext::getContext(CommitMessage *message, SVN::Pool &in_pool)
{
    apr_pool_t *pool = in_pool.getPool();
    svn_client_ctx_t *ctx = m_context;

    /* Make a temporary copy of ctx to restore at pool cleanup to avoid
       leaving references to dangling pointers.

       Note that this allows creating a stack of context changes if
       the function is invoked multiple times with different pools.
     */
    clearctx_baton_t *bt =
      reinterpret_cast<clearctx_baton_t *>(apr_pcalloc(pool, sizeof(*bt)));
    bt->ctx = ctx;
    bt->backup =
      reinterpret_cast<svn_client_ctx_t*>(apr_pmemdup(pool, ctx, sizeof(*ctx)));
    apr_pool_cleanup_register(in_pool.getPool(), bt, clear_ctx_ptrs,
                              clear_ctx_ptrs);

    if (!ctx->config)
      {
        apr_hash_t * configData = getConfigData();

        ctx->config = configData;
        bt->backup->config = ctx->config;
      }

    ctx->auth_baton = getAuthBaton(in_pool);
    ctx->log_msg_baton3 = message;
    resetCancelRequest();

    SVN_JNI_ERR(svn_wc_context_create(&ctx->wc_ctx, NULL,
                                      in_pool.getPool(), in_pool.getPool()),
                NULL);

    return ctx;
}

void
ClientContext::notify(void *baton,
                      const svn_wc_notify_t *notify,
                      apr_pool_t *pool)
{
  jobject jctx = (jobject) baton;
  JNIEnv *env = JNIUtil::getEnv();

  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->GetObjectClass(jctx);
      if (JNIUtil::isJavaExceptionThrown())
        return;

      mid = env->GetMethodID(clazz, "onNotify",
                             "(" JAVAHL_ARG("/ClientNotifyInformation;") ")V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return;

      env->DeleteLocalRef(clazz);
    }

  jobject jInfo = CreateJ::ClientNotifyInformation(notify);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->CallVoidMethod(jctx, mid, jInfo);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->DeleteLocalRef(jInfo);
}

svn_error_t *
ClientContext::resolve(svn_wc_conflict_result_t **result,
                       const svn_wc_conflict_description2_t *desc,
                       void *baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  jobject jctx = (jobject) baton;
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->GetObjectClass(jctx);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN(SVN_NO_ERROR);

      mid = env->GetMethodID(clazz, "resolve",
                             "(" JAVAHL_ARG("/ConflictDescriptor;") ")"
                             JAVAHL_ARG("/ConflictResult;"));
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        POP_AND_RETURN(SVN_NO_ERROR);
    }

  // Create an instance of the conflict descriptor.
  jobject jdesc = CreateJ::ConflictDescriptor(desc);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN(SVN_NO_ERROR);

  // Invoke the Java conflict resolver callback method using the descriptor.
  jobject jresult = env->CallObjectMethod(jctx, mid, jdesc);
  if (JNIUtil::isJavaExceptionThrown())
    {
      // If an exception is thrown by our conflict resolver, remove it
      // from the JNI env, and convert it into a Subversion error.
      SVN::Pool tmpPool(scratch_pool);
      const char *msg = JNIUtil::thrownExceptionToCString(tmpPool);
      svn_error_t *err = svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                          NULL, msg);
      env->PopLocalFrame(NULL);
      return err;
    }

  *result = javaResultToC(jresult, result_pool);
  if (*result == NULL)
    {
      // Unable to convert the result into a C representation.
      env->PopLocalFrame(NULL);
      return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL, NULL);
    }

  env->PopLocalFrame(NULL);
  return SVN_NO_ERROR;
}

svn_wc_conflict_result_t *
ClientContext::javaResultToC(jobject jresult, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  static jmethodID getChoice = 0;
  static jmethodID getMergedPath = 0;

  jclass clazz = NULL;
  if (getChoice == 0 || getMergedPath == 0)
    {
      clazz = env->FindClass(JAVAHL_CLASS("/ConflictResult"));
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  if (getChoice == 0)
    {
      getChoice = env->GetMethodID(clazz, "getChoice",
                                   "()" JAVAHL_ARG("/ConflictResult$Choice;"));
      if (JNIUtil::isJavaExceptionThrown() || getChoice == 0)
        POP_AND_RETURN_NULL;
    }
  if (getMergedPath == 0)
    {
      getMergedPath = env->GetMethodID(clazz, "getMergedPath",
                                       "()Ljava/lang/String;");
      if (JNIUtil::isJavaExceptionThrown() || getMergedPath == 0)
        POP_AND_RETURN_NULL;
    }

  jobject jchoice = env->CallObjectMethod(jresult, getChoice);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jmergedPath = (jstring) env->CallObjectMethod(jresult, getMergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  JNIStringHolder mergedPath(jmergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  svn_wc_conflict_result_t *result =
         svn_wc_create_conflict_result(EnumMapper::toConflictChoice(jchoice),
                                       mergedPath.pstrdup(pool),
                                       pool);

  env->PopLocalFrame(NULL);
  return result;
}
