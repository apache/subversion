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
 * @file CommitEditor.cpp
 * @brief Implementation of the class CommitEditor
 */

#include "CommitEditor.h"
#include "EnumMapper.h"
#include "InputStream.h"
#include "Iterator.h"
#include "JNIByteArray.h"
#include "LockTokenTable.h"
#include "RevpropTable.h"
#include "RemoteSession.h"

#include <apr_tables.h>
#include "svn_checksum.h"
#include "private/svn_editor.h"
#include "private/svn_ra_private.h"
#include "svn_private_config.h"

CommitEditor*
CommitEditor::getCppObject(jobject jthis)
{
  static jfieldID fid = 0;
  jlong cppAddr = SVNBase::findCppAddrForJObject(
      jthis, &fid, JAVA_PACKAGE"/remote/CommitEditor");
  return (cppAddr == 0 ? NULL : reinterpret_cast<CommitEditor*>(cppAddr));
}

jlong
CommitEditor::createInstance(jobject jsession,
                             jobject jrevprops,
                             jobject jcommit_callback,
                             jobject jlock_tokens,
                             jboolean jkeep_locks)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session, 0);

  CommitEditor* editor = new CommitEditor(session,
                                          jrevprops, jcommit_callback,
                                          jlock_tokens, jkeep_locks);
  if (JNIUtil::isJavaExceptionThrown())
    {
      delete editor;
      return 0;
    }
  return editor->getCppAddr();
}


CommitEditor::CommitEditor(RemoteSession* session,
                           jobject jrevprops, jobject jcommit_callback,
                           jobject jlock_tokens, jboolean jkeep_locks)
  : m_valid(false),
    m_callback(jcommit_callback),
    m_session(session),
    m_editor(NULL),
    m_callback_session(NULL),
    m_callback_session_url(NULL),
    m_callback_session_uuid(NULL)
{
  // Store the repository root identity from the current session as we
  // may need it to open another session in get_copysrc_kind_cb.
  SVN_JNI_ERR(svn_ra_get_repos_root2(session->m_session,
                                     &m_callback_session_url,
                                     pool.getPool()),);
  SVN_JNI_ERR(svn_ra_get_uuid2(session->m_session,
                               &m_callback_session_uuid,
                               pool.getPool()),);

  RevpropTable revprops(jrevprops, true);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  LockTokenTable lock_tokens(jlock_tokens);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN::Pool subPool(pool);
  SVN_JNI_ERR(svn_ra__get_commit_ev2(
                  &m_editor,
                  session->m_session,
                  revprops.hash(subPool, false),
                  m_callback.callback, &m_callback,
                  lock_tokens.hash(subPool, true),
                  bool(jkeep_locks),
                  NULL,               // svn_ra__provide_base_cb_t
                  NULL,               // svn_ra__provide_props_cb_t
                  this->get_copysrc_kind_cb, this,
                  pool.getPool(),     // result pool
                  subPool.getPool()), // scratch pool
              );
  m_valid = true;
}

CommitEditor::~CommitEditor() {}

void CommitEditor::dispose(jobject jthis)
{
  if (m_valid)
    abort();

  static jfieldID fid = 0;
  SVNBase::dispose(jthis, &fid, JAVA_PACKAGE"/remote/CommitEditor");
}

