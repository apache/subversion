/* version.c --- functions for working with filesystem versions
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

#include "db.h"

#include "svn_fs.h"
#include "fs.h"
#include "version.h"
#include "err.h"
#include "id.h"
#include "convert-size.h"
#include "skel.h"
#include "dbt.h"


/* Building some often-used error objects.  */

static svn_error_t *
corrupt_version (svn_fs_t *fs, svn_vernum_t v)
{
  return
    svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
		       "corrupt root data for version %d of filesystem `%s'",
		       v, fs->env_path);
}



/* Reading versions.  */


/* Set *SKEL to the VERSION skel of version V of the filesystem FS.
   SKEL and the data block it points into will both be freed when POOL
   is cleared.

   Beyond verifying that it's a syntactically valid skel, this doesn't
   validate the data returned at all.  */
static svn_error_t *
get_version_skel (skel_t **skel,
		  svn_fs_t *fs,
		  svn_vernum_t v, 
		  apr_pool_t *pool)
{
  DBT key, value;
  char key_bytes[200];
  int key_len;
  skel_t *version;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Generate the ASCII decimal form of the version number.  */
  key_len = svn_fs__putsize (key_bytes, sizeof (key_bytes), v);
  if (! key_len)
    abort ();
  svn_fs__set_dbt (&key, key_bytes, key_len);

  svn_fs__result_dbt (&value);
  SVN_ERR (DB_ERR (fs, "reading version root from filesystem",
		   fs->versions->get (fs->versions,
				      0, /* no transaction */
				      &key, &value, 0)));
  svn_fs__track_dbt (&value, pool);

  version = svn_fs__parse_skel (value.data, value.size, pool);
  if (! version)
    return corrupt_version (fs, v);
  *skel = version;
  return 0;
}


/* Set *ID to ID of the root of version V of the filesystem FS.
   Allocate the ID in POOL.  */
svn_error_t *
svn_fs__version_root (svn_fs_id_t **id_p,
		      svn_fs_t *fs,
		      svn_vernum_t v,
		      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool, 0);
  skel_t *version, *id_skel;
  svn_fs_id_t *id;

  SVN_ERR (get_version_skel (&version, fs, v, subpool));
  if (svn_fs__list_length (version) != 3
      || ! svn_fs__is_atom (version->children, "version"))
    goto corrupt;

  id_skel = version->children->next;
  if (! id_skel->is_atom)
    goto corrupt;

  id = svn_fs__parse_id (id_skel->data, id_skel->len, pool);
  if (! id)
    goto corrupt;

  *id_p = id;
  return 0;

 corrupt:
  apr_destroy_pool (subpool);
  return corrupt_version (fs, v);
}
