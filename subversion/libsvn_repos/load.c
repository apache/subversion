/* load.c --- parsing a 'dumpfile'-formatted stream.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
 */


#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_hash.h"
#include "svn_path.h"


/*----------------------------------------------------------------------*/

/** The parser **/


svn_error_t *
svn_repos_parse_dumpstream (svn_stream_t *stream,
                            const svn_repos_parser_fns_t *parse_fns,
                            apr_pool_t *pool)
{
  /* ### do work. */

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/** vtable for doing commits to a fs **/

/* ### right now, this vtable will do nothing but stupid printf's */

struct revision_baton
{
  svn_revnum_t rev;
  apr_hash_t *prophash;
  apr_pool_t *pool;
};

struct node_baton
{
  const char *path;
  enum svn_node_kind kind;
  enum svn_node_action action;
  apr_hash_t *prophash;

  struct revision_baton *rb;
  apr_pool_t *pool;
};


static struct node_baton *
make_node_baton (apr_hash_t *headers,
                 apr_pool_t *pool)
{
  /* ### parse the headers into a node_baton struct */
  return NULL;
}

static struct revision_baton *
make_revision_baton (apr_hash_t *headers,
                     apr_pool_t *pool)
{
  /* ### parse the headers into a revision_baton struct */
  return NULL;
}


static svn_error_t *
new_revision_record (void **revision_baton,
                     apr_hash_t *headers,
                     apr_pool_t *pool)
{
  printf ("Got a new revision record.\n");
  *revision_baton = make_revision_baton (headers, pool);

  /* ### Create a new txn someday. */

  return SVN_NO_ERROR;
}


static svn_error_t *
new_node_record (void **node_baton,
                 apr_hash_t *headers,
                 apr_pool_t *pool)
{
  printf ("Got a new node record.\n");

  *node_baton = make_node_baton (headers, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property (void *baton,
                       const char *name,
                       const svn_string_t *value)
{
  printf("Got a revision prop.\n");
  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property (void *baton,
                   const char *name,
                   const svn_string_t *value)
{
  printf("Got a node prop.\n");
  return SVN_NO_ERROR;
}


static svn_error_t *
set_fulltext (svn_stream_t **stream,
              void *node_baton)
{
  printf ("Not interested in fulltext.\n");
  *stream = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_node (void *baton)
{
  printf ("End of node\n");
  return SVN_NO_ERROR;
}


static svn_error_t *
close_revision (void *baton)
{
  /* ### someday commit a txn here. */

  printf ("End of revision\n");
  return SVN_NO_ERROR;
}


static const svn_repos_parser_fns_t parser_callbacks =
  {
    new_revision_record,
    new_node_record,
    set_revision_property,
    set_node_property,
    set_fulltext,
    close_node,
    close_revision
  };


/*----------------------------------------------------------------------*/

/** The main loader routine. **/


svn_error_t *
svn_repos_load_fs (svn_repos_t *repos,
                   svn_stream_t *stream,
                   apr_pool_t *pool)
{

  /* ### Someday, pass repos info into some kind of
     get_parser_callbacks function.  Heh, of course this means that
     our parser callbacks will need some kind of global baton to hold
     the fs, so that each new revision baton can create a txn
     somewhere. */

  /* This is really simple. */  
  SVN_ERR (svn_repos_parse_dumpstream (stream, &parser_callbacks, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
