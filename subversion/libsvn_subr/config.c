/*
 * config.c :  reading configuration files
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



#include <string.h>

#include "svn_private_config.h"

#include <apr_lib.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_config.h"


#ifdef SVN_WIN32
/* Sorry about the #ifdefs, but some things can't be helped. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif /* SVN_WIN32 */



/* The configuration data. This is a superhash of sections and options. */
struct svn_config_t
{
  /* Table of cfg_section_t's. */
  apr_hash_t *sections;

  /* Pool for hash tables, table entries and unexpanded values */
  apr_pool_t *pool;

  /* Pool for expanded values -- this is separate, so that we can
     clear it when modifying the config data. */
  apr_pool_t *x_pool;

  /* Indicates that some values in the configuration have been expanded. */
  svn_boolean_t x_values;


  /* Temporary string used for lookups. */
  svn_stringbuf_t *tmp_key;
};


/* Section table entries. */
typedef struct cfg_section_t cfg_section_t;
struct cfg_section_t
{
  /* The section name -- hash key. */
  const char *name;

  /* Table of cfg_option_t's. */
  apr_hash_t *options;
};


/* Option table entries. */
typedef struct cfg_option_t cfg_option_t;
struct cfg_option_t
{
  /* The option name -- hash key. */
  const char *name;

  /* The unexpanded option value. */
  svn_stringbuf_t *value;

  /* The expanded option value. */
  svn_stringbuf_t *x_value;

  /* Expansion flag. If this is TRUE, this value has already been expanded.
     In this case, if x_value is NULL, no expansions were necessary,
     and value should be used directly. */
  svn_boolean_t expanded;
};



static svn_error_t *parse_file (svn_config_t *cfg,
                                const char *file,
                                svn_boolean_t must_exist);

#ifdef SVN_WIN32
static svn_error_t *parse_registry (svn_config_t *cfg,
                                    const char *file,
                                    svn_boolean_t must_exist);
#endif /* SVN_WIN32 */



/* Remove variable expansions from CFG.  Walk through the options tree,
   killing all expanded values, then clear the expanded value pool. */
static void
remove_expansions (svn_config_t *cfg)
{
  apr_hash_index_t *sec_ndx;

  if (!cfg->x_values)
    return;

  for (sec_ndx = apr_hash_first (cfg->x_pool, cfg->sections);
       sec_ndx != NULL;
       sec_ndx = apr_hash_next (sec_ndx))
    {
      void *sec_ptr;
      cfg_section_t *sec;
      apr_hash_index_t *opt_ndx;

      apr_hash_this (sec_ndx, NULL, NULL, &sec_ptr);
      sec = sec_ptr;

      for (opt_ndx = apr_hash_first (cfg->x_pool, sec->options);
           opt_ndx != NULL;
           opt_ndx = apr_hash_next (opt_ndx))
        {
          void *opt_ptr;
          cfg_option_t *opt;

          apr_hash_this (opt_ndx, NULL, NULL, &opt_ptr);
          opt = opt_ptr;

          if (opt->expanded)
            {
              opt->x_value = NULL;
              opt->expanded = FALSE;
            }
        }
    }

  apr_pool_clear (cfg->x_pool);
}



svn_error_t *
svn_config_read (svn_config_t **cfgp, const char *file,
                 svn_boolean_t must_exist, apr_pool_t *pool)
{
  apr_pool_t *cfg_pool = svn_pool_create (pool);
  svn_config_t *cfg = apr_palloc (cfg_pool, sizeof (*cfg));
  svn_error_t *err;

  cfg->sections = apr_hash_make(cfg_pool);
  cfg->pool = cfg_pool;
  cfg->x_pool = svn_pool_create(cfg_pool);
  cfg->x_values = FALSE;
  cfg->tmp_key = svn_stringbuf_create ("", cfg_pool);

#ifdef SVN_WIN32
  if (0 == strncmp (file, "REGISTRY:", 9))
    err = parse_registry (cfg, file, must_exist);
  else
#endif /* SVN_WIN32 */
    err = parse_file (cfg, file, must_exist);

  if (err != SVN_NO_ERROR)
    svn_config_destroy (cfg);
  else
    *cfgp = cfg;

  return err;
}