namespace {
void throw_illegal_state(const char* msg)
{
  JNIEnv *env = JNIUtil::getEnv();

  jclass clazz = env->FindClass("java/lang/IllegalStateException");
  if (JNIUtil::isJavaExceptionThrown())
    return;

  static jmethodID ctor_mid = 0;
  if (0 == ctor_mid)
    {
      ctor_mid = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  jstring jmsg = JNIUtil::makeJString(msg);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  jobject jexception = env->NewObject(clazz, ctor_mid, jmsg);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->Throw((jthrowable)jexception);
}

void throw_editor_inactive()
{
  throw_illegal_state(_("The editor is not active"));
}

void throw_not_implemented(const char* fname)
{
  std::string msg = _("Not implemented: ");
  msg += "CommitEditor.";
  msg += fname;
  throw_illegal_state(msg.c_str());
}

const apr_array_header_t*
build_children(const Iterator& iter, SVN::Pool& pool)
{
  apr_pool_t* result_pool = pool.getPool();
  apr_array_header_t* children = apr_array_make(
      result_pool, 0, sizeof(const char*));
  while (iter.hasNext())
    {
      JNIStringHolder path((jstring)iter.next());
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;
      APR_ARRAY_PUSH(children, const char*) = path.pstrdup(result_pool);
    }
  return children;
}

svn_checksum_t
build_checksum(jobject jchecksum, SVN::Pool& pool)
{
  apr_pool_t* result_pool = pool.getPool();
  svn_checksum_t checksum = { 0 };
  if (jchecksum)
    {
      JNIEnv *env = JNIUtil::getEnv();

      static jmethodID digest_mid = 0;
      static jmethodID kind_mid = 0;

      if (0 == digest_mid || 0 == kind_mid)
        {
          jclass cls = env->FindClass(JAVA_PACKAGE"/types/Checksum");
          if (JNIUtil::isJavaExceptionThrown())
            return checksum;

          digest_mid = env->GetMethodID(cls, "getDigest", "()[B");
          if (JNIUtil::isJavaExceptionThrown())
            return checksum;
          kind_mid = env->GetMethodID(cls, "getKind", "()L"
                                      JAVA_PACKAGE"/types/Checksum$Kind;");
          if (JNIUtil::isJavaExceptionThrown())
            return checksum;
        }

      jobject jdigest = env->CallObjectMethod(jchecksum, digest_mid);
      if (JNIUtil::isJavaExceptionThrown())
        return checksum;
      jobject jkind = env->CallObjectMethod(jchecksum, kind_mid);
      if (JNIUtil::isJavaExceptionThrown())
        return checksum;
      JNIByteArray bdigest((jbyteArray)jdigest, true);
      if (JNIUtil::isJavaExceptionThrown())
        return checksum;

      void* digest = apr_palloc(result_pool, bdigest.getLength());
      memcpy(digest, bdigest.getBytes(), bdigest.getLength());
      checksum.digest = static_cast<const unsigned char*>(digest);
      checksum.kind = EnumMapper::toChecksumKind(jkind);
    }

  return checksum;
}

// void
// build_rotation(const apr_array_header_t **p_relpaths,
//                const apr_array_header_t **p_revisions,
//                const Iterator& iter, SVN::Pool& pool)
// {
//   *p_relpaths = *p_revisions = NULL;
//
//   static jfieldID relpath_fid = 0;
//   static jfieldID revision_fid = 0;
//
//   JNIEnv *env = JNIUtil::getEnv();
//   if (0 == relpath_fid || 0 == revision_fid)
//     {
//       jclass cls = env->FindClass(JAVA_PACKAGE"/ISVNEditor$RotatePair");
//       if (JNIUtil::isJavaExceptionThrown())
//         return;
//
//       relpath_fid = env->GetFieldID(cls, "relativePath", "Ljava/lang/String;");
//       if (JNIUtil::isJavaExceptionThrown())
//         return;
//       revision_fid = env->GetFieldID(cls, "revision", "J");
//       if (JNIUtil::isJavaExceptionThrown())
//         return;
//     }
//
//   apr_pool_t* result_pool = pool.getPool();
//   apr_array_header_t* relpaths = apr_array_make(
//       result_pool, 0, sizeof(const char*));
//   apr_array_header_t* revisions = apr_array_make(
//       result_pool, 0, sizeof(svn_revnum_t));
//   while (iter.hasNext())
//     {
//       jobject jpair = iter.next();
//       jobject jrelpath = env->GetObjectField(jpair, relpath_fid);
//       if (JNIUtil::isJavaExceptionThrown())
//         return;
//       jlong jrevision = env->GetLongField(jpair, revision_fid);
//       if (JNIUtil::isJavaExceptionThrown())
//         return;
//       JNIStringHolder relpath((jstring)jrelpath);
//       if (JNIUtil::isJavaExceptionThrown())
//         return;
//
//       APR_ARRAY_PUSH(relpaths, const char*) = relpath.pstrdup(result_pool);
//       APR_ARRAY_PUSH(revisions, svn_revnum_t) = svn_revnum_t(jrevision);
//     }
//
//   *p_relpaths = relpaths;
//   *p_revisions = revisions;
// }
} // anonymous namespace


void CommitEditor::addDirectory(jstring jrelpath,
                                jobject jchildren, jobject jproperties,
                                jlong jreplaces_revision)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  Iterator children(jchildren);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  RevpropTable properties(jproperties, true);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN::Pool subPool(pool);
  Relpath relpath(jrelpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(relpath.error_occurred(),);

  SVN_JNI_ERR(svn_editor_add_directory(m_editor, relpath.c_str(),
                                       build_children(children, subPool),
                                       properties.hash(subPool, false),
                                       svn_revnum_t(jreplaces_revision)),);
}

void CommitEditor::addFile(jstring jrelpath,
                           jobject jchecksum, jobject jcontents,
                           jobject jproperties,
                           jlong jreplaces_revision)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  InputStream contents(jcontents);
  RevpropTable properties(jproperties, true);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN::Pool subPool(pool);
  Relpath relpath(jrelpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(relpath.error_occurred(),);

  svn_checksum_t checksum = build_checksum(jchecksum, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  SVN_JNI_ERR(svn_editor_add_file(m_editor, relpath.c_str(),
                                  &checksum, contents.getStream(subPool),
                                  properties.hash(subPool, false),
                                  svn_revnum_t(jreplaces_revision)),);
}

void CommitEditor::addSymlink(jstring jrelpath,
                              jstring jtarget, jobject jproperties,
                              jlong jreplaces_revision)
{
  throw_not_implemented("addSymlink");
}

void CommitEditor::addAbsent(jstring jrelpath, jobject jkind,
                             jlong jreplaces_revision)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  SVN::Pool subPool(pool);
  Relpath relpath(jrelpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(relpath.error_occurred(),);

  SVN_JNI_ERR(svn_editor_add_absent(m_editor, relpath.c_str(),
                                    EnumMapper::toNodeKind(jkind),
                                    svn_revnum_t(jreplaces_revision)),);
}

void CommitEditor::alterDirectory(jstring jrelpath, jlong jrevision,
                                  jobject jchildren, jobject jproperties)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  Iterator children(jchildren);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  RevpropTable properties(jproperties, true);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN::Pool subPool(pool);
  Relpath relpath(jrelpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(relpath.error_occurred(),);

  SVN_JNI_ERR(svn_editor_alter_directory(
                  m_editor, relpath.c_str(), svn_revnum_t(jrevision),
                  (jchildren ? build_children(children, subPool) : NULL),
                  properties.hash(subPool, true)),);
}

void CommitEditor::alterFile(jstring jrelpath, jlong jrevision,
                             jobject jchecksum, jobject jcontents,
                             jobject jproperties)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  InputStream contents(jcontents);
  RevpropTable properties(jproperties, true);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN::Pool subPool(pool);
  Relpath relpath(jrelpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(relpath.error_occurred(),);

  svn_checksum_t checksum = build_checksum(jchecksum, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  SVN_JNI_ERR(svn_editor_alter_file(
                  m_editor, relpath.c_str(), svn_revnum_t(jrevision),
                  (jcontents ? &checksum : NULL),
                  (jcontents ? contents.getStream(subPool) : NULL),
                  properties.hash(subPool, true)),);
}

void CommitEditor::alterSymlink(jstring jrelpath, jlong jrevision,
                                jstring jtarget, jobject jproperties)
{
  throw_not_implemented("alterSymlink");
}

void CommitEditor::remove(jstring jrelpath, jlong jrevision)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  SVN::Pool subPool(pool);
  Relpath relpath(jrelpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(relpath.error_occurred(),);

  SVN_JNI_ERR(svn_editor_delete(m_editor, relpath.c_str(),
                                svn_revnum_t(jrevision)),);
}

void CommitEditor::copy(jstring jsrc_relpath, jlong jsrc_revision,
                        jstring jdst_relpath, jlong jreplaces_revision)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  SVN::Pool subPool(pool);
  Relpath src_relpath(jsrc_relpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(src_relpath.error_occurred(),);
  Relpath dst_relpath(jdst_relpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(dst_relpath.error_occurred(),);

  SVN_JNI_ERR(svn_editor_copy(m_editor,
                              src_relpath.c_str(),
                              svn_revnum_t(jsrc_revision),
                              dst_relpath.c_str(),
                              svn_revnum_t(jreplaces_revision)),);
}

void CommitEditor::move(jstring jsrc_relpath, jlong jsrc_revision,
                        jstring jdst_relpath, jlong jreplaces_revision)
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  SVN::Pool subPool(pool);
  Relpath src_relpath(jsrc_relpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(src_relpath.error_occurred(),);
  Relpath dst_relpath(jdst_relpath, subPool);
  if (JNIUtil::isExceptionThrown())
    return;
  SVN_JNI_ERR(dst_relpath.error_occurred(),);

  SVN_JNI_ERR(svn_editor_move(m_editor,
                              src_relpath.c_str(),
                              svn_revnum_t(jsrc_revision),
                              dst_relpath.c_str(),
                              svn_revnum_t(jreplaces_revision)),);
}

void CommitEditor::rotate(jobject jelements)
{
//   if (!m_valid)
//     {
//       throw_editor_inactive();
//       return;
//     }
//   SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);
//
//   SVN::Pool subPool(pool);
//   const apr_array_header_t* relpaths;
//   const apr_array_header_t* revisions;
//   build_rotation(&relpaths, &revisions, jelements, subPool);
//   if (JNIUtil::isJavaExceptionThrown())
//     return;
//
//   SVN_JNI_ERR(svn_editor_rotate(m_editor, relpaths, revisions),);
  throw_not_implemented("rotate");
}

void CommitEditor::complete()
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  SVN_JNI_ERR(svn_editor_complete(m_editor),);
  m_valid = false;
}

void CommitEditor::abort()
{
  if (!m_valid)
    {
      throw_editor_inactive();
      return;
    }
  SVN_JNI_ERR(m_session->m_context->checkCancel(m_session->m_context),);

  SVN_JNI_ERR(svn_editor_abort(m_editor),);
  m_valid = false;
}


svn_error_t*
CommitEditor::get_copysrc_kind_cb(svn_node_kind_t* kind, void* baton,
                                  const char* repos_relpath,
                                  svn_revnum_t src_revision,
                                  apr_pool_t *scratch_pool)
{
  CommitEditor* editor = static_cast<CommitEditor*>(baton);
  if (!editor->m_callback_session)
    {
      const char* corrected_url;
      SVN_ERR(svn_ra_open4(&editor->m_callback_session, &corrected_url,
                           editor->m_callback_session_url,
                           editor->m_callback_session_uuid,
                           editor->m_session->m_context->getCallbacks(),
                           editor->m_session->m_context->getCallbackBaton(),
                           editor->m_session->m_context->getConfigData(),
                           editor->pool.getPool()));

      if (corrected_url)
        {
          // This shouldn't happen -- the open session will give us
          // the final redirected repository URL. There's an edge case
          // where redirects might change while the session is open;
          // but we'll just punt handling that to the caller.
          return svn_error_createf(
              SVN_ERR_RA_ILLEGAL_URL, NULL,
              _("Repository URL changed while session was open.\n"
                "Expected URL: %s\nApparent URL: %s"),
              editor->m_callback_session_url, corrected_url);
        }
    }

  SVN::Pool subPool(editor->pool);
  return svn_ra_check_path(editor->m_callback_session,
                           repos_relpath, src_revision, kind,
                           subPool.getPool());
}
