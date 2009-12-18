/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @file RevpropTable.h
 * @brief Interface of the class RevpropTable
 */

#ifndef REVPROPTABLE_H
#define REVPROPTABLE_H

#include <jni.h>

struct apr_hash_t;
struct svn_error_t;
class Pool;

#include "Path.h"
#include <map>
#include <string>

class RevpropTable
{
 private:
  std::map<std::string, std::string> m_revprops;
  jobject m_revpropTable;
 public:
  RevpropTable(jobject jrevpropTable);
  ~RevpropTable();
  const apr_hash_t *hash(const Pool &pool);
};

#endif // REVPROPTABLE_H
