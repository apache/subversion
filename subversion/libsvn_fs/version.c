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


static svn_error_t *
no_such_version (svn_fs_t *fs, svn_vernum_t v)
{
  return
    svn_error_createf (SVN_ERR_FS_NO_SUCH_VERSION, 0, 0, fs->pool,
		       "filesystem `%s' has no version number %d",
		       fs->env_path, v);
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
  db_recno_t recno;
  DBT key, value;
  int db_err;
  skel_t *version;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Turn the version number into a Berkeley DB record number.
     Versions are numbered starting with zero; Berkeley DB record numbers
     begin with one.  */
  recno = v + 1;
  svn_fs__set_dbt (&key, &recno, sizeof (recno));

  svn_fs__result_dbt (&value);
  db_err = fs->versions->get (fs->versions, 0, /* no transaction */ 
			      &key, &value, DB_SET_RECNO);
  if (db_err == DB_NOTFOUND)
    return no_such_version (fs, v);
  SVN_ERR (DB_ERR (fs, "reading version root from filesystem", db_err));
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
  apr_pool_t *subpool = svn_pool_create (pool);
  skel_t *version, *id_skel;
  svn_fs_id_t *id;

  SVN_ERR (get_version_skel (&version, fs, v, subpool));
  if (svn_fs__list_length (version) != 3
      || ! svn_fs__is_atom (version->children, "version"))
    goto corrupt;

  id_skel = version->children->next;
  if (! id_skel->is_atom)
    goto corrupt;

  id = svn_fs__parse_id (id_skel->data, id_skel->len, 0, pool);
  if (! id)
    goto corrupt;

  *id_p = id;
  return 0;

 corrupt:
  apr_destroy_pool (subpool);
  return corrupt_version (fs, v);
}



/* Writing versions.  */


/* Add VERSION_SKEL as a new version to FS's `versions' table.  Set *V
   to the number of the new version created.

   Do this as part of the Berkeley DB transaction TXN; if TXN is zero,
   then make the change without transaction protection.

   Do any necessary temporary allocation in POOL.  */
static svn_error_t *
put_version_skel (svn_fs_t *fs,
		  DB_TXN *txn,
		  skel_t *version_skel,
		  svn_vernum_t *v,
		  apr_pool_t *pool)
{
  svn_string_t *version = svn_fs__unparse_skel (version_skel, pool);
  db_recno_t recno;
  DBT key, value;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Since we use the DB_APPEND flag, the `put' call sets recno to the record
     number of the new version.  */
  recno = 0;
  svn_fs__clear_dbt (&key);
  key.data = &recno;
  key.size = key.ulen = sizeof (recno);
  key.flags |= DB_DBT_USERMEM;

  svn_fs__set_dbt (&value, version->data, version->len);
  SVN_ERR (DB_ERR (fs, "adding new version",
		   fs->versions->put (fs->versions, txn, &key, &value, 
				      DB_APPEND)));

  /* Turn the record number into a Subversion version number.
     Versions are numbered starting with zero; Berkeley DB record numbers
     begin with one.  */
  *v = recno - 1;
  return 0;
}



/* Creating and opening a filesystem's `versions' table.  */


/* Open / create FS's `versions' table.  FS->env must already be open;
   this function initializes FS->versions.  If CREATE is non-zero, assume
   we are creating the filesystem afresh; otherwise, assume we are
   simply opening an existing database.  */
static svn_error_t *
make_versions (svn_fs_t *fs, int create)
{
  DB *versions;

  SVN_ERR (DB_ERR (fs, "allocating `versions' table object",
		   db_create (&versions, fs->env, 0)));
  SVN_ERR (DB_ERR (fs, "creating `versions' table",
		   versions->open (versions, "versions", 0, DB_RECNO,
				   create ? (DB_CREATE | DB_EXCL) : 0,
				   0666)));

  if (create)
    {
      /* Create the initial version.  */
      static char version_0[] = "(version 3 0.0 ())";
      skel_t *version_skel = svn_fs__parse_skel (version_0,
						 sizeof (version_0) - 1,
						 fs->pool);
      svn_vernum_t v;
      SVN_ERR (put_version_skel (fs, 0, version_skel, &v, fs->pool));

      /* That had better have created version zero.  */
      if (v != 0)
	abort ();
    }

  fs->versions = versions;
  return 0;
}


/* Create a new `versions' table for the new filesystem FS.  FS->env
   must already be open; this sets FS->versions.  */
svn_error_t *
svn_fs__create_versions (svn_fs_t *fs)
{
  return make_versions (fs, 1);
}


/* Open the existing `versions' table for the filesystem FS.  FS->env
   must already be open; this sets FS->versions.  */
svn_error_t *
svn_fs__open_versions (svn_fs_t *fs)
{
  return make_versions (fs, 0);
}
