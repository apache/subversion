/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Entry
 *
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
#include "schedule.h"
#include "revision.h"
#include "nodekind.h"

/*** Defines ***/
#define SVN_JNI_ENTRY__CLASS "org/tigris/subversion/lib/Entry"
#define SVN_JNI_ENTRY__SIG "()V"
#define SVN_JNI_ENTRY__SET_URL "setUrl"
#define SVN_JNI_ENTRY__SET_URL_SIG \
"(Ljava/lang/String;)V"
#define SVN_JNI_ENTRY__GET_URL "getUrl"
#define SVN_JNI_ENTRY__GET_URL_SIG \
"()Ljava/lang/String;"
#define SVN_JNI_ENTRY__SET_REVISION "setRevision"
#define SVN_JNI_ENTRY__SET_REVISION_SIG \
"(Lorg/tigris/subversion/lib/Revision;)V"
#define SVN_JNI_ENTRY__GET_REVISION "getRevision"
#define SVN_JNI_ENTRY__GET_REVISION_SIG \
"()Lorg/tigris/subversion/lib/Revision;"
#define SVN_JNI_ENTRY__SET_KIND "setKind"
#define SVN_JNI_ENTRY__SET_KIND_SIG \
"(Lorg/tigris/subversion/lib/Nodekind;)V"
#define SVN_JNI_ENTRY__GET_KIND "getKind"
#define SVN_JNI_ENTRY__GET_KIND_SIG \
"()Lorg/tigris/subversion/lib/Nodekind;"
#define SVN_JNI_ENTRY__SET_SCHEDULE "setSchedule"
#define SVN_JNI_ENTRY__SET_SCHEDULE_SIG \
"(Lorg/tigris/subversion/lib/Schedule;)V"
#define SVN_JNI_ENTRY__GET_SCHEDULE "getSchedule"
#define SVN_JNI_ENTRY__GET_SCHEDULE_SIG \
"()Lorg/tigris/subversion/lib/Schedule;"
#define SVN_JNI_ENTRY__SET_CONFLICTED "setConflicted"
#define SVN_JNI_ENTRY__GET_CONFLICTED "getConflicted"
#define SVN_JNI_ENTRY__SET_COPIED "setCopied"
#define SVN_JNI_ENTRY__GET_COPIED "getCopied"
#define SVN_JNI_ENTRY__SET_TEXTTIME "setTexttime"
#define SVN_JNI_ENTRY__SET_TEXTTIME_SIG \
"(Ljava/util/Date;)V"
#define SVN_JNI_ENTRY__GET_TEXTTIME "getTexttime"
#define SVN_JNI_ENTRY__GET_TEXTTIME_SIG \
"()Ljava/util/Date;"
#define SVN_JNI_ENTRY__SET_PROPTIME "setProptime"
#define SVN_JNI_ENTRY__SET_PROPTIME_SIG \
"(Ljava/util/Date;)V"
#define SVN_JNI_ENTRY__GET_PROPTIME "getProptime"
#define SVN_JNI_ENTRY__GET_PROPTIME_SIG \
"()Ljava/util/Date;"
#define SVN_JNI_ENTRY__SET_ATTRIBUTES "setAttributes"
#define SVN_JNI_ENTRY__SET_ATTRIBUTES_SIG \
"(Ljava/util/Hashtable;)V"
#define SVN_JNI_ENTRY__GET_ATTRIBUTES "getAttributes"
#define SVN_JNI_ENTRY__GET_ATTRIBUTES_SIG \
"()Ljava/util/Hashtable;"

/*
 * Do you want to debug code in this file?
 * Just uncomment the following define.
 */
//#define SVN_JNI_ENTRY__DEBUG

/*** Code ***/
jobject
entry__create(JNIEnv *env, jboolean *hasException)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__create()\n");
#endif

  /*
   * needed references:
   * -result
   * = 1
   */
    
  if( (*env)->PushLocalFrame(env, 1) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      jclass entryClass = NULL;
      jmethodID entryConstructor = NULL;
     
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

      (*env)->PopLocalFrame(env, result);
    }

#ifdef SVN_JNI_ENTRY__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  SVN_JNI__DEBUG_PTR(result);
  if( _hasException )
  fprintf(stderr, "\n<<<entry__create\n");
#endif
 
  if( (hasException != NULL) && _hasException )
    {
      *hasException = JNI_TRUE;
    }
            
  return result;
}

