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
#include <apr_thread_proc.h>

#include "private/svn_atomic.h"
#include "private/svn_thread_cond.h"


/* Top of the task tree.
 *
 * It is accessible from all tasks and contains all necessary resource
 * pools and synchronization mechanisms.
 */
typedef struct root_t
{
  /* Global mutex protecting the whole task tree.
   * Any modification on the tree structure or task state requires
   * serialization through this mutex.
   * 
   * In single-threaded execution, this will be a no-op dummy.
   */
  svn_mutex__t *mutex;

  /* Used to portably implement a memory barrier in C. */
  svn_mutex__t *memory_barrier_mutex;

  /* Signals to waiting ("sleeping") worker threads that they need to wake
   * up.  This may be due to new tasks being available or because the task
   * runner is about to terminate.
   *
   * Waiting tasks must hold the MUTEX when entering the waiting state.
   *
   * NULL if execution is single-threaded.
   */
  svn_thread_cond__t *worker_wakeup;

  /* Signals to the foreground thread that some tasks may have been processed
   * and output process may commence.  However, there is no guarantee that
   * any task actually completed nor that it is the one whose output needs
   * to be processed next. 
   *
   * Waiting tasks must hold the MUTEX when entering the waiting state.
   *
   * NULL if execution is single-threaded.
   */
  svn_thread_cond__t *task_processed;

  /* The actual root task. */
  svn_task__t *task;

  /* All allocations from TASK_POOL must be serialized through this mutex. */
  svn_mutex__t *task_alloc_mutex;

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
   * processed (the parameters will not be needed anymore).
   */
  apr_pool_t *process_pool;

  /* Allocate per-task results_t as well as the actual outputs from sub-pools
   * of this.  Allocation will happen just before calling the processing
   * function.  Release the memory immediately afterwards, unless some actual
   * output has been produced.
   */
  apr_pool_t *results_pool;

  /* Context construction parameters as passed in to svn_task__run(). */
  svn_task__thread_context_constructor_t context_constructor;
  void *context_baton;

  /* If TRUE, end task processing.  In multi-threaded execution, the main
   * (= output) thread will set this upon error, cancellation or simply
   * when all work is done.  Worker threads will check for it and terminate
   * asap.
   */
  svn_atomic_t terminate;

} root_t;

/* Sub-structure of svn_task__t containing that task's processing output.
 */
typedef struct results_t
{
  /* (Last part of the) output produced by the task.
   * If the task has sub-tasks, additional output (produced before creating
   * each sub-task) may be found in the respective sub-task's
   * PRIOR_PARENT_OUTPUT.  NULL, if no output was produced.
   */
  void *output;

  /* Error code returned by the proccessing function. */
  svn_error_t *error;

  /* Parent task's output before this task has been created, i.e. the part
   * that shall be passed to the output function before this task's output.
   * NULL, if there is no prior parent output.
   *
   * This has to be allocated in the parent's OUTPUT->POOL.
   */
  void *prior_parent_output;

  /* The tasks' output may be split into multiple parts, produced before
   * and in between sub-tasks.  Those will be stored in the OUTPUT structs
   * of those sub-tasks but have been allocated in (their parent's) POOL.
   *
   * This flag indicates if such partial results exist.  POOL must not be
   * destroyed in that case, until all sub-tasks' outputs have been handled.
   */
  svn_boolean_t has_partial_results;

  /* Pool used to allocate this structure as well as the contents of OUTPUT
   * and PRIOR_PARENT_OUTPUT in any immediate sub-task. */
  apr_pool_t *pool;

} results_t;

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
   * (PARENT==NULL) == (this==ROOT->TASK).
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
   * processing has at least been started.
   * 
   * If this==FIRST_READY, this task itself waits for being proccessed.
   * In that case, there can't be any sub-tasks.
   */
  svn_task__t *first_ready;

  /* The first immidiate sub-task that has not been processed.  If this is
   * NULL, they might still be unprocessed tasks deeper down the tree.
   *
   * Use this to efficiently find unprocessed tasks high up in the tree.
   */
  svn_task__t *first_unprocessed;

  /* Task state. */

  /* The callbacks to use. Never NULL. */
  callbacks_t *callbacks;

  /* Process baton to pass into CALLBACKS->PROCESS_FUNC. */
  void *process_baton;

  /* Pool used to allocate the PROCESS_BATON. 
   * Sub-pool of ROOT->PROCESS_POOL.
   *
   * NULL, iff processing of this task has completed (sub-tasks may still
   * need processing).
   */
  apr_pool_t *process_pool;

  /* The processing results.
   *
   * Will be NULL before processing and may be NULL afterwards, if all
   * fields would be NULL.
   */
  results_t *results;
};


