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
 */

#include "Core.h"
#include "Pool.h"

#include "Common.h"

#include "svn_error.h"
#include "svn_dso.h"

#include <iostream>
#include <locale.h>

namespace SVN {
namespace Private {

Core *Core::m_singleton;

Core *
Core::getCore()
{
  ensureSingleton();
  return m_singleton;
}

apr_pool_t *
Core::getGlobalPool()
{
  return m_global_pool;
}

Core::Core()
{
  globalInit();
}

void
Core::globalInit()
{
  // This method should only be run once.
  static bool run = false;

  if (run)
    return;

  run = true;

  initLocale();
  initAPR();

  m_global_pool = svn_pool_create(NULL);
  if (m_global_pool == NULL)
    throw Exception("libsvn++: Cannot initialize global pool");
}

void
Core::initLocale()
{
  /* C programs default to the "C" locale. But because svn is supposed
     to be i18n-aware, it should inherit the default locale of its
     environment.  */
  if (!setlocale(LC_ALL, ""))
    {
      char buf[512];
      const char *env_vars[] = { "LC_ALL", "LC_CTYPE", "LANG", NULL };
      const char **env_var = &env_vars[0], *env_val = NULL;
      while (*env_var)
        {
          env_val = getenv(*env_var);
          if (env_val && env_val[0])
            break;
          ++env_var;
        }

      if (!*env_var)
        {
          /* Unlikely. Can setlocale fail if no env vars are set? */
          --env_var;
          env_val = "not set";
        }

        snprintf(buf, sizeof(buf),
                "%s: error: cannot set LC_ALL locale\n"
                "%s: error: environment variable %s is %s\n"
                "%s: error: please check that your locale name is "
                "correct\n",
                "svnjavahl", "svnjavahl", *env_var, env_val, "svnjavahl");

        throw Exception(buf);
    }
}

void
Core::initAPR()
{
  svn_error_t *err;
  char err_buf[256];

  /* Initialize the APR subsystem, and register an atexit() function
   * to Uninitialize that subsystem at program exit. */
  apr_status_t status = apr_initialize();
  if (status)
    {
      char buf[1024];
      apr_strerror(status, buf, sizeof(buf) - 1);
      snprintf(err_buf, sizeof(err_buf),
               "%s: error: cannot initialize APR: %s\n",
               "svnjavahl", buf);
      throw Exception(err_buf);
    }

  /* This has to happen before any pools are created. */
  SVN_CPP_ERR(svn_dso_initialize2());

  if (0 > atexit(Core::dispose))
    {
      snprintf(err_buf, sizeof(err_buf),
               "%s: error: atexit registration failed\n",
               "svnjavahl");
      throw Exception(err_buf);
    }
}

void
Core::ensureSingleton()
{
  if (!m_singleton)
    m_singleton = new Core();
}

void
Core::dispose()
{
  delete m_singleton;
  m_singleton = NULL;

  // Don't check the status here, 'cause we're on the way out, anyhow.
  apr_terminate();
}

Core::~Core()
{
  svn_pool_destroy(m_global_pool);
}

}
}
