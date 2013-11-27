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
 * @file org_apache_subversion_javahl_remote_CommitEditor.cpp
 * @brief Implementation of the native methods in the Java class CommitEditor
 */

#include "../include/org_apache_subversion_javahl_remote_CommitEditor.h"

#include "JNIStackElement.h"
#include "JNIUtil.h"
#include "CommitEditor.h"

#include "svn_private_config.h"


JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_finalize(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(CommitEditor, finalize);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  if (editor != NULL)
    editor->finalize();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeDispose(
    JNIEnv *env, jobject jthis)
{
  JNIEntry(CommitEditor, nativeDispose);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  if (editor != NULL)
    editor->dispose(jthis);
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeCreateInstance(
    JNIEnv *env, jclass thisclass, jobject jsession, jobject jrevprops,
    jobject jcommit_callback, jobject jlock_tokens, jboolean jkeep_locks,
    jobject jget_base_cb, jobject jget_props_cb, jobject jget_kind_cb)
{
  jobject jthis = NULL;         // Placeholder -- this is a static method
  JNIEntry(CommitEditor, nativeCreateInstance);

  return CommitEditor::createInstance(
      jsession, jrevprops, jcommit_callback, jlock_tokens, jkeep_locks,
      jget_base_cb, jget_props_cb, jget_kind_cb);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_addDirectory(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jobject jchildren, jobject jproperties,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, addDirectory);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addDirectory(jrelpath, jchildren, jproperties, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_addFile(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jobject jchecksum, jobject jcontents,
    jobject jproperties, jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, addFile);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addFile(jrelpath, jchecksum, jcontents, jproperties,
                  jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_addSymlink(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jstring jtarget, jobject jproperties,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, addSymlink);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addSymlink(jrelpath, jtarget, jproperties, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_addAbsent(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jobject jkind, jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, addAbsent);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addAbsent(jrelpath, jkind, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_alterDirectory(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jlong jrevision, jobject jchildren, jobject jproperties)
{
  JNIEntry(CommitEditor, alterDirectory);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->alterDirectory(jrelpath, jrevision, jchildren, jproperties);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_alterFile(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jlong jrevision, jobject jchecksum, jobject jcontents,
    jobject jproperties)
{
  JNIEntry(CommitEditor, alterFile);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->alterFile(jrelpath, jrevision, jchecksum, jcontents, jproperties);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_alterSymlink(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jlong jrevision, jstring jtarget, jobject jproperties)
{
  JNIEntry(CommitEditor, alterSymlink);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->alterSymlink(jrelpath, jrevision, jtarget, jproperties);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_delete(
    JNIEnv* env, jobject jthis,
    jstring jrelpath, jlong jrevision)
{
  JNIEntry(CommitEditor, delete);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->remove(jrelpath, jrevision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_copy(
    JNIEnv* env, jobject jthis,
    jstring jsrc_relpath, jlong jsrc_revision, jstring jdst_relpath,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, copy);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->copy(jsrc_relpath, jsrc_revision, jdst_relpath, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_move(
    JNIEnv* env, jobject jthis,
    jstring jsrc_relpath, jlong jsrc_revision, jstring jdst_relpath,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, move);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->move(jsrc_relpath, jsrc_revision, jdst_relpath, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_complete(
    JNIEnv* env, jobject jthis)
{
  JNIEntry(CommitEditor, complete);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->complete();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_abort(
    JNIEnv* env, jobject jthis, jobject jsession)
{
  JNIEntry(CommitEditor, abort);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->abort();
}
