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
#include "LockTokenTable.h"
#include "RevpropTable.h"
#include "RemoteSession.h"

#include "svn_delta.h"
#include "private/svn_editor.h"
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

  CommitEditor* editor = new CommitEditor(session->m_session,
                                          jrevprops, jcommit_callback,
                                          jlock_tokens, bool(jkeep_locks));
  if (JNIUtil::isJavaExceptionThrown())
    {
      delete editor;
      return 0;
    }
  return editor->getCppAddr();
}


CommitEditor::CommitEditor(svn_ra_session_t* session,
                           jobject jrevprops, jobject jcommit_callback,
                           jobject jlock_tokens, bool keep_locks)
  : m_valid(false),
    m_callback(jcommit_callback),
    m_editor(NULL),
    m_extra_baton(NULL)
{
  RevpropTable revprops(jrevprops, true);
  LockTokenTable lock_tokens(jlock_tokens);

  SVN::Pool subPool(pool);
  const svn_delta_editor_t* delta_editor = NULL;
  void* delta_edit_baton = NULL;
  SVN_JNI_ERR(svn_ra_get_commit_editor3(
                  session, &delta_editor, &delta_edit_baton,
                  revprops.hash(subPool, false),
                  m_callback.callback, &m_callback,
                  lock_tokens.hash(subPool, true),
                  keep_locks, pool.getPool()),
              );

//  SVN_JNI_ERR(svn_delta__editor_from_delta(
//                  &m_editor, &m_extra_baton,
//
//                  svn_delta__unlock_func_t *unlock_func,
//                  void **unlock_baton,
//
//                  delta_editor, delta_edit_baton,
//
//                  svn_boolean_t *send_abs_paths,
//                  const char *repos_root,
//                  const char *base_relpath,
//                  svn_cancel_func_t cancel_func,
//                  void *cancel_baton,
//                  svn_delta_fetch_kind_func_t fetch_kind_func,
//                  void *fetch_kind_baton,
//                  svn_delta_fetch_props_func_t fetch_props_func,
//                  void *fetch_props_baton,
//
//                  pool.getPool(),    // result_pool
//                  subpool.getPool()) // scratch_pool
//              );

  m_valid = true;
}

CommitEditor::~CommitEditor() {}

void CommitEditor::dispose(jobject jthis)
{
  //if (m_valid)
  //  abort();

  static jfieldID fid = 0;
  SVNBase::dispose(jthis, &fid, JAVA_PACKAGE"/remote/CommitEditor");
}

namespace {
void throw_editor_inactive()
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

  jstring jmsg = JNIUtil::makeJString(_("The editor is not active"));
  if (JNIUtil::isJavaExceptionThrown())
    return;

  jobject jexception = env->NewObject(clazz, ctor_mid, jmsg);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  env->Throw((jthrowable)jexception);
}
} // anonymous namespace


void CommitEditor::addDirectory(jobject jsession, jstring jrelpath,
                                jobject jchildren, jobject jproperties,
                                jlong jreplaces_revision)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::addFile(jobject jsession, jstring jrelpath,
                           jobject jchecksum, jobject jcontents,
                           jobject jproperties,
                           jlong jreplaces_revision)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::addSymlink(jobject jsession, jstring jrelpath,
                              jstring jtarget, jobject jproperties,
                              jlong jreplaces_revision)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::addAbsent(jobject jsession, jstring jrelpath,
                             jobject jkind, jlong jreplaces_revision)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::alterDirectory(jobject jsession,
                                  jstring jrelpath, jlong jrevision,
                                  jobject jchildren, jobject jproperties)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::alterFile(jobject jsession,
                             jstring jrelpath, jlong jrevision,
                             jobject jchecksum, jobject jcontents,
                             jobject jproperties)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::alterSymlink(jobject jsession,
                                jstring jrelpath, jlong jrevision,
                                jstring jtarget, jobject jproperties)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::remove(jobject jsession, jstring jrelpath, jlong jrevision)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::copy(jobject jsession,
                        jstring jsrc_relpath, jlong jsrc_revision,
                        jstring jdst_relpath, jlong jreplaces_revision)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::move(jobject jsession,
                        jstring jsrc_relpath, jlong jsrc_revision,
                        jstring jdst_relpath, jlong jreplaces_revision)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::rotate(jobject jsession, jobject jelements)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }
}

void CommitEditor::complete(jobject jsession)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }

  // do stuff

  m_valid = false;
}

void CommitEditor::abort(jobject jsession)
{
  RemoteSession* session = RemoteSession::getCppObject(jsession);
  CPPADDR_NULL_PTR(session,);
  SVN_JNI_ERR(session->m_context->checkCancel(session->m_context),);

  if (!m_valid || 1/*FIXME:*/)
    {
      throw_editor_inactive();
      return;
    }

  // do stuff

  m_valid = false;
}
