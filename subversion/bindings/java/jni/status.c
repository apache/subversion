/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Status
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
#include "global.h"
#include "entry.h"
#include "status.h"

/*** Defines ***/
#define SVN_JNI_STATUS__CLASS "org/tigris/subversion/lib/Status"
#define SVN_JNI_STATUS__SIG "()V"
#define SVN_JNI_STATUS__SET_ENTRY "setEntry"
#define SVN_JNI_STATUS__SET_ENTRY_SIG "(Lorg/tigris/subversion/lib/Entry;)V"
#define SVN_JNI_STATUS__SET_REPOS_REV "setReposRev"
#define SVN_JNI_STATUS__SET_REPOS_REV_SIG "(J)V"
#define SVN_JNI_STATUS__SET_TEXT_STATUS "setTextStatus"
#define SVN_JNI_STATUS__SET_TEXT_STATUS_SIG "(I)V"
#define SVN_JNI_STATUS__SET_PROP_STATUS "setPropStatus"
#define SVN_JNI_STATUS__SET_PROP_STATUS_SIG "(I)V"
#define SVN_JNI_STATUS__SET_LOCKED "setLocked"
#define SVN_JNI_STATUS__SET_LOCKED_SIG "(Z)V"
#define SVN_JNI_STATUS__SET_REPOS_TEXT_STATUS "setReposTextStatus"
#define SVN_JNI_STATUS__SET_REPOS_TEXT_STATUS_SIG "(I)V"
#define SVN_JNI_STATUS__SET_REPOS_PROP_STATUS "setReposPropStatus"
#define SVN_JNI_STATUS__SET_REPOS_PROP_STATUS_SIG "(I)V"


/*** Code ***/
jobject
status__create(JNIEnv *env, svn_wc_status_t *status, 
               jboolean *hasException)
{
  jobject jstatus = NULL;
  jobject jentry = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__create(");
  SVN_JNI__DEBUG_PTR(status);
  if( status != NULL )
    {
      SVN_JNI__DEBUG_PTR(status->entry);
    }
  fprintf(stderr, ")\n");
#endif

  /* 
   * needed references:
   * - statusClass
   * - statusConstructor
   * - jentry
   * - jstatus
   * = 4
   */
  if( (*env)->PushLocalFrame(env, 4) >= 0 )
    {
      jclass statusClass = NULL;
      jmethodID statusConstructor = NULL;

      /* create java Entry from status->entry */
      jentry = entry__create(env, &_hasException,
                                 status->entry);
      
      if( !_hasException )
        {
          statusClass = j__get_class(env, &_hasException,
                                     SVN_JNI_STATUS__CLASS);
        }

      if( !_hasException )
	{
	  statusConstructor = j__get_method(env, &_hasException,
                                            statusClass,
                                            "<init>", 
                                            SVN_JNI_STATUS__SIG);

	  if( statusConstructor == NULL )
	  {
	      _hasException = JNI_TRUE;
	  }
	}

      if( !_hasException )
        {
          jstatus = (*env)->NewObject(env, statusClass,
                                      statusConstructor);
          
          _hasException = (*env)->ExceptionCheck(env);
        }

      if( !_hasException )
        {
          status__set_entry(env, &_hasException, 
                            jstatus, jentry);
        }

      if( !_hasException )
        {
          status__set_repos_rev(env, &_hasException,
                                jstatus, status->repos_rev);
        }

      if( !_hasException )
        {
          status__set_text_status(env, &_hasException,
                                  jstatus, status->text_status);
        }

      if( !_hasException )
        {
          status__set_prop_status(env, &_hasException,
                                  jstatus, status->prop_status);
        }

      if( !_hasException )
        {
          status__set_locked(env, &_hasException,
                             jstatus, status->locked);
        }

      if( !_hasException )
        {
          status__set_repos_text_status(env, &_hasException,
                                        jstatus, 
                                        status->repos_text_status);
        }

      if( !_hasException )
        {
          status__set_repos_prop_status(env, &_hasException,
                                        jstatus,
                                        status->repos_prop_status);
        }
#ifdef SVN_JNI__VERBOSE
      SVN_JNI__DEBUG_PTR(statusClass);
      SVN_JNI__DEBUG_PTR(statusConstructor);
      SVN_JNI__DEBUG_PTR(jstatus);
#endif

      (*env)->PopLocalFrame(env, jstatus);
    }

#ifdef SVN_JNI__VERBOSE
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<status__create\n");
#endif

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }

  return jstatus;
}

