/* task.c : Implement the parallel task execution machine.
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

#include "private/svn_task.h"

#include <assert.h>

#include "private/svn_atomic.h"
#include "private/svn_thread_cond.h"

/* Top of the task tree.
 *
 * It is accessible from all tasks and contains all necessary ressource
 * pools and synchronization mechanisms.
 */
typedef struct root_t
{
  /* The actual root task. */
  svn_task__t *task;

  /* Pools "segregated" for reduced lock contention when multi-threading.
   * Ordered by lifetime of the objects allocated in them (long to short).
   */

  /* Allocate tasks and callbacks here.
   * These have the longest lifetimes and will (currently) not be released
   * until this root object gets cleaned up.
   */
  apr_pool_t *task_pool;

  /* Allocate per-task parameters (process_baton) from sub-pools of this.
   * They should be cleaned up as soon as the respective task has been
   * has been processed (the parameters will not be needed anymore).
   */
  apr_pool_t *process_pool;

  /* Allocate per-task output_t as well as the actual outputs here.
   * Allocation will happen just before calling the processing function.
   * Release the memory immediately afterwards, unless some actual output
   * has been produced.
   */
  apr_pool_t *output_pool;

  /* Context construction parameters as passed in to svn_task__run(). */
  svn_task__thread_context_constructor_t context_constructor;
  void *context_baton;

} root_t;

/* Sub-structure of svn_task__t containing that task's output.
 */
typedef struct output_t
{
  /* (Last part of the) output produced by the task.
   * If the task has sub-tasks, additional output (produced before creating
   * the sub-task) may be found in the respective sub-tasks
   * PRIOR_PARENT_OUTPUT.  NULL, if no output was produced.
   */
  void *output;

  /* Error code returned by the proccessing function. */
  svn_error_t *error;

  /* Parent tasks output before this task has been created, i.e. the part
   * that shall be passed to the output function before this tasks' output.
   * NULL, if there is no prior parent output.
   *
   * This has to be allocated in the parant's OUTPUT->POOL.
   */
  void *prior_parent_output;

  /* The tasks' output may be split into multiple parts, produced before
   * and in between sub-tasks.  Those will be stored in the OUTPUT structs
   * of those sub-tasks but have been allocated in (their parent's) POOL.
   *
   * This flag indicates if such partial results exist.  POOL must not be
   * destroyed in that case, until all sub-tasks outputs have been handled.
   */
  svn_boolean_t has_partial_results;

  /* Pool used to allocate this structure as well as the contents of OUTPUT
   * and PRIOR_PARENT_OUTPUT in any immediate sub-task. */
  apr_pool_t *pool;

} output_t;

/* The task's callbacks.
 *
 * We keep them in a separate structure such that they may be shared
 * easily between task & sub-task.
 */
typedef struct callbacks_t
{
  /* Process function to call.
   * The respective baton is task-specific and held in svn_task__t.
   *
   * NULL is legal here (for stability reasons and maybe future extensions)
   * but pointless as no processing will happen and no output can be
   * produced, in turn bypassing OUTPUT_FUNC.
   */
  svn_task__process_func_t process_func;

  /* Output function to call, if there was output. */
  svn_task__output_func_t output_func;

  /* Baton to pass into OUTPUT_FUNC. */
  void *output_baton;

} callbacks_t;

/* Task main data structure - a node in the task tree.
 */
struct svn_task__t
{
  /* Root object where all allocation & synchronization happens. */
  root_t *root;

  /* Tree structure */

  /* Parent task.
   * NULL for the root node:
   * (PARENT==NULL) == (*this==ROOT->TASK).
   */
  svn_task__t *parent;

  /* First immediate sub-task (in creation order). */
  svn_task__t *first_sub;

  /* Latest immediate sub-task (in creation order). */
  svn_task__t *last_sub;

  /* Next sibling, i.e. next in the list of PARENT's immediate sub-tasks
   * (in creation order). */
  svn_task__t *next;

  /* Index of this task within the PARENT's sub-task list, i.e. the number
   * of siblings created before this one.  The value will *not* be adjusted
   * should prior siblings be removed.
   *
   * This will be used to efficiently determine before / after relationships
   * between arbitrary siblings.
   */
  apr_size_t sub_task_idx;

