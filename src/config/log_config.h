#ifndef RECODEX_WORKER_LOG_CONFIG_H
#define RECODEX_WORKER_LOG_CONFIG_H

#include "spdlog/spdlog.h"
#include <string>

/**
 * Structure which stores all information needed to initialize logger.
 */
struct log_config {
public:
	/** Determines in which directory log will be saved. */
	std::string log_path = "/tmp/recodex/";
	/** Filemane of log file without extension. */
	std::string log_basename = "worker";
	/** Log filename extension. */
	std::string log_suffix = "log";
	/** Level of logging. Log levels are taken from spdlog. */
	std::string log_level = "debug";
	/** File size of one log file. */
	std::size_t log_file_size = 1024 * 1024;
	/** Number of rotations which will be used. */
	std::size_t log_files_count = 3;

	/**
	 * Classical equality operator on log_config structures.
	 * @param second compared structure
	 * @return true if this instance and second has the same variables values
	 */
	bool operator==(const log_config &second) const
	{
		return (log_path == second.log_path && log_basename == second.log_basename && log_suffix == second.log_suffix &&
			log_level == second.log_level && log_file_size == second.log_file_size &&
			log_files_count == second.log_files_count);
	}

	/**
	 * Opposite of equality operator
	 * @param second compared structure
	 * @return true if compared structured has different values
	 */
	bool operator!=(const log_config &second) const
	{
		return !((*this) == second);
	}
};

#endif // RECODEX_WORKER_LOG_CONFIG_H
