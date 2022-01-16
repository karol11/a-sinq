#ifndef _SUNGLE_THREAD_EXECUTOR_H_
#define _SUNGLE_THREAD_EXECUTOR_H_

#include <vector>
#include <functional>

namespace testing {

    /// <summary>
    /// Executes tasks in single thread at the the moment explicitly defined by caller.
    /// Intended primarily for testing purposes.
    /// </summary>
    class single_thread_executor
    {
        std::vector<std::function<void()>> tasks;

    public:
        /// <summary>
        /// Schedules a task for later execution.
        /// </summary>
        void schedule(std::function<void()> task)
        {
            tasks.emplace_back(move(task));
        }

        /// <summary>
        /// Executes all tasks accumulated so far and all tasks scheduled from them.
        /// </summary>
        void execute()
        {
            while (!tasks.empty())
            {
                std::vector<std::function<void()>> current_tasks;
                swap(current_tasks, tasks);
                for (auto& t : current_tasks)
                {
                    t();
                }
            }
        }
    };
}

#endif  // _SUNGLE_THREAD_EXECUTOR_H_
