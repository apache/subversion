/* fs_fs.c --- filesystem operations specific to fs_fs
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "fs_fs.h"

#include <apr_uuid.h>

#include "svn_hash.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_version.h"

#include "cached_data.h"
#include "id.h"
#include "rep-cache.h"
#include "revprops.h"
#include "transaction.h"
#include "tree.h"
#include "util.h"
#include "index.h"

#include "private/svn_fs_util.h"
#include "private/svn_string_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* The default maximum number of files per directory to store in the
   rev and revprops directory.  The number below is somewhat arbitrary,
   and can be overridden by defining the macro while compiling; the
   figure of 1000 is reasonable for VFAT filesystems, which are by far
   the worst performers in this area. */
#ifndef SVN_FS_FS_DEFAULT_MAX_FILES_PER_DIR
#define SVN_FS_FS_DEFAULT_MAX_FILES_PER_DIR 1000
#endif

/* Begin deltification after a node history exceeded this this limit.
   Useful values are 4 to 64 with 16 being a good compromise between
   computational overhead and repository size savings.
   Should be a power of 2.
   Values < 2 will result in standard skip-delta behavior. */
#define SVN_FS_FS_MAX_LINEAR_DELTIFICATION 16

/* Finding a deltification base takes operations proportional to the
   number of changes being skipped. To prevent exploding runtime
   during commits, limit the deltification range to this value.
   Should be a power of 2 minus one.
   Values < 1 disable deltification. */
#define SVN_FS_FS_MAX_DELTIFICATION_WALK 1023




/* Check that BUF, a nul-terminated buffer of text from format file PATH,
   contains only digits at OFFSET and beyond, raising an error if not.

   Uses POOL for temporary allocation. */
static svn_error_t *
check_format_file_buffer_numeric(const char *buf, apr_off_t offset,
                                 const char *path, apr_pool_t *pool)
{
  return check_file_buffer_numeric(buf, offset, path, "Format", pool);
}

static svn_error_t *
read_format(int *pformat, int *max_files_per_dir,
            const char *path, apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stream_t *stream;
  svn_stringbuf_t *content;
  svn_stringbuf_t *buf;
  svn_boolean_t eos = FALSE;

  err = svn_stringbuf_from_file2(&content, path, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* Treat an absent format file as format 1.  Do not try to
         create the format file on the fly, because the repository
         might be read-only for us, or this might be a read-only
         operation, and the spirit of FSFS is to make no changes
         whatseover in read-only operations.  See thread starting at
         http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=97600
         for more. */
      svn_error_clear(err);
      *pformat = 1;
      *max_files_per_dir = 0;

      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  stream = svn_stream_from_stringbuf(content, pool);
  SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eos, pool));
  if (buf->len == 0 && eos)
    {
      /* Return a more useful error message. */
      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                               _("Can't read first line of format file '%s'"),
                               svn_dirent_local_style(path, pool));
    }

  /* Check that the first line contains only digits. */
  SVN_ERR(check_format_file_buffer_numeric(buf->data, 0, path, pool));
  SVN_ERR(svn_cstring_atoi(pformat, buf->data));

  /* Set the default values for anything that can be set via an option. */
  *max_files_per_dir = 0;

  /* Read any options. */
  while (!eos)
    {
      SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eos, pool));
      if (buf->len == 0)
        break;

      if (*pformat >= SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT &&
          strncmp(buf->data, "layout ", 7) == 0)
        {
          if (strcmp(buf->data + 7, "linear") == 0)
            {
              *max_files_per_dir = 0;
              continue;
            }

          if (strncmp(buf->data + 7, "sharded ", 8) == 0)
            {
              /* Check that the argument is numeric. */
              SVN_ERR(check_format_file_buffer_numeric(buf->data, 15, path, pool));
              SVN_ERR(svn_cstring_atoi(max_files_per_dir, buf->data + 15));
              continue;
            }
        }

      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
         _("'%s' contains invalid filesystem format option '%s'"),
         svn_dirent_local_style(path, pool), buf->data);
    }

  return SVN_NO_ERROR;
}

/* Write the format number and maximum number of files per directory
   to a new format file in PATH, possibly expecting to overwrite a
   previously existing file.

   Use POOL for temporary allocation. */
svn_error_t *
svn_fs_fs__write_format(svn_fs_t *fs, apr_pool_t *pool)
{
  svn_stringbuf_t *sb;
  const char *path = path_format(fs, pool);
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *path_tmp;

  SVN_ERR_ASSERT(1 <= ffd->format && ffd->format <= SVN_FS_FS__FORMAT_NUMBER);

  sb = svn_stringbuf_createf(pool, "%d\n", ffd->format);

  if (ffd->format >= SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT)
    {
      if (ffd->max_files_per_dir)
        svn_stringbuf_appendcstr(sb, apr_psprintf(pool, "layout sharded %d\n",
                                                  ffd->max_files_per_dir));
      else
        svn_stringbuf_appendcstr(sb, "layout linear\n");
    }

  SVN_ERR(svn_io_write_unique(&path_tmp,
                              svn_dirent_dirname(path, pool),
                              sb->data, sb->len,
                              svn_io_file_del_none, pool));

  /* rename the temp file as the real destination */
  SVN_ERR(svn_io_file_rename(path_tmp, path, pool));

  /* And set the perms to make it read only */
  return svn_io_set_file_read_only(path, FALSE, pool);
}

/* Return the error SVN_ERR_FS_UNSUPPORTED_FORMAT if FS's format
   number is not the same as a format number supported by this
   Subversion. */
static svn_error_t *
check_format(int format)
{
  /* Blacklist.  These formats may be either younger or older than
     SVN_FS_FS__FORMAT_NUMBER, but we don't support them. */
  if (format == SVN_FS_FS__PACKED_REVPROP_SQLITE_DEV_FORMAT)
    return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                             _("Found format '%d', only created by "
                               "unreleased dev builds; see "
                               "http://subversion.apache.org"
                               "/docs/release-notes/1.7#revprop-packing"),
                             format);

  /* We support all formats from 1-current simultaneously */
  if (1 <= format && format <= SVN_FS_FS__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
     _("Expected FS format between '1' and '%d'; found format '%d'"),
     SVN_FS_FS__FORMAT_NUMBER, format);
}

svn_boolean_t
svn_fs_fs__fs_supports_mergeinfo(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return ffd->format >= SVN_FS_FS__MIN_MERGEINFO_FORMAT;
}

/* Find the youngest revision in a repository at path FS_PATH and
   return it in *YOUNGEST_P.  Perform temporary allocations in
   POOL. */
static svn_error_t *
get_youngest(svn_revnum_t *youngest_p,
             const char *fs_path,
             apr_pool_t *pool)
{
  svn_stringbuf_t *buf;
  SVN_ERR(read_content(&buf, svn_dirent_join(fs_path, PATH_CURRENT, pool),
                       pool));

  *youngest_p = SVN_STR_TO_REV(buf->data);

  return SVN_NO_ERROR;
}


/* Read the configuration information of the file system at FS_PATH
 * and set the respective values in FFD.  Use POOL for allocations.
 */