jobject
entry__create_from_svn_wc_entry_t(JNIEnv *env, jboolean *hasException,
                                  svn_wc_entry_t *entry)
{
  jboolean _hasException = JNI_FALSE;
  jobject result = NULL;

#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__create_from_wc_entry_t(");
  SVN_JNI__DEBUG_PTR(entry);
  if( entry != NULL )
    {
      SVN_JNI__DEBUG_STR(entry->url);
    }
  fprintf(stderr, ")\n");
#endif

  /*
   * needed references:
   * -result
   * -url
   * -schedule
   * -text_time
   * -prop_time
   * -attributes
   * -result
   * = 7
   */
    
  if( (*env)->PushLocalFrame(env, 7) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      /*
       * create the instance of the
       * java class Entry
       */

      result = entry__create(env, &_hasException);
      
       /*
        * convert the structure members to the
        * corresponding java types
        */

      // member: revision
      if( !_hasException )
        {
          jobject revision = revision__create(env, &_hasException,
                                              entry->revision);

          if( !_hasException )
            {
              entry__set_revision(env, &_hasException,
                                  result, revision);
            }
        }
      
      // member: url
      if( !_hasException )
        {
          jstring url = 
            string__c_to_j(env, (char*)entry->url->data, 
                           &_hasException);

          if( !_hasException )
            {
              entry__set_url(env, &_hasException, 
                             result, url);
            }
        }

      // member: kind
      if( !_hasException )
        {
          jobject kind = 
            nodekind__create_from_svn_node_kind(env, 
                                                &_hasException,
                                                entry->kind);

          if( !_hasException )
            {
              entry__set_kind(env, &_hasException,
                              result, kind);
            }
        }

      // member: schedule
      if( !_hasException )
        {
          jobject schedule = 
            schedule__create_from_svn_wc_schedule_t(env,
                                                    &_hasException,
                                                    entry->schedule);

          if( !_hasException )
            {
              entry__set_schedule(env, &_hasException,
                                  result, schedule);
            }
        }


      // member: conflicted
      if( !_hasException )
        {
          entry__set_conflicted(env, &_hasException,
                                result, entry->conflicted);
        }

      // member: copied
      if( !_hasException )
        {
          entry__set_copied(env, &_hasException,
                            result, entry->copied);
        }

      // member: text_time
      if( !_hasException )
        {
          jobject text_time = 
            date__create_from_apr_time_t(env, &_hasException, 
                                         entry->text_time);

          if( !_hasException )
            {
              entry__set_texttime(env, &_hasException,
                                  result, text_time);
            }
       }

      // member: prop_time
      if( !_hasException )
        {
          jobject prop_time = 
            date__create_from_apr_time_t(env, &_hasException,
                                         entry->prop_time);

          if( !_hasException )
            {
              entry__set_proptime(env, &_hasException,
                                  result, prop_time);
            }
        }
      
      // member: attributes
      if( !_hasException )
        {
          jobject attributes = hashtable__create(env, &_hasException);
          
          /* 
           * TODO: conversion of the apr_hashtable with the
           * attributes to a java hashtable
           * NOW THERE IS ONLY AN EMPTY HASHTABLE!!!!
           */

          if( !_hasException )
            {
              entry__set_attributes(env, &_hasException,
                                    result, attributes);
            }
        }

      (*env)->PopLocalFrame(env, NULL);
    }
#ifdef SVN_JNI_ENTRY__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  if( _hasException )
  fprintf(stderr, "\n<<<entry__create_from_wc_entry_t\n");
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
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__set_url(...)\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_URL,
                SVN_JNI_ENTRY__SET_URL_SIG,
                jentry, jurl);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_url\n");
#endif
}

