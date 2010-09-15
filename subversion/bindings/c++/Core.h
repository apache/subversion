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
 * @file Core.h
 * @brief Interface of the class Core
 */

#ifndef CORE_H
#define CORE_H

#include "svn_pools.h"

namespace SVN
{

namespace Private
{

  /** A private class to manage low-level resources.
      Just like anything in the Private namespace, consumers should not need
      to interact with this manually.
      
      This class uses the C types or standard types, and thus should not
      depend on any other class within the C++ bindings. */
  class Core
  {
    private:
      Core();

      /** Various initialization routines. */
      bool globalInit();
      bool initLocale();
      bool initAPR();

      /** Ensure the singleton exists.  This should be called from each of
          the various accessors. */
      static void ensureSingleton();

      /** Destroy the singleton, and terminate APR. */
      static void dispose();

      /** The parent lifetime global pool. */
      apr_pool_t *m_global_pool;

      /** The core singleton. */
      static Core *m_singleton;

    public:
      /** The destructor needs to be public. */
      virtual ~Core();

      /** Singleton accessor, return the core object. */
      static Core *getCore();

      /** Get a handle to the global pool. */
      apr_pool_t *getGlobalPool();

  };
}
}

#endif // CORE_H