/* Adding tasks to the tree. */

/* Return the index of the first immediate sub-task of TASK with a ready
 * sub-task in its respective sub-tree.  TASK must have at least one proper
 * sub-task.
 */
static apr_size_t first_ready_sub_task_idx(const svn_task__t *task)
{
  svn_task__t *sub_task = task->first_ready;

  /* Don't use this function if there is no ready sub-task. */
  assert(sub_task);
  assert(sub_task != task);

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

  if (task->parent->first_unprocessed == NULL)
    task->parent->first_unprocessed = task;

  /* Test invariants for new tasks.
   *
   * We have to do it while still holding task tree mutex; background
   * processing of this task may already have started otherwise. */
  assert(task->parent != NULL);
  assert(task->first_sub == NULL);
  assert(task->last_sub == NULL);
  assert(task->next == NULL);
  assert(task->first_ready == task);
  assert(task->first_unprocessed == NULL);
  assert(task->callbacks != NULL);
  assert(task->process_pool != NULL);

  return SVN_NO_ERROR;
}

/* If TASK has no RESULTS sub-structure, add one.  Return the RESULTS struct.
 *
 * In multi-threaded environments, calls to this must be serialized with
 * root_t changes. */
static results_t *ensure_results(svn_task__t *task)
{
  if (!task->results)
    {
      apr_pool_t * results_pool = svn_pool_create(task->root->results_pool);
      task->results = apr_pcalloc(results_pool, sizeof(*task->results));
      task->results->pool = results_pool;
    }

  return task->results;
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

  /* The root node has its own special construction code and does not use
   * this function.  So, here we will always have a parent. */
  assert(parent != NULL);

  /* Catch construction snafus early in the process. */
  assert(callbacks != NULL);

  SVN_MUTEX__WITH_LOCK(parent->root->task_alloc_mutex,
                       alloc_task(&new_task, parent->root->task_pool));

  new_task->root = parent->root;
  new_task->process_baton = process_baton;
  new_task->process_pool = process_pool;

  new_task->parent = parent;
  if (partial_output && parent->callbacks->output_func)
    {
      ensure_results(parent)->has_partial_results = TRUE;
      ensure_results(new_task)->prior_parent_output = partial_output;
    }

  /* The new task will be ready for execution once we link it up in PARENT. */
  new_task->first_ready = new_task;
  new_task->callbacks = callbacks;

  SVN_MUTEX__WITH_LOCK(new_task->root->mutex, link_new_task(new_task));

  /* Wake up all waiting worker threads:  There is work to do.
   * If there is not enough work for all, some will go back to sleep.
   *
   * In single-threaded execution, no signalling is needed. */
  if (new_task->root->worker_wakeup)
    SVN_ERR(svn_thread_cond__broadcast(new_task->root->worker_wakeup));

  return SVN_NO_ERROR;
}

svn_error_t *svn_task__add(
  svn_task__t *current,
  apr_pool_t *process_pool,
  void *partial_output,
  svn_task__process_func_t process_func,
  void *process_baton,
  svn_task__output_func_t output_func,
  void *output_baton)
{
  callbacks_t *callbacks;
  SVN_MUTEX__WITH_LOCK(current->root->task_alloc_mutex,
                       alloc_callbacks(&callbacks, current->root->task_pool));

  callbacks->process_func = process_func;
  callbacks->output_func = output_func;
  callbacks->output_baton = output_baton;

  return svn_error_trace(add_task(current, process_pool, partial_output,
                                  callbacks, process_baton));
}

svn_error_t* svn_task__add_similar(
  svn_task__t* current,
  apr_pool_t *process_pool,
  void* partial_output,
  void* process_baton)
{
  return svn_error_trace(add_task(current, process_pool, partial_output,
                                  current->callbacks, process_baton));
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
 */
static void clear_errors(svn_task__t *task)
{
  svn_task__t *sub_task;
  for (sub_task = task->first_sub; sub_task; sub_task = sub_task->next)
    clear_errors(sub_task);

  if (task->results)
    svn_error_clear(task->results->error);
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

/* Utility function that follows the chain of siblings and returns the
 * first unprocessed task.
 * 
 * Returns TASK if TASK is unprocessed.  Returns NULL if all direct sub-
 * tasks of TASK->PARENT are already being processed or have been completed.
 */
static svn_task__t *next_unprocessed(svn_task__t *task)
{
  for (; task; task = task->next)
    if (task->first_ready == task)
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

  /* Update FIRST_PROCESSED as well.  Since this points only from parent to
   * some immediate sub-task, no bubble-up action is required here. */
  if (task->parent && task->parent->first_unprocessed == task)
    task->parent->first_unprocessed = next_unprocessed(task->next);
}


/* Task processing and outputting results */

/* Return TRUE if there are signs that another worker thread is working on
 * the sub-tree of TASK or its next sibling.  Detection does not need to be
 * perfect as this is just a hint to the scheduling strategy.  See
 * set_processed_and_pick() for details.
 *
 * This function must be called with TASK->ROOT->MUTEX acquired.
 */
static svn_boolean_t is_contented(const svn_task__t *task)
{
  /* Assuming TASK has just been processed, the first sub-task should now
   * be ready for execution.  Having no sub-tasks is also o.k.  If both
   * pointers differ, some other worker already picked up a sub-task. */
  if (task->first_sub != task->first_ready)
    return TRUE;

  /* If this whole sub-tree has been completed, check whether we can
   * continue with the next sibling.  If that is already being processed,
   * we would "step on somebody else's toes". */
  if (   task->first_ready == NULL
      && task->next
      && task->next->first_ready == task->next)
    return TRUE;

  /* No signs of a clash found. */
  return FALSE;
}

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
  return (task->process_pool == NULL);
}

/* Mark TASK as "processing completed" and pick a *NEXT_TASK to continue
 * and mark it as "being processed".  If no good candidate has been found,
 * set *NEXT_TASK to NULL.
 *
 * The heuristics in here are crucial for an efficient parallel traversal
 * of deep, unbalanced and growing trees.  In their sequential form, many
 * of the processing functions use parent context information to cache data
 * and speed up processing in general.  Processing tasks in tree order
 * takes full advantage of this.
 *
 * However, having N workers serving the same sub-tree, that advantage gets
 * cut down propertionally and may turn into pure overhead.  Ideally, we
 * want workers to be on distant sub-trees where they can process many tasks
 * before stepping onto a different worker's "turf".  Whenever the latter
 * happends, we crawl up the tree to find the most distant (=most high up)
 * unprocessed task in the tree.  If that fails too, return NULL here and
 * continue with the "canonical" next_task().
 *
 * Please note that we are free to execute tasks in *any* order and this
 * here is just about being efficient.
 *
 * This function must be called with TASK->ROOT->MUTEX acquired.
 */
static svn_error_t *set_processed_and_pick(svn_task__t **next_task,
                                           svn_task__t *task)
{
  set_processed(task);

  /* Are we still alone in our sub-tree? */
  if (is_contented(task))
    {
      /* Nope.
       * Maybe there is some untouched sub-tree under one of our parents.
       * If so, find the one the highest up in the tree.
       *
       * Note that we may not find any despite some "cousin" being a good
       * option.  This is because we only walk up the ancestry line and
       * check for immediate children there. */
      while (task->parent && task->parent->first_unprocessed)
        task = task->parent;

      task = task->first_unprocessed;
    }
  else
    {
      /* Probably yes.
       * Just pick the next task, continue at the parent as needed. */
      while (task->first_ready == NULL && task->parent)
        task = task->parent;

      task = task->first_ready;
    }

  /* Atomic operation:  We need to mark the new task as "in process" while
   * still holding the mutex. */
  if (task)
    unready_task(task);

  *next_task = task;
  return SVN_NO_ERROR;
}

/* Full memory barrier to make sure we see all data changes made by other
 * cores up to this point.  ROOT provides any necessary synchronization
 * objects.
 */
static svn_error_t *enforce_sequential_consistency(root_t *root)
{
  /* We only need GCC's "asm volatile" construct but that is not portable. */

  /* Mutexes come with full memory barriers as a side-effect.  But because
   * we use several mutexes for each single task, adding this mutex one,
   * which is even never contested, adds only moderate overhead. */
  SVN_MUTEX__WITH_LOCK(root->memory_barrier_mutex, SVN_NO_ERROR);
  return SVN_NO_ERROR;
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
      results_t *results = ensure_results(task);
      results->error = callbacks->process_func(&results->output, task,
                                               thread_context,
                                               task->process_baton,
                                               cancel_func, cancel_baton,
                                               results->pool, scratch_pool);

      /* If there is no way to output the results, we simply ignore them. */
      if (!callbacks->output_func)
        results->output = NULL;

      /* Anything left that we may want to output? */
      if (   !results->error
          && !results->output
          && !results->prior_parent_output
          && !results->has_partial_results)
        {
          /* Nope.  Release the memory and reset OUTPUT such that
           * output_processed() can quickly skip it. */
          svn_pool_destroy(results->pool);
          task->results = NULL;
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
  results_t *results;
  callbacks_t *callbacks;

  while (current && is_processed(current))
    {
      svn_pool_clear(iterpool);

      /* When tasks are executed in background threads, we do have a
       * potential memory ordering issue:  We may see a mix of old and new
       * state in CURRENT, i.e. the "is processed" state (new) but without
       * the updates to the other fields and sub-structures.
       *
       * The worker threads acquire a mutex before setting the "is processed"
       * state and those act as full memory barriers, i.e. commit all prior
       * changes to memory.  On the hardware level, this is enough to make
       * sure the we read the updated data once we saw the state change.
       *
       * The compiler, however, does not know that this is (potentially)
       * threaded code.  So, VERY technically, it is allowed to reorder
       * operations and read fields of CURRENT *before* reading the state.
       * This would no longer be sequentially consistent.  Prevent that. */
      enforce_sequential_consistency(current->root);

      /* Post-order, i.e. dive into sub-tasks first.
       *
       * Note that the post-order refers to the task ordering and the output
       * plus errors returned by the processing function.  The CURRENT task
       * may have produced additional, partial outputs and attached them to
       * the sub-tasks.  These outputs will be processed with the respective
       * sub-tasks.
       *
       * Also note that the "background" processing for CURRENT has completed
       * and only the output function may add further sub-tasks. */
      if (current->first_sub)
        {
          current = current->first_sub;
          results = current->results;
          callbacks = current->parent->callbacks;

          /* We will handle this sub-task in the next iteration but we
           * may have to process results produced before or in between
           * sub-tasks.  Also note that PRIOR_PARENT_OUTPUT not being NULL
           * implies that OUTPUT_FUNC is also not NULL. */
          if (results && results->prior_parent_output)
            SVN_ERR(callbacks->output_func(
                        current->parent, results->prior_parent_output,
                        callbacks->output_baton,
                        cancel_func, cancel_baton,
                        result_pool, iterpool));
        }
      else
        {
          /* No deeper sub-task.  Process the results from CURRENT. */
          results = current->results;
          if (results)
            {
              /* Return errors.
               * Make sure they don't get cleaned up with the task tree. */
              svn_error_t *err = results->error;
              results->error = SVN_NO_ERROR;
              SVN_ERR(err);

              /* Handle remaining output of the CURRENT task. */
              callbacks = current->callbacks;
              if (results->output)
                SVN_ERR(callbacks->output_func(
                            current, results->output,
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
              SVN_MUTEX__WITH_LOCK(to_delete->root->mutex,
                                   remove_task(to_delete));

              /* We have output all sub-nodes, including all partial results.
               * Therefore, the last used thing allocated in OUTPUT->POOL is
               * OUTPUT itself and it is safe to clean that up. */
              if (results)
                svn_pool_destroy(results->pool);
            }
        }
    }

  svn_pool_destroy(iterpool);
  *task = current;

  return SVN_NO_ERROR;
}


/* Execution models */

/* From ROOT, find the first unprocessed task - in pre-order - mark it as
 * "in process" and return it in *TASK.  If no such task exists, wait for
 * the ROOT->WORKER_WAKEUP condition and retry.
 *
 * If ROOT->TERMINATE is set, return NULL for *TASK.
 *
 * If the main thread is waiting on us to process tasks, this logic will
 * implicitly pick that task.  So, by default, all workers start on those
 * tasks that are immediately useful for the output processing.  Only later
 * will contention detection bounce most of them off to other sub-tasks.
 * However, there are still likely to come back to this once in a while and
 * ensure that progress is made at the beginning of the tree and the output
 * is not delayed much.
 *
 * This function must be called with ROOT->MUTEX acquired.
 */
static svn_error_t *next_task(svn_task__t **task, root_t *root)
{
  while (TRUE)
    {
      /* Spurious wakeups are being handled implicitly
       * (check conditions and go back to sleep). */

      /* Worker thread needs to terminate? */
      if (svn_atomic_read(&root->terminate))
        {
          *task = NULL;
          return SVN_NO_ERROR;
        }

      /* If there are unprocessed tasks, pick the first one. */
      if (root->task->first_ready)
        {
          svn_task__t *current = root->task->first_ready;
          unready_task(current);
          *task = current;

          return SVN_NO_ERROR;
        }
      
      /* No task, no termination.  Wait for one of these to happen. */
      SVN_ERR(svn_thread_cond__wait(root->worker_wakeup, root->mutex));
    }

  return SVN_NO_ERROR;
}

/* Cancellation function to be used within background threads.
 * BATON is the root_t object.
 *
 * This simply checks for cancellation by the forground thread.
 * Normal cancellation is handled by the output function and then simply
 * indicated by flag.
 * 
 * Note that termination due to errors returned by other tasks will also
 * be treated as a cancellation with the respective SVN_ERR_CANCELLED
 * error being returned from the current tasks in all workers.
 */
static svn_error_t *worker_cancelled(void *baton)
{
  root_t *root = baton;
  return svn_atomic_read(&root->terminate)
       ? svn_error_create(SVN_ERR_CANCELLED, NULL, NULL)
       : SVN_NO_ERROR;
}

/* Set the TERMINATE flag in ROOT and make sure all worker threads get the
 * message.  The latter is required to actually terminate all workers once
 * all tasks have been completed, because workers don't terminate themselves
 * unless there is some internal error.
 */
static svn_error_t *send_terminate(root_t *root)
{
  svn_atomic_set(&root->terminate, TRUE);
  return svn_thread_cond__broadcast(root->worker_wakeup);
}

/* Background worker processing any task in ROOT until termination has been
 * signalled in ROOT.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *worker(root_t *root, apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_task__t *task = NULL;

  /* The context may be quite complex, so we use the ITERPOOL to clean up any
   * memory that was used temporarily during context creation. */
  void *thread_context = NULL;
  if (root->context_constructor)
    SVN_ERR(root->context_constructor(&thread_context, root->context_baton,
                                      scratch_pool, iterpool));

  /* Keep processing tasks until termination.
   * If no tasks need processing, sleep until being signalled
   * (new task or termination). */
  while (!svn_atomic_read(&root->terminate))
    {
      svn_pool_clear(iterpool);
      if (!task)
        {
          /* We did not pick a suitable task to continue with.
           *
           * Make sure the output task is not sleeping (we may have processed
           * many tasks in a large sub-tree without telling the forground
           * thread), so it may tell us to continue or terminate. */
          SVN_ERR(svn_thread_cond__signal(root->task_processed));

          /* Pick the next task in pre-order.  If none exists, sleep until
           * woken up. */
          SVN_MUTEX__WITH_LOCK(root->mutex, next_task(&task, root));

          /* None existed, we slept, got woken up and there still was nothing.
           * This implies termination. */
          if (!task)
            break;
        }

      /* Process this TASK and pick a suitable next one, if available. */
      process(task, thread_context, worker_cancelled, root, iterpool);
      SVN_MUTEX__WITH_LOCK(root->mutex,
                           set_processed_and_pick(&task, task));
    }

  /* Cleanup. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* The plain APR thread around the worker function.
 * DATA is the root_t object to work on. */
static void * APR_THREAD_FUNC
worker_thread(apr_thread_t *thread, void *data)
{
  /* Each thread uses a separate single-threaded pool tree for minimum overhead
   */
  apr_pool_t *pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  apr_status_t result = APR_SUCCESS;
  svn_error_t *err = worker(data, pool);
  if (err)
    {
      result = err->apr_err;
      svn_error_clear(err);
    }
  
  svn_pool_destroy(pool);

  /* End thread explicitly to prevent APR_INCOMPLETE return codes in
     apr_thread_join(). */
  apr_thread_exit(thread, result);
  return NULL;
}

/* If TASK has not been processed, yet, wait for it.  Before waiting for
 * the "task processed" signal, start a new worker thread, allocated in a
 * THREAD_SAFE_POOL, and add it to the array of THREADS.
 * 
 * So, everytime we run out of processing results, we add a new worker.
 * This results in a slightly delayed spawning of new threads.  The total
 * number of worker threads is limited to THREAD_COUNT.
 *
 * This function must be called with TASK->ROOT->MUTEX acquired.
 */
static svn_error_t *wait_for_outputting_state(
  svn_task__t *task,
  apr_int32_t thread_count,
  apr_array_header_t *threads,
  apr_pool_t *thread_safe_pool)
{
  root_t* root = task->root;
  while (TRUE)
    {
      if (is_processed(task))
        return SVN_NO_ERROR;

      /* Maybe spawn another worker thread because there are waiting tasks.
       */
      if (thread_count > threads->nelts)
        {
          apr_thread_t *thread;
          apr_status_t status = apr_thread_create(&thread, NULL,
                                                  worker_thread,
                                                  root,
                                                  thread_safe_pool);
          if (status)
            return svn_error_wrap_apr(status,
                                      "Creating worker thread failed");

          APR_ARRAY_PUSH(threads, apr_thread_t *) = thread;
        }

      /* Efficiently wait for tasks to (maybe) be completed. */
      SVN_ERR(svn_thread_cond__wait(root->task_processed, root->mutex));
    }

  return SVN_NO_ERROR;
}

/* Run the (root) TASK to completion, including dynamically added sub-tasks.
 * Use up to THREAD_COUNT worker threads for that.
 *
 * Pass CANCEL_FUNC and CANCEL_BATON only into the output function callbacks.
 * Pass the RESULT_POOL into the task output functions and use SCRATCH_POOL
 * for everything else (unless covered by task pools).
 */
static svn_error_t *execute_concurrently(
  svn_task__t *task,
  apr_int32_t thread_count,
  svn_cancel_func_t cancel_func,
  void *cancel_baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  int i;
  svn_task__t *current = task;
  root_t *root = task->root;
  svn_error_t *task_err = SVN_NO_ERROR;
  svn_error_t *sync_err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *threads = apr_array_make(scratch_pool, thread_count,
                                               sizeof(apr_thread_t *));

  /* We need a thread-safe-pool to create the actual thread objects. */
  apr_pool_t *thread_safe_pool = svn_pool_create(root->results_pool);

  /* Main execution loop */ 
  while (current && !task_err)
    {
      svn_pool_clear(iterpool);

      /* Spawns worker thread as needed.
       *
       * Acquiring the mutex also acts as a full memory barrier such that we
       * will see updates to task states.  Since we set_processed() last,
       * all other information relevant to the task will be valid, too. */
      SVN_MUTEX__WITH_LOCK(root->mutex,
                           wait_for_outputting_state(current, thread_count,
                                                     threads,
                                                     thread_safe_pool));

      /* Crawl processed tasks and output results until we exhaust processed
       * tasks. */
      task_err = output_processed(&current,
                                  cancel_func, cancel_baton,
                                  result_pool, iterpool);
    }
  
  /* Tell all worker threads to terminate. */
  SVN_MUTEX__WITH_LOCK(root->mutex, send_terminate(root));

  /* Wait for all threads to terminate. */
  for (i = 0; i < threads->nelts; ++i)
    {
      apr_thread_t *thread = APR_ARRAY_IDX(threads, i, apr_thread_t *);
      apr_status_t retval;
      apr_status_t sync_status = apr_thread_join(&retval, thread);
      
      if (retval != APR_SUCCESS)
        sync_err = svn_error_compose_create(sync_err,
                        svn_error_wrap_apr(retval,
                                          "Worker thread returned error"));
        
      if (sync_status != APR_SUCCESS)
        sync_err = svn_error_compose_create(sync_err,
                        svn_error_wrap_apr(sync_status,
                                          "Worker thread join error"));
    }
  
  /* Explicitly release any (other) error.  Leave pools as they are.
   * This is important in the case of early exists due to error returns.
   *
   * However, don't do it if something went wrong while waiting for worker
   * threads to terminate.  They might still be doing something and might
   * crash at any time.  Doing nothing here might increase our chance for
   * the SYNC_ERR to eventually be reported to the user. */
  if (!sync_err)
    clear_errors(task);
  svn_pool_destroy(iterpool);

  /* Get rid of all remaining tasks. */
  return svn_error_trace(svn_error_compose_create(sync_err, task_err));
}

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

  /* Explicitly release any (other) error.  Leave pools as they are.
   * This is important in the case of early exists due to error returns. */
  clear_errors(task);
  svn_pool_destroy(iterpool);

  /* Get rid of all remaining tasks. */
  return svn_error_trace(task_err);
}


/* Root data structure */

/* Pool cleanup function to make sure we free the root pools (allocators) */
static apr_status_t
root_cleanup(void *baton)
{
  root_t *root = baton;
  svn_pool_destroy(root->task_pool);
  svn_pool_destroy(root->process_pool);
  svn_pool_destroy(root->results_pool);

  return APR_SUCCESS;
}

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

  /* Pick execution model.
   *
   * Note that multi-threading comes with significant overheads and should
   * not be used unless requested. */
#if APR_HAS_THREADS
  svn_boolean_t threaded_execution = thread_count > 1;
#else
  svn_boolean_t threaded_execution = FALSE;
#endif

  /* Allocation on stack is fine as this function will not exit before
   * all task processing has been completed. */
  callbacks_t callbacks;

  /* The mutexes must always be constructed.  But we only need their light-
   * weight versions in single-threaded execution. */
  SVN_ERR(svn_mutex__init(&root->mutex, threaded_execution, scratch_pool));
  SVN_ERR(svn_mutex__init(&root->memory_barrier_mutex, threaded_execution,
                          scratch_pool));
  SVN_ERR(svn_mutex__init(&root->task_alloc_mutex, threaded_execution,
                          scratch_pool));

  /* Inter-thread signalling (condition variables) are only needed when
   * we use worker threads. */
  if (threaded_execution)
    {
      SVN_ERR(svn_thread_cond__create(&root->task_processed, scratch_pool));
      SVN_ERR(svn_thread_cond__create(&root->worker_wakeup, scratch_pool));
    }

  /* Permanently allocating a pool for each task is too expensive.
   * So, we will allocate directly from this pool - which requires
   * serialization of each apr_alloc() call.  Therefore, we don't need
   * the added overhead of allocator serialization here. */
  root->task_pool =
    apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  /* The sub-pools of these will used in a single thread each but created
   * from different worker threads.  Using allocator serialization is good
   * enough for that. */
  root->process_pool =
    apr_allocator_owner_get(svn_pool_create_allocator(threaded_execution));
  root->results_pool =
    apr_allocator_owner_get(svn_pool_create_allocator(threaded_execution));

  /* Be sure to clean the root pools up afterwards. */
  apr_pool_cleanup_register(scratch_pool, root, root_cleanup,
                            apr_pool_cleanup_null);

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
  root->terminate = FALSE;

  /* Go, go, go! */
  if (threaded_execution)
    {
      SVN_ERR(execute_concurrently(root->task, thread_count,
                                   cancel_func, cancel_baton,
                                   result_pool, scratch_pool));
    }
  else
   {
     SVN_ERR(execute_serially(root->task,
                              cancel_func, cancel_baton,
                              result_pool, scratch_pool));
   }    

  return SVN_NO_ERROR;
}