static svn_error_t *
read_config(fs_fs_data_t *ffd,
            const char *fs_path,
            apr_pool_t *pool)
{
  SVN_ERR(svn_config_read3(&ffd->config,
                           svn_dirent_join(fs_path, PATH_CONFIG, pool),
                           FALSE, FALSE, FALSE, pool));

  /* Initialize ffd->rep_sharing_allowed. */
  if (ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    SVN_ERR(svn_config_get_bool(ffd->config, &ffd->rep_sharing_allowed,
                                CONFIG_SECTION_REP_SHARING,
                                CONFIG_OPTION_ENABLE_REP_SHARING, TRUE));
  else
    ffd->rep_sharing_allowed = FALSE;

  /* Initialize deltification settings in ffd. */
  if (ffd->format >= SVN_FS_FS__MIN_DELTIFICATION_FORMAT)
    {
      SVN_ERR(svn_config_get_bool(ffd->config, &ffd->deltify_directories,
                                  CONFIG_SECTION_DELTIFICATION,
                                  CONFIG_OPTION_ENABLE_DIR_DELTIFICATION,
                                  TRUE));
      SVN_ERR(svn_config_get_bool(ffd->config, &ffd->deltify_properties,
                                  CONFIG_SECTION_DELTIFICATION,
                                  CONFIG_OPTION_ENABLE_PROPS_DELTIFICATION,
                                  TRUE));
      SVN_ERR(svn_config_get_int64(ffd->config, &ffd->max_deltification_walk,
                                   CONFIG_SECTION_DELTIFICATION,
                                   CONFIG_OPTION_MAX_DELTIFICATION_WALK,
                                   SVN_FS_FS_MAX_DELTIFICATION_WALK));
      SVN_ERR(svn_config_get_int64(ffd->config, &ffd->max_linear_deltification,
                                   CONFIG_SECTION_DELTIFICATION,
                                   CONFIG_OPTION_MAX_LINEAR_DELTIFICATION,
                                   SVN_FS_FS_MAX_LINEAR_DELTIFICATION));
    }
  else
    {
      ffd->deltify_directories = FALSE;
      ffd->deltify_properties = FALSE;
      ffd->max_deltification_walk = SVN_FS_FS_MAX_DELTIFICATION_WALK;
      ffd->max_linear_deltification = SVN_FS_FS_MAX_LINEAR_DELTIFICATION;
    }

  /* Initialize revprop packing settings in ffd. */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT)
    {
      SVN_ERR(svn_config_get_bool(ffd->config, &ffd->compress_packed_revprops,
                                  CONFIG_SECTION_PACKED_REVPROPS,
                                  CONFIG_OPTION_COMPRESS_PACKED_REVPROPS,
                                  TRUE));
      SVN_ERR(svn_config_get_int64(ffd->config, &ffd->revprop_pack_size,
                                   CONFIG_SECTION_PACKED_REVPROPS,
                                   CONFIG_OPTION_REVPROP_PACK_SIZE,
                                   ffd->compress_packed_revprops
                                       ? 0x100
                                       : 0x40));

      ffd->revprop_pack_size *= 1024;
    }
  else
    {
      ffd->revprop_pack_size = 0x10000;
      ffd->compress_packed_revprops = FALSE;
    }

  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      SVN_ERR(svn_config_get_int64(ffd->config, &ffd->block_size,
                                   CONFIG_SECTION_IO,
                                   CONFIG_OPTION_BLOCK_SIZE,
                                   64));
      SVN_ERR(svn_config_get_int64(ffd->config, &ffd->l2p_page_size,
                                   CONFIG_SECTION_IO,
                                   CONFIG_OPTION_L2P_PAGE_SIZE,
                                   0x2000));
      SVN_ERR(svn_config_get_int64(ffd->config, &ffd->p2l_page_size,
                                   CONFIG_SECTION_IO,
                                   CONFIG_OPTION_P2L_PAGE_SIZE,
                                   64));

      ffd->block_size *= 0x400;
      ffd->p2l_page_size *= 0x400;
    }
  else
    {
      /* should be irrelevant but we initialize them anyway */
      ffd->block_size = 0x1000;
      ffd->l2p_page_size = 0x2000;
      ffd->p2l_page_size = 0x1000;
    }
  
  return SVN_NO_ERROR;
}

