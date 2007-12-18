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
 * @file SVNBase.h
 * @brief Interface of the class SVNBase
 */

#ifndef SVNBASE_H
#define SVNBASE_H

#include <jni.h>

class SVNBase
{
 public:
  SVNBase();
  virtual ~SVNBase();

  /**
   * Return @c this as a @c jlong.
   *
   * @since 1.4.0
   */
  jlong getCppAddr();

  /**
   * Deletes this C++ peer object, and clears the memory address of
   * the corresponding Java object @a jthis which points to it.
   *
   * @since 1.4.0
   */
  virtual void dispose(jobject jthis) = 0;

  /**
   * This method should never be called, as @c dispose() should be
   * called explicitly.  When @c dispose() fails to be called, this
   * method assures that this C++ peer object has been enqueued for
   * deletion.
   *
   * @since 1.4.0
   */
  void finalize();

 protected:
  /**
   * Return the value of the @c cppAddr instance field from the @a
   * jthis Java object, or @c 0 if an error occurs, or the address
   * otherwise can't be determined.  @a fid is expected to point to
   * 0 if not already known, in which case it's looked up using @a
   * className.
   *
   * @since 1.4.0
   */
  static jlong findCppAddrForJObject(jobject jthis, jfieldID *fid,
                                     const char *className);

  /**
   * Deletes @c this, then attempts to null out the @c jthis.cppAddr
   * instance field on the corresponding Java object.
   *
   * @since 1.4.0
   */
  void dispose(jobject jthis, jfieldID *fid, const char *className);

 private:
  /**
   * If the value pointed to by @a fid is zero, find the @c jfieldID
   * for the @c cppAddr instance field of @c className.
   *
   * @since 1.4.0
   */
  static void findCppAddrFieldID(jfieldID *fid, const char *className,
                                 JNIEnv *env);
};

#endif // SVNBASE_H
