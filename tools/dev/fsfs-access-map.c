/* fsfs-access-map.c -- convert strace output into FSFS access bitmap
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

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_io.h"

#include "private/svn_string_private.h"

/* The information we gather for each file.  There will be one instance
 * per file name - even if the file got deleted and re-created.
 */
typedef struct file_stats_t
{
  /* file name as found in the open() call */
  const char *name;

  /* file size as determined during the tool run.  Will be 0 for
   * files that no longer exist.  However, there may still be entries
   * in the read_map. */
  apr_int64_t size;

  /* for rev files (packed or non-packed), this will be the first revision
   * that file. -1 for non-rev files. */
  apr_int64_t rev_num;

  /* number of times this file got opened */
  apr_int64_t open_count;

  /* number of lseek counts */
  apr_int64_t seek_count;

  /* number of lseek calls to clusters not previously read */
  apr_int64_t uncached_seek_count;

  /* number of lseek counts not followed by a read */
  apr_int64_t unnecessary_seeks;

  /* number of read() calls */
  apr_int64_t read_count;

  /* number of read() calls that returned 0 bytes */
  apr_int64_t empty_reads;

  /* total number of bytes returned by those reads */
  apr_int64_t read_size;

  /* number of clusters read */
  apr_int64_t clusters_read;

  /* number of different clusters read
   * (i.e. number of non-zero entries in read_map). */
  apr_int64_t unique_clusters_read;

  /* cluster -> read count mapping (1 word per cluster, saturated at 64k) */
  apr_array_header_t *read_map;

} file_stats_t;

/* Represents an open file handle.  It refers to a file and concatenates
 * consecutive reads such that we don't artificially hit the same cluster
 * multiple times.  Instances of this type will be reused to limit the
 * allocation load on the lookup map.
 */
typedef struct handle_info_t
{
  /* the open file */
  file_stats_t *file;

  /* file offset at which the current series of reads started (default: 0) */
  apr_int64_t last_read_start;

  /* bytes read so far in the current series of reads started (default: 0) */
  apr_int64_t last_read_size;

  /* number of read() calls in this series */
  apr_int64_t read_count;
} handle_info_t;

/* useful typedef */
typedef unsigned char byte;
typedef unsigned short word;

/* an RGB color */
typedef byte color_t[3];

/* global const char * file name -> *file_info_t map */
static apr_hash_t *files = NULL;

/* global int handle -> *handle_info_t map.  Entries don't get removed
 * by close().  Instead, we simply recycle (and re-initilize) existing
 * instances. */
static apr_hash_t *handles = NULL;

/* assume cluster size.  64 and 128kB are typical values for RAIDs. */
static apr_int64_t cluster_size = 64 * 1024;

/* Call this after a sequence of reads has been ended by either close()
 * or lseek() for this HANDLE_INFO.  This will update the read_map and
 * unique_clusters_read members of the underlying file_info_t structure.
 */
static void
store_read_info(handle_info_t *handle_info)
{
  if (handle_info->last_read_size)
    {
      apr_size_t i;
      apr_size_t first_cluster
         = (apr_size_t)(handle_info->last_read_start / cluster_size);
      apr_size_t last_cluster
         = (apr_size_t)((  handle_info->last_read_start
                         + handle_info->last_read_size
                         - 1) / cluster_size);

      /* auto-expand access map in case the file later shrunk or got deleted */
      while (handle_info->file->read_map->nelts <= last_cluster)
        APR_ARRAY_PUSH(handle_info->file->read_map, word) = 0;

      /* accumulate the accesses per cluster. Saturate and count first
       * (i.e. disjoint) accesses clusters */
      handle_info->file->clusters_read += last_cluster - first_cluster + 1;
      for (i = first_cluster; i <= last_cluster; ++i)
        {
          word *count = &APR_ARRAY_IDX(handle_info->file->read_map, i, word);
          if (*count == 0)
            handle_info->file->unique_clusters_read++;
          if (*count < 0xffff)
            ++*count;
        }
    }
  else if (handle_info->read_count == 0)
    {
      /* two consecutive seeks */
      handle_info->file->unnecessary_seeks++;
    }
}

