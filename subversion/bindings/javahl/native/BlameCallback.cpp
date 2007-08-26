/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2007 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file BlameCallback.cpp
 * @brief Implementation of the class BlameCallback
 */

#include "BlameCallback.h"
#include "JNIUtil.h"
#include "svn_time.h"
/**
 * Create a BlameCallback object
 * @param jcallback the Java callback object.
 */
BlameCallback::BlameCallback(jobject jcallback)
{
  m_callback = jcallback;
}
/**
 * Destroy a BlameCallback object
 */
BlameCallback::~BlameCallback()
{
  // the m_callback does not need to be destroyed, because it is the passed
  // in parameter to the Java SVNClient.blame method.
}

svn_error_t *
BlameCallback::callback(void *baton,
                        apr_int64_t line_no,
                        svn_revnum_t revision,
                        const char *author,
                        const char *date,
                        svn_revnum_t merged_revision,
                        const char *merged_author,
                        const char *merged_date,
                        const char *merged_path,
                        const char *line,
                        apr_pool_t *pool)
{
  if (baton)
    return ((BlameCallback *)baton)->singleLine(revision, author, date,
                                                merged_revision, merged_author,
                                                merged_date, merged_path, line,
                                                pool);

  return SVN_NO_ERROR;
}

/**
 * Callback called for a single line in the file, for which the blame
 * information was requested
 * @param revision  the revision number, when the line was last changed
 *                  or -1, if not changed during the request revision
 *                  interval
 * @param author    the author, who performed the last change of the line
 * @param date      the date of the last change of the line
 * @param line      the content of the line
 * @param pool      memory pool for the use of this function
 */
svn_error_t *
BlameCallback::singleLine(svn_revnum_t revision, const char *author,
                          const char *date, svn_revnum_t mergedRevision,
                          const char *mergedAuthor, const char *mergedDate,
                          const char *mergedPath, const char *line,
                          apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // The method id will not change during the time this library is
  // loaded, so it can be cached.
  static jmethodID mid = 0;
  if (mid == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/BlameCallback2");
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

      mid = env->GetMethodID(clazz, "singleLine",
                             "(Ljava/util/Date;JLjava/lang/String;"
                             "Ljava/util/Date;JLjava/lang/String;"
                             "Ljava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || mid == 0)
        return SVN_NO_ERROR;

      env->DeleteLocalRef(clazz);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  // convert the parameters to their Java relatives
  jstring jauthor = JNIUtil::makeJString(author);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  jobject jdate = NULL;
  if (date != NULL && *date != '\0')
    {
      apr_time_t timeTemp;
      svn_error_t *err = svn_time_from_cstring(&timeTemp, date, pool);
      if (err != SVN_NO_ERROR)
        return err;

      jdate = JNIUtil::createDate(timeTemp);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  jstring jmergedAuthor = JNIUtil::makeJString(mergedAuthor);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  jobject jmergedDate = NULL;
  if (mergedDate != NULL && *mergedDate != '\0')
    {
      apr_time_t timeTemp;
      svn_error_t *err = svn_time_from_cstring(&timeTemp, mergedDate, pool);
      if (err != SVN_NO_ERROR)
        return err;

      jmergedDate = JNIUtil::createDate(timeTemp);
      if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;
    }

  jstring jmergedPath = JNIUtil::makeJString(mergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  jstring jline = JNIUtil::makeJString(line);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // call the Java method
  env->CallVoidMethod(m_callback, mid, jdate, (jlong)revision, jauthor,
                      jmergedDate, (jlong)mergedRevision, jmergedAuthor,
                      jmergedPath, jline);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  // cleanup the temporary Java objects
  env->DeleteLocalRef(jauthor);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jdate);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jmergedAuthor);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jmergedDate);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jmergedPath);
  if (JNIUtil::isJavaExceptionThrown())
    return SVN_NO_ERROR;

  env->DeleteLocalRef(jline);
  // No need to check for an exception here, because we return anyway.

  return SVN_NO_ERROR;
}
