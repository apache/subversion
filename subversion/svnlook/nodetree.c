/*
 * nodetree.c:  a repository node tree suite
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */




#include <stdio.h>
#include "apr_pools.h"
#include "svn_types.h"
#include "svnlook.h"



/*** Tree structures and functions. ***/


repos_node_t *
svnlook_create_node (const char *name, 
                     apr_pool_t *pool)
{
  repos_node_t *node = apr_palloc (pool, sizeof (repos_node_t));
  node->sibling = NULL;
  node->child = NULL;
  node->text_mod = FALSE;
  node->prop_mod = FALSE;
  node->action = 'R';
  node->kind = svn_node_unknown;
  node->name = apr_pstrdup (pool, name);
  return node;
}


repos_node_t *
svnlook_create_sibling_node (repos_node_t *elder, 
                             const char *name, 
                             apr_pool_t *pool)
{
  repos_node_t *tmp_node;
  
  /* No ELDER sibling?  That's just not gonna work out. */
  if (! elder)
    return NULL;

  /* Run to the end of the list of siblings of ELDER. */
  tmp_node = elder;
  while (tmp_node->sibling)
    tmp_node = tmp_node->sibling;

  /* Create a new youngest sibling and return that. */
  return (tmp_node->sibling = svnlook_create_node (name, pool));
}


repos_node_t *
svnlook_create_child_node (repos_node_t *parent, 
                           const char *name, 
                           apr_pool_t *pool)
{
  /* No PARENT node?  That's just not gonna work out. */
  if (! parent)
    return NULL;

  /* If PARENT has no children, create its first one and return that. */
  if (! parent->child)
    return (parent->child = svnlook_create_node (name, pool));

  /* If PARENT already has a child, create a new sibling for its first
     child and return that. */
  return svnlook_create_sibling_node (parent->child, name, pool);
}


repos_node_t *
svnlook_find_child_by_name (repos_node_t *parent, 
                            const char *name)
{
  repos_node_t *tmp_node;

  /* No PARENT node, or a barren PARENT?  Nothing to find. */
  if ((! parent) || (! parent->child))
    return NULL;

  /* Look through the children for a node with a matching name. */
  tmp_node = parent->child;
  while (1)
    {
      if (! strcmp (tmp_node->name, name))
        {
          return tmp_node;
        }
      else
        {
          if (tmp_node->sibling)
            tmp_node = tmp_node->sibling;
          else
            break;
        }
    }

  return NULL;
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