  /* Efficiently track tasks that need processing */

  /* The first task, in pre-order, of this sub-tree whose processing has not
   * been started yet.  This will be NULL, iff for all tasks in this sub-tree,
   * processing has at least been started.  If *this==FIRST_READY, this task
   * itself waits for being proccessed.
   *
   * Note that immediate and / or indirect sub-tasks may already have been
   * processed before this one.
   */
  svn_task__t *first_ready;

  /* Task state. */

  /* The callbacks to use. Never NULL. */
  callbacks_t *callbacks;

  /* Process baton to pass into CALLBACKS->PROCESS_FUNC. */
  void *process_baton;

  /* Pool used to allocate the PROCESS_BATON. 
   * Sub-pool of ROOT->PROCESS_POOL.
   *
   * NULL, iff processing of this task has completed (sub-tasks may still
   * need processing).  Used to check whether processing has been completed.
   */
  apr_pool_t *process_pool;

  /* The processing results.
   *
   * Will be NULL before processing and may be NULL afterwards, if all
   * fields would be NULL.
   */
  output_t *output;
};

/* Adding tasks to the tree. */

/* Return the index of the first immediate sub-task of TASK with a ready
 * sub-task in its respective sub-tree.  TASK must have at least one such
 * sub-task.
 */
static apr_size_t first_ready_sub_task_idx(const svn_task__t *task)
{
  svn_task__t *sub_task = task->first_ready;
  assert(sub_task);

  while (sub_task->parent != task)
    sub_task = sub_task->parent;

  return sub_task->sub_task_idx;
}

/* Link TASK up with TASK->PARENT. */
static svn_error_t *link_new_task(svn_task__t *task)
{
  svn_task__t *current, *parent;

  /* Insert into parent's sub-task list. */
  if (task->parent->last_sub)
    {
      task->parent->last_sub->next = task;
      task->sub_task_idx = task->parent->last_sub->sub_task_idx + 1;
    }

  task->parent->last_sub = task;
  if (!task->parent->first_sub)
    task->parent->first_sub = task;

  /* TASK is ready for execution.
   *
   * It may be the first one in pre-order.  Update parents until they
   * have a "FIRST_READY" in a sub-tree before (in pre-order) the one
   * containing TASK. */
  for (current = task, parent = task->parent;
          parent
       && (   !parent->first_ready
           || first_ready_sub_task_idx(parent) >= current->sub_task_idx);
       current = parent, parent = parent->parent)
    {
      parent->first_ready = task;
    }

  return SVN_NO_ERROR;
}

/* If TASK has no OUTPUT sub-structure, add one.  Return the OUTPUT struct.
 *
 * In multi-threaded environments, calls to this must be serialized with
 * root_t changes. */
static output_t *ensure_output(svn_task__t *task)
{
  if (!task->output)
    {
      apr_pool_t * output_pool = svn_pool_create(task->root->output_pool);
      task->output = apr_pcalloc(output_pool, sizeof(*task->output));
      task->output->pool = output_pool;
    }

  return task->output;
}

/* Allocate a new task in POOL and return it in *RESULT.
 *
 * In multi-threaded environments, calls to this must be serialized with
 * root_t changes. */
static svn_error_t *alloc_task(svn_task__t **result, apr_pool_t *pool)
{
  *result = apr_pcalloc(pool, sizeof(**result));
  return SVN_NO_ERROR;
}

/* Allocate a new callbacks structure in POOL and return it in *RESULT.
 *
 * In multi-threaded environments, calls to this must be serialized with
 * root_t changes. */
static svn_error_t *alloc_callbacks(callbacks_t **result, apr_pool_t *pool)
{
  *result = apr_pcalloc(pool, sizeof(**result));
  return SVN_NO_ERROR;
}

/* Allocate a new task and append it to PARENT's sub-task list.
 * Store PROCESS_POOL, CALLBACKS and PROCESS_BATON in the respective
 * fields of the task struct.  If PARTIAL_OUTPUT is not NULL, it will
 * be stored in the new task's OUTPUT structure.
 *
 * This function does not return the new struct.  Instead, it is scheduled
 * to eventually be picked up by the task runner, i.e. execution is fully
 * controlled by the execution model and sub-tasks may only be added when
 * the new task itself is being processed.
 */
