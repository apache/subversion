/*
 * url.c:  converting paths to urls
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



#include <apr_pools.h>

#include "svn_error.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_path.h"
#include "client.h"




svn_error_t *
svn_client_url_from_path(const char **url,
                         const char *path_or_url,
                         apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;          
  const svn_wc_entry_t *entry;  
  svn_boolean_t is_url = svn_path_is_url(path_or_url);
  
  if (is_url)
    {
      *url = path_or_url;
    }
  else
    {
      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url,
                                     FALSE, 0, NULL, NULL, pool));
      SVN_ERR(svn_wc_entry(&entry, path_or_url, adm_access, FALSE, pool));
      SVN_ERR(svn_wc_adm_close(adm_access));
      
      *url = entry ? entry->url : NULL;
    }

  return SVN_NO_ERROR;
}
