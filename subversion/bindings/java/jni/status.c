/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Status
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
#include "global.h"
#include "entry.h"
#include "status.h"
#include "statuskind.h"

/*** Defines ***/
#define SVN_JNI_STATUS__CLASS "org/tigris/subversion/lib/Status"
#define SVN_JNI_STATUS__SIG "()V"
#define SVN_JNI_STATUS__SET_ENTRY "setEntry"
#define SVN_JNI_STATUS__SET_ENTRY_SIG \
"(Lorg/tigris/subversion/lib/Entry;)V"
#define SVN_JNI_STATUS__SET_TEXT_STATUS "setTextStatus"
#define SVN_JNI_STATUS__SET_TEXT_STATUS_SIG \
"(Lorg/tigris/subversion/lib/StatusKind;)V"
#define SVN_JNI_STATUS__SET_PROP_STATUS "setPropStatus"
#define SVN_JNI_STATUS__SET_PROP_STATUS_SIG \
"(Lorg/tigris/subversion/lib/StatusKind;)V"
#define SVN_JNI_STATUS__SET_COPIED "setCopied"
#define SVN_JNI_STATUS__SET_COPIED_SIG "(Z)V"
#define SVN_JNI_STATUS__SET_LOCKED "setLocked"
#define SVN_JNI_STATUS__SET_LOCKED_SIG "(Z)V"
#define SVN_JNI_STATUS__SET_REPOS_TEXT_STATUS "setReposTextStatus"
#define SVN_JNI_STATUS__SET_REPOS_TEXT_STATUS_SIG \
"(Lorg/tigris/subversion/lib/StatusKind;)V"
#define SVN_JNI_STATUS__SET_REPOS_PROP_STATUS "setReposPropStatus"
#define SVN_JNI_STATUS__SET_REPOS_PROP_STATUS_SIG \
"(Lorg/tigris/subversion/lib/StatusKind;)V"


/*
 * Do you want to debug code in this file?
 * Just uncomment the following define
 */
//#define SVN_JNI_STATUS__DEBUG


/*** Code ***/
jobject
status__create(JNIEnv *env, jboolean *hasException)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__create()\n");
#endif

  /* 
   * needed references:
   * - status
   * = 1
   */
  if( (*env)->PushLocalFrame(env, 4) >= 0 )
    {
      jclass statusClass = NULL;
      jmethodID statusConstructor = NULL;

      statusClass = j__get_class(env, &_hasException,
                                 SVN_JNI_STATUS__CLASS);

      if( !_hasException )
	{
	  statusConstructor = 
            j__get_method(env, &_hasException,
                          statusClass, "<init>", 
                          SVN_JNI_STATUS__SIG);

	  if( statusConstructor == NULL )
	  {
	      _hasException = JNI_TRUE;
	  }
	}

      if( !_hasException )
        {
          result = (*env)->NewObject(env, statusClass,
                                     statusConstructor);
          
          _hasException = (*env)->ExceptionCheck(env);
        }

#ifdef SVN_JNI_STATUS__DEBUG
      SVN_JNI__DEBUG_PTR(statusClass);
      SVN_JNI__DEBUG_PTR(statusConstructor);
#endif
      
      (*env)->PopLocalFrame(env, result);
    }

#ifdef SVN_JNI_STATUS__DEBUG
  SVN_JNI__DEBUG_PTR(result);
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<status__create\n");
#endif

  if( (hasException != NULL) && _hasException )
    {
      *hasException = JNI_TRUE;
    }

  return result;
}

jobject
status__create_from_svn_wc_status_t(JNIEnv *env, 
                                    jboolean *hasException,
                                    svn_wc_status_t *status)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__create_from_svn_wc_status_t(");
  SVN_JNI__DEBUG_PTR(status);
  if( status != NULL )
    {
      SVN_JNI__DEBUG_PTR(status->entry);
    }
  fprintf(stderr, ")\n");
#endif

  /* 
   * needed references:
   * - entry
   * - status
   * = 2
   */
  if( (*env)->PushLocalFrame(env, 2) >= 0 )
    {
      result = status__create(env, &_hasException);

      // member: entry
      if( !_hasException )
        {
          /* create java Entry from status->entry */
          jobject entry =
            entry__create_from_svn_wc_entry_t(env, &_hasException,
                                              status->entry);

          if( !_hasException )
            {
              status__set_entry(env, &_hasException, 
                                result, entry);
            }
        }
      
      // member: text_status
      if( !_hasException )
        {
          jobject text_status =
            statuskind__create_from_svn_wc_status_kind(env, 
                                                       &_hasException, 
                                                       status->text_status);
           
          if( !_hasException )
            {
              status__set_text_status(env, &_hasException,
                                      result, text_status);
            }
        }

      // member: prop_status
      if( !_hasException )
        {
          jobject prop_status = 
            statuskind__create_from_svn_wc_status_kind(env, 
                                                       &_hasException, 
                                                       status->prop_status);

          if( !_hasException )
            {
              status__set_prop_status(env, &_hasException,
                                      result, prop_status);
            }
        }

      // member: locked
      if( !_hasException )
        {
          status__set_locked(env, &_hasException,
                             result, status->locked);
        }

      // member: copied
      if( !_hasException )
        {
          status__set_copied(env, &_hasException,
                             result, status->copied);
        }

      // member: repos_text_status
      if( !_hasException )
        {
          jobject repos_text_status =
            statuskind__create_from_svn_wc_status_kind(env,
                                                       &_hasException,
                                                       status->repos_text_status);

          if( !_hasException )
            {
              status__set_repos_text_status(env, &_hasException,
                                            result, 
                                            repos_text_status);
            }
        }

      // member: repos_prop_status
      if( !_hasException )
        {
          jobject repos_prop_status =
            statuskind__create_from_svn_wc_status_kind(env,
                                                       &_hasException,
                                                       status->repos_prop_status);

          if( !_hasException )
            {
              status__set_repos_prop_status(env, &_hasException,
                                            result,
                                            repos_prop_status);
            }
        }

      (*env)->PopLocalFrame(env, result);
    }

#ifdef SVN_JNI_STATUS__DEBUG
  SVN_JNI__DEBUG_PTR(result);
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<status__create_from_svn_wc_status_t\n");
#endif

  if( (hasException != NULL) && _hasException )
    {
      *hasException = JNI_TRUE;
    }

  return result;
}

