/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
 * @file LogMessageCallback.cpp
 * @brief Implementation of the class LogMessageCallback
 */

#include "LogMessageCallback.h"
#include "JNIUtil.h"
#include "svn_time.h"
#include "svn_sorts.h"
/**
 * Create a LogMessageCallback object
 * @param jcallback the java callback object.
 */
LogMessageCallback::LogMessageCallback(jobject jcallback)
{
    m_callback = jcallback;
}
/**
 * Destroy a LogMessageCallback object
 */
LogMessageCallback::~LogMessageCallback()
{
    // the m_callback does not need to be destroyed, because it is the passed
    // in parameter to the java SVNClient.blame method.
}

svn_error_t *
LogMessageCallback::callback(void *baton, apr_hash_t *changed_paths,
                             svn_revnum_t rev, const char *author,
                             const char *date, const char *msg,
                             apr_pool_t *pool)
{
    if (baton)
        return ((LogMessageCallback *)baton)->singleMessage(changed_paths, rev,
                                                            author, date, msg,
                                                            pool);

    return SVN_NO_ERROR;
}

/**
 * Callback called for a single log message
 * @param revision  the revision number, when the line was last changed
 *                  or -1, if not changed during the request revision
 *                  interval
 *
 * @param author    the author, who performed the last change of the line
 * @param date      the date of the last change of the line
 * @param line      the content of the line
 * @param pool      memory pool for the use of this function
 */
svn_error_t *
LogMessageCallback::singleMessage(apr_hash_t *changed_paths,
                                  svn_revnum_t rev,
                                  const char *author, const char *date,
                                  const char *msg, apr_pool_t *pool)
{
    JNIEnv *env = JNIUtil::getEnv();

    static jmethodID sm_mid = 0; // the method id will not change during
                                 // the time this library is loaded, so
                                 // it can be cached.
    if (sm_mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/LogMessageCallback");
        if (JNIUtil::isJavaExceptionThrown())
        {
           return SVN_NO_ERROR;
        }
        sm_mid = env->GetMethodID(clazz, "singleMessage",
            "([L"JAVA_PACKAGE"/ChangePath;JLjava/lang/String;"
            "Ljava/util/Date;Ljava/lang/String;)V");
        if (JNIUtil::isJavaExceptionThrown())
        {
           return SVN_NO_ERROR;
        }
        env->DeleteLocalRef(clazz);
        if (JNIUtil::isJavaExceptionThrown())
        {
           return SVN_NO_ERROR;
        }
    }

    jclass clazzCP = env->FindClass(JAVA_PACKAGE"/ChangePath");
    if (JNIUtil::isJavaExceptionThrown())
    {
       return SVN_NO_ERROR;
    }

    static jmethodID midCP = 0;
    if (midCP == 0)
    {
        midCP = env->GetMethodID(clazzCP, "<init>",
            "(Ljava/lang/String;JLjava/lang/String;C)V");
        if (JNIUtil::isJavaExceptionThrown())
        {
           return SVN_NO_ERROR;
        }
    }

    jobject jdate = NULL;
    if (date != NULL && *date != '\0')
    {
        apr_time_t timeTemp;

        SVN_ERR(svn_time_from_cstring(&timeTemp, date, pool));
        jdate = JNIUtil::createDate(timeTemp);
        if (JNIUtil::isJavaExceptionThrown())
        {
           return SVN_NO_ERROR;
        }
    }

    jstring jauthor = JNIUtil::makeJString(author);
    if (JNIUtil::isJavaExceptionThrown())
    {
       return SVN_NO_ERROR;
    }

    jobjectArray jChangedPaths = NULL;
    if (changed_paths)
    {
        apr_array_header_t *sorted_paths;
        int i;

        /* Get an array of sorted hash keys. */
        sorted_paths = svn_sort__hash(changed_paths,
                                      svn_sort_compare_items_as_paths,
                                      pool);

        jChangedPaths = env->NewObjectArray(sorted_paths->nelts, clazzCP, NULL);

        for (i = 0; i < sorted_paths->nelts; i++)
        {
            svn_sort__item_t *item = &(APR_ARRAY_IDX(sorted_paths, i,
                                                     svn_sort__item_t));
            const char *path = (const char *)item->key;
            svn_log_changed_path_t *log_item
                = (svn_log_changed_path_t *)
                    apr_hash_get(changed_paths, item->key, item->klen);

            jstring jpath = JNIUtil::makeJString(path);
            if (JNIUtil::isJavaExceptionThrown())
            {
                return SVN_NO_ERROR;
            }
            jstring jcopyFromPath =
                JNIUtil::makeJString(log_item->copyfrom_path);
            if (JNIUtil::isJavaExceptionThrown())
            {
                return SVN_NO_ERROR;
            }
            jlong jcopyFromRev = log_item->copyfrom_rev;
            jchar jaction = log_item->action;

            jobject cp = env->NewObject(clazzCP, midCP, jpath, jcopyFromRev,
                                        jcopyFromPath, jaction);
            if (JNIUtil::isJavaExceptionThrown())
            {
                return SVN_NO_ERROR;
            }

            env->SetObjectArrayElement(jChangedPaths, i, cp);
            if (JNIUtil::isJavaExceptionThrown())
            {
                return SVN_NO_ERROR;
            }

            env->DeleteLocalRef(cp);
            if (JNIUtil::isJavaExceptionThrown())
            {
                return SVN_NO_ERROR;
            }
            env->DeleteLocalRef(jpath);
            if (JNIUtil::isJavaExceptionThrown())
            {
                return SVN_NO_ERROR;
            }
            env->DeleteLocalRef(jcopyFromPath);
            if (JNIUtil::isJavaExceptionThrown())
            {
                return SVN_NO_ERROR;
            }
        }
    }

    jstring jmessage = JNIUtil::makeJString(msg);
    if (JNIUtil::isJavaExceptionThrown())
    {
       return SVN_NO_ERROR;
    }

    env->CallVoidMethod(m_callback, sm_mid, jChangedPaths, (jlong)rev, jauthor,
                        jdate, jmessage);
    if (JNIUtil::isJavaExceptionThrown())
    {
       return SVN_NO_ERROR;
    }

    env->DeleteLocalRef(jauthor);
    if (JNIUtil::isJavaExceptionThrown())
    {
       return SVN_NO_ERROR;
    }
    env->DeleteLocalRef(jmessage);
    if (JNIUtil::isJavaExceptionThrown())
    {
       return SVN_NO_ERROR;
    }

    return SVN_NO_ERROR;
}
