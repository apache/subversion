/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 * @file JNIUtil.h
 * @brief Interface of the class JNIUtil
 */

#if !defined(AFX_JNIUTIL_H__82301908_C6CB_4A77_8A28_899E72FBEEFF__INCLUDED_)
#define AFX_JNIUTIL_H__82301908_C6CB_4A77_8A28_899E72FBEEFF__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <list>
struct apr_pool_t;
struct svn_error;
class JNIMutex;
class SVNBase;
class Pool;
#include <jni.h>
#include <fstream>
#include <apr_time.h>
#include <string>
struct svn_error_t;

#define JAVA_PACKAGE "org/tigris/subversion/javahl"
/**
 * class to hold a number of JNI relate utility methods. No Objects of this 
 * class are ever created
 */
class JNIUtil
{
public:
    static void throwNullPointerException(const char *message);
    static jbyteArray makeJByteArray(const signed char *data, int length);
    static void setRequestPool(Pool *pool);
    static Pool * getRequestPool();
    static jobject createDate(apr_time_t time);
    static void logMessage(const char *message);
    static int getLogLevel();
    static char * getFormatBuffer();
    static void initLogFile(int level, jstring path);
    static jstring makeJString(const char *txt);
    static bool isJavaExceptionThrown();
    static JNIEnv * getEnv();
    static void setEnv(JNIEnv *);
    static bool isExceptionThrown();
    static void handleAPRError(int error, const char *op);
    static void putFinalizedClient(SVNBase *cl);
    static void handleSVNError(svn_error_t *err);
    static void throwError(const char *message);
    static apr_pool_t * getPool();
    static bool JNIInit(JNIEnv *env);
    static JNIMutex *getGlobalPoolMutex();
    enum { formatBufferSize = 2048 };
    enum { noLog, errorLog, exceptionLog, entryLog } LogLevel;
private:
    static void assembleErrorMessage(svn_error_t *err, int depth,
                                         apr_status_t parent_apr_err,
                                         std::string &buffer);
    static void setExceptionThrown();
    /** 
     * the log level of this module
     */ 
    static int g_logLevel;
    /**
     * global master pool. All other pool are subpools of this pool
     */
    static apr_pool_t* g_pool;
    /**
     * list of objects finalized, where the C++ peer has not yet be deleted
     */
    static std::list<SVNBase*> g_finalizedObjects;
    /**
     * mutex to secure the g_finalizedObjects list
     */
    static JNIMutex *g_finalizedObjectsMutex;
    /**
     * mutex to secure the access to the log file
     */
    static JNIMutex *g_logMutex;
    /**
     * flag, that an exception occured during our initialization
     */
    static bool g_initException;
    /**
     * flag, that one thread is in the init code. Cannot use mutex here since 
     * apr is not initialized yes
     */
    static bool g_inInit;
    /**
     * the JNI environment used during initialization
     */
    static JNIEnv *g_initEnv;
    /**
     * buffer the format error messages during initialization
     */
    static char g_initFormatBuffer[formatBufferSize];
    /**
     * the stream to write log messages to
     */
    static std::ofstream g_logStream;
    /**
     * flag to secure our global pool
     */
    static JNIMutex *g_globalPoolMutext;
};
// !defined(AFX_JNIUTIL_H__82301908_C6CB_4A77_8A28_899E72FBEEFF__INCLUDED_)
#endif
