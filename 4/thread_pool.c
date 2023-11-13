#define _POSIX_C_SOURCE 200809
#include "thread_pool.h"
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

// Structure for Thread Task
struct thread_task
{
	thread_task_f function; // Function to be executed by the task
	void *arg;				// Argument to the task function
	void *result;			// Result from the task function

	pthread_mutex_t *mutex; // Mutex for task synchronization
	pthread_cond_t *cond;	// Condition variable for task synchronization

	enum
	{
		TASK_STATE_DETACHED,
		TASK_STATE_CREATED,
		TASK_STATE_PUSHED,
		TASK_STATE_RUNNING,
		TASK_STATE_FINISHED,
		TASK_STATE_JOINED
	} state; // Current state of the task
};

// Structure for Thread Pool
struct thread_pool
{
	pthread_t *threads; // Array of worker threads

	int threads_max; // Maximum number of threads
	int threads_cnt; // Current number of threads
	int threads_run; // Number of threads currently running

	pthread_mutex_t *mutex; // Mutex for thread pool synchronization
	pthread_cond_t *cond;	// Condition variable for thread pool synchronization

	struct thread_task **tasks_queue; // Queue of tasks
	int tasks_cnt;					  // Current number of tasks in the queue

	bool delete; // Flag to indicate if the pool is being deleted
};

void *worker_function(void *arg)
{
	struct thread_pool *pool = (struct thread_pool *)arg;

	// Main loop that keeps the worker thread running
	while (1)
	{
		// Lock the pool's mutex to safely access shared resources
		pthread_mutex_lock(pool->mutex);

		// Wait for tasks to be available or for the pool to be deleted
		while (!pool->delete &&pool->tasks_cnt == 0)
		{
			pthread_cond_wait(pool->cond, pool->mutex);
		}

		// Exit the loop and clean up if the pool is being deleted
		if (pool->delete)
		{
			pthread_mutex_unlock(pool->mutex);
			return NULL;
		}

		// Fetch the next task from the queue
		pool->tasks_cnt--;
		struct thread_task *task = pool->tasks_queue[pool->tasks_cnt];
		pool->threads_run++;
		// Lock the task's mutex before modifying it
		pthread_mutex_lock(task->mutex);
		// Unlock the pool's mutex as the task is now secured
		pthread_mutex_unlock(pool->mutex);

		// Set the task's state to running if it's not detached
		if (task->state != TASK_STATE_DETACHED)
		{
			task->state = TASK_STATE_RUNNING;
		}

		thread_task_f worker_f = task->function;
		void *worker_arg = task->arg;
		// Unlock the task's mutex as it's now safe to access it
		pthread_mutex_unlock(task->mutex);
		// Execute the task
		void *result = worker_f(worker_arg);

		// Lock the task's mutex again to update its state and result
		pthread_mutex_lock(task->mutex);
		// Lock the pool's mutex to update the pool's state
		pthread_mutex_lock(pool->mutex);
		task->result = result;
		// Mark the task as finished and signal any waiting threads
		if (task->state != TASK_STATE_DETACHED)
		{
			task->state = TASK_STATE_FINISHED;
			pthread_cond_signal(task->cond);
		}

		// Handle task cleanup if it's detached
		if (task->state == TASK_STATE_DETACHED)
		{
			task->state = TASK_STATE_JOINED;
			pthread_mutex_unlock(task->mutex);
			thread_task_delete(task);
		}
		else
		{
			pthread_mutex_unlock(task->mutex);
		}

		// Update the pool's running thread count and unlock its mutex
		pool->threads_run--;
		pthread_mutex_unlock(pool->mutex);
	}

	return NULL;
}

int thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	// Check if the specified thread count is within the valid range
	if (max_thread_count > TPOOL_MAX_THREADS || max_thread_count <= 0)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	// Allocate memory for the thread pool structure
	struct thread_pool *new_pool = malloc(sizeof(struct thread_pool));

	// Initialize the thread pool structure
	new_pool->threads = malloc(TPOOL_MAX_THREADS * sizeof(pthread_t));
	new_pool->threads_max = max_thread_count;
	new_pool->threads_cnt = 0;
	new_pool->threads_run = 0;

	new_pool->mutex = malloc(sizeof(pthread_mutex_t));
	new_pool->cond = malloc(sizeof(pthread_cond_t));

	new_pool->tasks_queue = malloc(TPOOL_MAX_TASKS * sizeof(struct thread_task *));
	new_pool->tasks_cnt = 0;

	new_pool->delete = false;

	// Initialize mutex and condition variables
	pthread_mutex_init(new_pool->mutex, NULL);
	pthread_cond_init(new_pool->cond, NULL);

	*pool = new_pool;
	return 0;
}

int thread_pool_thread_count(const struct thread_pool *pool)
{
	// Lock the pool mutex to get a consistent thread count
	pthread_mutex_lock(pool->mutex);
	int count = pool->threads_cnt;
	pthread_mutex_unlock(pool->mutex);
	return count;
}

int thread_pool_delete(struct thread_pool *pool)
{
	pthread_mutex_lock(pool->mutex);

	// Check if there are unfinished tasks or running threads
	if (pool->tasks_cnt || pool->threads_run)
	{
		pthread_mutex_unlock(pool->mutex);
		return TPOOL_ERR_HAS_TASKS;
	}

	// Set the delete flag and wake all worker threads
	pool->delete = true;
	pthread_cond_broadcast(pool->cond);

	pthread_mutex_unlock(pool->mutex);

	// Wait for all threads to finish
	for (int i = 0; i < pool->threads_cnt; ++i)
	{
		pthread_join(pool->threads[i], NULL);
	}

	// Free the allocated resources
	free(pool->tasks_queue);
	free(pool->threads);
	pthread_mutex_destroy(pool->mutex);
	free(pool->mutex);
	pthread_cond_destroy(pool->cond);
	free(pool->cond);
	free(pool);

	return 0;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	pthread_mutex_lock(pool->mutex);

	// Check if the task queue is full
	if (pool->tasks_cnt >= TPOOL_MAX_TASKS)
	{
		pthread_mutex_unlock(pool->mutex);
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	// Add the task to the queue and update its state
	pthread_mutex_lock(task->mutex);
	pool->tasks_queue[pool->tasks_cnt++] = task;
	task->state = TASK_STATE_PUSHED;
	pthread_mutex_unlock(task->mutex);

	// Create a new thread if necessary and possible
	if (pool->threads_cnt < pool->threads_max && pool->threads_run == pool->threads_cnt)
	{
		pthread_create(&(pool->threads[pool->threads_cnt++]), NULL, worker_function, pool);
	}

	pthread_mutex_unlock(pool->mutex);
	pthread_cond_signal(pool->cond); // Signal a waiting worker thread

	return 0;
}

int thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	// Allocate and initialize a new thread task
	*task = malloc(sizeof(struct thread_task));

	(*task)->mutex = malloc(sizeof(pthread_mutex_t));
	(*task)->cond = malloc(sizeof(pthread_cond_t));

	pthread_mutex_init((*task)->mutex, NULL);
	pthread_cond_init((*task)->cond, NULL);

	// Set the task function and argument
	(*task)->function = function;
	(*task)->arg = arg;
	(*task)->state = TASK_STATE_CREATED;

	return 0;
}

bool thread_task_is_finished(const struct thread_task *task) // Not used
{
	if (!task)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}
	return task->state == TASK_STATE_FINISHED;
}

bool thread_task_is_running(const struct thread_task *task) // Not used
{
	if (!task)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}
	return task->state == TASK_STATE_RUNNING;
}

int thread_task_join(struct thread_task *task, void **result)
{
	if (!task)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	pthread_mutex_lock(task->mutex);

	if (task->state == TASK_STATE_DETACHED)
	{
		pthread_mutex_unlock(task->mutex);
		return -1; // there is no reasonable error for it in the header file
	}

	if (task->state == TASK_STATE_CREATED)
	{
		pthread_mutex_unlock(task->mutex);
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	while (task->state != TASK_STATE_FINISHED)
	{
		pthread_cond_wait(task->cond, task->mutex);
	}

	*result = task->result;
	task->state = TASK_STATE_JOINED;

	pthread_mutex_unlock(task->mutex);
	return 0;
}

#ifdef NEED_TIMED_JOIN
int thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	if (!task)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	if (timeout <= 0)
	{
		return TPOOL_ERR_TIMEOUT;
	}

	pthread_mutex_lock(task->mutex);

	// Check the state of the task
	if (task->state == TASK_STATE_CREATED)
	{
		pthread_mutex_unlock(task->mutex);
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	if (task->state == TASK_STATE_DETACHED)
	{
		pthread_mutex_unlock(task->mutex);
		return -1; // there is no reasonable error for it in the header file
	}

	// Calculate the absolute time for the timeout
	struct timespec timeout_time;
	clock_gettime(CLOCK_REALTIME, &timeout_time);
	long seconds = (long)timeout;
	long nanoseconds = (long)((timeout - seconds) * 1e9);
	timeout_time.tv_sec += seconds;
	timeout_time.tv_nsec += nanoseconds;

	// Handle nanosecond overflow
	if (timeout_time.tv_nsec >= 1e9)
	{
		timeout_time.tv_nsec -= 1e9;
		timeout_time.tv_sec += 1;
	}

	// Wait for the task to finish or the timeout to occur
	while (task->state != TASK_STATE_FINISHED)
	{
		struct timespec current_time;
		clock_gettime(CLOCK_REALTIME, &current_time);
		seconds = current_time.tv_sec - timeout_time.tv_sec;
		nanoseconds = current_time.tv_nsec - timeout_time.tv_nsec;
		if (current_time.tv_sec > timeout_time.tv_sec || (current_time.tv_sec == timeout_time.tv_sec && current_time.tv_nsec >= timeout_time.tv_nsec))
		{
			pthread_mutex_unlock(task->mutex);
			return TPOOL_ERR_TIMEOUT;
		}
		pthread_cond_timedwait(task->cond, task->mutex, &timeout_time);
	}

	// Task is finished; retrieve the result
	*result = task->result;
	task->state = TASK_STATE_JOINED;
	pthread_mutex_unlock(task->mutex);
	return 0;
}
#endif

int thread_task_delete(struct thread_task *task)
{
	if (!task)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	pthread_mutex_lock(task->mutex);
	if (task->state == TASK_STATE_DETACHED)
	{
		pthread_mutex_unlock(task->mutex);
		return -1; // there is no reasonable error for it in the header file
	}

	if (task->state == TASK_STATE_FINISHED || task->state == TASK_STATE_PUSHED || task->state == TASK_STATE_RUNNING)
	{
		pthread_mutex_unlock(task->mutex);
		return TPOOL_ERR_TASK_IN_POOL;
	}

	pthread_mutex_unlock(task->mutex);
	pthread_cond_destroy(task->cond);
	free(task->cond);
	pthread_mutex_destroy(task->mutex);
	free(task->mutex);
	free(task);

	return 0;
}

#ifdef NEED_DETACH
int thread_task_detach(struct thread_task *task)
{
	if (!task)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	pthread_mutex_lock(task->mutex);
	if (task->state == TASK_STATE_DETACHED)
	{
		pthread_mutex_unlock(task->mutex);
		return -1; // there is no reasonable error for it in the header file
	}

	if (task->state == TASK_STATE_CREATED)
	{
		pthread_mutex_unlock(task->mutex);
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	if (task->state == TASK_STATE_FINISHED)
	{
		task->state = TASK_STATE_JOINED;
		pthread_mutex_unlock(task->mutex);
		thread_task_delete(task);
	}
	else
	{
		task->state = TASK_STATE_DETACHED;
		pthread_mutex_unlock(task->mutex);
	}

	return 0;
}
#endif
