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
 * @file EditorProxy.cpp
 * @brief Interface of all editor proxy classes
 */
#include <apr_pools.h>

#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "EditorProxy.h"
#include "CreateJ.h"
#include "EnumMapper.h"

// Newstyle: stream wrapper
#include <memory>
#include "NativeStream.hpp"
#include "jniwrapper/jni_stack.hpp"

#include "svn_error.h"
#include "svn_private_config.h"

EditorProxy::EditorProxy(jobject jeditor, apr_pool_t* edit_pool,
                         const char* repos_root_url, const char* base_relpath,
                         svn_cancel_func_t cancel_func, void* cancel_baton,
                         const EditorProxyCallbacks& callbacks)
  : m_valid(false),
    m_jeditor(JNIUtil::getEnv()->NewGlobalRef(jeditor)),
    m_edit_pool(edit_pool),
    m_repos_root_url(NULL),
    m_base_relpath(NULL),
    m_found_paths(false),
    m_editor(NULL),
    m_delta_editor(NULL),
    m_delta_baton(NULL),
    m_proxy_callbacks(callbacks)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::EditorProxy(...)\n");

  static const svn_editor_cb_many_t editor_many_cb = {
    cb_add_directory, cb_add_file, cb_add_symlink, cb_add_absent,
    cb_alter_directory, cb_alter_file, cb_alter_symlink,
    cb_delete, cb_copy, cb_move,
    cb_complete, cb_abort
  };

  SVN::Pool scratchPool(edit_pool);
  apr_pool_t* scratch_pool = scratchPool.getPool();

  svn_error_t* err = svn_editor_create(&m_editor, this,
                                       cancel_func, cancel_baton,
                                       edit_pool, scratch_pool);
  if (!err)
    err = svn_editor_setcb_many(m_editor, &editor_many_cb, scratch_pool);
  if (!err)
    {
      m_repos_root_url =
        static_cast<const char*>(apr_pstrdup(edit_pool, repos_root_url));
      m_base_relpath =
        static_cast<const char*>(apr_pstrdup(edit_pool, base_relpath));

      svn_boolean_t found_paths;
      err = svn_delta__delta_from_editor(&m_delta_editor,
                                         &m_delta_baton,
                                         m_editor,
                                         m_proxy_callbacks.m_unlock_func,
                                         m_proxy_callbacks.m_baton,
                                         &found_paths,
                                         repos_root_url, base_relpath,
                                         m_proxy_callbacks.m_fetch_props_func,
                                         m_proxy_callbacks.m_baton,
                                         m_proxy_callbacks.m_fetch_base_func,
                                         m_proxy_callbacks.m_baton,
                                         &m_proxy_callbacks.m_extra_baton,
                                         edit_pool);
      m_found_paths = found_paths;
    }

  if (err)
    JNIUtil::handleSVNError(err);
  else
    m_valid = true;
}

EditorProxy::~EditorProxy()
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::~EditorProxy()\n");

  if (m_jeditor)
    JNIUtil::getEnv()->DeleteGlobalRef(m_jeditor);
}

namespace {
inline svn_error_t* invalid_editor()
{
  return svn_error_create(SVN_ERR_RA_SVN_EDIT_ABORTED, NULL,
                          _("The editor is not valid"));
}

svn_error_t*
get_editor_method(jmethodID& mid, const char* name, const char* sig)
{
  if (0 != mid)
    return SVN_NO_ERROR;        // Already known.

  JNIEnv* env = JNIUtil::getEnv();
  jclass cls = env->FindClass(JAVAHL_CLASS("/ISVNEditor"));
  SVN_JNI_CATCH(,SVN_ERR_RA_SVN_EDIT_ABORTED);
  SVN_JNI_CATCH(mid = env->GetMethodID(cls, name, sig),
                 SVN_ERR_RA_SVN_EDIT_ABORTED);
  return SVN_NO_ERROR;
}

jobject wrap_input_stream(svn_stream_t* stream)
{
  std::auto_ptr<JavaHL::NativeInputStream>
    wrapped(new JavaHL::NativeInputStream());
  apr_pool_t* const wrapped_pool = wrapped->get_pool().getPool();
  wrapped->set_stream(svn_stream_disown(stream, wrapped_pool));
  const jobject jstream = wrapped->create_java_wrapper();
  wrapped.release();
  return jstream;
}
} // anonymous namespace