static svn_error_t *add_task(
  svn_task__t *parent,
  apr_pool_t *process_pool,
  void *partial_output,
  callbacks_t *callbacks,
  void *process_baton)
{
  svn_task__t *new_task;
  SVN_ERR(alloc_task(&new_task, parent->root->task_pool));

  new_task->root = parent->root;
  new_task->process_baton = process_baton;
  new_task->process_pool = process_pool;

  new_task->parent = parent;
  if (partial_output && parent->callbacks->output_func)
    {
      ensure_output(parent)->has_partial_results = TRUE;
      ensure_output(new_task)->prior_parent_output = partial_output;
    }

  /* The new task will be ready for execution once we link it up in PARENT. */
  new_task->first_ready = new_task;
  new_task->callbacks = callbacks;

  SVN_ERR(link_new_task(new_task));

  return SVN_NO_ERROR;
}

svn_error_t *svn_task__add(
  svn_task__t *task,
  apr_pool_t *process_pool,
  void *partial_output,
  svn_task__process_func_t process_func,
  void *process_baton,
  svn_task__output_func_t output_func,
  void *output_baton)
{
  callbacks_t *callbacks;
  SVN_ERR(alloc_callbacks(&callbacks, task->root->task_pool));

  callbacks->process_func = process_func;
  callbacks->output_func = output_func;
  callbacks->output_baton = output_baton;

  return svn_error_trace(add_task(task, process_pool, partial_output,
                                  callbacks, process_baton));
}

svn_error_t* svn_task__add_similar(
  svn_task__t* task,
  apr_pool_t *process_pool,
  void* partial_output,
  void* process_baton)
{
  return svn_error_trace(add_task(task, process_pool, partial_output,
                                  task->callbacks, process_baton));
}

apr_pool_t *svn_task__create_process_pool(
  svn_task__t *parent)
{
  return svn_pool_create(parent->root->process_pool);
}

/* Removing tasks from the tree */

/* Remove TASK from the parent tree.
 * TASK must have been fully processed and there shall be no more sub-tasks.
 */
static svn_error_t *remove_task(svn_task__t *task)
{
  svn_task__t *parent = task->parent;

  assert(task->first_ready == NULL);
  assert(task->first_sub == NULL);

  if (parent)
    {
      if (parent->first_sub == task)
        parent->first_sub = task->next;
      if (parent->last_sub == task)
        parent->last_sub = NULL;
    }

  return SVN_NO_ERROR;
}

/* Recursively free all errors in TASK.
 *
 * Don't try to clean up pools etc. because we will call this function
 * mainly to prevent error leaks when terminating the task runner early.
 * It is better to have the standard pool cleanups to deal with memory
 * management in that case.
 */
static void free_sub_tasks(svn_task__t *task)
{
  while (task->first_sub)
    free_sub_tasks(task->first_sub);

  if (task->output)
    svn_error_clear(task->output->error);

  if (task->parent && task->parent->first_sub == task)
    task->parent->first_sub = task->next;
}

/* Picking the next task to process */

/* Utility function that follows the chain of siblings and returns the first
 * that has *some* unprocessed task in its sub-tree.
 * 
 * Returns TASK if either TASK or any of its sub-tasks is unprocessed.
 * Returns NULL if all direct or indirect sub-tasks of TASK->PARENT are
 * already being processed or have been completed.
 */
static svn_task__t *next_ready(svn_task__t *task)
{
  for (; task; task = task->next)
    if (task->first_ready)
      break;

  return task;
}

/* Mark TASK as no longer being unprocessed.
 * Call this before starting actual processing of TASK.
 */
static void unready_task(svn_task__t *task)
{
  svn_task__t *parent, *current;
  svn_task__t *first_ready = NULL;

  /* Make sure that processing on TASK has not already started. */
  assert(task->first_ready == task);

  /* Also, there should be no sub-tasks before processing this one.
   * Sub-tasks may only be added by processing the immediate parent. */
  assert(task->first_sub == NULL);
 
  /* There are no sub-tasks, hence nothing in the sub-tree could be ready. */
  task->first_ready = NULL;

  /* Bubble up the tree while TASK is the "first ready" one.
   * Update the pointers to the next one ready. */
  for (current = task, parent = task->parent;
       parent && (parent->first_ready == task);
       current = parent, parent = parent->parent)
    {
      /* If we have not found another task that is ready, search the
       * siblings for one.   A suitable one cannot be *before* CURRENT
       * or otherwise, PARENT->FIRST_READY would not equal TASK.
       * It is possible that we won't find one at the current level. */
      if (!first_ready)
        {
          svn_task__t *first_ready_sub_task = next_ready(current->next);
          first_ready = first_ready_sub_task
                      ? first_ready_sub_task->first_ready
                      : NULL;
        }

      parent->first_ready = first_ready;
    }
}

/* Task processing and outputting results */

/* The forground output_processed() function will now consider TASK's
 * processing function to be completed.  Sub-tasks may still be pending. */
static void set_processed(svn_task__t *task)
{
  task->process_pool = NULL;
}

/* Return whether TASK's processing function has been completed.
 * Pending sub-tasks will be ignored. */
static svn_boolean_t is_processed(const svn_task__t *task)
{
  return task->process_pool == NULL;
}

/* Process a single TASK within the given THREAD_CONTEXT.  It may add
 * sub-tasks but those need separate calls to this function to be processed.
 *
 * Pass CANCEL_FUNC, CANCEL_BATON and SCRATCH_POOL to the TASK's process
 * function.
 *
 * This will destroy TASK->PROCESS_POOL but will not reset the pointer.
 * You have to do that explicitly by calling set_processed().  The reason
 * is that in multi-threaded execution, you may want that transition to
 * be atomic with other tree operations.
 */
static void process(svn_task__t *task,
                    void *thread_context,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  callbacks_t *callbacks = task->callbacks;

  if (callbacks->process_func)
    {
      /* Depending on whether there is prior parent output, we may or 
       * may not have already an OUTPUT structure allocated for TASK. */
      output_t *output = ensure_output(task);
      output->error = callbacks->process_func(&output->output, task,
                                              thread_context,
                                              task->process_baton,
                                              cancel_func, cancel_baton,
                                              output->pool, scratch_pool);

      /* If there is no way to output the results, we simply ignore them. */
      if (!callbacks->output_func)
        output->output = NULL;

      /* Anything left that we may want to output? */
      if (   !output->error
          && !output->output
          && !output->prior_parent_output
          && !output->has_partial_results)
        {
          /* Nope.  Release the memory and reset OUTPUT such that
           * output_processed() can quickly skip it. */
          svn_pool_destroy(output->pool);
          task->output = NULL;
        }
    }

  svn_pool_destroy(task->process_pool);
}

/* Output *TASK results in post-order until we encounter a task that has not
 * been processed, yet - which may be *TASK itself - and return it in *TASK.
 *
 * Pass CANCEL_FUNC, CANCEL_BATON and RESULT_POOL into the respective task
 * output functions.  Use SCRATCH_POOL for temporary allocations.
 */  