/* Handle a open() call.  Ensures that a file_info_t for the given NAME
 * exists.  Auto-create and initialize a handle_info_t for it linked to
 * HANDLE.
 */
static void
open_file(const char *name, int handle)
{
  file_stats_t *file = apr_hash_get(files, name, APR_HASH_KEY_STRING);
  handle_info_t *handle_info = apr_hash_get(handles, &handle, sizeof(handle));

  /* auto-create file info */
  if (!file)
    {
      apr_pool_t *pool = apr_hash_pool_get(files);
      apr_pool_t *subpool = svn_pool_create(pool);

      apr_file_t *apr_file = NULL;
      apr_finfo_t finfo = { 0 };
      int cluster_count = 0;

      /* determine file size (if file still exists) */
      apr_file_open(&apr_file, name,
                    APR_READ | APR_BUFFERED, APR_OS_DEFAULT, subpool);
      if (apr_file)
        apr_file_info_get(&finfo, APR_FINFO_SIZE, apr_file);
      svn_pool_destroy(subpool);

      file = apr_pcalloc(pool, sizeof(*file));
      file->name = apr_pstrdup(pool, name);
      file->size = finfo.size;

      /* pre-allocate cluster map accordingly
       * (will be auto-expanded later if necessary) */
      cluster_count = (int)(1 + (file->size - 1) / cluster_size);
      file->read_map = apr_array_make(pool, file->size
                                          ? cluster_count
                                          : 1, sizeof(word));

      while (file->read_map->nelts < cluster_count)
        APR_ARRAY_PUSH(file->read_map, byte) = 0;

      /* determine first revision of rev / packed rev files */
      if (strstr(name, "/db/revs/") != NULL && strstr(name, "manifest") == NULL)
        if (strstr(name, ".pack/pack") != NULL)
          file->rev_num = SVN_STR_TO_REV(strstr(name, "/db/revs/") + 9);
        else
          file->rev_num = SVN_STR_TO_REV(strrchr(name, '/') + 1);
      else
        file->rev_num = -1;

      /* filter out log/phys index files */
      if (file->rev_num >= 0)
        {
          const char *suffix = name + strlen(name) - 4;
          if (strcmp(suffix, ".l2p") == 0 || strcmp(suffix, ".p2l") == 0)
            file->rev_num = -1;
        }

      apr_hash_set(files, file->name, APR_HASH_KEY_STRING, file);
    }

  file->open_count++;

  /* auto-create handle instance */
  if (!handle_info)
    {
      apr_pool_t *pool = apr_hash_pool_get(handles);
      int *key = apr_palloc(pool, sizeof(*key));
      *key = handle;

      handle_info = apr_pcalloc(pool, sizeof(*handle_info));
      apr_hash_set(handles, key, sizeof(*key), handle_info);
    }

  /* link handle to file */
  handle_info->file = file;
  handle_info->last_read_start = 0;
  handle_info->last_read_size = 0;
}

/* COUNT bytes have been read from file with the given HANDLE.
 */
static void
read_file(int handle, apr_int64_t count)
{
  handle_info_t *handle_info = apr_hash_get(handles, &handle, sizeof(handle));
  if (handle_info)
    {
      /* known file handle -> expand current read sequence */

      handle_info->read_count++;
      handle_info->last_read_size += count;
      handle_info->file->read_count++;
      handle_info->file->read_size += count;

      if (count == 0)
        handle_info->file->empty_reads++;
    }
}

/* Seek to offset LOCATION in file given by HANDLE.
 */
static void
seek_file(int handle, apr_int64_t location)
{
  handle_info_t *handle_info = apr_hash_get(handles, &handle, sizeof(handle));
  if (handle_info)
    {
      /* known file handle -> end current read sequence and start a new one */

      apr_size_t cluster = (apr_size_t)(location / cluster_size);

      store_read_info(handle_info);

      handle_info->last_read_size = 0;
      handle_info->last_read_start = location;
      handle_info->read_count = 0;
      handle_info->file->seek_count++;

      /* if we seek to a location that had not been read from before,
       * there will probably be a real I/O seek on the following read.
       */
      if (   handle_info->file->read_map->nelts <= cluster
          || APR_ARRAY_IDX(handle_info->file->read_map, cluster, word) == 0)
        handle_info->file->uncached_seek_count++;
    }
}

