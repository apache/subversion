/*
 * server_init.c :   parse server configuration file
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
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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


#include <apr_hash.h>
#include <apr_dso.h>
#include "svn_svr.h"
#include "svn_parse.h"


/* 
   svn_svr_load_plugin() : 
         Utility to load & register a server plugin into a policy

   Input:    * a policy in which to register the plugin
             * pathname of the shared library to load
             * name of the initialization routine in the plugin
            
   Returns:  error structure or SVN_SUCCESS

   ASSUMES that ap_dso_init() has already been called!

*/

svn_error_t *
svn_svr_load_plugin (svn_svr_policies_t *policy,
                     const svn_string_t *path,
                     const svn_string_t *init_routine)
{
  ap_dso_handle_t *library;
  ap_dso_handle_sym_t initfunc;
  ap_status_t result;
  svn_error_t *error;

  char *my_path   = svn_string_2cstring (path, policy->pool);
  char *my_sym    = svn_string_2cstring (init_routine, policy->pool);

  /* Load the plugin */
  result = ap_dso_load (&library, my_path, policy->pool);

  if (result != APR_SUCCESS)
    {
      char *msg =
        ap_psprintf (policy->pool,
                     "svn_svr_load_plugin(): can't load DSO %s", my_path); 
      return svn_create_error (result, NULL, msg, NULL, policy->pool);
    }
  

  /* Find the plugin's initialization routine. */
  
  result = ap_dso_sym (&initfunc, library, my_sym);

  if (result != APR_SUCCESS)
    {
      char *msg =
        ap_psprintf (policy->pool,
                     "svn_svr_load_plugin(): can't find symbol %s", my_sym); 
      return svn_create_error (result, NULL, msg, NULL, policy->pool);
    }

  /* Call the plugin's initialization routine.  

     This causes the plugin to call svn_svr_register_plugin(), the end
     result of which is a new plugin structure safely nestled within
     our policy structure.  */

  error = (*initfunc) (policy, library);

  if (error)
    {
      return svn_quick_wrap_error
        (error, "svn_svr_load_plugin(): plugin initialization failed.");
    }
  
  return SVN_SUCCESS;
}





/*  svn__svr_load_all_plugins :  NOT EXPORTED

    Loops through hash of plugins, loads each using APR's DSO
    routines.  Each plugin ultimately registers (appends) itself into
    the policy structure.  */

svn_error_t *
svn__svr_load_all_plugins (ap_hash_t *plugins, svn_svr_policies_t *policy)
{
  ap_hash_index_t *hash_index;
  void *key, *val;
  size_t keylen;
  svn_error_t *err, *latesterr;
  err = latesterr = NULL;
  
  /* Initialize the APR DSO mechanism*/
  ap_status_t result = ap_dso_init();

  if (result != APR_SUCCESS)
    {
      char *msg = "svr__load_plugins(): fatal: can't ap_dso_init() ";
      return (svn_create_error (result, NULL, msg, NULL, policy->pool));
    }

  /* Loop through the hash of plugins from configdata */

  for (hash_index = ap_hash_first (plugins);    /* get first hash entry */
       hash_index;                              /* NULL if out of entries */
       hash_index = ap_hash_next (hash_index))  /* get next hash entry */
    {
      svn_string_t keystring, *valstring;

      ap_hash_this (hash_index, &key, &keylen, &val);

      keystring.data = key;
      keystring.len = keylen;
      keystring.blocksize = keylen;
      valstring = val;

      err = svn_svr_load_plugin (policy, &keystring, val);
      if (err)
        {
          /* Nest all errors returned from failed plugins, 
             but DON'T RETURN yet!  */
          err->child = latesterr;
          latesterr = err;
        }
    }

  /* If no plugins failed, this will be NULL, which still means
     "success".  If one or more plugins failed to load, this will
     contain a nesty list of each plugin's error structure. */
  return latesterr;

}





/* 
   svn_svr_init()   -- create a new, empty "policy" structure

   Input:  ptr to policy ptr, pool
    
   Returns: alloc's empty policy structure, 
            returns svn_error_t * or SVN_SUCCESS

*/