svn_error_t*
EditorProxy::cb_add_directory(void *baton,
                              const char *relpath,
                              const apr_array_header_t *children,
                              apr_hash_t *props,
                              svn_revnum_t replaces_rev,
                              apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_add_directory('%s')\n", relpath);
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "addDirectory",
                                "(Ljava/lang/String;"
                                "Ljava/lang/Iterable;"
                                "Ljava/util/Map;J)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jchildren = (!children ? NULL : CreateJ::StringSet(children));
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jprops = CreateJ::PropertyMap(props, scratch_pool);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jrelpath, jchildren, jprops,
                         jlong(replaces_rev));
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_add_file(void *baton,
                         const char *relpath,
                         const svn_checksum_t *checksum,
                         svn_stream_t *contents,
                         apr_hash_t *props,
                         svn_revnum_t replaces_rev,
                         apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_add_file('%s')\n", relpath);
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "addFile",
                                "(Ljava/lang/String;"
                                JAVAHL_ARG("/types/Checksum;")
                                "Ljava/io/InputStream;"
                                "Ljava/util/Map;J)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jchecksum = CreateJ::Checksum(checksum);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jprops = CreateJ::PropertyMap(props, scratch_pool);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      jobject jcontents = NULL;
      if (contents != NULL)
        jcontents = wrap_input_stream(contents);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jrelpath, jchecksum, jcontents,
                         jprops, jlong(replaces_rev));
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_add_symlink(void *baton,
                            const char *relpath,
                            const char *target,
                            apr_hash_t *props,
                            svn_revnum_t replaces_rev,
                            apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_add_symlink('%s')\n", relpath);
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "addSymlink",
                                "(Ljava/lang/String;"
                                "Ljava/lang/String;"
                                "Ljava/util/Map;J)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jstring jtarget = JNIUtil::makeJString(target);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jprops = CreateJ::PropertyMap(props, scratch_pool);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jrelpath, jtarget, jprops,
                         jlong(replaces_rev));
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_add_absent(void *baton,
                           const char *relpath,
                           svn_node_kind_t kind,
                           svn_revnum_t replaces_rev,
                           apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_add_absent('%s')\n", relpath);
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "addAbsent",
                                "(Ljava/lang/String;"
                                JAVAHL_ARG("/types/NodeKind;")
                                "J)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jkind = EnumMapper::mapNodeKind(kind);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jrelpath, jkind,
                         jlong(replaces_rev));
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_alter_directory(void *baton,
                                const char *relpath,
                                svn_revnum_t revision,
                                const apr_array_header_t *children,
                                apr_hash_t *props,
                                apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_alter_directory('%s', r%lld)\n",
  //DEBUG:        relpath, static_cast<long long>(revision));
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "alterDirectory",
                                "(Ljava/lang/String;J"
                                "Ljava/lang/Iterable;"
                                "Ljava/util/Map;)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jchildren = (!children ? NULL : CreateJ::StringSet(children));
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jprops = CreateJ::PropertyMap(props, scratch_pool);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jrelpath, jlong(revision),
                         jchildren, jprops);
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_alter_file(void *baton,
                           const char *relpath,
                           svn_revnum_t revision,
                           const svn_checksum_t *checksum,
                           svn_stream_t *contents,
                           apr_hash_t *props,
                           apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_alter_file('%s', r%lld)\n",
  //DEBUG:        relpath, static_cast<long long>(revision));
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "alterFile",
                                "(Ljava/lang/String;J"
                                JAVAHL_ARG("/types/Checksum;")
                                "Ljava/io/InputStream;"
                                "Ljava/util/Map;)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jchecksum = CreateJ::Checksum(checksum);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jprops = CreateJ::PropertyMap(props, scratch_pool);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      jobject jcontents = NULL;
      if (contents != NULL)
        jcontents = wrap_input_stream(contents);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jrelpath, jlong(revision),
                         jchecksum, jcontents, jprops);
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_alter_symlink(void *baton,
                              const char *relpath,
                              svn_revnum_t revision,
                              const char *target,
                              apr_hash_t *props,
                              apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_alter_symlink('%s', r%lld)\n",
  //DEBUG:        relpath, static_cast<long long>(revision));
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "alterSymlink",
                                "(Ljava/lang/String;J"
                                "Ljava/lang/String;"
                                "Ljava/util/Map;)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jstring jtarget = JNIUtil::makeJString(target);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jobject jprops = CreateJ::PropertyMap(props, scratch_pool);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jrelpath, jlong(revision),
                         jtarget, jprops);
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_delete(void *baton,
                       const char *relpath,
                       svn_revnum_t revision,
                       apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_delete('%s', r%lld)\n",
  //DEBUG:        relpath, static_cast<long long>(revision));
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "delete",
                                "(Ljava/lang/String;J)V"));

      jstring jrelpath = JNIUtil::makeJString(relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid, jrelpath);
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_copy(void *baton,
                     const char *src_relpath,
                     svn_revnum_t src_revision,
                     const char *dst_relpath,
                     svn_revnum_t replaces_rev,
                     apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_copy('%s', r%lld, '%s')\n",
  //DEBUG:        src_relpath, static_cast<long long>(src_revision), dst_relpath);
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "copy",
                                "(Ljava/lang/String;J"
                                "Ljava/lang/String;J)V"));

      jstring jsrc_relpath = JNIUtil::makeJString(src_relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jstring jdst_relpath = JNIUtil::makeJString(dst_relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jsrc_relpath, jlong(src_revision),
                         jdst_relpath, jlong(replaces_rev));
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_move(void *baton,
                     const char *src_relpath,
                     svn_revnum_t src_revision,
                     const char *dst_relpath,
                     svn_revnum_t replaces_rev,
                     apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_move('%s', r%lld, '%s')\n",
  //DEBUG:        src_relpath, static_cast<long long>(src_revision), dst_relpath);
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "move",
                                "(Ljava/lang/String;J"
                                "Ljava/lang/String;J)V"));

      jstring jsrc_relpath = JNIUtil::makeJString(src_relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);
      jstring jdst_relpath = JNIUtil::makeJString(dst_relpath);
      SVN_JAVAHL_OLDSTYLE_EXCEPTION_CHECK(env);

      env.CallVoidMethod(ep->m_jeditor, mid,
                         jsrc_relpath, jlong(src_revision),
                         jdst_relpath, jlong(replaces_rev));
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_complete(void *baton, apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_complete()\n");
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();
      ep->m_valid = false;

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "complete", "()V"));

      env.CallVoidMethod(ep->m_jeditor, mid);
    });
  return SVN_NO_ERROR;
}

svn_error_t*
EditorProxy::cb_abort(void *baton, apr_pool_t *scratch_pool)
{
  //DEBUG:fprintf(stderr, "  (n) EditorProxy::cb_abort()\n");
  const ::Java::Env env;
  SVN_JAVAHL_CATCH(env, SVN_ERR_RA_SVN_EDIT_ABORTED,
    {
      ::Java::LocalFrame frame(env);

      EditorProxy* const ep = static_cast<EditorProxy*>(baton);
      if (!ep || !ep->m_valid)
        return invalid_editor();
      ep->m_valid = false;

      static jmethodID mid = 0;
      SVN_ERR(get_editor_method(mid, "abort", "()V"));

      env.CallVoidMethod(ep->m_jeditor, mid);
    });
  return SVN_NO_ERROR;
}
