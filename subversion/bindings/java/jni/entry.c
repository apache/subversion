/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Entry
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
 */

/*** Includes ***/
#include <jni.h>
#include <svn_wc.h>
#include "j.h"
#include "string.h"
#include "date.h"
#include "global.h"
#include "hashtable.h"
#include "entry.h"

/*** Defines ***/
#define SVN_JNI_ENTRY__CLASS "org/tigris/subversion/lib/Entry"
#define SVN_JNI_ENTRY__SIG "()V"
#define SVN_JNI_ENTRY__SET_URL "setUrl"
#define SVN_JNI_ENTRY__SET_URL_SIG "(Ljava/lang/String;)V"
#define SVN_JNI_ENTRY__SET_REVISION "setRevision"
#define SVN_JNI_ENTRY__SET_NODEKIND "setNodekind"
#define SVN_JNI_ENTRY__SET_SCHEDULE "setSchedule"
#define SVN_JNI_ENTRY__SET_CONFLICTED "setConflicted"
#define SVN_JNI_ENTRY__SET_CONFLICTED_SIG "(Z)V"
#define SVN_JNI_ENTRY__SET_COPIED "setCopied"
#define SVN_JNI_ENTRY__SET_COPIED_SIG "(Z)V"
#define SVN_JNI_ENTRY__SET_TEXTTIME "setTexttime"
#define SVN_JNI_ENTRY__SET_TEXTTIME_SIG "(Ljava/util/Date;)V"
#define SVN_JNI_ENTRY__SET_PROPTIME "setProptime"
#define SVN_JNI_ENTRY__SET_PROPTIME_SIG "(Ljava/util/Date;)V"
#define SVN_JNI_ENTRY__SET_ATTRIBUTES "setAttributes"
#define SVN_JNI_ENTRY__SET_ATTRIBUTES_SIG "(Ljava/util/Hashtable;)V"

/*
 * Do you want to debug code in this file?
 * Just uncomment the following define.
 */
#define SVN_JNI__DEBUG_STATUS

/*** Code ***/
jobject
entry__create(JNIEnv *env, jboolean *hasException,
	      svn_wc_entry_t *entry)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__create(");
  SVN_JNI__DEBUG_PTR(entry);
  if( entry != NULL )
    {
      SVN_JNI__DEBUG_STR(entry->url);
    }
  fprintf(stderr, ")\n");
#endif

  /*
   * needed references:
   * -entryClass
   * -entryConstructor
   * -jurl
   * -jtext_time
   * -jprop_time
   * -jattributes
   * -result
   * = 7
   */
    
  if( (*env)->PushLocalFrame(env, 7) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      jclass entryClass = NULL;
      jmethodID entryConstructor = NULL;
      jstring jurl = NULL;
      jobject jtexttime = NULL;
      jobject jproptime = NULL;
      jobject jattributes = NULL;
      
      jurl = string__c_to_j(env, (char*)entry->url->data, 
                            &_hasException);
            
      if( !_hasException )
        {
          jtexttime = date__apr_to_j(env, &_hasException, 
                                     entry->text_time);
        }
      
      if( !_hasException )
        {
          jproptime = date__apr_to_j(env, &_hasException,
                                     entry->prop_time);
        }
      
      if( !_hasException )
        {
          jattributes = hashtable__create(env, &_hasException);
          
          /* 
           * TODO: conversion of the apr_hashtable with the
           * attributes to a java hashtable
           * NOW THERE IS ONLY AN EMPTY HASHTABLE!!!!
           */
        }
      
      if( !_hasException )
        {
          entryClass = j__get_class(env, &_hasException,
                                    SVN_JNI_ENTRY__CLASS);
        }
      
      if( !_hasException )
        {
          entryConstructor = j__get_method(env, &_hasException,
                                           entryClass,
                                           "<init>",
                                           SVN_JNI_ENTRY__SIG);
        }
      if( !_hasException )
        {
          result = (*env)->NewObject(env, entryClass, 
                                     entryConstructor);
          if( result == NULL )
            {
              _hasException = JNI_TRUE;
            }

        }

#ifdef SVN_JNI__DEBUG_ENTRY
      SVN_JNI__DEBUG_PTR(result);
#endif
      
      if( !_hasException )
        {
          entry__set_revision(env, &_hasException,
                              result, entry->revision);
        }

      if( !_hasException )
        {
          entry__set_url(env, &_hasException, 
                         result, jurl);
        }

      if( !_hasException )
        {
          entry__set_nodekind(env, &_hasException,
                              result, entry->kind);
        }

      if( !_hasException )
        {
          entry__set_schedule(env, &_hasException,
                              result, entry->schedule);
        }

      if( !_hasException )
        {
          entry__set_conflicted(env, &_hasException,
                                result, entry->conflicted);
        }

      if( !_hasException )
        {
          entry__set_copied(env, &_hasException,
                            result, entry->copied);
        }

      if( !_hasException )
        {
          entry__set_texttime(env, &_hasException,
                              result, jtexttime);
        }

      if( !_hasException )
        {
          entry__set_proptime(env, &_hasException,
                              result, jproptime);
        }

      if( !_hasException )
        {
          entry__set_attributes(env, &_hasException,
                                result, jattributes);
        }
      
      (*env)->PopLocalFrame(env, result);
    }
#ifdef SVN_JNI__DEBUG_ENTRY
  SVN_JNI__DEBUG_BOOL(_hasException);
  if( _hasException )
  fprintf(stderr, "\n<<<entry__create\n");
#endif
 
  if( (hasException != NULL) && _hasException )
    {
      *hasException = JNI_TRUE;
    }
            
  return result;
}

