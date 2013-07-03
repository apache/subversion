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
#include <memory>
#include <set>

#include "JNIByteArray.h"
#include "JNIStringHolder.h"
#include "JNIUtil.h"
#include "Path.h"

#include "svn_ra.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"

#include "CreateJ.h"
#include "EnumMapper.h"
#include "Iterator.h"
#include "LogMessageCallback.h"
#include "OutputStream.h"
#include "Prompter.h"
#include "Revision.h"
#include "RemoteSession.h"
#include "EditorProxy.h"
#include "StateReporter.h"

#include <apr_strings.h>
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
                    jobject jconfigHandler,
                    jstring jusername, jstring jpassword,
                    jobject jprompter, jobject jprogress)
{
  JNIEnv *env = JNIUtil::getEnv();

  SVN::Pool requestPool;
  URL url(jurl, requestPool);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  SVN_JNI_ERR(url.error_occurred(), NULL);
  env->DeleteLocalRef(jurl);

  JNIStringHolder uuid(juuid);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  env->DeleteLocalRef(juuid);

  Path configDirectory(jconfigDirectory, requestPool);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  SVN_JNI_ERR(configDirectory.error_occurred(), NULL);
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
      jretryAttempts, url.c_str(), uuid,
      configDirectory.c_str(), jconfigHandler,
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
                    const char* configDirectory, jobject jconfigHandler,
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
      &jthis_out, jretryAttempts, url, uuid,
      configDirectory, jconfigHandler,
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
                             jobject jconfigHandler,
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
      jremoteSession, pool, configDirectory, jconfigHandler,
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

  if (cycle_detected)
    {
      jstring exmsg = JNIUtil::makeJString(
          apr_psprintf(pool.getPool(),
                       _("Redirect cycle detected for URL '%s'"),
                       corrected_url));

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

  if (corrected_url)
    {
      jstring exmsg = JNIUtil::makeJString(_("Too many redirects"));
      if (JNIUtil::isJavaExceptionThrown())
        return;

      jstring exurl = JNIUtil::makeJString(corrected_url);
      if (JNIUtil::isJavaExceptionThrown())
        return;

      jclass excls = env->FindClass(
          JAVA_PACKAGE "/remote/RetryOpenSession");
      if (JNIUtil::isJavaExceptionThrown())
        return;

      static jmethodID exctor = 0;
      if (exctor == 0)
        {
          exctor = env->GetMethodID(excls, "<init>", "(JJ)V");
          if (JNIUtil::isJavaExceptionThrown())
            return;
        }

      jobject ex = env->NewObject(excls, exctor, exmsg, exurl);
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

void RemoteSession::reparent(jstring jurl)
{
  SVN::Pool subPool(pool);
  URL url(jurl, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(url.error_occurred(),);

  SVN_JNI_ERR(svn_ra_reparent(m_session, url.c_str(), subPool.getPool()), );
}

jstring
RemoteSession::getSessionUrl()
{
  SVN::Pool subPool(pool);
  const char* url;
  SVN_JNI_ERR(svn_ra_get_session_url(m_session, &url, subPool.getPool()), NULL);

  jstring jurl = JNIUtil::makeJString(url);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jurl;
}

jstring
RemoteSession::getSessionRelativePath(jstring jurl)
{
  SVN::Pool subPool(pool);
  URL url(jurl, subPool);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  SVN_JNI_ERR(url.error_occurred(), NULL);

  const char* rel_path;
  SVN_JNI_ERR(svn_ra_get_path_relative_to_session(
                  m_session, &rel_path, url.c_str(), subPool.getPool()),
              NULL);
  jstring jrel_path = JNIUtil::makeJString(rel_path);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jrel_path;
}

jstring
RemoteSession::getReposRelativePath(jstring jurl)
{
  SVN::Pool subPool(pool);
  URL url(jurl, subPool);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  SVN_JNI_ERR(url.error_occurred(), NULL);

  const char* rel_path;
  SVN_JNI_ERR(svn_ra_get_path_relative_to_root(m_session, &rel_path,
                                               url.c_str(),
                                               subPool.getPool()),
              NULL);

  jstring jrel_path = JNIUtil::makeJString(rel_path);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jrel_path;
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

jstring
RemoteSession::getReposRootUrl()
{
  SVN::Pool subPool(pool);
  const char* url;
  SVN_JNI_ERR(svn_ra_get_repos_root2(m_session, &url, subPool.getPool()),
              NULL);

  jstring jurl = JNIUtil::makeJString(url);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jurl;
}

jlong
RemoteSession::getLatestRevision()
{
  SVN::Pool subPool(pool);
  svn_revnum_t rev;
  SVN_JNI_ERR(svn_ra_get_latest_revnum(m_session, &rev, subPool.getPool()),
              SVN_INVALID_REVNUM);
  return rev;
}

jlong
RemoteSession::getRevisionByTimestamp(jlong timestamp)
{
  SVN::Pool subPool(pool);
  svn_revnum_t rev;
  SVN_JNI_ERR(svn_ra_get_dated_revision(m_session, &rev,
                                        apr_time_t(timestamp),
                                        subPool.getPool()),
              SVN_INVALID_REVNUM);
  return rev;
}

namespace {
bool byte_array_to_svn_string(JNIByteArray& ary, svn_string_t& str)
{
  if (ary.isNull())
    return false;

  str.data = reinterpret_cast<const char*>(ary.getBytes());
  str.len = ary.getLength();
  return true;
}
} // anonymous namespace

void
RemoteSession::changeRevisionProperty(
    jlong jrevision, jstring jname,
    jbyteArray jold_value, jbyteArray jvalue)
{
  JNIStringHolder name(jname);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIByteArray old_value(jold_value);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIByteArray value(jvalue);
  if (JNIUtil::isExceptionThrown())
    return;

  svn_string_t str_old_value;
  svn_string_t* const p_old_value = &str_old_value;
  svn_string_t* const* pp_old_value = NULL;
  if (byte_array_to_svn_string(old_value, str_old_value))
      pp_old_value = &p_old_value;

  svn_string_t str_value;
  svn_string_t* p_value = NULL;
  if (byte_array_to_svn_string(value, str_value))
      p_value = &str_value;

  SVN::Pool subPool(pool);
  SVN_JNI_ERR(svn_ra_change_rev_prop2(m_session,
                                      svn_revnum_t(jrevision),
                                      name, pp_old_value, p_value,
                                      subPool.getPool()), );
}

jobject
RemoteSession::getRevisionProperties(jlong jrevision)
{
  SVN::Pool subPool(pool);
  apr_hash_t *props;
  SVN_JNI_ERR(svn_ra_rev_proplist(m_session, svn_revnum_t(jrevision),
                                  &props, subPool.getPool()),
              NULL);

  return CreateJ::PropertyMap(props);
}

jbyteArray
RemoteSession::getRevisionProperty(jlong jrevision, jstring jname)
{
  JNIStringHolder name(jname);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  SVN::Pool subPool(pool);
  svn_string_t *propval;
  SVN_JNI_ERR(svn_ra_rev_prop(m_session, svn_revnum_t(jrevision),
                              name, &propval, subPool.getPool()),
              NULL);

  return JNIUtil::makeJByteArray(propval);
}

jlong
RemoteSession::getFile(jlong jrevision, jstring jpath,
                       jobject jcontents, jobject jproperties)
{
  OutputStream contents_proxy(jcontents);
  if (JNIUtil::isExceptionThrown())
    return SVN_INVALID_REVNUM;

  SVN::Pool subPool(pool);
  Relpath path(jpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return SVN_INVALID_REVNUM;
  SVN_JNI_ERR(path.error_occurred(), SVN_INVALID_REVNUM);

  apr_hash_t* props = NULL;
  svn_revnum_t fetched_rev = svn_revnum_t(jrevision);
  svn_stream_t* contents = (!jcontents ? NULL
                            : contents_proxy.getStream(subPool));

  SVN_JNI_ERR(svn_ra_get_file(m_session, path.c_str(), fetched_rev,
                              contents, &fetched_rev,
                              (jproperties ? &props : NULL),
                              subPool.getPool()),
              SVN_INVALID_REVNUM);

  if (jproperties)
    {
      CreateJ::FillPropertyMap(jproperties, props);
      if (JNIUtil::isExceptionThrown())
        return SVN_INVALID_REVNUM;
    }

  return fetched_rev;
}

namespace {
void fill_dirents(const char* base_url, const char* base_relpath,
                  jobject jdirents, apr_hash_t* dirents,
                  apr_pool_t* scratch_pool)
{
  if (!dirents)
    return;

  base_url = apr_pstrcat(scratch_pool, base_url, "/", base_relpath, NULL);
  base_url = svn_uri_canonicalize(base_url, scratch_pool);
  svn_stringbuf_t* abs_path = svn_stringbuf_create(base_url, scratch_pool);
  svn_stringbuf_appendbyte(abs_path, '/');
  const apr_size_t base_len = abs_path->len;

  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  // We have no way of knowing the exact type of `dirents' in advance
  // so we cannot remember the "put" method ID across calls.
  jmethodID put_mid =
    env->GetMethodID(env->GetObjectClass(jdirents), "put",
                     "(Ljava/lang/Object;Ljava/lang/Object;)"
                     "Ljava/lang/Object;");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  static jfieldID path_fid = 0;
  if (path_fid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE "/types/DirEntry");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();

      path_fid = env->GetFieldID(clazz, "path", "Ljava/lang/String;");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();
    }

  for (apr_hash_index_t* hi = apr_hash_first(scratch_pool, dirents);
       hi; hi = apr_hash_next(hi))
    {
      const char* path;
      svn_dirent_t* dirent;

      const void *v_key;
      void *v_val;

      apr_hash_this(hi, &v_key, NULL, &v_val);
      path = static_cast<const char*>(v_key);
      dirent = static_cast<svn_dirent_t*>(v_val);
      abs_path->len = base_len;
      svn_stringbuf_appendcstr(abs_path, path);

      jobject jdirent = CreateJ::DirEntry(path, abs_path->data, dirent);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();

      // Use the existing DirEntry.path field as the key
      jstring jpath = jstring(env->GetObjectField(jdirent, path_fid));
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();

      env->CallObjectMethod(jdirents, put_mid, jpath, jdirent);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NOTHING();
      env->DeleteLocalRef(jdirent);
    }

  POP_AND_RETURN_NOTHING();
}
} // anonymous namespace

jlong
RemoteSession::getDirectory(jlong jrevision, jstring jpath,
                            jint jdirent_fields, jobject jdirents,
                            jobject jproperties)
{
  SVN::Pool subPool(pool);
  Relpath path(jpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return SVN_INVALID_REVNUM;
  SVN_JNI_ERR(path.error_occurred(), SVN_INVALID_REVNUM);

  apr_hash_t* props = NULL;
  apr_hash_t* dirents = NULL;
  svn_revnum_t fetched_rev = svn_revnum_t(jrevision);

  SVN_JNI_ERR(svn_ra_get_dir2(m_session, (jdirents ? &dirents : NULL),
                              &fetched_rev, (jproperties ? &props : NULL),
                              path.c_str(), fetched_rev,
                              apr_uint32_t(jdirent_fields),
                              subPool.getPool()),
              SVN_INVALID_REVNUM);

  if (jdirents)
    {
      // We will construct the absolute path in the DirEntry objects
      // from the session URL and directory relpath.
      const char* base_url;
      SVN_JNI_ERR(svn_ra_get_session_url(m_session, &base_url,
                                         subPool.getPool()),
                  SVN_INVALID_REVNUM);
      fill_dirents(base_url, path.c_str(), jdirents, dirents,
                   subPool.getPool());
      if (JNIUtil::isExceptionThrown())
        return SVN_INVALID_REVNUM;
    }

  if (jproperties)
    {
      CreateJ::FillPropertyMap(jproperties, props);
      if (JNIUtil::isExceptionThrown())
        return SVN_INVALID_REVNUM;
    }

  return fetched_rev;
}

// TODO: getMergeinfo
// TODO: update
// TODO: switch

namespace {
svn_error_t*
status_unlock_func(void* baton, const char* path, apr_pool_t* scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) status_unlock_func('%s')\n", path);
  return SVN_NO_ERROR;
}

svn_error_t*
status_fetch_props_func(apr_hash_t **props, void *baton,
                        const char *path, svn_revnum_t base_revision,
                        apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) status_fetch_props_func('%s', r%lld)\n",
  //DEBUG:        path, static_cast<long long>(base_revision));
  *props = apr_hash_make(scratch_pool);
  return SVN_NO_ERROR;
}

svn_error_t*
status_fetch_base_func(const char **filename, void *baton,
                       const char *path, svn_revnum_t base_revision,
                       apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) status_fetch_base_func('%s', r%lld)\n",
  //DEBUG:        path, static_cast<long long>(base_revision));
  *filename = NULL;
  return SVN_NO_ERROR;
}

svn_error_t*
status_start_edit_func(void* baton, svn_revnum_t start_revision)
{
  //DEBUG:fprintf(stderr, "  (n) status_start_edit_func(r%lld)\n",
  //DEBUG:        static_cast<long long>(start_revision));
  return SVN_NO_ERROR;
}

svn_error_t*
status_target_revision_func(void* baton, svn_revnum_t target_revision,
                            apr_pool_t* scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) status_target_revision_func(r%lld)\n",
  //DEBUG:        static_cast<long long>(target_revision));
  *static_cast<svn_revnum_t*>(baton) = target_revision;
  return SVN_NO_ERROR;
}

const EditorProxyCallbacks template_status_editor_callbacks = {
  status_unlock_func,
  status_fetch_props_func,
  status_fetch_base_func,
  { status_start_edit_func, status_target_revision_func, NULL },
  NULL
};
} // anonymous namespace

void
RemoteSession::status(jobject jthis, jstring jstatus_target,
                      jlong jrevision, jobject jdepth,
                      jobject jstatus_editor, jobject jreporter)
{
  StateReporter *rp = StateReporter::getCppObject(jreporter);
  CPPADDR_NULL_PTR(rp,);

  SVN::Pool scratchPool(rp->get_report_pool());
  Relpath status_target(jstatus_target, scratchPool);
  if (JNIUtil::isExceptionThrown())
    return;

  apr_pool_t* scratch_pool = scratchPool.getPool();
  const char* repos_root_url;
  SVN_JNI_ERR(svn_ra_get_repos_root2(m_session, &repos_root_url,
                                     scratch_pool),);
  const char* session_root_url;
  SVN_JNI_ERR(svn_ra_get_session_url(m_session, &session_root_url,
                                     scratch_pool),);
  const char* base_relpath;
  SVN_JNI_ERR(svn_ra_get_path_relative_to_root(m_session, &base_relpath,
                                               session_root_url,
                                               scratch_pool),);

  EditorProxyCallbacks proxy_callbacks =
    template_status_editor_callbacks;
  proxy_callbacks.m_extra_baton.baton = &rp->m_target_revision;

  apr_pool_t* report_pool = rp->get_report_pool();
  std::auto_ptr<EditorProxy> editor(
      new EditorProxy(jstatus_editor, report_pool,
                      repos_root_url, base_relpath,
                      m_context->checkCancel, m_context,
                      proxy_callbacks));
  if (JNIUtil::isExceptionThrown())
    return;

  const svn_ra_reporter3_t* raw_reporter;
  void* report_baton;
  SVN_JNI_ERR(svn_ra_do_status2(m_session,
                                &raw_reporter, &report_baton,
                                status_target.c_str(),
                                svn_revnum_t(jrevision),
                                EnumMapper::toDepth(jdepth),
                                editor->delta_editor(),
                                editor->delta_baton(),
                                report_pool),);
  rp->set_reporter_data(raw_reporter, report_baton, editor.release());
}

// TODO: diff

namespace {
const apr_array_header_t*
build_string_array(const Iterator& iter,
                   bool contains_relpaths, SVN::Pool& pool)
{
  apr_pool_t* result_pool = pool.getPool();
  apr_array_header_t* array = apr_array_make(
      result_pool, 0, sizeof(const char*));
  while (iter.hasNext())
    {
      const char* element;
      jstring jitem = (jstring)iter.next();
      if (contains_relpaths)
        {
          Relpath item(jitem, pool);
          if (JNIUtil::isExceptionThrown())
            return NULL;
          SVN_JNI_ERR(item.error_occurred(), NULL);
          element = apr_pstrdup(result_pool, item.c_str());
        }
      else
        {
          JNIStringHolder item(jitem);
          if (JNIUtil::isJavaExceptionThrown())
            return NULL;
          element = item.pstrdup(result_pool);
        }
      APR_ARRAY_PUSH(array, const char*) = element;
    }
  return array;
}
}

void
RemoteSession::getLog(jobject jpaths,
                      jlong jstartrev, jlong jendrev, jint jlimit,
                      jboolean jstrict_node_history,
                      jboolean jdiscover_changed_paths,
                      jboolean jinclude_merged_revisions,
                      jobject jrevprops, jobject jlog_callback)
{
  Iterator pathiter(jpaths);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  Iterator revpropiter(jrevprops);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  LogMessageCallback receiver(jlog_callback);

  SVN::Pool subPool(pool);
  const apr_array_header_t* paths = build_string_array(pathiter,
                                                       true, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  const apr_array_header_t* revprops = build_string_array(revpropiter,
                                                          false, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN_JNI_ERR(svn_ra_get_log2(m_session, paths,
                              svn_revnum_t(jstartrev), svn_revnum_t(jendrev),
                              int(jlimit),
                              bool(jdiscover_changed_paths),
                              bool(jstrict_node_history),
                              bool(jinclude_merged_revisions),
                              revprops,
                              receiver.callback, &receiver,
                              subPool.getPool()),);
}

jobject
RemoteSession::checkPath(jstring jpath, jlong jrevision)
{
  SVN::Pool subPool(pool);
  Relpath path(jpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  SVN_JNI_ERR(path.error_occurred(), NULL);

  svn_node_kind_t kind;
  SVN_JNI_ERR(svn_ra_check_path(m_session, path.c_str(),
                                svn_revnum_t(jrevision),
                                &kind, subPool.getPool()),
              NULL);

  return EnumMapper::mapNodeKind(kind);
}

// TODO: stat
// TODO: getLocations
// TODO: getLocationSegments
// TODO: getFileRevisions
// TODO: lock
// TODO: unlock
// TODO: getLock

jobject
RemoteSession::getLocks(jstring jpath, jobject jdepth)
{
  svn_depth_t depth = EnumMapper::toDepth(jdepth);
  if (JNIUtil::isExceptionThrown())
    return NULL;

  SVN::Pool subPool(pool);
  Relpath path(jpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return NULL;
  SVN_JNI_ERR(path.error_occurred(), NULL);

  apr_hash_t *locks;
  SVN_JNI_ERR(svn_ra_get_locks2(m_session, &locks, path.c_str(), depth,
                                subPool.getPool()),
              NULL);

  return CreateJ::LockMap(locks, subPool.getPool());
}

// TODO: replayRange
// TODO: replay
// TODO: getDeletedRevision
// TODO: getInheritedProperties

jboolean
RemoteSession::hasCapability(jstring jcapability)
{
  JNIStringHolder capability(jcapability);
  if (JNIUtil::isExceptionThrown())
    return false;

  SVN::Pool subPool(pool);
  svn_boolean_t has;
  SVN_JNI_ERR(svn_ra_has_capability(m_session, &has, capability,
                                    subPool.getPool()),
              false);

  return jboolean(has);
}
