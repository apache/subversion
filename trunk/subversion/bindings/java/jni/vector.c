/*
 * utility functions to handle the java class java.util.Vector
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
#include "global.h"
#include "j.h"

/*** Defines ***/
#define SVN_JNI_VECTOR__CLASS "java/util/Vector"
#define SVN_JNI_VECTOR__CONSTRUCTOR "<init>"
#define SVN_JNI_VECTOR__CONSTRUCTOR_SIG "()V"
#define SVN_JNI_VECTOR__ADD "addElement"
#define SVN_JNI_VECTOR__ADD_SIG "(Ljava/lang/Object;)V"

/*
 * Do you want to debug code in this file?
 * Just uncomment the following define.
 */
//#define SVN_JNI_VECTOR__DEBUG

/*** Code ***/
jobject
vector__create(JNIEnv *env, jboolean *hasException)
{
  jobject vector = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_VECTOR__DEBUG
  fprintf(stderr, ">>>vector__create\n");
#endif
  
  /* is there enough memory to have twoadditional
   * local references? 
   * - class reference
   * - constructor method id
   */
  if( (*env)->PushLocalFrame(env, 3) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      jclass vectorClass = NULL;
      jmethodID vectorConstructor = NULL;

      vectorClass = j__get_class(env, &_hasException,
                                    SVN_JNI_VECTOR__CLASS);

      if( !_hasException )
	{
	  vectorConstructor = 
            j__get_method(env, &_hasException, 
                          vectorClass,
                          SVN_JNI_VECTOR__CONSTRUCTOR,
                          SVN_JNI_VECTOR__CONSTRUCTOR_SIG);
	}

      if( !_hasException )
	{
	  vector = (*env)->NewObject(env, vectorClass,
					vectorConstructor);
	}

      if( vector == NULL )
	{
	  _hasException = JNI_TRUE;
	}

#ifdef SVN_JNI_VECTOR__DEBUG
      SVN_JNI__DEBUG_PTR(vectorClass);
      SVN_JNI__DEBUG_PTR(vectorConstructor);
      SVN_JNI__DEBUG_PTR(vector);
#endif

      /* pop local frame but preserve the newly create vector */
      (*env)->PopLocalFrame(env, vector);
    }


#ifdef SVN_JNI_VECTOR__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<vector__create\n");
#endif
              
  /* return wether an exception has occured */
  if( (hasException != NULL) && _hasException )
    {
      (*hasException) = JNI_TRUE;
    }

  return vector;
}

void
vector__add(JNIEnv *env, jobject vector, jobject value,
            jboolean *hasException)
{
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_VECTOR__DEBUG
  fprintf(stderr, ">>>vector__add(");
  SVN_JNI__DEBUG_PTR(vector);
  SVN_JNI__DEBUG_PTR(value);
  fprintf(stderr, ")\n");
#endif

  /* enough space for two local references?
   * - class reference
   * - method id
   */
  if( (*env)->PushLocalFrame(env, 2) >= 0 )
    {
      jclass vectorClass = NULL;
      jmethodID vectorAdd = NULL;

      vectorClass = j__get_class(env, &_hasException, 
                                 SVN_JNI_VECTOR__CLASS);

      if( !_hasException )
        {
          vectorAdd = j__get_method(env, &_hasException,
                                    vectorClass,
                                    SVN_JNI_VECTOR__ADD,
                                    SVN_JNI_VECTOR__ADD_SIG);
        }

      if( !_hasException )
        {
#ifdef SVN_JNI_VECTOR__DEBUG
          fprintf(stderr, ">>>CallBooleanMethod(");
          SVN_JNI__DEBUG_PTR(vector);
          SVN_JNI__DEBUG_PTR(vectorAdd);
          SVN_JNI__DEBUG_PTR(value);
          fprintf(stderr, ")\n");
#endif

	  (*env)->CallVoidMethod(env, vector, vectorAdd, value);
          _hasException = (*env)->ExceptionCheck(env);
#ifdef SVN_JNI_VECTOR__DEBUG
          fprintf(stderr, "<<<CallBooleanMethod(");
          SVN_JNI__DEBUG_BOOL(_hasException);
          fprintf(stderr, ")\n");
#endif
	}

      /* pop local references */
      (*env)->PopLocalFrame(env, NULL);
    }

#ifdef SVN_JNI_VECTOR__DEBUG
  fprintf(stderr, "\n<<<vector__add(");
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, ")\n");
#endif

  /* check wether an exception has occured */
  if( (hasException != NULL) && _hasException )
    {
      (*hasException) = JNI_TRUE;
    }
} 

/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */


