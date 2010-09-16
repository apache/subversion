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
 * @file Revision.h
 * @brief Interface of the class Revision
 */

#ifndef REVISION_H
#define REVISION_H

#include "svn_opt.h"

#include <string>

namespace SVN
{

  class Revision
  {
    private:
      /** The constructors. */
      Revision(svn_opt_revision_kind kind);
      Revision(svn_revnum_t revnum);
      Revision(apr_time_t date, bool foo);

      svn_opt_revision_t m_revision;

    public:
      /** The destructor needs to be public. */
      virtual ~Revision();

      static const Revision getNumberRev(svn_revnum_t revnum);
      static const Revision getDateRev(apr_time_t date);

      const svn_opt_revision_t *revision() const;

      bool operator==(svn_opt_revision_t *rev) const;

      /** Some constant revision types. */
      const static Revision HEAD;
      const static Revision COMMITTED;
      const static Revision PREVIOUS;
      const static Revision BASE;
      const static Revision WORKING;
  };
}

#endif // REVISION_H