/* The given file HANDLE has been closed.
 */
static void
close_file(int handle)
{
  /* for known file handles, end current read sequence */

  handle_info_t *handle_info = apr_hash_get(handles, &handle, sizeof(handle));
  if (handle_info)
    store_read_info(handle_info);
}

/* Parse / process non-empty the LINE from an strace output.
 */
static void
parse_line(svn_stringbuf_t *line)
{
  /* determine function name, first parameter and return value */
  char *func_end = strchr(line->data, '(');
  char *return_value = strrchr(line->data, ' ');
  char *first_param_end;
  apr_int64_t func_return = 0;
  char *func_start = strchr(line->data, ' ');

  if (func_end == NULL || return_value == NULL)
    return;

  if (func_start == NULL || func_start > func_end)
    func_start = line->data;
  else
    while(*func_start == ' ')
      func_start++;

  first_param_end = strchr(func_end, ',');
  if (first_param_end == NULL)
    first_param_end = strchr(func_end, ')');

  if (first_param_end == NULL)
    return;

  *func_end++ = 0;
  *first_param_end = 0;
  ++return_value;

  /* (try to) convert the return value into an integer.
   * If that fails, continue anyway as defaulting to 0 will be safe for us. */
  svn_error_clear(svn_cstring_atoi64(&func_return, return_value));

  /* process those operations that we care about */
  if (strcmp(func_start, "open") == 0)
    {
      /* remove double quotes from file name parameter */
      *func_end++ = 0;
      *--first_param_end = 0;

      open_file(func_end, (int)func_return);
    }
  else if (strcmp(func_start, "read") == 0)
    read_file(atoi(func_end), func_return);
  else if (strcmp(func_start, "lseek") == 0)
    seek_file(atoi(func_end), func_return);
  else if (strcmp(func_start, "close") == 0)
    close_file(atoi(func_end));
}

/* Process the strace output stored in FILE.
 */
static void
parse_file(apr_file_t *file)
{
  apr_pool_t *pool = svn_pool_create(NULL);
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* limit lines to 4k (usually, we need less than 200 bytes) */
  svn_stringbuf_t *line = svn_stringbuf_create_ensure(4096, pool);

  do
    {
      svn_error_t *err = NULL;

      line->len = line->blocksize-1;
      err = svn_io_read_length_line(file, line->data, &line->len, iterpool);
      svn_error_clear(err);
      if (err)
        break;

      parse_line(line);
      svn_pool_clear(iterpool);
    }
  while (line->len > 0);
}

/* qsort() callback.  Sort files by revision number.
 */
static int
compare_files(file_stats_t **lhs, file_stats_t **rhs)
{
  return (*lhs)->rev_num < (*rhs)->rev_num;
}

/* Return all rev (and packed rev) files sorted by revision number.
 * Allocate the result in POOL.
 */
static apr_array_header_t *
get_rev_files(apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *result = apr_array_make(pool,
                                              apr_hash_count(files),
                                              sizeof(file_stats_t *));

  /* select all files that have a rev number */
  for (hi = apr_hash_first(pool, files); hi; hi = apr_hash_next(hi))
    {
      const char *name = NULL;
      apr_ssize_t len = 0;
      file_stats_t *file = NULL;

      apr_hash_this(hi, (const void **)&name, &len, (void**)&file);
      if (file->rev_num >= 0)
        APR_ARRAY_PUSH(result, file_stats_t *) = file;
    }

  /* sort them */
  qsort(result->elts, result->nelts, result->elt_size,
        (int (*)(const void *, const void *))compare_files);

  /* return the result */
  return result;
}

/* store VALUE to DEST in little-endian format.  Assume that the target
 * buffer is filled with 0.
 */
static void
write_number(byte *dest, int value)
{
  while (value)
    {
      *dest = (byte)(value % 256);
      value /= 256;
      ++dest;
    }
}

/* Return a linearly interpolated y value for X with X0 <= X <= X1 and
 * the corresponding Y0 and Y1 values.
 */
static int
interpolate(int y0, int x0, int y1, int x1, int x)
{
  return y0 + ((y1 - y0) * (x - x0)) / (x1 - x0);
}

/* Return the BMP-encoded 24 bit COLOR for the given value.
 */
static void
select_color(byte color[3], word value)
{
  enum { COLOR_COUNT = 10 };

  /* value -> color table. Missing values get interpolated.
   * { count, B - G - R } */
  word table[COLOR_COUNT][4] =
    {
      {     0, 255, 255, 255 },   /* unread -> white */
      {     1,  64, 128,   0 },   /* read once -> turquoise  */
      {     2,   0, 128,   0 },   /* twice  -> green */
      {     8,   0, 192, 192 },   /*    8x  -> yellow */
      {    64,   0,   0, 192 },   /*   64x  -> red */
      {   256,  64,  32, 230 },   /*  256x  -> bright red */
      {   512, 192,   0, 128 },   /*  512x  -> purple */
      {  1024,  96,  32,  96 },   /* 1024x  -> UV purple */
      {  4096,  32,  16,  32 },   /* 4096x  -> EUV purple */
      { 65535,   0,   0,   0 }    /*   max  -> black */
    };

  /* find upper limit entry for value */
  int i;
  for (i = 0; i < COLOR_COUNT; ++i)
    if (table[i][0] >= value)
      break;

  /* exact match? */
  if (table[i][0] == value)
    {
      color[0] = (byte)table[i][1];
      color[1] = (byte)table[i][2];
      color[2] = (byte)table[i][3];
    }
  else
    {
      /* interpolate */
      color[0] = (byte)interpolate(table[i-1][1], table[i-1][0],
                                   table[i][1], table[i][0],
                                   value);
      color[1] = (byte)interpolate(table[i-1][2], table[i-1][0],
                                   table[i][2], table[i][0],
                                   value);
      color[2] = (byte)interpolate(table[i-1][3], table[i-1][0],
                                   table[i][3], table[i][0],
                                   value);
    }
}

/* Writes a BMP image header to FILE for a 24-bit color picture of the
 * given XSIZE and YSIZE dimension.
 */
static void
write_bitmap_header(apr_file_t *file, int xsize, int ysize)
{
  /* BMP file header (some values need to filled in later)*/
  byte header[54] =
    {
      'B', 'M',        /* magic */
      0, 0, 0, 0,      /* file size (to be written later) */
      0, 0, 0, 0,      /* reserved, unused */
      54, 0, 0, 0,     /* pixel map starts at offset 54dec */

      40, 0, 0, 0,     /* DIB header has 40 bytes */
      0, 0, 0, 0,      /* x size in pixel */
      0, 0, 0, 0,      /* y size in pixel */
      1, 0,            /* 1 color plane */
      24, 0,           /* 24 bits / pixel */
      0, 0, 0, 0,      /* no pixel compression used */
      0, 0, 0, 0,      /* size of pixel array (to be written later) */
      0xe8, 3, 0, 0,   /* 1 pixel / mm */
      0xe8, 3, 0, 0,   /* 1 pixel / mm */
      0, 0, 0, 0,      /* no colors in palette */
      0, 0, 0, 0       /* no colors to import */
    };

  apr_size_t written;

  /* rows in BMP files must be aligned to 4 bytes */
  int row_size = APR_ALIGN(xsize * 3, 4);

  /* write numbers to header */
  write_number(header + 2, ysize * row_size + 54);
  write_number(header + 18, xsize);
  write_number(header + 22, ysize);
  write_number(header + 38, ysize * row_size);

  /* write header to file */
  written = sizeof(header);
  apr_file_write(file, header, &written);
}

/* To COLOR, add the fractional value of SOURCE from fractional indexes
 * SOURCE_START to SOURCE_END and apply the SCALING_FACTOR.
 */
static void
add_sample(color_t color,
           color_t *source,
           double source_start,
           double source_end,
           double scaling_factor)
{
  double factor = (source_end - source_start) / scaling_factor;

  apr_size_t i;
  for (i = 0; i < sizeof(color_t) / sizeof(*color); ++i)
    color[i] += (source_end - source_start < 0.5) && source_start > 1.0
              ? factor * source[(apr_size_t)source_start - 1][i]
              : factor * source[(apr_size_t)source_start][i];
}

