/* dbt.h --- interface to DBT-frobbing functions
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */

#ifndef SVN_LIBSVN_FS_DBT_H
#define SVN_LIBSVN_FS_DBT_H

#include "db.h"
#include "apr_pools.h"

/* Set all fields of DBT to zero.  Return DBT.  */
DBT *svn_fs__clear_dbt (DBT *dbt);


/* Set DBT to refer to the SIZE bytes at DATA.  Return DBT.  */
DBT *svn_fs__set_dbt (DBT *dbt, void *data, u_int32_t size);


/* Prepare DBT to hold data returned from Berkeley DB.  Return DBT.

   Clear all its fields to zero, but set the DB_DBT_MALLOC flag,
   requesting that Berkeley DB place the returned data in a freshly
   malloc'd block.  If the database operation succeeds, the caller
   then owns the data block, and is responsible for making sure it
   gets freed.  

   You can use this with svn_fs__track_dbt:

       svn_fs__result_dbt (&foo);
       ... some Berkeley DB operation that puts data in foo ...
       svn_fs__track_dbt (&foo, pool);

   This arrangement is:
   - thread-safe --- the returned data is allocated via malloc, and
     won't be overwritten if some other thread performs an operation
     on the same table.  See the explanation of ``Retrieved key/data
     permanence'' in the section of the Berkeley DB manual on the DBT
     type.
   - pool-friendly --- the data returned by Berkeley DB is now guaranteed
     to be freed when POOL is cleared.  */
DBT *svn_fs__result_dbt (DBT *dbt);


/* Arrange for POOL to `track' DBT's data: when POOL is cleared,
   DBT->data will be freed, using `free'.  If DBT->data is zero,
   do nothing.

   This is meant for use with svn_fs__result_dbt; see the explanation
   there.  */
DBT *svn_fs__track_dbt (DBT *dbt, apr_pool_t *pool);


/* Compare two DBT values in byte-by-byte lexicographic order.  */
int svn_fs__compare_dbt (DBT *a, DBT *b);


#endif /* SVN_LIBSVN_FS_DBT_H */