void
status__set_entry(JNIEnv *env, jboolean *hasException,
                  jobject jstatus, jobject jentry)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__set_entry(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_PTR(jentry);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException, 
                SVN_JNI_STATUS__CLASS,
                SVN_JNI_STATUS__SET_ENTRY,
                SVN_JNI_STATUS__SET_ENTRY_SIG,
                jstatus, jentry);
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<status__set_entry\n");
#endif
}

void
status__set_repos_rev(JNIEnv *env, jboolean *hasException,
                      jobject jstatus, jlong jrepos_rev)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__set_repos_rev(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(jrepos_rev);
  fprintf(stderr, ")\n");
#endif
  j__set_long(env, hasException,
              SVN_JNI_STATUS__CLASS, 
              SVN_JNI_STATUS__SET_REPOS_REV,
              jstatus, jrepos_rev);
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<status__set_repos_rev\n");
#endif
}

void
status__set_text_status(JNIEnv *env, jboolean *hasException,
                        jobject jstatus, jint jtext_status)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__set_text_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(jtext_status);
  fprintf(stderr, ")\n");
#endif
  j__set_int(env, hasException,
             SVN_JNI_STATUS__CLASS,
             SVN_JNI_STATUS__SET_TEXT_STATUS,
             jstatus, jtext_status);
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<status__set_text_status\n");
#endif
}

void 
status__set_prop_status(JNIEnv *env, jboolean *hasException,
                        jobject jstatus, jint jprop_status)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__set_prop_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(jprop_status);
  fprintf(stderr, ")\n");
#endif
  j__set_int(env, hasException,
             SVN_JNI_STATUS__CLASS,
             SVN_JNI_STATUS__SET_PROP_STATUS,
             jstatus, jprop_status);
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<status__set_prop_status\n");
#endif
}

void
status__set_locked(JNIEnv *env, jboolean *hasException,
                   jobject jstatus, jboolean jlocked)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__set_locked(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(jlocked);
  fprintf(stderr, ")\n");
#endif
  j__set_boolean(env, hasException,
                 SVN_JNI_STATUS__CLASS,
                 SVN_JNI_STATUS__SET_LOCKED,
                 jstatus, jlocked);
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<status__set_locked\n");
#endif
}

void 
status__set_repos_text_status(JNIEnv *env, jboolean *hasException,
                              jobject jstatus, 
                              jint jrepos_text_status)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__set_repos_text_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(jrepos_text_status);
  fprintf(stderr, ")\n");
#endif
  j__set_int(env, hasException,
             SVN_JNI_STATUS__CLASS,
             SVN_JNI_STATUS__SET_REPOS_TEXT_STATUS,
             jstatus, jrepos_text_status);
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<status__set_repos_text_status\n");
#endif
}

void
status__set_repos_prop_status(JNIEnv *env, jboolean *hasException,
                              jobject jstatus,
                              jint jrepos_prop_status)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>status__set_repos_prop_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(jrepos_prop_status);
  fprintf(stderr, ")\n");
#endif
  j__set_int(env, hasException,
             SVN_JNI_STATUS__CLASS,
             SVN_JNI_STATUS__SET_REPOS_PROP_STATUS,
             jstatus, jrepos_prop_status);
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<status__set_repos_prop_status\n");
#endif
}

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */
