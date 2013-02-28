/* string_table.c : operations on string tables
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

#include <string.h>
#include <apr_tables.h>

#include "svn_string.h"
#include "svn_sorts.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "string_table.h"



#define MAX_DATA_SIZE 0xffff
#define MAX_SHORT_STRING_LEN (MAX_DATA_SIZE / 4)
#define TABLE_SHIFT 13
#define MAX_STRINGS_PER_TABLE (1 << (TABLE_SHIFT - 1))
#define LONG_STRING_MASK (1 << (TABLE_SHIFT - 1))
#define STRING_INDEX_MASK ((1 << (TABLE_SHIFT - 1)) - 1)


typedef struct builder_string_t
{
  svn_string_t string;
  int position;
  apr_size_t depth;
  struct builder_string_t *previous;
  struct builder_string_t *next;
  apr_size_t previous_match_len;
  apr_size_t next_match_len;
  struct builder_string_t *left;
  struct builder_string_t *right;
} builder_string_t;

typedef struct builder_table_t
{
  apr_size_t max_data_size;
  builder_string_t *top;
  builder_string_t *first;
  builder_string_t *last;
  apr_array_header_t *short_strings;
  apr_array_header_t *long_strings;
  apr_hash_t *long_string_dict;
} builder_table_t;

struct string_table_builder_t
{
  apr_pool_t *pool;
  apr_array_header_t *tables;
};

typedef struct string_header_t
{
  apr_uint16_t head_string;
  apr_uint16_t head_length;
  apr_uint16_t tail_start;
  apr_uint16_t tail_length;
} string_header_t;

typedef struct string_sub_table_t
{
  char *data;
  apr_size_t data_size;

  string_header_t *short_strings;
  apr_size_t short_string_count;

  svn_string_t *long_strings;
  apr_size_t long_string_count;
} string_sub_table_t;

struct string_table_t
{
  apr_size_t size;
  string_sub_table_t *sub_tables;
};


/* Accessing ID Pieces.  */

static builder_table_t *
add_table(string_table_builder_t *builder)
{
  builder_table_t *table = apr_pcalloc(builder->pool, sizeof(*table));
  table->max_data_size = MAX_DATA_SIZE;
  table->short_strings = apr_array_make(builder->pool, 64,
                                        sizeof(builder_string_t *));
  table->long_strings = apr_array_make(builder->pool, 0,
                                       sizeof(svn_string_t));
  table->long_string_dict = svn_hash__make(builder->pool);

  APR_ARRAY_PUSH(builder->tables, builder_table_t *) = table;

  return table;
}

string_table_builder_t *
svn_fs_fs__string_table_builder_create(apr_pool_t *pool)
{
  string_table_builder_t *result = apr_palloc(pool, sizeof(*result));
  result->pool = pool;
  result->tables = apr_array_make(pool, 1, sizeof(builder_table_t *));

  add_table(result);
  
  return result;
}

static void
balance(builder_table_t *table,
        builder_string_t **parent,
        builder_string_t *node)
{
  unsigned left_hight = node->left ? node->left->depth + 1 : 0;
  unsigned right_hight = node->right ? node->right->depth + 1 : 0;

  if (left_hight > right_hight + 1)
    {
      builder_string_t *temp = node->left->right;
      node->left->right = node;
      *parent = node->left;
      node->left = temp;
      
      --left_hight;
    }
  else if (left_hight + 1 < right_hight)
    {
      builder_string_t *temp = node->right->left;
      *parent = node->right;
      node->right->left = node;
      node->right = temp;

      --right_hight;
    }

  node->depth = MAX(left_hight, right_hight);
}

static apr_uint16_t
match_length(const svn_string_t *lhs,
             const svn_string_t *rhs)
{
  apr_size_t len = MIN(rhs->len, rhs->len);
  return (apr_uint16_t)svn_cstring__match_length(lhs->data, rhs->data, len);
}

static apr_uint16_t
insert_string(builder_table_t *table,
              builder_string_t **parent,
              builder_string_t *to_insert)
{
  apr_uint16_t result;
  builder_string_t *current = *parent;
  int diff = strcmp(current->string.data, to_insert->string.data);
  if (diff == 0)
    {
      apr_array_pop(table->short_strings);
      return current->position;
    }

  if (diff < 0)
    {
      if (current->left == NULL)
        {
          current->left = to_insert;

          to_insert->previous = current->previous;
          to_insert->next = current;

          if (to_insert->previous == NULL)
            {
              table->first = to_insert;
            }
          else
            {
              builder_string_t *previous = to_insert->previous;
              to_insert->previous_match_len
                = match_length(&previous->string, &to_insert->string);

              previous->next = to_insert;
              previous->next_match_len = to_insert->previous_match_len;
            }

          current->previous = to_insert;
          to_insert->next_match_len
            = match_length(&current->string, &to_insert->string);
          current->previous_match_len = to_insert->next_match_len;
          
          table->max_data_size -= MIN(to_insert->previous_match_len,
                                      to_insert->next_match_len);

          return to_insert->position;
        }
      else
        result = insert_string(table, &current->left, to_insert);
    }
  else
    {
      if (current->right == NULL)
        {
          current->right = to_insert;

          to_insert->next = current->next;
          to_insert->previous = current;

          if (to_insert->next == NULL)
            {
              table->last = to_insert;
            }
          else
            {
              builder_string_t *next = to_insert->next;
              to_insert->next_match_len
                = match_length(&next->string, &to_insert->string);

              next->previous = to_insert;
              next->previous_match_len = to_insert->next_match_len;
            }

          current->next = current->right;
          to_insert->previous_match_len
            = match_length(&current->string, &to_insert->string);
          current->next_match_len = to_insert->previous_match_len;

          table->max_data_size -= MIN(to_insert->previous_match_len,
                                      to_insert->next_match_len);

          return to_insert->position;
        }
      else
        result = insert_string(table, &current->right, to_insert);
    }

  balance(table, parent, current);
  return result;
}

apr_size_t
svn_fs_fs__string_table_builder_add(string_table_builder_t *builder,
                                    const char *string,
                                    apr_size_t len)
{
  apr_size_t result = -1;
  builder_table_t *table = APR_ARRAY_IDX(builder->tables,
                                         builder->tables->nelts - 1,
                                         builder_table_t *);
  if (len == 0)
    len = strlen(string);

  if (len > MAX_SHORT_STRING_LEN)
    {
      svn_string_t item;
      item.data = string;
      item.len = len;
      
      result
        = (apr_uintptr_t)apr_hash_get(table->long_string_dict, string, len);
      if (result)
        return result - 1
             + LONG_STRING_MASK
             + (((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT);

      if (table->long_strings->nelts == MAX_STRINGS_PER_TABLE)
        table = add_table(builder);

      result = table->long_strings->nelts
             + LONG_STRING_MASK
             + (((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT);
      APR_ARRAY_PUSH(table->long_strings, svn_string_t) = item;
      apr_hash_set(table->long_string_dict, string, len,
                   (void*)(apr_uintptr_t)table->long_strings->nelts);
    }
  else
    {
      builder_string_t *item = apr_pcalloc(builder->pool, sizeof(*item));
      item->string.data = string;
      item->string.len = len;
      item->previous_match_len = APR_SIZE_MAX;
      item->next_match_len = APR_SIZE_MAX;

      if (   table->long_strings->nelts == MAX_STRINGS_PER_TABLE
          || table->max_data_size < len)
        table = add_table(builder);

      item->position = (apr_size_t)table->short_strings->nelts;
      APR_ARRAY_PUSH(table->short_strings, builder_string_t *) = item;

      if (table->top == NULL)
        {
          table->max_data_size -= len;
          table->top = item;
          table->first = item;
          table->last = item;

          result = ((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT;
        }
      else
        {
          result = insert_string(table, &table->top, item)
                 + (((apr_size_t)builder->tables->nelts - 1) << TABLE_SHIFT);
        }
    }

  return result;
}

static void
create_table(string_sub_table_t *target,
             builder_table_t* source,
             apr_pool_t *pool,
             apr_pool_t *scratch_pool)
{
  int i = 0;
  apr_hash_t *tails = svn_hash__make(scratch_pool);
  svn_stringbuf_t *data
    = svn_stringbuf_create_ensure(MAX_DATA_SIZE - source->max_data_size,
                                  scratch_pool);

  target->short_string_count = (apr_size_t)source->short_strings->nelts;
  target->short_strings = apr_palloc(pool, sizeof(*target->short_strings) *
                                           target->short_string_count);
  for (i = 0; i < source->short_strings->nelts; ++i)
    {
      const builder_string_t *string
        = APR_ARRAY_IDX(source->short_strings, i, const builder_string_t *);

      string_header_t *entry = &target->short_strings[i];
      const char *tail = string->string.data + string->previous_match_len;
      string_header_t *tail_match;
      apr_size_t head_length = string->previous_match_len;

      int base = i - 1;
      if (head_length)
        while (   base >= 0
               && target->short_strings[i].head_length >= head_length)
          --base;
          
      entry->head_string = (apr_uint16_t)base;
      entry->head_length = (apr_uint16_t)head_length;
      entry->tail_length
        = (apr_uint16_t)(string->string.len - entry->head_length);

      tail_match = apr_hash_get(tails, tail, entry->tail_length);
      if (tail_match)
        {
          entry->tail_start = tail_match->tail_start;
        }
      else
        {
          entry->tail_start = (apr_uint16_t)data->len;
          svn_stringbuf_appendbytes(data, tail, entry->tail_length);
          apr_hash_set(tails, tail, entry->tail_length, entry);
        }
    }

  target->long_string_count = (apr_size_t)source->long_strings->nelts;
  target->long_strings = apr_palloc(pool, sizeof(*target->long_strings) *
                                          target->long_string_count);
  for (i = 0; i < source->long_strings->nelts; ++i)
    {
      svn_string_t *string = &target->long_strings[i];
      *string = APR_ARRAY_IDX(source->long_strings, i, svn_string_t);
      string->data = apr_pstrmemdup(pool, string->data, string->len);
    }

  target->data = apr_pmemdup(pool, data->data, data->len);
  target->data_size = data->len;
}

string_table_t *
svn_fs_fs__string_table_create(const string_table_builder_t *builder,
                               apr_pool_t *pool)
{
  apr_size_t i;
  
  string_table_t *result = apr_pcalloc(pool, sizeof(*result));
  result->size = (apr_size_t)builder->tables->nelts;
  result->sub_tables
    = apr_pcalloc(pool, result->size * sizeof(*result->sub_tables));

  for (i = 0; i < result->size; ++i)
    create_table(&result->sub_tables[i],
                 APR_ARRAY_IDX(builder->tables, i, builder_table_t*),
                 pool,
                 builder->pool);

  return result;
}

static void
table_copy_string(char *buffer,
                  apr_size_t size,
                  const string_sub_table_t *table,
                  string_header_t *header)
{
  apr_size_t len = header->head_length + header->tail_length;
  apr_size_t to_copy = len;

  while (to_copy)
    {
      memcpy(buffer + header->head_length,
             table->data + header->tail_start,
             len - header->head_length);
      to_copy = header->head_length;
      header = &table->short_strings[header->head_string];
    }

  if (size > len)
    buffer[len] = '\0';
}

const char*
svn_fs_fs__string_table_get(const string_table_t *table,
                            apr_size_t idx,
                            apr_pool_t *pool)
{
  apr_size_t table_number = idx >> TABLE_SHIFT;
  apr_size_t sub_index = idx & STRING_INDEX_MASK;

  if (table_number < table->size)
    {
      string_sub_table_t *sub_table = &table->sub_tables[table_number];
      if (idx & LONG_STRING_MASK)
        {
          if (sub_index < sub_table->long_string_count)
            return apr_pstrmemdup(pool,
                                  sub_table->long_strings[sub_index].data,
                                  sub_table->long_strings[sub_index].len);
        }
      else
        {
          if (sub_index < sub_table->short_string_count)
            {
              string_header_t *header = sub_table->short_strings + sub_index;
              apr_size_t len = header->head_length + header->tail_length + 1;
              char *result = apr_palloc(pool, len);
              table_copy_string(result, len, sub_table, header);
            }
        }
    }

  return apr_pstrmemdup(pool, "", 0);
}

apr_size_t
svn_fs_fs__string_table_copy_string(char *buffer,
                                    apr_size_t size,
                                    const string_table_t *table,
                                    apr_size_t idx)
{
  apr_size_t table_number = idx >> TABLE_SHIFT;
  apr_size_t sub_index = idx & STRING_INDEX_MASK;

  if (table_number < table->size)
    {
      string_sub_table_t *sub_table = &table->sub_tables[table_number];
      if (idx & LONG_STRING_MASK)
        {
          if (sub_index < sub_table->long_string_count)
            {
              apr_size_t len = sub_table->long_strings[sub_index].len;
              if (size > len)
                memcpy(buffer, sub_table->long_strings[sub_index].data,
                       len + 1);
              else if (size == len)
                memcpy(buffer, sub_table->long_strings[sub_index].data, len);

              return len;
            }
        }
      else
        {
          if (sub_index < sub_table->short_string_count)
            {
              string_header_t *header = sub_table->short_strings + sub_index;
              apr_size_t len = header->head_length + header->tail_length;
              if (size >= len)
                table_copy_string(buffer, size, sub_table, header);

              return len;
            }
        }
    }

  if (size > 0)
    buffer[0] = '\0';

  return 0; 
}
