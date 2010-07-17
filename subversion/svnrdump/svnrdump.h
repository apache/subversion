/*
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
 */

#ifndef SVNRDUMP_H_
#define SVNRDUMP_H_

#include "dump_editor.h"

struct replay_baton {
  const svn_delta_editor_t *editor;
  void *edit_baton;
};

/* Write the properties in the hashtable PROPERTIES to STRBUF
   allocated in pool. DELETED is used to indicate deleted
   properties */
void
write_hash_to_stringbuf(apr_hash_t *properties,
                        svn_boolean_t deleted,
                        svn_stringbuf_t **strbuf,
                        apr_pool_t *pool);

/* Extract and dump the properties and del_properties stored in the
   edit baton EB using pool for any temporary allocations. If
   TRIGGER_VAR is passed, it is unset. DUMP_DATA_TOO triggers dumping
   data along with the properties */
svn_error_t *
dump_props(struct dump_edit_baton *eb,
           svn_boolean_t *trigger_var,
           svn_boolean_t dump_data_too,
           apr_pool_t *pool);

#endif
