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
 * @file JNIByteArray.h
 * @brief Interface of the class JNIByteArray
 */

#ifndef JNIBYTEARRAY_H
#define JNIBYTEARRAY_H

#include <jni.h>

/**
 * This class holds a Java byte array to give easy access to its
 * bytes.
 */
class JNIByteArray
{
 private:
  /**
   *  A local reference to the byte array.
   */
  jbyteArray m_array;

  /**
   * The cache bytes of the byte array.
   */
  jbyte *m_data;

  /**
   * Flag that the underlying byte array reference should be deleted
   * at destruction.
   */
  bool m_deleteByteArray;
 public:
  bool isNull();
  const signed char *getBytes();
  int getLength();
  JNIByteArray(jbyteArray jba, bool deleteByteArray = false);
  ~JNIByteArray();
};

#endif // JNIBYTEARRAY_H
