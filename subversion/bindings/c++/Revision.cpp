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

#include "Revision.h"

namespace SVN {

const Revision Revision::HEAD(svn_opt_revision_head);
const Revision Revision::COMMITTED(svn_opt_revision_committed);
const Revision Revision::PREVIOUS(svn_opt_revision_previous);
const Revision Revision::BASE(svn_opt_revision_base);
const Revision Revision::WORKING(svn_opt_revision_working);

Revision::Revision(svn_opt_revision_kind kind)
{
  m_revision.kind = kind;
}

Revision::Revision(svn_revnum_t revnum)
{
  m_revision.kind = svn_opt_revision_number;
  m_revision.value.number = revnum;
}

Revision::Revision(apr_time_t date, bool foo)
{
  m_revision.kind = svn_opt_revision_date;
  m_revision.value.date = date;
}

Revision::~Revision()
{
}

bool
Revision::operator==(svn_opt_revision_t *rev) const
{
  if (rev->kind != m_revision.kind)
      return false;

  switch (rev->kind)
    {
      case svn_opt_revision_number:
        return (rev->value.number == m_revision.value.number);
      case svn_opt_revision_date:
        return (rev->value.date == m_revision.value.date);
      default:
        return true;
    }
}

const Revision
Revision::getNumberRev(svn_revnum_t revnum)
{
  return Revision(revnum);
}

const Revision
Revision::getDateRev(apr_time_t date)
{
  return Revision(date, false);
}

const svn_opt_revision_t *
Revision::revision() const
{
  return &m_revision;
}

}
