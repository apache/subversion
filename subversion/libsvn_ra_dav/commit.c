/*
 * commit.c :  routines for committing changes to the server
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
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
 * individuals on behalf of CollabNet.
 *
 * Portions of this software were originally written by Greg Stein as a
 * sourceXchange project sponsored by SapphireCreek.
 */



#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"

#include "ra_session.h"


typedef struct
{
  svn_ra_session_t *ras;

} commit_ctx_t;


static svn_error_t *
commit_delete (svn_string_t *name,
               void *edit_baton,
               void *parent_baton)
{
  return NULL;
}

static svn_error_t *
commit_add_dir (svn_string_t *name,
                void *edit_baton,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_vernum_t ancestor_version,
                void **child_baton)
{
  return NULL;
}

static svn_error_t *
commit_rep_dir (svn_string_t *name,
                void *edit_baton,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_vernum_t ancestor_version,
                void **child_baton)
{
  return NULL;
}

static svn_error_t *
commit_change_dir_prop (void *edit_baton,
                        void *dir_baton,
                        svn_string_t *name,
                        svn_string_t *value)
{
  return NULL;
}

static svn_error_t *
commit_change_dirent_prop (void *edit_baton,
                           void *dir_baton,
                           svn_string_t *entry,
                           svn_string_t *name,
                           svn_string_t *value)
{
  return NULL;
}

static svn_error_t *
commit_finish_dir (void *edit_baton, void *dir_baton)
{
  return NULL;
}

static svn_error_t *
commit_add_file (svn_string_t *name,
                 void *edit_baton,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_vernum_t ancestor_version,
                 void **file_baton)
{
  return NULL;
}

static svn_error_t *
commit_rep_file (svn_string_t *name,
                 void *edit_baton,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_vernum_t ancestor_version,
                 void **file_baton)
{
  return NULL;
}

static svn_error_t *
commit_apply_txdelta (void *edit_baton,
                      void *parent_baton,
                      void *file_baton, 
                      svn_txdelta_window_handler_t **handler,
                      void **handler_baton)
{
  return NULL;
}

static svn_error_t *
commit_change_file_prop (void *edit_baton,
                         void *parent_baton,
                         void *file_baton,
                         svn_string_t *name,
                         svn_string_t *value)
{
  return NULL;
}

static svn_error_t *
commit_finish_file (void *edit_baton, void *file_baton)
{
  return NULL;
}

/*
** This structure is used during the commit process. An external caller
** uses these callbacks to describe all the changes in the working copy
** that must be committed to the server.
*/
static const svn_delta_edit_fns_t commit_editor = {
  commit_delete,
  commit_add_dir,
  commit_rep_dir,
  commit_change_dir_prop,
  commit_change_dirent_prop,
  commit_finish_dir,
  commit_add_file,
  commit_rep_file,
  commit_apply_txdelta,
  commit_change_file_prop,
  commit_finish_file
};

svn_error_t *
svn_ra_get_commit_editor(svn_ra_session_t *ras,
                         const svn_delta_edit_fns_t **editor,
                         void **edit_baton)
{
  commit_ctx_t *cc = apr_pcalloc(ras->pool, sizeof(*cc));

  cc->ras = ras;
  *edit_baton = cc;

  *editor = &commit_editor;

  return NULL;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
