/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @file JNIByteArray.cpp
 * @brief Implementation of the class JNIByteArray
 */
#include "JNIByteArray.h"
#include "JNIUtil.h"

/**
 * Create a new object
 * @param jba the local reference to the Java byte array
 * @param flag that the underlying byte array reference should be deleted at
 *        destruction
 */
JNIByteArray::JNIByteArray(jbyteArray jba, bool deleteByteArray)
{
  m_array = jba;
  m_deleteByteArray = deleteByteArray;
  if (jba != NULL)
    {
      // Get the bytes.
      JNIEnv *env = JNIUtil::getEnv();
      m_data = env->GetByteArrayElements(jba, NULL);
    }
  else
    {
      m_data = NULL;
    }
}

JNIByteArray::~JNIByteArray()
{
  if (m_array != NULL)
    {
      // Release the bytes
      JNIUtil::getEnv()->ReleaseByteArrayElements(m_array,
                                                  m_data,
                                                  JNI_ABORT);
      if (m_deleteByteArray)
        // And if needed the byte array.
        JNIUtil::getEnv()->DeleteLocalRef(m_array);
    }
}

/**
 * Returns the number of bytes in the byte array.
 * @return the number of bytes
 */
int JNIByteArray::getLength()
{
  if (m_data == NULL)
    return 0;
  else
    return JNIUtil::getEnv()->GetArrayLength(m_array);
}

/**
 * Returns the bytes of the byte array.
 * @return the bytes
 */
const signed char *JNIByteArray::getBytes()
{
  return m_data;
}

/**
 * Returns if the byte array was not set.
 * @return if the byte array was not set
 */
bool JNIByteArray::isNull()
{
  return m_data == NULL;
}