void
entry__set_url(JNIEnv *env, jboolean *hasException,
               jobject jentry, jstring jurl)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_url(...)\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_URL,
                SVN_JNI_ENTRY__SET_URL_SIG,
                jentry, jurl);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_url\n");
#endif
}

void
entry__set_revision(JNIEnv *env, jboolean *hasException,
                    jobject jentry, jlong jrevision)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_revision(...)\n");
#endif
  j__set_long(env, hasException, 
              SVN_JNI_ENTRY__CLASS, SVN_JNI_ENTRY__SET_REVISION,
              jentry, jrevision);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_revision\n");
#endif
}

void
entry__set_nodekind(JNIEnv *env, jboolean *hasException,
                    jobject jentry, jint jnodekind)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_nodekind(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_DEC(jnodekind);
  fprintf(stderr, ")\n");
#endif
  j__set_int(env, hasException, 
             SVN_JNI_ENTRY__CLASS, SVN_JNI_ENTRY__SET_NODEKIND,
             jentry, jnodekind);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_nodekind\n");
#endif
}

void
entry__set_schedule(JNIEnv *env, jboolean *hasException,
                    jobject jentry, jint jschedule)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_schedule(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_DEC(jschedule);
  fprintf(stderr, ")\n");
#endif
  j__set_int(env, hasException,
             SVN_JNI_ENTRY__CLASS, SVN_JNI_ENTRY__SET_SCHEDULE,
             jentry, jschedule);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_schedule\n");
#endif
}

void 
entry__set_conflicted(JNIEnv *env, jboolean *hasException,
                      jobject jentry, jboolean jconflicted)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_conflicted(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_BOOL(jconflicted);
  fprintf(stderr, ");");
#endif
  j__set_int(env, hasException,
             SVN_JNI_ENTRY__CLASS, SVN_JNI_ENTRY__SET_CONFLICTED,
             jentry, jconflicted);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_conflicted\n");
#endif
}

void 
entry__set_copied(JNIEnv *env, jboolean *hasException,
                  jobject jentry, jboolean jcopied)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_copied(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_BOOL(jcopied);
  fprintf(stderr, ");");
#endif
  j__set_int(env, hasException,
             SVN_JNI_ENTRY__CLASS, SVN_JNI_ENTRY__SET_COPIED,
             jentry, jcopied);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_copied\n");
#endif
}

void 
entry__set_texttime(JNIEnv *env, jboolean *hasException,
                     jobject jentry, jobject jtexttime)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_texttime(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_PTR(jtexttime);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_TEXTTIME,
                SVN_JNI_ENTRY__SET_TEXTTIME_SIG,
                jentry, jtexttime);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_texttime\n");
#endif
}

void 
entry__set_proptime(JNIEnv *env, jboolean *hasException,
                     jobject jentry, jobject jproptime)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_proptime(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_PTR(jproptime);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_PROPTIME,
                SVN_JNI_ENTRY__SET_PROPTIME_SIG,
                jentry, jproptime);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry_set_proptime\n");
#endif
}

void 
entry__set_attributes(JNIEnv *env, jboolean *hasException,
                      jobject jentry, jobject jattributes)
{
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, ">>>entry__set_attributes(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_PTR(jattributes);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_ATTRIBUTES,
                SVN_JNI_ENTRY__SET_ATTRIBUTES_SIG,
                jentry, jattributes);
#ifdef SVN_JNI__DEBUG_ENTRY
  fprintf(stderr, "\n<<<entry__set_attributes\n");
#endif
}

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */
