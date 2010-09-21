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

#include "Common.h"

#include "svn_error.h"

#include <stdexcept>


namespace SVN
{

void
Exception::assembleErrorMessage(svn_error_t *err, int depth,
                                apr_status_t parent_apr_err,
                                std::string &buffer)
{
  // buffer for a single error message
  char errbuf[256];

  /* When we're recursing, don't repeat the top-level message if its
   * the same as before. */
  if (depth == 0 || err->apr_err != parent_apr_err)
    {
      /* Is this a Subversion-specific error code? */
      if ((err->apr_err > APR_OS_START_USEERR)
          && (err->apr_err <= APR_OS_START_CANONERR))
        buffer.append(svn_strerror(err->apr_err, errbuf, sizeof(errbuf)));
      /* Otherwise, this must be an APR error code. */
      else
        buffer.append(apr_strerror(err->apr_err, errbuf, sizeof(errbuf)));
      buffer.append("\n");
    }
  if (err->message)
    buffer.append("svn: ").append(err->message).append("\n");

  if (err->child)
    assembleErrorMessage(err->child, depth + 1, err->apr_err, buffer);
}

Exception::Exception(svn_error_t *err)
{
  if (!err)
    throw std::logic_error("Attempting to throw an exeption from "
                           "a NULL svn error");

  svn_error_t *purged = svn_error_purge_tracing(err);
  assembleErrorMessage(purged, 0, APR_SUCCESS, m_description);

#ifdef SVN_DEBUG
#ifndef SVN_ERR__TRACING
  if (err->file)
    {
      std::ostringstream buf;
      buf << err->file;
      if (err->line > 0)
        buf << ':' << err->line;
      m_source = buf.str();
    }
#endif
#endif

  m_apr_err = purged->apr_err;

  svn_error_clear(err);
}

Exception::Exception(const std::string &message)
  : m_description(message),
    m_apr_err(SVN_ERR_CPP_EXCEPTION)
{
}

Exception::Exception(int apr_err, const std::string &message)
  : m_description(message),
    m_apr_err(apr_err)
{
}

Exception::Exception(int apr_err)
  : m_apr_err(apr_err)
{
  char buf[256];
  m_description = svn_strerror(apr_err, buf, sizeof(buf));
}

Exception::~Exception() throw ()
{
}

const char *
Exception::what() const throw()
{
  return m_description.c_str();
}

const std::string &
Exception::getSource()
{
  return m_source;
}

int
Exception::getAPRErr()
{
  return m_apr_err;
}

svn_error_t *
Exception::c_err()
{
  return svn_error_create(m_apr_err, NULL,
                          m_description.size() > 0 ? m_description.c_str()
                            : NULL);
}


}
