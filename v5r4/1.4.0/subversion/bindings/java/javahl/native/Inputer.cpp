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
 * @file Inputer.cpp
 * @brief Implementation of the class Inputer
 */

#include "Inputer.h"
#include "JNIUtil.h"
#include "JNIByteArray.h"
/**
 * create an Inputer object
 * @param jthis the java object to be stored
 */
Inputer::Inputer(jobject jthis)
{
    m_jthis = jthis;
}
/**
 * destroy an Inputer object
 */
Inputer::~Inputer()
{
    // the m_jthis does not need to be destroyed, because it is the passed
    // in parameter to the java method.

}
/**
 * create a svn_stream_t structure for this object. This will be used as an
 * input stream by subversion
 * @param pool  the pool, from which the structure is allocated
 * @return the input stream
 */
svn_stream_t *Inputer::getStream(const Pool & pool)
{
    // create a stream with this as the baton and set the read and close
    // functions
    svn_stream_t *ret = svn_stream_create(this, pool.pool());
    svn_stream_set_read(ret, Inputer::read);
    svn_stream_set_close(ret, Inputer::close);
    return ret;
}
/**
 * implements svn_read_fn_t to read to data into subversion
 * @param baton     an Inputer object for the callback
 * @param buffer    the buffer for the read data
 * @param len       on input the buffer len, on output the number of read bytes
 * @return a subversion error or SVN_NO_ERROR
 */
svn_error_t *Inputer::read(void *baton, char *buffer, apr_size_t *len)
{
    JNIEnv *env = JNIUtil::getEnv();
    // an object of our class is passed in as the baton
    Inputer *that = (Inputer*)baton;

    // the method id will not change during
    // the time this library is loaded, so
    // it can be cached.
    static jmethodID mid = 0;
    if(mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/InputInterface");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return SVN_NO_ERROR;
        }
        mid = env->GetMethodID(clazz, "read", "([B)I");
        if(JNIUtil::isJavaExceptionThrown() || mid == 0)
        {
            return SVN_NO_ERROR;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return SVN_NO_ERROR;
        }
    }

    // allocate a java byte array to read the data
    jbyteArray data =
        JNIUtil::makeJByteArray((const signed char*)buffer, *len);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    // read the data
    jint jread = env->CallIntMethod(that->m_jthis, mid, data);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    // put the java byte array into a helper object to retrieve the data bytes
    JNIByteArray outdata(data, true);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    // catch when the java method tells us, it read to much data.
    if(jread > (jint) *len)
        jread = -1;

    // in the case of success, copy the data back to the subversion buffer
    if(jread > 0)
        memcpy(buffer, outdata.getBytes(), jread);

    // copy the number of read bytes back to subversion
    *len = jread;

    return SVN_NO_ERROR;
}
/**
 * implements svn_close_fn_t to close the input stream
 * @param baton     an Inputer object for the callback
 * @return a subversion error or SVN_NO_ERROR
 */
svn_error_t *Inputer::close(void *baton)
{
    JNIEnv *env = JNIUtil::getEnv();
    // an object of our class is passed in as the baton
    Inputer *that = (Inputer*)baton;

    // the method id will not change during
    // the time this library is loaded, so
    // it can be cached.
    static jmethodID mid = 0;
    if(mid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/InputInterface");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return SVN_NO_ERROR;
        }
        mid = env->GetMethodID(clazz, "close", "()V");
        if(JNIUtil::isJavaExceptionThrown() || mid == 0)
        {
            return SVN_NO_ERROR;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return SVN_NO_ERROR;
        }
    }

    // call the java object, to close the stream
    env->CallVoidMethod(that->m_jthis, mid);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    return SVN_NO_ERROR;
}
