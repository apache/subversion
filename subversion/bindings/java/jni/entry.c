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

/*** Defines ***/
#define SVN_JNI_ENTRY__CLASS "org/tigris/subversion/lib/Entry"
#define SVN_JNI_ENTRY__SIG "(JLjava/lang/String;" \
"IIILjava/util/Date;Ljava/util/Date;Ljava/util/Hashtable;)V"

/*** Code ***/
jobject
entry__create(JNIEnv *env, jboolean *hasException,
	      svn_wc_entry_t *entry)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

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
      jobject jtext_time = NULL;
      jobject jprop_time = NULL;
      jobject jattrobutes = NULL;
      
      entryClass = j__get_class(env, &_hasException,
                                SVN_JNI_ENTRY__CLASS);
      
      if( !_hasException )
        {
          entryConstructor = j__get_method(env, &_hasException,
                                           entryClass,
                                           "<init>",
                                           SVN_JNI_ENTRY__SIG);
        }
      
      if( !_hasException )
        {
          jurl = string_c_to_j(env, (char*)entry->url.data, 
                               &_hasException);
        }
      
      if( !_hasException )
        {
          jtext_time = date_apr_to_j(env, &_hasException, 
                                     entry->text_time);
        }
      
      if( !_hasException )
        {
          jprop_time = date_apr_to_j(env, &_hasException,
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
          result = (*env)->NewObject(env, entryClass,
                                     entryConstructor,
                                     entry->revision,
                                     jurl,
                                     entry->nodekind,
                                     entry->schedule,
                                     entry->existence,
                                     jtext_time,
                                     jprop_time,
                                     jattributes);
          
          if( result == NULL )
            {
              _hasException = JNI_TRUE;
            }
        }
      
      (*env)->PopLocalFrame(env, result);
    }
 
  if( hasException != NULL )
    {
      *hasException = _hasException;
    }
            
  return result;
}
            
	


/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */





