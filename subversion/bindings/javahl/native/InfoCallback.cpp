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
 * @file InfoCallback.cpp
 * @brief Implementation of the class InfoCallback
 */

#include "SVNClient.h"
#include "InfoCallback.h"
#include "EnumMapper.h"
#include "JNIUtil.h"
#include "svn_time.h"
#include "svn_path.h"

/**
 * Create a InfoCallback object.  jcallback is the java callback object.
 */
InfoCallback::InfoCallback(jobject jcallback)
{
    m_callback = jcallback;
    wcPath = NULL;
}

/**
 * Destroy a InfoCallback object
 */
InfoCallback::~InfoCallback()
{
    // the m_callback does not need to be destroyed, because it is the passed
    // in parameter to the java SVNClient.info method.
}

svn_error_t *
InfoCallback::callback(void *baton,
                       const char *path,
                       const svn_info_t *info,
                       apr_pool_t *pool)
{
    if (baton)
        return ((InfoCallback *)baton)->singleInfo(path, info, pool);

    return SVN_NO_ERROR;
}

void
InfoCallback::setWcPath(const char *path)
{
    wcPath = path;
}

/**
 * Callback called for a single path.  path is the path name, pool may be used
 * for memory allocation.
 */
svn_error_t *
InfoCallback::singleInfo(const char *path,
                         const svn_info_t *info,
                         apr_pool_t *pool)
{
    JNIEnv *env = JNIUtil::getEnv();

    static jmethodID mid = 0; // the method id will not change during
                              // the time this library is loaded, so
                              // it can be cached.
    if (mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/InfoCallback");
        if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;

        mid = env->GetMethodID(clazz, "singleInfo",
            "(L"JAVA_PACKAGE"/Info2;)V");
        if (JNIUtil::isJavaExceptionThrown() || mid == 0)
            return SVN_NO_ERROR;

        env->DeleteLocalRef(clazz);
        if (JNIUtil::isJavaExceptionThrown())
            return SVN_NO_ERROR;
    }

    jobject jinfo2 = createJavaInfo2(path, info, pool);
    if (jinfo2 == NULL || JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

    env->CallVoidMethod(m_callback, mid, jinfo2);
    if (JNIUtil::isJavaExceptionThrown())
        return SVN_NO_ERROR;

    env->DeleteLocalRef(jinfo2);
    // Return SVN_NO_ERROR here regardless of an exception or not.

    return SVN_NO_ERROR;
}

jobject
InfoCallback::createJavaInfo2(const char *path, const svn_info_t *info,
                              apr_pool_t *pool)
{
    svn_wc_adm_access_t *adm_access;
    const svn_wc_entry_t *entry;
    const char *fullPath = (wcPath ? svn_path_join(wcPath, path, pool) : path);
    SVN_JNI_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, fullPath, FALSE, 0,
                                       NULL, NULL, pool), NULL);
    SVN_JNI_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool), NULL);
    SVN_JNI_ERR(svn_wc_adm_close(adm_access), NULL);

    if (entry == NULL)
        // Unable to fully flesh out a Java Info2 object.
        return NULL;

    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE "/Info2");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;JILjava/lang/String;"
             "Ljava/lang/String;JLjava/util/Date;Ljava/lang/String;"
             "Lorg/tigris/subversion/javahl/Lock;ZILjava/lang/String;J"
             "Ljava/util/Date;Ljava/util/Date;"
             "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
             "Ljava/lang/String;Ljava/lang/String;ZZZZ)V");
        if (mid == 0 || JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jstring jpath = JNIUtil::makeJString(path);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jurl = JNIUtil::makeJString(info->URL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jlong jrev = info->rev;
    jint jnodeKind = EnumMapper::mapNodeKind(info->kind);
    jstring jreposRootUrl = JNIUtil::makeJString(info->repos_root_URL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jreportUUID = JNIUtil::makeJString(info->repos_UUID);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jlong jlastChangedRev = info->last_changed_rev;
    jobject jlastChangedDate = JNIUtil::createDate(info->last_changed_date);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jlastChangedAuthor =
        JNIUtil::makeJString(info->last_changed_author);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobject jlock = SVNClient::createJavaLock(info->lock);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jboolean jhasWcInfo = info->has_wc_info ? JNI_TRUE:JNI_FALSE;
    jint jschedule = EnumMapper::mapScheduleKind(info->schedule);
    jstring jcopyFromUrl = JNIUtil::makeJString(info->copyfrom_url);
    jlong jcopyFromRev = info->copyfrom_rev;
    jobject jtextTime = JNIUtil::createDate(info->text_time);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobject jpropTime = JNIUtil::createDate(info->prop_time);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jchecksum = JNIUtil::makeJString(info->checksum);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jconflictOld = JNIUtil::makeJString(info->conflict_old);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jconflictNew = JNIUtil::makeJString(info->conflict_new);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jconflictWrk = JNIUtil::makeJString(info->conflict_wrk);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jprejfile = JNIUtil::makeJString(info->prejfile);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jboolean jcopied = entry->copied ? JNI_TRUE : JNI_FALSE;
    jboolean jdeleted = entry->deleted ? JNI_TRUE : JNI_FALSE;
    jboolean jabsent = entry->absent ? JNI_TRUE : JNI_FALSE;
    jboolean jincomplete = entry->incomplete ? JNI_TRUE : JNI_FALSE;

    jobject jinfo2 = env->NewObject(clazz, mid, jpath, jurl, jrev, jnodeKind,
        jreposRootUrl, jreportUUID, jlastChangedRev, jlastChangedDate,
        jlastChangedAuthor, jlock, jhasWcInfo, jschedule, jcopyFromUrl,
        jcopyFromRev, jtextTime, jpropTime, jchecksum, jconflictOld,
        jconflictNew, jconflictWrk, jprejfile, jcopied, jdeleted, jabsent,
        jincomplete);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jpath);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jurl);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jreposRootUrl);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jlastChangedDate);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jlastChangedAuthor);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jlock);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jcopyFromUrl);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jchecksum);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jtextTime);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jpropTime);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jconflictOld);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jconflictNew);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jconflictWrk);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jprejfile);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return jinfo2;
}
