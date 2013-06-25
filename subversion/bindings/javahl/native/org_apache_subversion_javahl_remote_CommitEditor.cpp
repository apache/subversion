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
    jobject jcommit_callback, jobject jlock_tokens, jboolean jkeep_locks)
{
  jobject jthis = NULL;         // Placeholder -- this is a static method
  JNIEntry(CommitEditor, nativeCreateInstance);

  return CommitEditor::createInstance(jsession, jrevprops, jcommit_callback,
                                      jlock_tokens, jkeep_locks);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAddDirectory(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jobject jchildren, jobject jproperties,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, nativeAddDirectory);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addDirectory(jsession,
                       jrelpath, jchildren, jproperties, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAddFile(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jobject jchecksum, jobject jcontents,
    jobject jproperties, jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, nativeAddFile);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addFile(jsession,
                  jrelpath, jchecksum, jcontents, jproperties,
                  jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAddSymlink(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jstring jtarget, jobject jproperties,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, nativeAddSymlink);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addSymlink(jsession,
                     jrelpath, jtarget, jproperties, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAddAbsent(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jobject jkind, jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, nativeAddAbsent);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->addAbsent(jsession,
                    jrelpath, jkind, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAlterDirectory(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jlong jrevision, jobject jchildren, jobject jproperties)
{
  JNIEntry(CommitEditor, nativeAlterDirectory);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->alterDirectory(jsession,
                         jrelpath, jrevision, jchildren, jproperties);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAlterFile(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jlong jrevision, jobject jchecksum, jobject jcontents,
    jobject jproperties)
{
  JNIEntry(CommitEditor, nativeAlterFile);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->alterFile(jsession,
                    jrelpath, jrevision, jchecksum, jcontents, jproperties);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAlterSymlink(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jlong jrevision, jstring jtarget, jobject jproperties)
{
  JNIEntry(CommitEditor, nativeAlterSymlink);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->alterSymlink(jsession,
                       jrelpath, jrevision, jtarget, jproperties);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeDelete(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jrelpath, jlong jrevision)
{
  JNIEntry(CommitEditor, nativeDelete);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->remove(jsession, jrelpath, jrevision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeCopy(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jsrc_relpath, jlong jsrc_revision, jstring jdst_relpath,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, nativeCopy);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->copy(jsession,
               jsrc_relpath, jsrc_revision, jdst_relpath, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeMove(
    JNIEnv* env, jobject jthis, jobject jsession,
    jstring jsrc_relpath, jlong jsrc_revision, jstring jdst_relpath,
    jlong jreplaces_revision)
{
  JNIEntry(CommitEditor, nativeMove);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->move(jsession,
               jsrc_relpath, jsrc_revision, jdst_relpath, jreplaces_revision);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeRotate(
    JNIEnv* env, jobject jthis, jobject jsession, jobject jelements)
{
  JNIEntry(CommitEditor, nativeRotate);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->rotate(jsession, jelements);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeComplete(
    JNIEnv* env, jobject jthis, jobject jsession)
{
  JNIEntry(CommitEditor, nativeComplete);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->complete(jsession);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_remote_CommitEditor_nativeAbort(
    JNIEnv* env, jobject jthis, jobject jsession)
{
  JNIEntry(CommitEditor, nativeAbort);
  CommitEditor *editor = CommitEditor::getCppObject(jthis);
  CPPADDR_NULL_PTR(editor,);
  editor->abort(jsession);
}