static svn_error_t *
write_config(svn_fs_t *fs,
             apr_pool_t *pool)
{
#define NL APR_EOL_STR
  static const char * const fsfs_conf_contents =
"### This file controls the configuration of the FSFS filesystem."           NL
""                                                                           NL
"[" SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS "]"                          NL
"### These options name memcached servers used to cache internal FSFS"       NL
"### data.  See http://www.danga.com/memcached/ for more information on"     NL
"### memcached.  To use memcached with FSFS, run one or more memcached"      NL
"### servers, and specify each of them as an option like so:"                NL
"# first-server = 127.0.0.1:11211"                                           NL
"# remote-memcached = mymemcached.corp.example.com:11212"                    NL
"### The option name is ignored; the value is of the form HOST:PORT."        NL
"### memcached servers can be shared between multiple repositories;"         NL
"### however, if you do this, you *must* ensure that repositories have"      NL
"### distinct UUIDs and paths, or else cached data from one repository"      NL
"### might be used by another accidentally.  Note also that memcached has"   NL
"### no authentication for reads or writes, so you must ensure that your"    NL
"### memcached servers are only accessible by trusted users."                NL
""                                                                           NL
"[" CONFIG_SECTION_CACHES "]"                                                NL
"### When a cache-related error occurs, normally Subversion ignores it"      NL
"### and continues, logging an error if the server is appropriately"         NL
"### configured (and ignoring it with file:// access).  To make"             NL
"### Subversion never ignore cache errors, uncomment this line."             NL
"# " CONFIG_OPTION_FAIL_STOP " = true"                                       NL
""                                                                           NL
"[" CONFIG_SECTION_REP_SHARING "]"                                           NL
"### To conserve space, the filesystem can optionally avoid storing"         NL
"### duplicate representations.  This comes at a slight cost in"             NL
"### performance, as maintaining a database of shared representations can"   NL
"### increase commit times.  The space savings are dependent upon the size"  NL
"### of the repository, the number of objects it contains and the amount of" NL
"### duplication between them, usually a function of the branching and"      NL
"### merging process."                                                       NL
"###"                                                                        NL
"### The following parameter enables rep-sharing in the repository.  It can" NL
"### be switched on and off at will, but for best space-saving results"      NL
"### should be enabled consistently over the life of the repository."        NL
"### 'svnadmin verify' will check the rep-cache regardless of this setting." NL
"### rep-sharing is enabled by default."                                     NL
"# " CONFIG_OPTION_ENABLE_REP_SHARING " = true"                              NL
""                                                                           NL
"[" CONFIG_SECTION_DELTIFICATION "]"                                         NL
"### To conserve space, the filesystem stores data as differences against"   NL
"### existing representations.  This comes at a slight cost in performance," NL
"### as calculating differences can increase commit times.  Reading data"    NL
"### will also create higher CPU load and the data will be fragmented."      NL
"### Since deltification tends to save significant amounts of disk space,"   NL
"### the overall I/O load can actually be lower."                            NL
"###"                                                                        NL
"### The options in this section allow for tuning the deltification"         NL
"### strategy.  Their effects on data size and server performance may vary"  NL
"### from one repository to another.  Versions prior to 1.8 will ignore"     NL
"### this section."                                                          NL
"###"                                                                        NL
"### The following parameter enables deltification for directories. It can"  NL
"### be switched on and off at will, but for best space-saving results"      NL
"### should be enabled consistently over the life of the repository."        NL
"### Repositories containing large directories will benefit greatly."        NL
"### In rarely read repositories, the I/O overhead may be significant as"    NL
"### cache hit rates will most likely be low"                                NL
"### directory deltification is enabled by default."                         NL
"# " CONFIG_OPTION_ENABLE_DIR_DELTIFICATION " = true"                        NL
"###"                                                                        NL
"### The following parameter enables deltification for properties on files"  NL
"### and directories.  Overall, this is a minor tuning option but can save"  NL
"### some disk space if you merge frequently or frequently change node"      NL
"### properties.  You should not activate this if rep-sharing has been"      NL
"### disabled because this may result in a net increase in repository size." NL
"### property deltification is enabled by default."                          NL
"# " CONFIG_OPTION_ENABLE_PROPS_DELTIFICATION " = true"                      NL
"###"                                                                        NL
"### During commit, the server may need to walk the whole change history of" NL
"### of a given node to find a suitable deltification base.  This linear"    NL
"### process can impact commit times, svnadmin load and similar operations." NL
"### This setting limits the depth of the deltification history.  If the"    NL
"### threshold has been reached, the node will be stored as fulltext and a"  NL
"### new deltification history begins."                                      NL
"### Note, this is unrelated to svn log."                                    NL
"### Very large values rarely provide significant additional savings but"    NL
"### can impact performance greatly - in particular if directory"            NL
"### deltification has been activated.  Very small values may be useful in"  NL
"### repositories that are dominated by large, changing binaries."           NL
"### Should be a power of two minus 1.  A value of 0 will effectively"       NL
"### disable deltification."                                                 NL
"### For 1.8, the default value is 1023; earlier versions have no limit."    NL
"# " CONFIG_OPTION_MAX_DELTIFICATION_WALK " = 1023"                          NL
"###"                                                                        NL
"### The skip-delta scheme used by FSFS tends to repeatably store redundant" NL
"### delta information where a simple delta against the latest version is"   NL
"### often smaller.  By default, 1.8+ will therefore use skip deltas only"   NL
"### after the linear chain of deltas has grown beyond the threshold"        NL
"### specified by this setting."                                             NL
"### Values up to 64 can result in some reduction in repository size for"    NL
"### the cost of quickly increasing I/O and CPU costs. Similarly, smaller"   NL
"### numbers can reduce those costs at the cost of more disk space.  For"    NL
"### rarely read repositories or those containing larger binaries, this may" NL
"### present a better trade-off."                                            NL
"### Should be a power of two.  A value of 1 or smaller will cause the"      NL
"### exclusive use of skip-deltas (as in pre-1.8)."                          NL
"### For 1.8, the default value is 16; earlier versions use 1."              NL
"# " CONFIG_OPTION_MAX_LINEAR_DELTIFICATION " = 16"                          NL
""                                                                           NL
"[" CONFIG_SECTION_PACKED_REVPROPS "]"                                       NL
"### This parameter controls the size (in kBytes) of packed revprop files."  NL
"### Revprops of consecutive revisions will be concatenated into a single"   NL
"### file up to but not exceeding the threshold given here.  However, each"  NL
"### pack file may be much smaller and revprops of a single revision may be" NL
"### much larger than the limit set here.  The threshold will be applied"    NL
"### before optional compression takes place."                               NL
"### Large values will reduce disk space usage at the expense of increased"  NL
"### latency and CPU usage reading and changing individual revprops.  They"  NL
"### become an advantage when revprop caching has been enabled because a"    NL
"### lot of data can be read in one go.  Values smaller than 4 kByte will"   NL
"### not improve latency any further and quickly render revprop packing"     NL
"### ineffective."                                                           NL
"### revprop-pack-size is 64 kBytes by default for non-compressed revprop"   NL
"### pack files and 256 kBytes when compression has been enabled."           NL
"# " CONFIG_OPTION_REVPROP_PACK_SIZE " = 64"                                 NL
"###"                                                                        NL
"### To save disk space, packed revprop files may be compressed.  Standard"  NL
"### revprops tend to allow for very effective compression.  Reading and"    NL
"### even more so writing, become significantly more CPU intensive.  With"   NL
"### revprop caching enabled, the overhead can be offset by reduced I/O"     NL
"### unless you often modify revprops after packing."                        NL
"### Compressing packed revprops is enabled by default."                     NL
"# " CONFIG_OPTION_COMPRESS_PACKED_REVPROPS " = true"                        NL
""                                                                           NL
"[" CONFIG_SECTION_IO "]"                                                    NL
"### Parameters in this section control the data access granularity in"      NL
"### format 7 repositories and later.  The defaults should translate into"   NL
"### decent performance over a wide range of setups."                        NL
"###"                                                                        NL
"### When a specific piece of information needs to be read from disk,  a"    NL
"### data block is being read at once and its contents are being cached."    NL
"### If the repository is being stored on a RAID,  the block size should"    NL
"### be either 50% or 100% of RAID block size / granularity.  Also,  your"   NL
"### file system (clusters) should be properly aligned and sized.  In that"  NL
"### setup, each access will hit only one disk (minimizes I/O load) but"     NL
"### uses all the data provided by the disk in a single access."             NL
"### For SSD-based storage systems,  slightly lower values around 16 kB"     NL
"### may improve latency while still maximizing throughput."                 NL
"### Can be changed at any time but must be a power of 2."                   NL
"### block-size is 64 kBytes by default."                                    NL
"# " CONFIG_OPTION_BLOCK_SIZE " = 64"                                        NL
"###"                                                                        NL
"### The log-to-phys index maps data item numbers to offsets within the"     NL
"### rev or pack file.  A revision typically contains 2 .. 5 such items"     NL
"### per changed path.  For each revision, at least one page is being"       NL
"### allocated in the l2p index with unused parts resulting in no wasted"    NL
"### space."                                                                 NL
"### Changing this parameter only affects larger revisions with thousands"   NL
"### of changed paths.  A smaller value means that more pages need to be"    NL
"### allocated for such revisions,  increasing the size of the page table"   NL
"### meaning it takes longer to read that table (once).  Access to each"     NL
"### page is then faster because less data has to read.  So, if you have"    NL
"### several extremely large revisions (approaching 1 mio changes),  think"  NL
"### about increasing this setting.  Reducing the value will rarely result"  NL
"### in a net speedup."                                                      NL
"### This is an expert setting.  Any non-zero value is possible."            NL
"### l2p-page-size is 8192 entries by default."                              NL
"# " CONFIG_OPTION_L2P_PAGE_SIZE " = 8192"                                   NL
"###"                                                                        NL
"### The phys-to-log index maps positions within the rev or pack file to"    NL
"### to data items,  i.e. describes what piece of information is being"      NL
"### stored at that particular offset.  The index describes the rev file"    NL
"### in chunks (pages) and keeps a global list of all those pages.  Large"   NL
"### pages mean a shorter page table but a larger per-page description of"   NL
"### data items in it.  The latency sweetspot depends on the change size"    NL
"### distribution but is relatively wide."                                   NL
"### If the repository contains very large files,  i.e. individual changes"  NL
"### of tens of MB each,  increasing the page size will shorten the index"   NL
"### file at the expense of a slightly increased latency in sections with"   NL
"### smaller changes."                                                       NL
"### For practical reasons,  this should match block-size.  Differing"       NL
"### values are perfectly legal but may result in some processing overhead." NL
"### Must be a power of 2."                                                  NL
"### p2l-page-size is 64 kBytes by default."                                 NL
"# " CONFIG_OPTION_P2L_PAGE_SIZE " = 64"                                     NL
;
#undef NL
  return svn_io_file_create(svn_dirent_join(fs->path, PATH_CONFIG, pool),
                            fsfs_conf_contents, pool);
}