static svn_error_t *output_processed(
  svn_task__t **task,
  svn_cancel_func_t cancel_func,
  void *cancel_baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_task__t *current = *task;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  output_t *output;
  callbacks_t *callbacks;

  while (current && is_processed(current))
    {
      svn_pool_clear(iterpool);

      /* Post-order, i.e. dive into sub-tasks first.
       *
       * Note that the "background" processing for CURRENT has completed
       * and only the output function may add no further sub-tasks. */
      if (current->first_sub)
        {
          current = current->first_sub;
          output = current->output;
          callbacks = current->parent->callbacks;

          /* We will handle this sub-task in the next iteration but we
           * may have to process results produced before or in between
           * sub-tasks.  Also note that PRIOR_PARENT_OUTPUT will only be
           * set, of the OUTPUT_FUNC is not NULL. */
          if (output && output->prior_parent_output)
            SVN_ERR(callbacks->output_func(
                        current->parent, output->prior_parent_output,
                        callbacks->output_baton,
                        cancel_func, cancel_baton,
                        result_pool, iterpool));
        }
      else
        {
          /* No deeper sub-task.  Process the results from CURRENT. */
          output = current->output;
          if (output)
            {
              /* Return errors.
               * Make sure they don't get cleaned up with the task tree. */
              svn_error_t *err = output->error;
              output->error = SVN_NO_ERROR;
              SVN_ERR(err);

              /* Handle remaining output of the CURRENT task. */
              callbacks = current->callbacks;
              if (output->output)
                SVN_ERR(callbacks->output_func(
                            current, output->output,
                            callbacks->output_baton,
                            cancel_func, cancel_baton,
                            result_pool, iterpool));
            }

          /* The output function may have added further sub-tasks.
           * Handle those in the next iteration. */
          if (!current->first_sub)
            {
              /* Task completed. No further sub-tasks.
               * Remove this task from the tree and continue at the parent,
               * recursing into the next sub-task (==NEXT, if not NULL)
               * with the next iteration. */
              svn_task__t *to_delete = current;
              current = to_delete->parent;
              SVN_ERR(remove_task(to_delete));

              /* We have output all sub-nodes, including all partial results.
               * Therefore, the last used thing allocated in OUTPUT->POOL is
               * OUTPUT itself and it is safe to clean that up. */
              if (output)
                svn_pool_destroy(output->pool);
            }
        }
    }

  svn_pool_destroy(iterpool);
  *task = current;

  return SVN_NO_ERROR;
}

/* Execution models */

/* Run the (root) TASK to completion, including dynamically added sub-tasks.
 * Pass CANCEL_FUNC and CANCEL_BATON directly into the task callbacks.
 * Pass the RESULT_POOL into the task output functions and use SCRATCH_POOL
 * for everything else (unless covered by task pools).
 */
static svn_error_t *execute_serially(
  svn_task__t *task,
  svn_cancel_func_t cancel_func,
  void *cancel_baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  root_t* root = task->root;
  svn_error_t *task_err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Task to execute currently.
   * Always the first unprocessed task in pre-order. */
  svn_task__t *current = task;

  /* The context may be quite complex, so we use the ITERPOOL to clean up
   * any memory that was used temporarily during context creation. */
  void *thread_context = NULL;
  if (root->context_constructor)
    SVN_ERR(root->context_constructor(&thread_context, root->context_baton,
                                      scratch_pool, iterpool));

  /* Process one task at a time, stop upon error or when the whole tree
   * has been completed. */
  while (current && !task_err)
    {
      svn_pool_clear(iterpool);

      /* "would-be background" processing the CURRENT task. */
      unready_task(current);
      process(current, thread_context, cancel_func, cancel_baton, iterpool);
      set_processed(current);

      /* Output results in "forground" and move CURRENT to the next one
       * needing processing. */
      task_err = output_processed(&current,
                                  cancel_func, cancel_baton,
                                  result_pool, scratch_pool);
    }

  /* Explicitly release any (other) error and pool not cleaned up so far.
   * This is important in the case of early exists due to error returns. */
  free_sub_tasks(task);
  svn_pool_destroy(iterpool);

  /* Get rid of all remaining tasks. */
  return svn_error_trace(task_err);
}

/* Root data structure */

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
  apr_pool_t *scratch_pool)
{
  root_t *root = apr_pcalloc(scratch_pool, sizeof(*root));

  /* Allocation on stack is fine as this function will not exit before
   * all task processing has been completed. */
  callbacks_t callbacks;

  /* For now, we only have single-threaded execution.
   * So, no special consideration required regarding pools & serialization.*/
  root->task_pool = scratch_pool;
  root->process_pool = scratch_pool;
  root->output_pool = scratch_pool;

  callbacks.process_func = process_func;
  callbacks.output_func = output_func;
  callbacks.output_baton = output_baton;
  
  root->task = apr_pcalloc(scratch_pool, sizeof(*root->task));
  root->task->root = root;
  root->task->first_ready = root->task;
  root->task->callbacks = &callbacks;
  root->task->process_baton = process_baton;
  root->task->process_pool = svn_pool_create(root->process_pool);

  root->context_baton = context_baton;
  root->context_constructor = context_constructor;

  /* For now, there is only single-threaded execution. */
  SVN_ERR(execute_serially(root->task,
                           cancel_func, cancel_baton,
                           result_pool, scratch_pool));

  return SVN_NO_ERROR;
}
