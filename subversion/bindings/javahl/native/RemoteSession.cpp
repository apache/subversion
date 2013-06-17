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
 * @file RemoteSession.cpp
 * @brief Implementation of the class RemoteSession
 */

#include <cstring>
#include <set>

#include "JNIStringHolder.h"
#include "JNIUtil.h"

#include "svn_ra.h"

#include "CreateJ.h"
#include "EnumMapper.h"
#include "Prompter.h"
#include "Revision.h"
#include "RemoteSession.h"

#include "svn_private_config.h"

#define JAVA_CLASS_REMOTE_SESSION JAVA_PACKAGE "/remote/RemoteSession"

RemoteSession *
RemoteSession::getCppObject(jobject jthis)
{
  static jfieldID fid = 0;
  jlong cppAddr = SVNBase::findCppAddrForJObject(jthis, &fid,
      JAVA_CLASS_REMOTE_SESSION);
  return (cppAddr == 0 ? NULL : reinterpret_cast<RemoteSession *>(cppAddr));
}

jobject
RemoteSession::open(jint jretryAttempts,
                    jstring jurl, jstring juuid,
                    jstring jconfigDirectory,
                    jstring jusername, jstring jpassword,
                    jobject jprompter, jobject jprogress)
{
  JNIEnv *env = JNIUtil::getEnv();

  JNIStringHolder url(jurl);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jurl);

  JNIStringHolder uuid(juuid);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  env->DeleteLocalRef(juuid);

  JNIStringHolder configDirectory(jconfigDirectory);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jconfigDirectory);

  JNIStringHolder usernameStr(jusername);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jusername);

  JNIStringHolder passwordStr(jpassword);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  env->DeleteLocalRef(jpassword);

  Prompter *prompter = NULL;
  if (jprompter != NULL)
    {
      prompter = Prompter::makeCPrompter(jprompter);
      if (JNIUtil::isExceptionThrown())
        return NULL;
    }

  jobject jremoteSession = open(
      jretryAttempts, url, uuid, configDirectory,
      usernameStr, passwordStr, prompter, jprogress);
  if (JNIUtil::isExceptionThrown() || !jremoteSession)
    {
      delete prompter;
      jremoteSession = NULL;
    }
  return jremoteSession;
}

jobject
RemoteSession::open(jint jretryAttempts,
                    const char* url, const char* uuid,
                    const char* configDirectory,
                    const char*  usernameStr, const char*  passwordStr,
                    Prompter* prompter, jobject jprogress)
{
  /*
   * Initialize ra layer if we have not done so yet
   */
  static bool initialized = false;
  if (!initialized)
    {
      SVN_JNI_ERR(svn_ra_initialize(JNIUtil::getPool()), NULL);
      initialized = true;
    }

  jobject jthis_out = NULL;
  RemoteSession* session = new RemoteSession(
      &jthis_out, jretryAttempts, url, uuid, configDirectory,
      usernameStr, passwordStr, prompter, jprogress);
  if (JNIUtil::isJavaExceptionThrown() || !session)
    {
      delete session;
      jthis_out = NULL;
    }
  return jthis_out;
}


namespace{
  struct compare_c_strings
  {
    bool operator()(const char* a, const char* b)
      {
        return (0 < std::strcmp(a, b));
      }
  };
  typedef std::set<const char*, compare_c_strings> attempt_set;
  typedef std::pair<attempt_set::iterator, bool> attempt_insert;
} // anonymous namespace