svn_error_t *
svn_fs_fs__open(svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_file_t *uuid_file;
  int format, max_files_per_dir;
  char buf[APR_UUID_FORMATTED_LENGTH + 2];
  apr_size_t limit;

  fs->path = apr_pstrdup(fs->pool, path);

  /* Read the FS format number. */
  SVN_ERR(read_format(&format, &max_files_per_dir,
                      path_format(fs, pool), pool));
  SVN_ERR(check_format(format));

  /* Now we've got a format number no matter what. */
  ffd->format = format;
  ffd->max_files_per_dir = max_files_per_dir;

  /* Read in and cache the repository uuid. */
  SVN_ERR(svn_io_file_open(&uuid_file, path_uuid(fs, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  limit = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(uuid_file, buf, &limit, pool));
  fs->uuid = apr_pstrdup(fs->pool, buf);

  SVN_ERR(svn_io_file_close(uuid_file, pool));

  /* Read the min unpacked revision. */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
    SVN_ERR(update_min_unpacked_rev(fs, pool));

  /* Read the configuration file. */
  SVN_ERR(read_config(ffd, fs->path, pool));

  return get_youngest(&(ffd->youngest_rev_cache), path, pool);
}

/* Wrapper around svn_io_file_create which ignores EEXIST. */
static svn_error_t *
create_file_ignore_eexist(const char *file,
                          const char *contents,
                          apr_pool_t *pool)
{
  svn_error_t *err = svn_io_file_create(file, contents, pool);
  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  return svn_error_trace(err);
}

static svn_error_t *
upgrade_body(void *baton, apr_pool_t *pool)
{
  svn_fs_t *fs = baton;
  fs_fs_data_t *ffd = fs->fsap_data;
  int format, max_files_per_dir;
  const char *format_path = path_format(fs, pool);
  svn_node_kind_t kind;

  /* Read the FS format number and max-files-per-dir setting. */
  SVN_ERR(read_format(&format, &max_files_per_dir, format_path, pool));
  SVN_ERR(check_format(format));

  /* If the config file does not exist, create one. */
  SVN_ERR(svn_io_check_path(svn_dirent_join(fs->path, PATH_CONFIG, pool),
                            &kind, pool));
  switch (kind)
    {
    case svn_node_none:
      SVN_ERR(write_config(fs, pool));
      break;
    case svn_node_file:
      break;
    default:
      return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                               _("'%s' is not a regular file."
                                 " Please move it out of "
                                 "the way and try again"),
                               svn_dirent_join(fs->path, PATH_CONFIG, pool));
    }

  /* If we're already up-to-date, there's nothing else to be done here. */
  if (format == SVN_FS_FS__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  /* If our filesystem predates the existance of the 'txn-current
     file', make that file and its corresponding lock file. */
  if (format < SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    {
      SVN_ERR(create_file_ignore_eexist(path_txn_current(fs, pool), "0\n",
                                        pool));
      SVN_ERR(create_file_ignore_eexist(path_txn_current_lock(fs, pool), "",
                                        pool));
    }

  /* If our filesystem predates the existance of the 'txn-protorevs'
     dir, make that directory.  */
  if (format < SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    {
      /* We don't use path_txn_proto_rev() here because it expects
         we've already bumped our format. */
      SVN_ERR(svn_io_make_dir_recursively(
          svn_dirent_join(fs->path, PATH_TXN_PROTOS_DIR, pool), pool));
    }

  /* If our filesystem is new enough, write the min unpacked rev file. */
  if (format < SVN_FS_FS__MIN_PACKED_FORMAT)
    SVN_ERR(svn_io_file_create(path_min_unpacked_rev(fs, pool), "0\n", pool));

  /* If the file system supports revision packing but not revprop packing,
     pack the revprops up to the point that revision data has been packed. */
  if (   format >= SVN_FS_FS__MIN_PACKED_FORMAT
      && format < SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT)
    SVN_ERR(upgrade_pack_revprops(fs, pool));

  /* Bump the format file. */

  ffd->format = SVN_FS_FS__FORMAT_NUMBER;
  ffd->max_files_per_dir = max_files_per_dir;
  return svn_fs_fs__write_format(fs, pool);
}


svn_error_t *
svn_fs_fs__upgrade(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_fs_fs__with_write_lock(fs, upgrade_body, (void *)fs, pool);
}


svn_error_t *
svn_fs_fs__youngest_rev(svn_revnum_t *youngest_p,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(get_youngest(youngest_p, fs->path, pool));
  ffd->youngest_rev_cache = *youngest_p;

  return SVN_NO_ERROR;
}

/* Return SVN_ERR_FS_NO_SUCH_REVISION if the given revision is newer
   than the current youngest revision or is simply not a valid
   revision number, else return success.

   FSFS is based around the concept that commits only take effect when
   the number in "current" is bumped.  Thus if there happens to be a rev
   or revprops file installed for a revision higher than the one recorded
   in "current" (because a commit failed between installing the rev file
   and bumping "current", or because an administrator rolled back the
   repository by resetting "current" without deleting rev files, etc), it
   ought to be completely ignored.  This function provides the check
   by which callers can make that decision. */
static svn_error_t *
ensure_revision_exists(svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (! SVN_IS_VALID_REVNUM(rev))
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("Invalid revision number '%ld'"), rev);


  /* Did the revision exist the last time we checked the current
     file? */
  if (rev <= ffd->youngest_rev_cache)
    return SVN_NO_ERROR;

  SVN_ERR(get_youngest(&(ffd->youngest_rev_cache), fs->path, pool));

  /* Check again. */
  if (rev <= ffd->youngest_rev_cache)
    return SVN_NO_ERROR;

  return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                           _("No such revision %ld"), rev);
}

svn_error_t *
svn_fs_fs__ensure_revision_exists(svn_revnum_t rev,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool)
{
  /* Different order of parameters. */
  SVN_ERR(ensure_revision_exists(fs, rev, pool));
  return SVN_NO_ERROR;
}

/* Open the correct revision file for REV.  If the filesystem FS has
   been packed, *FILE will be set to the packed file; otherwise, set *FILE
   to the revision file for REV.  Return SVN_ERR_FS_NO_SUCH_REVISION if the
   file doesn't exist.

   TODO: Consider returning an indication of whether this is a packed rev
         file, so the caller need not rely on is_packed_rev() which in turn
         relies on the cached FFD->min_unpacked_rev value not having changed
         since the rev file was opened.

   Use POOL for allocations. */
svn_error_t *
svn_fs_fs__open_pack_or_rev_file(apr_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;
  svn_boolean_t retry = FALSE;

  do
    {
      const char *path = svn_fs_fs__path_rev_absolute(fs, rev, pool);

      /* open the revision file in buffered r/o mode */
      err = svn_io_file_open(file, path,
                            APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
            {
              /* Could not open the file. This may happen if the
               * file once existed but got packed later. */
              svn_error_clear(err);

              /* if that was our 2nd attempt, leave it at that. */
              if (retry)
                return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                         _("No such revision %ld"), rev);

              /* We failed for the first time. Refresh cache & retry. */
              SVN_ERR(update_min_unpacked_rev(fs, pool));

              retry = TRUE;
            }
          else
            {
              svn_error_clear(err);
              return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                       _("No such revision %ld"), rev);
            }
        }
      else
        {
          retry = FALSE;
        }
    }
  while (retry);

  return svn_error_trace(err);
}


