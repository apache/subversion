/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_task.h
 * @brief Structures and functions for threaded execution of tasks
 */

#ifndef SVN_TASK_H
#define SVN_TASK_H

#include "svn_pools.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * A task is a "unit of work", basically a glorified function call.
 * It shall not be confused with "thread".
 *
 * During execution, a task may add further sub-tasks - equivalent to
 * sub-function calls.  They will be executed after their parent task
 * has been processed forming an growing tree of *isolated* tasks.
 *
 * Tasks may be executed in arbitrary order, concurrently and in parallel.
 * To guarantee consistent output order and error handling, every task
 * consists of two functions.  The first is the "process function" that
 * should perform the bulk of the work, may be executed in some worker
 * thread and may produce some result.  The latter is later passed into
 * the second function, the "output function".  This one is called in the
 * main thread and strictly in pre-order wrt. the position of the respective
 * task within the tree.  Both, process and output functions, may add
 * further sub-tasks as needed.
 * 
 * Errors are detected in strictly the same order, with only the first one
 * being returned from the task runner.  In particular, it may not be the
 * first error that occurs timewise but only the first one encountered when
 * walking the tree and checking the processing results for errors.  Because
 * it takes some time make the workers cancel all outstanding tasks,
 * additional errors may occur before and after the one that ultimately
 * gets reported.  All those errors are being swallowed.
 *
 * Since tasks may be executed by worker threads, we need a way to set up
 * an appropriate environment for that thread to meet the single-threaded
 * execution assumed by SVN and APR APIs.  This environment is called
 * "thread context" and could be something like a svn_wc__db_t.
 *
 * Any of the functions may be NULL.  Process function and output function
 * may differ from task to task while the thread context constructor has
 * to be shared.
 *
 * The task runner will take care of creating pools with appropriate
 * lifetime and threading settings but the functions called may not make
 * any assumptions about them beyond that.  In particular, you cannot share
 * data that was allocated in those pools between tasks.
 */

/**
 * Opaque type of a task.
 */
typedef struct svn_task__t svn_task__t;

/**
 * Callback function type to process a single @a task.  The parameters are
 * given by @a process_baton and any thread-specific context data is
 * given by @a thread_context.  Either may be @a NULL.
 *
 * Any output must be allocated in @a result_pool and returned in @a *result.
 * If no output has been produced, @a *result should be set to @c NULL.
 * In that case, the task's output function will not be called and the
 * respective pool can be reclaimed immediately.  This may result in
 * significant runtime and memory savings.  Error reporting is not affected.
 *
 * @a cancel_func, @a cancel_baton and @a scratch_pool are the usual things.
 */
typedef svn_error_t *(*svn_task__process_func_t)(
  void **result,
  svn_task__t *task,
  void *thread_context,
  void *process_baton,
  svn_cancel_func_t cancel_func,
  void *cancel_baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Callback function type to output the @a result of a single @a task.
 * The function parameters are given by @a output_baton.
 *
 * All data that shall be returned by @a svn_task__run() needs to be
 * allocated from @a result_pool.
 *
 * @a cancel_func, @a cancel_baton and @a scratch_pool are the usual things.
 */
typedef svn_error_t *(*svn_task__output_func_t)(
  svn_task__t *task,
  void *result,
  void *output_baton,
  svn_cancel_func_t cancel_func,
  void *cancel_baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Callback function type to construct a new worker @a *thread_context.
 * The function parameters are given by @a context_baton.
 *
 * Allocate the result in @a result_pool.  The context is passed in to the
 * process function as-is and may be @c NULL.
 *
 * @a scratch_pool is the usual thing.
 */
typedef svn_error_t *(*svn_task__thread_context_constructor_t)(
  void **thread_context,
  void *context_baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Construct the root of the task tree and start processing from there.
 * This is the main API function and the only one to be called outside any
 * task callback.
 * 
 * Employ up to @a thread_count worker threads, depending on APR threading
 * support and processing needs.  If the @a thread_count is set to 1 or if
 * APR does not support multithreading, all tasks will be processed in the
 * current thread.
 *
 * For the root task, call the given @a process_func with @a process_baton.
 * Either may be @c NULL.  The task output will be produced by the
 * @a output_func called with @a output_baton.  Either may be @c NULL.
 *
 * For each worker thread - or the current thread in single-threaded
 * execution - a context object may be created if @a context_constructor
 * is not @c NULL.  The respective contexts are being created by calling
 * this constructor function with @a context_baton as parameter.
 *
 * Allocate the result in @a result_pool.  The context is passed in to the
 * process function as-is and may be @c NULL.
 *
 * @a cancel_func, @a cancel_baton and @a scratch_pool are the usual things.
 */
svn_error_t *svn_task__run(
  apr_int32_t thread_count,
  svn_task__process_func_t process_func,
  void *process_baton,
  svn_task__output_func_t output_func,
  void *output_baton,
  svn_task__thread_context_constructor_t context_constructor,
  void *context_baton,
  svn_cancel_func_t cancel_func,
  void *cancel_baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Create a new memory pool in the @a parent task.
 *
 * The main purpose is to hold the parameters (i.e. process baton) for a
 * new sub-task.  You must call this exactly once per sub-task and pass it
 * into either @a svn_task__add() or @a svn_task__add_similar() - even if
 * you use a @c NULL process baton.
 *
 * @todo Consider replace these pools with a single pool at the parent
 * and turn this into a simple getter.  We will end up with a lot fewer
 * pools that OTOH may live a lot longer.
 */
apr_pool_t *svn_task__create_process_pool(
  svn_task__t *parent);

/**
 * Append a new sub-task to the current @a task with the given
 * @a process_pool.  The latter must have been allocated with
 * @a svn_task__create_process_pool() for @a task.
 *
 * @a partial_output is the output produced by the current task so far
 * since adding the last sub-task or leading up to the first sub-task.
 * It must have been allocated in the @c result_pool that was handed in
 * to the process function of the current task.  If no output needs to
 * be passed to the current tasks output function, this should be @c NULL.
 *
 * The new sub-task will use the given @a process_func with @a process_baton
 * and output the results in @a output_func called with @a output_baton.
 * Any of these may be @c NULL.  If the functions are @c NULL, no output
 * and / or processing will take place.
 */
svn_error_t *svn_task__add(
  svn_task__t *task,
  apr_pool_t *process_pool,
  void *partial_output,
  svn_task__process_func_t process_func,
  void *process_baton,
  svn_task__output_func_t output_func,
  void *output_baton);

/**
 * This is a simplified version of @a svn_task__add().
 *
 * @a process_func, @a output_func and @a output_baton will be the same
 * as for the current task.  This is useful for recursive tasks.
 */
svn_error_t *svn_task__add_similar(
  svn_task__t *task,
  apr_pool_t *process_pool,
  void *partial_output,
  void *process_baton);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TASK_H */