RemoteSession::RemoteSession(jobject* jthis_out, int retryAttempts,
                             const char* url, const char* uuid,
                             const char* configDirectory,
                             const char*  username, const char*  password,
                             Prompter* prompter, jobject jprogress)
  : m_session(NULL), m_context(NULL)
{
  // Create java session object
  JNIEnv *env = JNIUtil::getEnv();

  jclass clazz = env->FindClass(JAVA_CLASS_REMOTE_SESSION);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(J)V");
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  jlong cppAddr = this->getCppAddr();

  jobject jremoteSession = env->NewObject(clazz, ctor, cppAddr);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  m_context = new RemoteSessionContext(
      jremoteSession, pool, configDirectory,
      username, password, prompter, jprogress);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  const char* corrected_url = NULL;
  bool cycle_detected = false;
  attempt_set attempted;

  while (retryAttempts-- >= 0)
    {
      SVN_JNI_ERR(
          svn_ra_open4(&m_session, &corrected_url,
                       url, uuid, m_context->getCallbacks(),
                       m_context->getCallbackBaton(),
                       m_context->getConfigData(),
                       pool.getPool()),
                  );

      if (!corrected_url)
        break;

      attempt_insert result = attempted.insert(corrected_url);
      if (!result.second)
        {
          cycle_detected = true;
          break;
        }
    }

  if (corrected_url)
    {
      jstring exmsg = JNIUtil::makeJString(
          (cycle_detected
           ? apr_psprintf(pool.getPool(),
                          _("Redirect cycle detected for URL '%s'"),
                          corrected_url)
           : _("Too many redirects")));
      if (JNIUtil::isJavaExceptionThrown())
        return;

      jclass excls = env->FindClass(
          JAVA_PACKAGE "/SubversionException");
      if (JNIUtil::isJavaExceptionThrown())
        return;

      static jmethodID exctor = 0;
      if (exctor == 0)
        {
          exctor = env->GetMethodID(excls, "<init>", "(J)V");
          if (JNIUtil::isJavaExceptionThrown())
            return;
        }

      jobject ex = env->NewObject(excls, exctor, exmsg);
      env->Throw(static_cast<jthrowable>(ex));
      return;
    }

  *jthis_out = jremoteSession;
}

RemoteSession::~RemoteSession()
{
  delete m_context;
}

void
RemoteSession::dispose(jobject jthis)
{
  static jfieldID fid = 0;
  SVNBase::dispose(jthis, &fid, JAVA_CLASS_REMOTE_SESSION);
}

jstring
RemoteSession::getSessionUrl()
{
  SVN::Pool subPool(pool);
  const char * url;

  SVN_JNI_ERR(svn_ra_get_session_url(m_session, &url, subPool.getPool()), NULL);

  jstring jurl = JNIUtil::makeJString(url);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jurl;
}

jstring
RemoteSession::getReposUUID()
{
  SVN::Pool subPool(pool);
  const char * uuid;

  SVN_JNI_ERR(svn_ra_get_uuid2(m_session, &uuid, subPool.getPool()), NULL);

  jstring juuid = JNIUtil::makeJString(uuid);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return juuid;
}

jobject
RemoteSession::getLatestRevision()
{
  SVN::Pool subPool(pool);
  svn_revnum_t rev;

  SVN_JNI_ERR(svn_ra_get_latest_revnum(m_session, &rev, subPool.getPool()),
              NULL);

  return Revision::makeJRevision(rev);
}

jobject
RemoteSession::getRevisionByTimestamp(jlong timestamp)
{
  SVN::Pool requestPool;
  svn_revnum_t rev;

  apr_time_t tm = timestamp;

  SVN_JNI_ERR(svn_ra_get_dated_revision(m_session, &rev, tm,
                                        requestPool.getPool()),
              NULL);

  return Revision::makeJRevision(rev);
}

jobject
RemoteSession::getLocks(jstring jpath, jobject jdepth)
{
  SVN::Pool requestPool;
  apr_hash_t *locks;

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  svn_depth_t depth = EnumMapper::toDepth(jdepth);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  SVN_JNI_ERR(svn_ra_get_locks2(m_session, &locks, path, depth,
                                requestPool.getPool()),
              NULL);

  return CreateJ::LockMap(locks, requestPool.getPool());
}

jobject
RemoteSession::checkPath(jstring jpath, jobject jrevision)
{
  SVN::Pool requestPool;
  svn_node_kind_t kind;

  JNIStringHolder path(jpath);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  Revision revision(jrevision);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  SVN_JNI_ERR(svn_ra_check_path(m_session, path,
                                revision.revision()->value.number,
                                &kind, requestPool.getPool()),
              NULL);

  return EnumMapper::mapNodeKind(kind);
}