jstring
entry__get_url(JNIEnv *env, jboolean *hasException,
               jobject jentry)
{
  jstring result = NULL;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_url\n");
#endif
  result= j__get_object(env, hasException,
                        SVN_JNI_ENTRY__CLASS,
                        SVN_JNI_ENTRY__GET_URL,
                        SVN_JNI_ENTRY__GET_URL_SIG,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_url\n");
#endif
  return result;
}

void
entry__set_revision(JNIEnv *env, jboolean *hasException,
                    jobject jentry, jobject jrevision)
{
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__set_revision(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_PTR(jrevision);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_REVISION,
                SVN_JNI_ENTRY__SET_REVISION_SIG,
                jentry, jrevision);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_revision\n");
#endif
}

jobject
entry__get_revision(JNIEnv *env, jboolean *hasException,
                    jobject jentry)
{
  jobject result = NULL;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_revision\n");
#endif
  result= j__get_object(env, hasException,
                        SVN_JNI_ENTRY__CLASS,
                        SVN_JNI_ENTRY__GET_REVISION,
                        SVN_JNI_ENTRY__GET_REVISION_SIG,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_revision\n");
#endif
  return result;
}

void
entry__set_kind(JNIEnv *env, jboolean *hasException,
                    jobject jentry, jobject jkind)
{
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__set_kind(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_PTR(jkind);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_KIND,
                SVN_JNI_ENTRY__SET_KIND_SIG,
                jentry, jkind);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_kind\n");
#endif
}

jobject
entry__get_kind(JNIEnv *env, jboolean *hasException,
                    jobject jentry)
{
  jobject result = NULL;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_kind\n");
#endif
  result= j__get_object(env, hasException,
                        SVN_JNI_ENTRY__CLASS,
                        SVN_JNI_ENTRY__GET_KIND,
                        SVN_JNI_ENTRY__GET_KIND_SIG,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_kind\n");
#endif
  return result;
}

void
entry__set_schedule(JNIEnv *env, jboolean *hasException,
                    jobject jentry, jobject jschedule)
{
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__set_schedule(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_PTR(jschedule);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException,
                SVN_JNI_ENTRY__CLASS,
                SVN_JNI_ENTRY__SET_SCHEDULE,
                SVN_JNI_ENTRY__SET_SCHEDULE_SIG,
                jentry, jschedule);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_schedule\n");
#endif
}

jobject
entry__get_schedule(JNIEnv *env, jboolean *hasException,
                    jobject jentry)
{
  jobject result = NULL;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_schedule\n");
#endif
  result= j__get_object(env, hasException,
                        SVN_JNI_ENTRY__CLASS,
                        SVN_JNI_ENTRY__GET_SCHEDULE,
                        SVN_JNI_ENTRY__GET_SCHEDULE_SIG,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_schedule\n");
#endif
  return result;
}

void 
entry__set_conflicted(JNIEnv *env, jboolean *hasException,
                      jobject jentry, jboolean jconflicted)
{
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__set_conflicted(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_BOOL(jconflicted);
  fprintf(stderr, ");");
#endif
  j__set_boolean(env, hasException,
                 SVN_JNI_ENTRY__CLASS, SVN_JNI_ENTRY__SET_CONFLICTED,
                 jentry, jconflicted);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_conflicted\n");
#endif
}

jboolean
entry__get_conflicted(JNIEnv *env, jboolean *hasException,
                      jobject jentry)
{
  jboolean result = JNI_FALSE;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_conflicted\n");
#endif
  result= j__get_boolean(env, hasException,
                         SVN_JNI_ENTRY__CLASS,
                         SVN_JNI_ENTRY__GET_CONFLICTED,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_conflicted\n");
#endif
  return result;
}

void 
entry__set_copied(JNIEnv *env, jboolean *hasException,
                  jobject jentry, jboolean jcopied)
{
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__set_copied(");
  SVN_JNI__DEBUG_PTR(jentry);
  SVN_JNI__DEBUG_BOOL(jcopied);
  fprintf(stderr, ");");
#endif
  j__set_boolean(env, hasException,
                 SVN_JNI_ENTRY__CLASS, SVN_JNI_ENTRY__SET_COPIED,
                 jentry, jcopied);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_copied\n");
#endif
}

jboolean
entry__get_copied(JNIEnv *env, jboolean *hasException,
                  jobject jentry)
{
  jboolean result = JNI_FALSE;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_copied\n");
#endif
  result= j__get_boolean(env, hasException,
                         SVN_JNI_ENTRY__CLASS,
                         SVN_JNI_ENTRY__GET_COPIED,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_copied\n");
#endif
  return result;
}

void 
entry__set_texttime(JNIEnv *env, jboolean *hasException,
                     jobject jentry, jobject jtexttime)
{
#ifdef SVN_JNI_ENTRY__DEBUG
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
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_texttime\n");
#endif
}

jobject
entry__get_texttime(JNIEnv *env, jboolean *hasException,
                    jobject jentry)
{
  jobject result = NULL;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_texttime\n");
#endif
  result= j__get_object(env, hasException,
                        SVN_JNI_ENTRY__CLASS,
                        SVN_JNI_ENTRY__GET_TEXTTIME,
                        SVN_JNI_ENTRY__GET_TEXTTIME_SIG,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_texttime\n");
#endif
  return result;
}

void 
entry__set_proptime(JNIEnv *env, jboolean *hasException,
                     jobject jentry, jobject jproptime)
{
#ifdef SVN_JNI_ENTRY__DEBUG
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
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry_set_proptime\n");
#endif
}

jobject
entry__get_proptime(JNIEnv *env, jboolean *hasException,
                    jobject jentry)
{
  jobject result = NULL;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_proptime\n");
#endif
  result= j__get_object(env, hasException,
                        SVN_JNI_ENTRY__CLASS,
                        SVN_JNI_ENTRY__GET_PROPTIME,
                        SVN_JNI_ENTRY__GET_PROPTIME_SIG,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_proptime\n");
#endif
  return result;
}

void 
entry__set_attributes(JNIEnv *env, jboolean *hasException,
                      jobject jentry, jobject jattributes)
{
#ifdef SVN_JNI_ENTRY__DEBUG
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
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__set_attributes\n");
#endif
}

jobject
entry__get_attributes(JNIEnv *env, jboolean *hasException,
                      jobject jentry)
{
  jobject result = NULL;
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, ">>>entry__get_attributes\n");
#endif
  result= j__get_object(env, hasException,
                        SVN_JNI_ENTRY__CLASS,
                        SVN_JNI_ENTRY__GET_ATTRIBUTES,
                        SVN_JNI_ENTRY__GET_ATTRIBUTES_SIG,
                        jentry);
#ifdef SVN_JNI_ENTRY__DEBUG
  fprintf(stderr, "\n<<<entry__get_attributes\n");
#endif
  return result;
}
