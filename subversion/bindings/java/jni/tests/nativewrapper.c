/**
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
 *
 */

#include <jni.h>
#include "nativewrapper.h"
#include "../vector.h"
#include "../date.h"
#include "../entry.h"
#include "../hashtable.h"
#include "../misc.h"
#include "../status.h"
#include "../string.h"
#include "../statuskind.h"
#include "../nodekind.h"
#include "../schedule.h"
#include "../revision.h"

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_vectorCreate
(JNIEnv *env, jclass vectorClass)
{
  return vector__create(env, NULL);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_vectorAdd
(JNIEnv *env, jclass clazz, jobject vector, jobject value)
{
  vector__add(env, vector, value, NULL);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_dateCreate
(JNIEnv *env, jclass clazz, jlong time)
{
  return date__create(env, NULL, time);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_dateCreateFromAprTimeT
(JNIEnv *env, jclass clazz, jlong time)
{
  return date__create_from_apr_time_t(env, NULL, time);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryCreate
(JNIEnv *env, jclass clazz)
{
  return entry__create(env, NULL);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryCreateFromSvnWcEntryT
(JNIEnv *env, jclass clazz, jobject entry)
{
  misc__throw_exception_by_name(env, "org/tigris/subversion/ToBeDoneException", NULL);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetUrl
(JNIEnv *env, jclass clazz, jobject entry, jstring url)
{
  entry__set_url(env, NULL, entry, url);
}

JNIEXPORT jstring JNICALL 
Java_NativeWrapper_entryGetUrl
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_url(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetRevision
(JNIEnv *env, jclass clazz, jobject entry, jobject revision)
{
  entry__set_revision(env, NULL, entry, revision);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryGetRevision
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_revision(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetKind
(JNIEnv *env, jclass clazz, jobject entry, jobject kind)
{
  entry__set_kind(env, NULL, entry, kind);
}
  
JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryGetKind
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_kind(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetSchedule
(JNIEnv *env, jclass clazz, jobject entry, jobject schedule)
{
  entry__set_schedule(env, NULL, entry, schedule);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryGetSchedule
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_schedule(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetConflicted
(JNIEnv *env, jclass clazz, jobject entry, jboolean conflicted)
{
  entry__set_conflicted(env, NULL, entry, conflicted);
}

JNIEXPORT jboolean JNICALL 
Java_NativeWrapper_entryGetConflicted
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_conflicted(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetCopied
(JNIEnv *env, jclass clazz, jobject entry, jboolean copied)
{
  entry__set_copied(env, NULL, entry, copied);
}

JNIEXPORT jboolean JNICALL 
Java_NativeWrapper_entryGetCopied
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_copied(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetTexttime
(JNIEnv *env, jclass clazz, jobject entry, jobject texttime)
{
  entry__set_texttime(env, NULL, entry, texttime);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryGetTexttime
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_texttime(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetProptime
(JNIEnv *env, jclass clazz, jobject entry, jobject proptime)
{
  entry__set_proptime(env, NULL, entry, proptime);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryGetProptime
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_proptime(env, NULL, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_entrySetAttributes
(JNIEnv *env, jclass clazz, jobject entry, jobject attributes)
{
  entry__set_attributes(env, NULL, entry, attributes);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_entryGetAttributes
(JNIEnv *env, jclass clazz, jobject entry)
{
  return entry__get_attributes(env, NULL, entry);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_hashtableCreate
(JNIEnv *env, jclass clazz)
{
  return hashtable__create(env, NULL);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_hashtablePut
(JNIEnv *env, jclass clazz, jobject hashtable, jobject key, jobject value)
{
  hashtable__put(env, hashtable, key, value, NULL);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_miscThrowExceptionByName
(JNIEnv *env, jclass clazz, jstring jname, jstring jmessage)
{
  apr_pool_t *pool = NULL;
  svn_string_t *name = NULL;
  svn_string_t *message = NULL;
  jboolean _hasException = JNI_FALSE;
  
  // prepare and convert...
  apr_pool_create(&pool, NULL);
  name = string__j_to_svn_string(env, jname, &_hasException, pool);
  if( !_hasException )
    {
      message = string__j_to_svn_string(env, jmessage, &_hasException, pool);
    }

  if( !_hasException )
    {
      // now comes the main action: throwing the exception
      misc__throw_exception_by_name(env, 
                                    name->data, 
                                    message->data);
    }
    
  // cleanup
  apr_pool_destroy(pool);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_statusCreate
(JNIEnv *env, jclass clazz)
{
  return status__create(env, NULL);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_statusSetEntry
(JNIEnv *env, jclass clazz, jobject status, jobject entry)
{
  status__set_entry(env, NULL, status, entry);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_statusSetTextStatus
(JNIEnv *env, jclass clazz, jobject status, jobject text_status)
{
  status__set_text_status(env, NULL, status, text_status);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_statusSetPropStatus
(JNIEnv *env, jclass clazz, jobject status, jobject prop_status)
{
  status__set_prop_status(env, NULL, status, prop_status);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_statusSetCopied
(JNIEnv *env, jclass clazz, jobject status, jboolean copied)
{
  status__set_copied(env, NULL, status, copied);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_statusSetLocked
(JNIEnv *env, jclass clazz, jobject status, jboolean locked)
{
  status__set_locked(env, NULL, status, locked);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_statusSetReposTextStatus
(JNIEnv *env, jclass clazz, jobject status, jobject repos_text_status)
{
  status__set_repos_text_status(env, NULL, status, repos_text_status);
}

JNIEXPORT void JNICALL 
Java_NativeWrapper_statusSetReposPropStatus
(JNIEnv *env, jclass clazz, jobject status, jobject repos_prop_status)
{
  status__set_repos_prop_status(env, NULL, status, repos_prop_status);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_nodekindCreate
(JNIEnv *env, jclass clazz, jint kind)
{
  return nodekind__create(env, NULL, kind);
}
  
JNIEXPORT jobject JNICALL 
Java_NativeWrapper_revisionCreate
(JNIEnv *env, jclass clazz, jlong rev)
{
  return revision__create(env, NULL, rev);
}

JNIEXPORT jobject JNICALL 
Java_NativeWrapper_statuskindCreate
(JNIEnv *env, jclass clazz, jint kind)
{
  return statuskind__create(env, NULL, kind);
}
