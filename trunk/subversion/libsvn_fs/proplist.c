/* proplist.c : operations on PROPLIST skels
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

#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "proplist.h"
#include "validate.h"


/* Generic PROPLIST skel routines. */


svn_error_t *
svn_fs__get_prop (svn_string_t **value_p,
                  skel_t *proplist,
                  const char *name,
                  apr_pool_t *pool)
{
  skel_t *prop;

  if (! svn_fs__is_valid_proplist (proplist))
    return
      svn_error_create
      (SVN_ERR_FS_CORRUPT, 0, NULL, pool,
       "svn_fs__get_prop: Malformed property list.");

  /* Search the proplist for a property with the right name.  */
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *this_name = prop;
      skel_t *this_value = prop->next;

      if (svn_fs__matches_atom (this_name, name))
        {
          *value_p = svn_string_ncreate (this_value->data,
                                         this_value->len, 
                                         pool);
          return SVN_NO_ERROR;
        }
    }

  *value_p = 0;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__make_prop_hash (apr_hash_t **prop_hash,
                        skel_t *proplist,
                        apr_pool_t *pool)
{
  skel_t *prop;
  apr_hash_t *table;

  if (! svn_fs__is_valid_proplist (proplist))
    return
      svn_error_create
      (SVN_ERR_FS_CORRUPT, 0, NULL, pool,
       "svn_fs__make_prop_hash: Malformed property list.");

  /* Build a hash table from the property list.  */
  table = apr_hash_make (pool);
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *this_name = prop;
      skel_t *this_value = prop->next;

      /* note: we need to copy and null-terminate the key (to produce a C
         string; the data in a skel_t references the raw skel string), and
         create an svn_stringbuf_t structure for the value. */

      apr_hash_set (table,
                    apr_pstrndup(pool, this_name->data, this_name->len),
                    this_name->len,
                    svn_string_ncreate (this_value->data, 
                                        this_value->len, 
                                        pool));
    }

  *prop_hash = table;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_prop (skel_t *proplist,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  skel_t *prop;
  skel_t *prev = NULL;

  if (! svn_fs__is_valid_proplist (proplist))
    return
      svn_error_create
      (SVN_ERR_FS_CORRUPT, 0, NULL, pool,
       "svn_fs__set_prop: Malformed property list.");

  /* Look through the proplist, trying to find property NAME. */
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *this_name = prop;
      skel_t *this_value = prop->next;

      if (svn_fs__matches_atom (this_name, name))
        {
          /* We've found the property we wish to change.  Let's see
             what kind of change we're supposed to be making here. */
          if (! value)
            {
              /* Our new value for this is NULL, so remove the
                 property altogether by effectively routing our linked
                 list of properties around the current property
                 name/value pair. */

              if (prev)
                {
                  /* If this isn't the first pair in the list, this
                     can be done by setting the previous value's next
                     pointer to the name of the following property
                     pair, if one exists, or zero if we are removing
                     the last name/value pair currently in the
                     list. */
                  if (prop->next)
                    prev->next->next = prop->next->next;
                  else
                    prev->next->next = 0;
                }
              else
                {
                  /* If, however, this is the first item in the list,
                     we'll set the children pointer of the PROPLIST
                     skel to the following name/value pair, if one
                     exists, or zero if we're removing the only
                     property pair in the list. */
                  if (prop->next)
                    proplist->children = prop->next->next;
                  else
                    proplist->children = 0;
                }
            }
          else
            {
              this_value->data = value->data;
              this_value->len = value->len;
            }

          /* Regardless of what we changed, we're done editing the
             list now that we've acted on the property we found. */
          break;
        }

      /* Squirrel away a pointer to this property name/value pair, as
         we may need this in the next iteration of this loop. */
      prev = prop;
    }

  if ((! prop) && value)
    {
      /* The property we were seeking to change is not currently in
         the property list, so well add its name and desired value to
         the beginning of the property list. */
      svn_fs__prepend (svn_fs__mem_atom (value->data,
                                         value->len,
                                         pool),
                       proplist);
      svn_fs__prepend (svn_fs__mem_atom (name,
                                         strlen (name),
                                         pool),
                       proplist);
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
