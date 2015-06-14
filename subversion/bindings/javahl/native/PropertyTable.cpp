/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file PropertyTable.cpp
 * @brief Implementation of the class PropertyTable
 */

#include "PropertyTable.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "JNIByteArray.h"
#include "Array.h"
#include <apr_tables.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include "svn_path.h"
#include "svn_props.h"
#include <iostream>

PropertyTable::~PropertyTable()
{
  if (m_revpropTable != NULL)
    JNIUtil::getEnv()->DeleteLocalRef(m_revpropTable);
}

apr_hash_t *PropertyTable::hash(const SVN::Pool &pool)
{
  if (!m_revpropTable && !m_empty_if_null)
    return NULL;

  apr_hash_t *revprop_table = apr_hash_make(pool.getPool());

  std::map<std::string, std::string>::const_iterator it;
  for (it = m_revprops.begin(); it != m_revprops.end(); ++it)
    {
      const char *propname = apr_pstrdup(pool.getPool(), it->first.c_str());
      if (!svn_prop_name_is_valid(propname))
        {
          const char *msg = apr_psprintf(pool.getPool(),
                                         "Invalid property name: '%s'",
                                         propname);
          JNIUtil::throwNativeException(JAVAHL_CLASS("/ClientException"), msg,
                                        NULL, SVN_ERR_CLIENT_PROPERTY_NAME);
          return NULL;
        }

      svn_string_t *propval = svn_string_ncreate(it->second.c_str(),
                                                 it->second.size(),
                                                 pool.getPool());

      apr_hash_set(revprop_table, propname, APR_HASH_KEY_STRING, propval);
    }

  return revprop_table;
}

PropertyTable::PropertyTable(jobject jrevpropTable, bool bytearray_values,
                             bool empty_if_null)
  : m_revpropTable(jrevpropTable),
    m_empty_if_null(empty_if_null)
{
  m_revpropTable = jrevpropTable;

  if (jrevpropTable != NULL)
    {
      static jmethodID keySet = 0, get = 0;
      JNIEnv *env = JNIUtil::getEnv();

      jclass mapClazz = env->FindClass("java/util/Map");

      if (keySet == 0)
        {
          keySet = env->GetMethodID(mapClazz, "keySet",
                                    "()Ljava/util/Set;");
          if (JNIUtil::isExceptionThrown())
            return;
        }

      jobject jkeySet = env->CallObjectMethod(jrevpropTable, keySet);
      if (JNIUtil::isExceptionThrown())
        return;

      if (get == 0)
        {
          get = env->GetMethodID(mapClazz, "get",
                                 "(Ljava/lang/Object;)Ljava/lang/Object;");
          if (JNIUtil::isExceptionThrown())
            return;
        }

      Array keyArray(jkeySet);
      std::vector<jobject> keys = keyArray.vector();

      for (std::vector<jobject>::const_iterator it = keys.begin();
            it < keys.end(); ++it)
        {
          JNIStringHolder propname((jstring)*it);
          if (JNIUtil::isExceptionThrown())
            return;

          jobject jpropval = env->CallObjectMethod(jrevpropTable, get, *it);
          if (JNIUtil::isExceptionThrown())
            return;

          std::string pv;
          if (bytearray_values)
            {
              JNIByteArray propval((jbyteArray)jpropval);
              if (JNIUtil::isExceptionThrown())
                return;
              if (!propval.isNull())
                pv = std::string(
                    reinterpret_cast<const char*>(propval.getBytes()),
                    propval.getLength());
            }
          else
            {
              JNIStringHolder propval((jstring)jpropval);
              if (JNIUtil::isExceptionThrown())
                return;
              if (NULL != static_cast<const char *>(propval))
                pv = static_cast<const char *>(propval);
            }

          m_revprops[std::string(static_cast<const char *>(propname))] = pv;

          JNIUtil::getEnv()->DeleteLocalRef(jpropval);
        }

      JNIUtil::getEnv()->DeleteLocalRef(jkeySet);
    }
}