svn_error_t *
svn_fs_fs__revision_proplist(apr_hash_t **proplist_p,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  SVN_ERR(get_revision_proplist(proplist_p, fs, rev, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__file_length(svn_filesize_t *length,
                       node_revision_t *noderev,
                       apr_pool_t *pool)
{
  if (noderev->data_rep)
    *length = noderev->data_rep->expanded_size;
  else
    *length = 0;

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs_fs__noderev_same_rep_key(representation_t *a,
                                representation_t *b)
{
  if (a == b)
    return TRUE;

  if (a == NULL || b == NULL)
    return FALSE;

  if (a->item_index != b->item_index)
    return FALSE;

  if (a->revision != b->revision)
    return FALSE;

  return memcmp(&a->uniquifier, &b->uniquifier, sizeof(a->uniquifier)) == 0;
}

svn_error_t *
svn_fs_fs__file_checksum(svn_checksum_t **checksum,
                         node_revision_t *noderev,
                         svn_checksum_kind_t kind,
                         apr_pool_t *pool)
{
  *checksum = NULL;

  if (noderev->data_rep)
    {
      svn_checksum_t temp;
      temp.kind = kind;
      
      switch(kind)
        {
          case svn_checksum_md5:
            temp.digest = noderev->data_rep->md5_digest;
            break;

          case svn_checksum_sha1:
            if (! noderev->data_rep->has_sha1)
              return SVN_NO_ERROR;

            temp.digest = noderev->data_rep->sha1_digest;
            break;

          default:
            return SVN_NO_ERROR;
        }

      *checksum = svn_checksum_dup(&temp, pool);
    }

  return SVN_NO_ERROR;
}

representation_t *
svn_fs_fs__rep_copy(representation_t *rep,
                    apr_pool_t *pool)
{
  representation_t *rep_new;

  if (rep == NULL)
    return NULL;

  rep_new = apr_palloc(pool, sizeof(*rep_new));

  memcpy(rep_new, rep, sizeof(*rep_new));

  return rep_new;
}


/* Write out the zeroth revision for filesystem FS. */
static svn_error_t *
write_revision_zero(svn_fs_t *fs)
{
  const char *path_revision_zero = path_rev(fs, 0, fs->pool);
  apr_hash_t *proplist;
  svn_string_t date;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Write out a rev file for revision 0. */
  if (ffd->format < SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    SVN_ERR(svn_io_file_create(path_revision_zero,
                               "PLAIN\nEND\nENDREP\n"
                               "id: 0.0.r0/17\n"
                               "type: dir\n"
                               "count: 0\n"
                               "text: 0 0 4 4 "
                               "2d2977d1c96f487abe4a1e202dd03b4e\n"
                               "cpath: /\n"
                               "\n\n17 107\n", fs->pool));
  else
    SVN_ERR(svn_io_file_create(path_revision_zero,
                               "PLAIN\nEND\nENDREP\n"
                               "id: 0.0.r0/2\n"
                               "type: dir\n"
                               "count: 0\n"
                               "text: 0 3 4 4 "
                               "2d2977d1c96f487abe4a1e202dd03b4e\n"
                               "cpath: /\n"
                               "\n\n", fs->pool));

  SVN_ERR(svn_io_set_file_read_only(path_revision_zero, FALSE, fs->pool));

  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      const char *path = path_l2p_index(fs, 0, fs->pool);
      SVN_ERR(svn_io_file_create_binary
                 (path,
                  "\0\1\x80\x40\1\1" /* rev 0, single page */
                  "\5\4"             /* page size: bytes, count */
                  "\0"               /* 0 container offsets in list */
                  "\0\x6b\x12\1",    /* phys offsets + 1 */
                  13,
                  fs->pool));
      SVN_ERR(svn_io_set_file_read_only(path, FALSE, fs->pool));

      path = path_p2l_index(fs, 0, fs->pool);
      SVN_ERR(svn_io_file_create_binary
                 (path,
                  "\0"                /* start rev */
                  "\x80\x80\4\1\x11"  /* 64k pages, 1 page using 17 bytes */
                  "\0"                /* offset entry 0 page 1 */
                  "\x11\x11\0\6"      /* len, type + 16 * count, (rev, item)* */
                  "\x59\x15\0\4"
                  "\1\x16\0\2"
                  "\x95\xff\3\0",     /* last entry fills up 64k page */
                  23,
                  fs->pool));
      SVN_ERR(svn_io_set_file_read_only(path, FALSE, fs->pool));
    }

  /* Set a date on revision 0. */
  date.data = svn_time_to_cstring(apr_time_now(), fs->pool);
  date.len = strlen(date.data);
  proplist = apr_hash_make(fs->pool);
  svn_hash_sets(proplist, SVN_PROP_REVISION_DATE, &date);
  return set_revision_proplist(fs, 0, proplist, fs->pool);
}

svn_error_t *
svn_fs_fs__create(svn_fs_t *fs,
                  const char *path,
                  apr_pool_t *pool)
{
  int format = SVN_FS_FS__FORMAT_NUMBER;
  fs_fs_data_t *ffd = fs->fsap_data;

  fs->path = apr_pstrdup(pool, path);
  /* See if compatibility with older versions was explicitly requested. */
  if (fs->config)
    {
      if (svn_hash_gets(fs->config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE))
        format = 1;
      else if (svn_hash_gets(fs->config, SVN_FS_CONFIG_PRE_1_5_COMPATIBLE))
        format = 2;
      else if (svn_hash_gets(fs->config, SVN_FS_CONFIG_PRE_1_6_COMPATIBLE))
        format = 3;
      else if (svn_hash_gets(fs->config, SVN_FS_CONFIG_PRE_1_8_COMPATIBLE))
        format = 4;
      else if (svn_hash_gets(fs->config, SVN_FS_CONFIG_PRE_1_9_COMPATIBLE))
        format = 6;
    }
  ffd->format = format;

  /* Override the default linear layout if this is a new-enough format. */
  if (format >= SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT)
    ffd->max_files_per_dir = SVN_FS_FS_DEFAULT_MAX_FILES_PER_DIR;

  /* Create the revision data directories. */
  if (ffd->max_files_per_dir)
    SVN_ERR(svn_io_make_dir_recursively(path_rev_shard(fs, 0, pool), pool));
  else
    SVN_ERR(svn_io_make_dir_recursively(svn_dirent_join(path, PATH_REVS_DIR,
                                                        pool),
                                        pool));

  /* Create the revprops directory. */
  if (ffd->max_files_per_dir)
    SVN_ERR(svn_io_make_dir_recursively(path_revprops_shard(fs, 0, pool),
                                        pool));
  else
    SVN_ERR(svn_io_make_dir_recursively(svn_dirent_join(path,
                                                        PATH_REVPROPS_DIR,
                                                        pool),
                                        pool));

  /* Create the transaction directory. */
  SVN_ERR(svn_io_make_dir_recursively(svn_dirent_join(path, PATH_TXNS_DIR,
                                                      pool),
                                      pool));

  /* Create the protorevs directory. */
  if (format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    SVN_ERR(svn_io_make_dir_recursively(svn_dirent_join(path, PATH_TXN_PROTOS_DIR,
                                                      pool),
                                        pool));

  /* Create the 'current' file. */
  SVN_ERR(svn_io_file_create(svn_fs_fs__path_current(fs, pool),
                              (format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT
                               ? "0\n" : "0 1 1\n"),
                             pool));
  SVN_ERR(svn_io_file_create_empty(path_lock(fs, pool), pool));
  SVN_ERR(svn_fs_fs__set_uuid(fs, NULL, pool));

  SVN_ERR(write_revision_zero(fs));

  SVN_ERR(write_config(fs, pool));

  SVN_ERR(read_config(ffd, fs->path, pool));

  /* Create the min unpacked rev file. */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
    SVN_ERR(svn_io_file_create(path_min_unpacked_rev(fs, pool), "0\n", pool));

  /* Create the txn-current file if the repository supports
     the transaction sequence file. */
  if (format >= SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    {
      SVN_ERR(svn_io_file_create(path_txn_current(fs, pool), "0\n", pool));
      SVN_ERR(svn_io_file_create_empty(path_txn_current_lock(fs, pool), pool));
    }

  /* This filesystem is ready.  Stamp it with a format number. */
  SVN_ERR(svn_fs_fs__write_format(fs, pool));

  ffd->youngest_rev_cache = 0;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_uuid(svn_fs_t *fs,
                    const char *uuid,
                    apr_pool_t *pool)
{
  char *my_uuid;
  apr_size_t my_uuid_len;
  const char *tmp_path;
  const char *uuid_path = path_uuid(fs, pool);

  if (! uuid)
    uuid = svn_uuid_generate(pool);

  /* Make sure we have a copy in FS->POOL, and append a newline. */
  my_uuid = apr_pstrcat(fs->pool, uuid, "\n", (char *)NULL);
  my_uuid_len = strlen(my_uuid);

  SVN_ERR(svn_io_write_unique(&tmp_path,
                              svn_dirent_dirname(uuid_path, pool),
                              my_uuid, my_uuid_len,
                              svn_io_file_del_none, pool));

  /* We use the permissions of the 'current' file, because the 'uuid'
     file does not exist during repository creation. */
  SVN_ERR(move_into_place(tmp_path, uuid_path,
                          svn_fs_fs__path_current(fs, pool), pool));

  /* Remove the newline we added, and stash the UUID. */
  my_uuid[my_uuid_len - 1] = '\0';
  fs->uuid = my_uuid;

  return SVN_NO_ERROR;
}

/** Node origin lazy cache. */

/* If directory PATH does not exist, create it and give it the same
   permissions as FS_path.*/
svn_error_t *
svn_fs_fs__ensure_dir_exists(const char *path,
                             const char *fs_path,
                             apr_pool_t *pool)
{
  svn_error_t *err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* We successfully created a new directory.  Dup the permissions
     from FS->path. */
  return svn_io_copy_perms(fs_path, path, pool);
}

/* Set *NODE_ORIGINS to a hash mapping 'const char *' node IDs to
   'svn_string_t *' node revision IDs.  Use POOL for allocations. */
static svn_error_t *
get_node_origins_from_file(svn_fs_t *fs,
                           apr_hash_t **node_origins,
                           const char *node_origins_file,
                           apr_pool_t *pool)
{
  apr_file_t *fd;
  svn_error_t *err;
  svn_stream_t *stream;

  *node_origins = NULL;
  err = svn_io_file_open(&fd, node_origins_file,
                         APR_READ, APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  stream = svn_stream_from_aprfile2(fd, FALSE, pool);
  *node_origins = apr_hash_make(pool);
  SVN_ERR(svn_hash_read2(*node_origins, stream, SVN_HASH_TERMINATOR, pool));
  return svn_stream_close(stream);
}

svn_error_t *
svn_fs_fs__get_node_origin(const svn_fs_id_t **origin_id,
                           svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *node_id,
                           apr_pool_t *pool)
{
  apr_hash_t *node_origins;

  *origin_id = NULL;
  SVN_ERR(get_node_origins_from_file(fs, &node_origins,
                                     path_node_origin(fs, node_id, pool),
                                     pool));
  if (node_origins)
    {
      svn_string_t *origin_id_str =
        svn_hash_gets(node_origins, node_id);
      if (origin_id_str)
        *origin_id = svn_fs_fs__id_parse(origin_id_str->data,
                                         origin_id_str->len, pool);
    }
  return SVN_NO_ERROR;
}


/* Helper for svn_fs_fs__set_node_origin.  Takes a NODE_ID/NODE_REV_ID
   pair and adds it to the NODE_ORIGINS_PATH file.  */
static svn_error_t *
set_node_origins_for_file(svn_fs_t *fs,
                          const char *node_origins_path,
                          const svn_fs_fs__id_part_t *node_id,
                          svn_string_t *node_rev_id,
                          apr_pool_t *pool)
{
  const char *path_tmp;
  svn_stream_t *stream;
  apr_hash_t *origins_hash;
  svn_string_t *old_node_rev_id;

  /* the hash serialization functions require strings as keys */
  char node_id_ptr[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__ui64tobase36(node_id_ptr, node_id->number);

  SVN_ERR(svn_fs_fs__ensure_dir_exists(svn_dirent_join(fs->path,
                                                       PATH_NODE_ORIGINS_DIR,
                                                       pool),
                                       fs->path, pool));

  /* Read the previously existing origins (if any), and merge our
     update with it. */
  SVN_ERR(get_node_origins_from_file(fs, &origins_hash,
                                     node_origins_path, pool));
  if (! origins_hash)
    origins_hash = apr_hash_make(pool);

  old_node_rev_id = apr_hash_get(origins_hash, node_id_ptr, len);

  if (old_node_rev_id && !svn_string_compare(node_rev_id, old_node_rev_id))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Node origin for '%s' exists with a different "
                               "value (%s) than what we were about to store "
                               "(%s)"),
                             node_id_ptr, old_node_rev_id->data,
                             node_rev_id->data);

  apr_hash_set(origins_hash, node_id_ptr, len, node_rev_id);

  /* Sure, there's a race condition here.  Two processes could be
     trying to add different cache elements to the same file at the
     same time, and the entries added by the first one to write will
     be lost.  But this is just a cache of reconstructible data, so
     we'll accept this problem in return for not having to deal with
     locking overhead. */

  /* Create a temporary file, write out our hash, and close the file. */
  SVN_ERR(svn_stream_open_unique(&stream, &path_tmp,
                                 svn_dirent_dirname(node_origins_path, pool),
                                 svn_io_file_del_none, pool, pool));
  SVN_ERR(svn_hash_write2(origins_hash, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  /* Rename the temp file as the real destination */
  return svn_io_file_rename(path_tmp, node_origins_path, pool);
}


svn_error_t *
svn_fs_fs__set_node_origin(svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *node_id,
                           const svn_fs_id_t *node_rev_id,
                           apr_pool_t *pool)
{
  svn_error_t *err;
  const char *filename = path_node_origin(fs, node_id, pool);

  err = set_node_origins_for_file(fs, filename,
                                  node_id,
                                  svn_fs_fs__id_unparse(node_rev_id, pool),
                                  pool);
  if (err && APR_STATUS_IS_EACCES(err->apr_err))
    {
      /* It's just a cache; stop trying if I can't write. */
      svn_error_clear(err);
      err = NULL;
    }
  return svn_error_trace(err);
}


/*** Revisions ***/

svn_error_t *
svn_fs_fs__revision_prop(svn_string_t **value_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         const char *propname,
                         apr_pool_t *pool)
{
  apr_hash_t *table;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_fs__revision_proplist(&table, fs, rev, pool));

  *value_p = svn_hash_gets(table, propname);

  return SVN_NO_ERROR;
}


/* Baton used for change_rev_prop_body below. */
struct change_rev_prop_baton {
  svn_fs_t *fs;
  svn_revnum_t rev;
  const char *name;
  const svn_string_t *const *old_value_p;
  const svn_string_t *value;
};

/* The work-horse for svn_fs_fs__change_rev_prop, called with the FS
   write lock.  This implements the svn_fs_fs__with_write_lock()
   'body' callback type.  BATON is a 'struct change_rev_prop_baton *'. */
static svn_error_t *
change_rev_prop_body(void *baton, apr_pool_t *pool)
{
  struct change_rev_prop_baton *cb = baton;
  apr_hash_t *table;

  SVN_ERR(svn_fs_fs__revision_proplist(&table, cb->fs, cb->rev, pool));

  if (cb->old_value_p)
    {
      const svn_string_t *wanted_value = *cb->old_value_p;
      const svn_string_t *present_value = svn_hash_gets(table, cb->name);
      if ((!wanted_value != !present_value)
          || (wanted_value && present_value
              && !svn_string_compare(wanted_value, present_value)))
        {
          /* What we expected isn't what we found. */
          return svn_error_createf(SVN_ERR_FS_PROP_BASEVALUE_MISMATCH, NULL,
                                   _("revprop '%s' has unexpected value in "
                                     "filesystem"),
                                   cb->name);
        }
      /* Fall through. */
    }
  svn_hash_sets(table, cb->name, cb->value);

  return set_revision_proplist(cb->fs, cb->rev, table, pool);
}

svn_error_t *
svn_fs_fs__change_rev_prop(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *name,
                           const svn_string_t *const *old_value_p,
                           const svn_string_t *value,
                           apr_pool_t *pool)
{
  struct change_rev_prop_baton cb;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  cb.fs = fs;
  cb.rev = rev;
  cb.name = name;
  cb.old_value_p = old_value_p;
  cb.value = value;

  return svn_fs_fs__with_write_lock(fs, change_rev_prop_body, &cb, pool);
}



/** Verifying. **/

/* Baton type expected by verify_walker().  The purpose is to reuse open
 * rev / pack file handles between calls.  Its contents need to be cleaned
 * periodically to limit resource usage.
 */
typedef struct verify_walker_baton_t
{
  /* number of calls to verify_walker() since the last clean */
  int iteration_count;

  /* number of files opened since the last clean */
  int file_count;

  /* progress notification callback to invoke periodically (may be NULL) */
  svn_fs_progress_notify_func_t notify_func;

  /* baton to use with NOTIFY_FUNC */
  void *notify_baton;

  /* remember the last revision for which we called notify_func */
  svn_revnum_t last_notified_revision;

  /* cached hint for successive calls to svn_fs_fs__check_rep() */
  void *hint;

  /* pool to use for the file handles etc. */
  apr_pool_t *pool;
} verify_walker_baton_t;

/* Used by svn_fs_fs__verify().
   Implements svn_fs_fs__walk_rep_reference().walker.  */
static svn_error_t *
verify_walker(representation_t *rep,
              void *baton,
              svn_fs_t *fs,
              apr_pool_t *scratch_pool)
{
  verify_walker_baton_t *walker_baton = baton;
  void *previous_hint;

  /* notify and free resources periodically */
  if (   walker_baton->iteration_count > 1000
      || walker_baton->file_count > 16)
    {
      if (   walker_baton->notify_func
          && rep->revision != walker_baton->last_notified_revision)
        {
          walker_baton->notify_func(rep->revision,
                                    walker_baton->notify_baton,
                                    scratch_pool);
          walker_baton->last_notified_revision = rep->revision;
        }

      svn_pool_clear(walker_baton->pool);

      walker_baton->iteration_count = 0;
      walker_baton->file_count = 0;
      walker_baton->hint = NULL;
    }

  /* access the repo data */
  previous_hint = walker_baton->hint;
  SVN_ERR(svn_fs_fs__check_rep(rep, fs, &walker_baton->hint,
                               walker_baton->pool));

  /* update resource usage counters */
  walker_baton->iteration_count++;
  if (previous_hint != walker_baton->hint)
    walker_baton->file_count++;

  return SVN_NO_ERROR;
}

/* Verify the rep cache DB's consistency with our rev / pack data.
 * The function signature is similar to svn_fs_fs__verify.
 * The values of START and END have already been auto-selected and
 * verified.
 */
static svn_error_t *
verify_rep_cache(svn_fs_t *fs,
                 svn_revnum_t start,
                 svn_revnum_t end,
                 svn_fs_progress_notify_func_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  svn_boolean_t exists;

  /* rep-cache verification. */
  SVN_ERR(svn_fs_fs__exists_rep_cache(&exists, fs, pool));
  if (exists)
    {
      /* provide a baton to allow the reuse of open file handles between
         iterations (saves 2/3 of OS level file operations). */
      verify_walker_baton_t *baton = apr_pcalloc(pool, sizeof(*baton));
      baton->pool = svn_pool_create(pool);
      baton->last_notified_revision = SVN_INVALID_REVNUM;
      baton->notify_func = notify_func;
      baton->notify_baton = notify_baton;

      /* tell the user that we are now ready to do *something* */
      if (notify_func)
        notify_func(SVN_INVALID_REVNUM, notify_baton, baton->pool);

      /* Do not attempt to walk the rep-cache database if its file does
         not exist,  since doing so would create it --- which may confuse
         the administrator.   Don't take any lock. */
      SVN_ERR(svn_fs_fs__walk_rep_reference(fs, start, end,
                                            verify_walker, baton,
                                            cancel_func, cancel_baton,
                                            pool));

      /* walker resource cleanup */
      svn_pool_destroy(baton->pool);
    }

  return SVN_NO_ERROR;
}

/* Verify that for all log-to-phys index entries for revisions START to
 * START + COUNT-1 in FS there is a consistent entry in the phys-to-log
 * index.  If given, invoke CANCEL_FUNC with CANCEL_BATON at regular
 * intervals. Use POOL for allocations.
 */
static svn_error_t *
compare_l2p_to_p2l_index(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t count,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  svn_revnum_t i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_array_header_t *max_ids;

  /* determine the range of items to check for each revision */
  SVN_ERR(svn_fs_fs__l2p_get_max_ids(&max_ids, fs, start, count, pool));

  /* check all items in all revisions if the given range */
  for (i = 0; i < max_ids->nelts; ++i)
    {
      apr_uint64_t k;
      apr_uint64_t max_id = APR_ARRAY_IDX(max_ids, i, apr_uint64_t);
      svn_revnum_t revision = start + i;

      for (k = 0; k < max_id; ++k)
        {
          apr_off_t offset;
          apr_uint32_t sub_item;
          svn_fs_fs__id_part_t *p2l_item;

          /* get L2P entry.  Ignore unused entries. */
          SVN_ERR(svn_fs_fs__item_offset(&offset, &sub_item, fs,
                                         revision, NULL, k, iterpool));
          if (offset == -1)
            continue;

          /* find the corresponding P2L entry */
          SVN_ERR(svn_fs_fs__p2l_item_lookup(&p2l_item, fs, start,
                                             offset, sub_item, iterpool));

          if (p2l_item == NULL)
            return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_INCONSISTENT,
                                      NULL,
                                      _("p2l index entry not found for "
                                        "PHYS o%" APR_OFF_T_FMT ":s%ld "
                                        "returned by l2p index for LOG "
                                        "r%ld:i%" APR_UINT64_T_FMT),
                                      offset, (long)sub_item, k, revision);

          if (p2l_item->number != k || p2l_item->revision != revision)
            return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_INCONSISTENT,
                                      NULL,
                                      _("p2l index info LOG r%ld:i%"
                                        APR_UINT64_T_FMT " does not match "
                                        "l2p index for LOG r%ld:i%"
                                        APR_UINT64_T_FMT),
                                      p2l_item->revision,
                                      (long)p2l_item->number, revision, k);

          svn_pool_clear(iterpool);
        }

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Verify that for all phys-to-log index entries for revisions START to
 * START + COUNT-1 in FS there is a consistent entry in the log-to-phys
 * index.  If given, invoke CANCEL_FUNC with CANCEL_BATON at regular
 * intervals. Use POOL for allocations.
 *
 * Please note that we can only check on pack / rev file granularity and
 * must only be called for a single rev / pack file.
 */
static svn_error_t *
compare_p2l_to_l2p_index(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t count,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_off_t max_offset;
  apr_off_t offset = 0;

  /* get the size of the rev / pack file as covered by the P2L index */
  SVN_ERR(svn_fs_fs__p2l_get_max_offset(&max_offset, fs, start, pool));

  /* for all offsets in the file, get the P2L index entries and check
     them against the L2P index */
  for (offset = 0; offset < max_offset; )
    {
      apr_array_header_t *entries;
      svn_fs_fs__p2l_entry_t *last_entry;
      int i;

      /* get all entries for the current block */
      SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, fs, start, offset,
                                          iterpool));
      if (entries->nelts == 0)
        return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_CORRUPTION,
                                 NULL,
                                 _("p2l does not cover offset %" APR_OFF_T_FMT
                                   " for revision %ld"),
                                 offset, start);

      /* process all entries (and later continue with the next block) */
      last_entry
        = &APR_ARRAY_IDX(entries, entries->nelts-1, svn_fs_fs__p2l_entry_t);
      offset = last_entry->offset + last_entry->size;
      
      for (i = 0; i < entries->nelts; ++i)
        {
          apr_uint32_t k;
          svn_fs_fs__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t);

          /* check all sub-items for consist entries in the L2P index */
          for (k = 0; k < entry->item_count; ++k)
            {
              apr_off_t l2p_offset;
              apr_uint32_t sub_item;
              svn_fs_fs__id_part_t *p2l_item = &entry->items[k];

              SVN_ERR(svn_fs_fs__item_offset(&l2p_offset, &sub_item, fs,
                                             p2l_item->revision, NULL,
                                             p2l_item->number, iterpool));

              if (sub_item != k || l2p_offset != entry->offset)
                return svn_error_createf(SVN_ERR_FS_ITEM_INDEX_INCONSISTENT,
                                         NULL,
                                         _("l2p index entry PHYS o%"
                                           APR_OFF_T_FMT ":s%ld does not "
                                           "match p2l index value LOG r%ld:i%"
                                           APR_UINT64_T_FMT " for PHYS o%"
                                           APR_OFF_T_FMT ":s%ld"),
                                         l2p_offset, (long)sub_item,
                                         p2l_item->revision, p2l_item->number,
                                         entry->offset, (long)k);
            }
        }

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Verify that the log-to-phys indexes and phys-to-log indexes are
 * consistent with each other.  The function signature is similar to
 * svn_fs_fs__verify.
 *
 * The values of START and END have already been auto-selected and
 * verified.  You may call this for format7 or higher repos.
 */
static svn_error_t *
verify_index_consistency(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t end,
                         svn_fs_progress_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_revnum_t revision, pack_start, pack_end;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (revision = start; revision <= end; revision = pack_end)
    {
      pack_start = packed_base_rev(fs, revision);
      pack_end = pack_start + pack_size(fs, revision);

      if (notify_func && (pack_start % ffd->max_files_per_dir == 0))
        notify_func(pack_start, notify_baton, iterpool);

      /* two-way index check */
      SVN_ERR(compare_l2p_to_p2l_index(fs, pack_start, pack_end - pack_start,
                                       cancel_func, cancel_baton, iterpool));
      SVN_ERR(compare_p2l_to_l2p_index(fs, pack_start, pack_end - pack_start,
                                       cancel_func, cancel_baton, iterpool));

      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__verify(svn_fs_t *fs,
                  svn_revnum_t start,
                  svn_revnum_t end,
                  svn_fs_progress_notify_func_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_revnum_t youngest = ffd->youngest_rev_cache; /* cache is current */

  /* Input validation. */
  if (! SVN_IS_VALID_REVNUM(start))
    start = 0;
  if (! SVN_IS_VALID_REVNUM(end))
    end = youngest;
  SVN_ERR(ensure_revision_exists(fs, start, pool));
  SVN_ERR(ensure_revision_exists(fs, end, pool));

  /* log/phys index consistency.  We need to check them first to make
     sure we can access the rev / pack files in format7. */
  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    SVN_ERR(verify_index_consistency(fs, start, end,
                                     notify_func, notify_baton,
                                     cancel_func, cancel_baton, pool));

  /* rep cache consistency */
  if (ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    SVN_ERR(verify_rep_cache(fs, start, end, notify_func, notify_baton,
                             cancel_func, cancel_baton, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__info_format(int *fs_format,
                       svn_version_t **supports_version,
                       svn_fs_t *fs,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  *fs_format = ffd->format;
  *supports_version = apr_palloc(result_pool, sizeof(svn_version_t));

  (*supports_version)->major = SVN_VER_MAJOR;
  (*supports_version)->minor = 1;
  (*supports_version)->patch = 0;
  (*supports_version)->tag = "";

  switch (ffd->format)
    {
    case 1:
      break;
    case 2:
      (*supports_version)->minor = 4;
      break;
    case 3:
      (*supports_version)->minor = 5;
      break;
    case 4:
      (*supports_version)->minor = 6;
      break;
    case 6:
      (*supports_version)->minor = 8;
      break;
    case 7:
      (*supports_version)->minor = 9;
      break;
#ifdef SVN_DEBUG
# if SVN_FS_FS__FORMAT_NUMBER != 7
#  error "Need to add a 'case' statement here"
# endif
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__info_config_files(apr_array_header_t **files,
                             svn_fs_t *fs,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  *files = apr_array_make(result_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(*files, const char *) = svn_dirent_join(fs->path, PATH_CONFIG,
                                                         result_pool);
  return SVN_NO_ERROR;
}