svn_error_t *
svn_svr_init (svn_svr_policies_t **policy,
              ap_pool_t *pool)
{
  ap_status_t result;

  /* First, allocate a `policy' structure and all of its internal
     lists */

  *policy = 
    (svn_svr_policies_t *) ap_palloc (pool, sizeof(svn_svr_policies_t));

  *policy->repos_aliases = ap_make_hash (pool);
  *policy->global_restrictions = ap_make_hash (pool);
  *policy->plugins = ap_make_hash (pool);


  /* A policy structure has its own private memory pool, a sub-pool of
     the pool passed in.  */

  result = ap_create_pool (& (*policy->pool), pool);

  if (result != APR_SUCCESS)
    {
      char *msg = "svr_init(): can't create sub-pool within policy struct";
      return (svn_create_error (result, NULL, msg, NULL, pool));
    }

  return SVN_SUCCESS;
}



svn_error_t *
svn_svr_load_policy (svn_svr_policies_t *policy, 
                     const char *filename)
{
  ap_hash_t *configdata;
  svn_error_t *err;

  /* Parse the file, get a hash-of-hashes back */
  err = svn_parse (&configdata, filename, policy->pool);
  if (err)
    return (svn_quick_wrap_error
            (err, "svn_svr_load_policy():  parser failed."));


  /* Ben sez:  we need a debugging system here.  Let's get one quick. (TODO)
     i.e.  
            if (DEBUGLVL >= 2) {  printf...;  svn_uberhash_print(); }
  */
  svn_uberhash_print (configdata, stdout);


  /* Now walk through our Uberhash, filling in the policy as we go. */
  {
    ap_hash_index_t *hash_index;
    void *key, *val;
    size_t keylen;

    for (hash_index = ap_hash_first (configdata); /* get first hash entry */
         hash_index;                              /* NULL if out of entries */
         hash_index = ap_hash_next (hash_index))  /* get next hash entry */
      {
        /* Retrieve key and val from current hash entry */
        ap_hash_this (hash_index, &key, &keylen, &val);

        /* Figure out which `section' of svn.conf we're looking at */

        if (svn_string_compare_2cstring ((svn_string_t *) key,
                                         "repos_aliases"))
          {
            /* The "val" is a pointer to a hash full of repository
               aliases, alrady as we want them.  Just store this value
               in our policy structure! */

            printf ("svr_init(): got repository aliases.\n");
            policy->repos_aliases = (ap_hash_t *) val;
          }

        else if (svn_string_compare_2cstring ((svn_string_t *) key,
                                              "security"))
          {
            /* The "val" is a pointer to a hash full of security
               commands; again, we just store a pointer to this hash
               in our policy (the commands are interpreted elsewhere) */

            printf ("svr_init(): got security restrictions.\n");
            policy->global_restrictions = (ap_hash_t *) val;
          }

        else if (svn_string_compare_2cstring ((svn_string_t *) key,
                                              "plugins"))
          {
            /* The "val" is a pointer to a hash containing plugin
               libraries to load up.  We'll definitely do that here
               and now! */
            
            printf ("svr_init(): loading list of plugins...\n");
            
            svn__svr_load_all_plugins ((ap_hash_t *) val, policy);

          }

        else
          {
            svn_string_t *msg = 
              svn_string_create 
              ("svn_svr_load_policy(): warning: ignoring unknown section: ", 
                                 pool);
            svn_string_appendstr (msg, (svn_string_t *) key, pool);
            svn_handle_error (svn_create_error 
                              (SVN_ERR_UNRECOGNIZED_SECTION, NULL,
                               svn_string_2cstring (msg, pool),
                               NULL, pool), stderr);            
          }
      }    /* for (hash_index...)  */
       
  } /* closing of Uberhash walk-through */
  

  return SVN_SUCCESS;
}




/* Add a plugin structure to a server policy structure.
   Called by each plugin's init() routine. */

svn_error_t *
svn_svr_register_plugin (svn_svr_policies_t *policy,
                         svn_svr_plugin_t *new_plugin)
{
  ap_hash_set (policy->plugins,         /* the policy's plugin hashtable */
               new_plugin->name->data,  /* key = name of the plugin */
               new_plugin->name->len,   /* length of this name */
               new_plugin);             /* val = ptr to the plugin itself */

  /* Hm... how would this routine fail? :)  */

  return SVN_SUCCESS;  /* success */
}








/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