svn_error_t *
svn_config_merge (svn_config_t *cfg, const char *file,
                  svn_boolean_t must_exist)
{
  svn_error_t *err;

  remove_expansions (cfg);

#ifdef SVN_WIN32
  if (0 == strncmp (file, "REGISTRY:", 9))
    err = parse_registry (cfg, file, must_exist);
  else
#endif /* SVN_WIN32 */
    err = parse_file (cfg, file, must_exist);

  return err;
}


void
svn_config_destroy (svn_config_t *cfg)
{
  apr_pool_destroy(cfg->pool);
}



/* Canonicalize a string for hashing.  Modifies KEY in place. */
static char *
make_hash_key (char *key)
{
  register char *p;
  for (p = key; *p != 0; ++p)
    *p = apr_toupper (*p);
  return key;
}


/* Return a pointer to an option in CFG, or NULL if it doesn't exist.
   if SECTIONP is non-null, return a pointer to the option's section. */
static cfg_option_t *
find_option (svn_config_t *cfg, const char *section, const char *option,
             cfg_section_t **sectionp)
{
  void *sec_ptr;

  /* Canonicalize the hash key */
  svn_stringbuf_set (cfg->tmp_key, section);
  make_hash_key (cfg->tmp_key->data);

  sec_ptr = apr_hash_get (cfg->sections, cfg->tmp_key->data,
                          APR_HASH_KEY_STRING);
  if (sectionp != NULL)
    *sectionp = sec_ptr;

  if (sec_ptr != NULL)
    {
      cfg_section_t *sec = sec_ptr;

      /* Canonicalize the option key */
      svn_stringbuf_set (cfg->tmp_key, option);
      make_hash_key (cfg->tmp_key->data);

      return apr_hash_get (sec->options, cfg->tmp_key->data,
                           APR_HASH_KEY_STRING);
    }

  return NULL;
}



void
svn_config_get (svn_config_t *cfg, svn_string_t *valuep,
                const char *section, const char *option,
                const char *default_value)
{
  cfg_section_t *sec;
  cfg_option_t *opt;

  opt = find_option (cfg, section, option, &sec);
  if (opt != NULL)
    {
      /* TODO: Expand the option's value */
      if (opt->x_value)
        {
          valuep->data = opt->x_value->data;
          valuep->len = opt->x_value->len;
        }
      else
        {
          valuep->data = opt->value->data;
          valuep->len = opt->value->len;
        }
    }
  else
    {
      /* TODO: Expand default_value */
      valuep->data = default_value;
      valuep->len = strlen (default_value);
    }
}



void
svn_config_set (svn_config_t *cfg,
                const char *section, const char *option,
                const char *value)
{
  cfg_section_t *sec;
  cfg_option_t *opt;

  remove_expansions (cfg);

  opt = find_option (cfg, section, option, &sec);
  if (opt != NULL)
    {
      /* Replace the option's value. */
      svn_stringbuf_set (opt->value, value);
      return;
    }

  /* Create a new option */
  opt = apr_palloc (cfg->pool, sizeof (*opt));
  opt->name = make_hash_key (apr_pstrdup (cfg->pool, option));

  opt->value = svn_stringbuf_create (value, cfg->pool);
  opt->x_value = NULL;
  opt->expanded = FALSE;

  if (sec == NULL)
    {
      /* Even the section doesn't exist. Create it. */
      sec = apr_palloc (cfg->pool, sizeof (*sec));
      sec->name = make_hash_key (apr_pstrdup (cfg->pool, section));
      sec->options = apr_hash_make(cfg->pool);
      apr_hash_set (cfg->sections, sec->name, APR_HASH_KEY_STRING, sec);
    }

  apr_hash_set (sec->options, opt->name, APR_HASH_KEY_STRING, opt);
}



void
svn_config_enumerate (svn_config_t *cfg, const char *section,
                      svn_boolean_t (*callback) (const svn_string_t*))
{
  (void)(cfg);
  (void)(section);
  (void)(callback);
}



static svn_error_t *
parse_file (svn_config_t *cfg, const char *file, svn_boolean_t must_exist)
{
  (void)(cfg);
  (void)(file);
  (void)(must_exist);
  return SVN_NO_ERROR;
}



#ifdef SVN_WIN32
static svn_error_t *
parse_registry (svn_config_t *cfg, const char *file, svn_boolean_t must_exist)
{
  (void)(cfg);
  (void)(file);
  (void)(must_exist);
  return SVN_NO_ERROR;
}
#endif /* SVN_WIN32 */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