/* Scale the IN_LEN RGB values from IN to OUT_LEN RGB values in OUT.
 */
static void
scale_line(color_t* out,
           int out_len,
           color_t *in,
           int in_len)
{
  double scaling_factor = (double)(in_len) / (double)(out_len);

  apr_size_t i;
  memset(out, 0, out_len * sizeof(color_t));
  for (i = 0; i < out_len; ++i)
    {
      color_t color = { 0 };

      double source_start = i * scaling_factor;
      double source_end = (i + 1) * scaling_factor;

      if ((apr_size_t)source_start == (apr_size_t)source_end)
        {
          add_sample(color, in, source_start, source_end, scaling_factor);
        }
      else
        {
          apr_size_t k;
          apr_size_t first_sample_end = (apr_size_t)source_start + 1;
          apr_size_t last_sample_start = (apr_size_t)source_end;

          add_sample(color, in, source_start, first_sample_end, scaling_factor);
          for (k = first_sample_end; k < last_sample_start; ++k)
            add_sample(color, in, k, k + 1, scaling_factor);

          add_sample(color, in, last_sample_start, source_end, scaling_factor);
        }

      memcpy(out[i], color, sizeof(color));
    }
}

/* Write the cluster read map for all files in INFO as BMP image to FILE.
 * If MAX_X is not 0, scale all lines to MAX_X pixels.  Use POOL for
 * allocations.
 */
static void
write_bitmap(apr_array_header_t *info,
             int max_x,
             apr_file_t *file,
             apr_pool_t *pool)
{
  int ysize = info->nelts;
  int xsize = 0;
  int x, y;
  apr_size_t row_size;
  apr_size_t written;
  color_t *line, *scaled_line;
  svn_boolean_t do_scale = max_x > 0;

  /* xsize = max cluster number */
  for (y = 0; y < ysize; ++y)
    if (xsize < APR_ARRAY_IDX(info, y, file_stats_t *)->read_map->nelts)
      xsize = APR_ARRAY_IDX(info, y, file_stats_t *)->read_map->nelts;

  /* limit picture dimensions (16k pixels in each direction) */
  if (xsize >= 0x4000)
    xsize = 0x3fff;
  if (ysize >= 0x4000)
    ysize = 0x3fff;
  if (max_x == 0)
    max_x = xsize;

  /* rows in BMP files must be aligned to 4 bytes */
  row_size = APR_ALIGN(max_x * sizeof(color_t), 4);

  /**/
  line = apr_pcalloc(pool, xsize * sizeof(color_t));
  scaled_line = apr_pcalloc(pool, row_size);

  /* write header to file */
  write_bitmap_header(file, max_x, ysize);

  /* write all rows */
  for (y = 0; y < ysize; ++y)
    {
      file_stats_t *file_info = APR_ARRAY_IDX(info, y, file_stats_t *);
      int block_count = file_info->read_map->nelts;
      for (x = 0; x < xsize; ++x)
        {
          color_t color = { 128, 128, 128 };
          if (x < block_count)
            {
              word count = APR_ARRAY_IDX(file_info->read_map, x, word);
              select_color(color, count);
            }

          memcpy(line[x], color, sizeof(color));
        }

      scale_line(scaled_line, max_x, line, block_count ? block_count : 1);

      written = row_size;
      apr_file_write(file, do_scale ? scaled_line : line, &written);
    }
}

/* write a color bar with (roughly) logarithmic scale as BMP image to FILE.
 */
static void
write_scale(apr_file_t *file)
{
  int x;
  word value = 0, inc = 1;

  /* write header to file */
  write_bitmap_header(file, 64, 1);

  for (x = 0; x < 64; ++x)
    {
      apr_size_t written;
      byte color[3] = { 128, 128, 128 };

      select_color(color, value);
      if (value + (int)inc < 0x10000)
        {
          value += inc;
          if (value >= 8 * inc)
            inc *= 2;
        }

      written = sizeof(color);
      apr_file_write(file, color, &written);
    }
}