void
status__set_entry(JNIEnv *env, jboolean *hasException,
                  jobject jstatus, jobject jentry)
{
#ifdef SVN_JNI_STATUS__DEBUG
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
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, "\n<<<status__set_entry\n");
#endif
}

void
status__set_text_status(JNIEnv *env, jboolean *hasException,
                        jobject jstatus, jobject jtext_status)
{
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__set_text_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_PTR(jtext_status);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException, 
                SVN_JNI_STATUS__CLASS,
                SVN_JNI_STATUS__SET_TEXT_STATUS,
                SVN_JNI_STATUS__SET_TEXT_STATUS_SIG,
                jstatus, jtext_status);
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, "\n<<<status__set_text_status\n");
#endif
}

void 
status__set_prop_status(JNIEnv *env, jboolean *hasException,
                        jobject jstatus, jobject jprop_status)
{
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__set_prop_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_PTR(jprop_status);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException, 
                SVN_JNI_STATUS__CLASS,
                SVN_JNI_STATUS__SET_PROP_STATUS,
                SVN_JNI_STATUS__SET_PROP_STATUS_SIG,
                jstatus, jprop_status);
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, "\n<<<status__set_prop_status\n");
#endif
}

void
status__set_copied(JNIEnv *env, jboolean *hasException,
                   jobject jstatus, jboolean copied)
{
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__set_copied(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(copied);
  fprintf(stderr, ")\n");
#endif
  j__set_boolean(env, hasException,
                 SVN_JNI_STATUS__CLASS,
                 SVN_JNI_STATUS__SET_COPIED,
                 jstatus, copied);
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, "\n<<<status__set_copied\n");
#endif
}

void
status__set_locked(JNIEnv *env, jboolean *hasException,
                   jobject jstatus, jboolean jlocked)
{
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__set_locked(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_DEC(jlocked);
  fprintf(stderr, ")\n");
#endif
  j__set_boolean(env, hasException,
                 SVN_JNI_STATUS__CLASS,
                 SVN_JNI_STATUS__SET_LOCKED,
                 jstatus, jlocked);
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, "\n<<<status__set_locked\n");
#endif
}

void 
status__set_repos_text_status(JNIEnv *env, jboolean *hasException,
                              jobject jstatus, 
                              jobject jrepos_text_status)
{
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__set_repos_text_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_PTR(jrepos_text_status);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException, 
                SVN_JNI_STATUS__CLASS,
                SVN_JNI_STATUS__SET_REPOS_TEXT_STATUS,
                SVN_JNI_STATUS__SET_REPOS_TEXT_STATUS_SIG,
                jstatus, jrepos_text_status);
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, "\n<<<status__set_repos_text_status\n");
#endif
}

void
status__set_repos_prop_status(JNIEnv *env, jboolean *hasException,
                              jobject jstatus,
                              jobject jrepos_prop_status)
{
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, ">>>status__set_repos_prop_status(");
  SVN_JNI__DEBUG_PTR(jstatus);
  SVN_JNI__DEBUG_PTR(jrepos_prop_status);
  fprintf(stderr, ")\n");
#endif
  j__set_object(env, hasException, 
                SVN_JNI_STATUS__CLASS,
                SVN_JNI_STATUS__SET_REPOS_PROP_STATUS,
                SVN_JNI_STATUS__SET_REPOS_PROP_STATUS_SIG,
                jstatus, jrepos_prop_status);
#ifdef SVN_JNI_STATUS__DEBUG
  fprintf(stderr, "\n<<<status__set_repos_prop_status\n");
#endif
}

/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */
