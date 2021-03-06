#ifndef RECODEX_WORKER_INTERNAL_RENAME_TASK_H
#define RECODEX_WORKER_INTERNAL_RENAME_TASK_H

#include "tasks/task_base.h"


/**
 * Rename one file.
 */
class rename_task : public task_base
{
public:
	/**
	 * Constructor with initialization.
	 * @param id Unique identificator of load order of tasks.
	 * @param task_meta Variable containing further info about task. It's required that
	 * @a cmd_args entry has just 2 arguments - current and new name of the file.
	 * The semantics is same as POSIX rename() function, see
	 * http://pubs.opengroup.org/onlinepubs/000095399/functions/rename.html or
	 * http://www.boost.org/doc/libs/1_59_0_b1/libs/filesystem/doc/reference.html#rename
	 * for more info.
	 * @throws task_exception when no argument provided.
	 */
	rename_task(std::size_t id, std::shared_ptr<task_metadata> task_meta);
	/**
	 * Destructor.
	 */
	~rename_task() override = default;
	/**
	 * Run the action.
	 * @return Evaluation results to be pushed back to frontend.
	 */
	std::shared_ptr<task_results> run() override;
};

#endif // RECODEX_WORKER_INTERNAL_RENAME_TASK_H