/* Write a summary of the I/O ops to stdout.
 * Use POOL for temporaries.
 */
static void
print_stats(apr_pool_t *pool)
{
  apr_int64_t open_count = 0;
  apr_int64_t seek_count = 0;
  apr_int64_t read_count = 0;
  apr_int64_t read_size = 0;
  apr_int64_t clusters_read = 0;
  apr_int64_t unique_clusters_read = 0;
  apr_int64_t uncached_seek_count = 0;
  apr_int64_t unnecessary_seek_count = 0;
  apr_int64_t empty_read_count = 0;

  apr_hash_index_t *hi;
  for (hi = apr_hash_first(pool, files); hi; hi = apr_hash_next(hi))
    {
      const char *name = NULL;
      apr_ssize_t len = 0;
      file_stats_t *file = NULL;

      apr_hash_this(hi, (const void **)&name, &len, (void**)&file);

      open_count += file->open_count;
      seek_count += file->seek_count;
      read_count += file->read_count;
      read_size += file->read_size;
      clusters_read += file->clusters_read;
      unique_clusters_read += file->unique_clusters_read;
      uncached_seek_count += file->uncached_seek_count;
      unnecessary_seek_count += file->unnecessary_seeks;
      empty_read_count += file->empty_reads;
    }

  printf("%20s files\n", svn__i64toa_sep(apr_hash_count(files), ',', pool));
  printf("%20s files opened\n", svn__i64toa_sep(open_count, ',', pool));
  printf("%20s seeks\n", svn__i64toa_sep(seek_count, ',', pool));
  printf("%20s unnecessary seeks\n", svn__i64toa_sep(unnecessary_seek_count, ',', pool));
  printf("%20s uncached seeks\n", svn__i64toa_sep(uncached_seek_count, ',', pool));
  printf("%20s reads\n", svn__i64toa_sep(read_count, ',', pool));
  printf("%20s empty reads\n", svn__i64toa_sep(empty_read_count, ',', pool));
  printf("%20s unique clusters read\n", svn__i64toa_sep(unique_clusters_read, ',', pool));
  printf("%20s clusters read\n", svn__i64toa_sep(clusters_read, ',', pool));
  printf("%20s bytes read\n", svn__i64toa_sep(read_size, ',', pool));
}

/* Some help output. */
static void
print_usage(void)
{
  printf("fsfs-access-map <file>\n\n");
  printf("Reads strace of some FSFS-based tool from <file>, prints some stats\n");
  printf("and writes a cluster access map to 'access.bmp' the current folder.\n");
  printf("Each pixel corresponds to one 64kB cluster and every line to a rev\n");
  printf("or packed rev file in the repository.  Turquoise and green indicate\n");
  printf("1 and 2 hits, yellow to read-ish colors for up to 20, shares of\n");
  printf("for up to 100 and black for > 200 hits.\n\n");
  printf("A typical strace invocation looks like this:\n");
  printf("strace -e trace=open,close,read,lseek -o strace.txt svn log ...\n");
}

/* linear control flow */
int main(int argc, const char *argv[])
{
  apr_pool_t *pool = NULL;
  apr_file_t *file = NULL;

  apr_initialize();
  atexit(apr_terminate);

  pool = svn_pool_create(NULL);
  files = apr_hash_make(pool);
  handles = apr_hash_make(pool);

  if (argc == 2)
    apr_file_open(&file, argv[1], APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                  pool);
  if (file == NULL)
    {
      print_usage();
      return 0;
    }
  parse_file(file);
  apr_file_close(file);

  print_stats(pool);

  apr_file_open(&file, "access.bmp",
                APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BUFFERED,
                APR_OS_DEFAULT, pool);
  write_bitmap(get_rev_files(pool), 0, file, pool);
  apr_file_close(file);

  apr_file_open(&file, "access_scaled.bmp",
                APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BUFFERED,
                APR_OS_DEFAULT, pool);
  write_bitmap(get_rev_files(pool), 1024, file, pool);
  apr_file_close(file);

  apr_file_open(&file, "scale.bmp",
                APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BUFFERED,
                APR_OS_DEFAULT, pool);
  write_scale(file);
  apr_file_close(file);

  return 0;
}
