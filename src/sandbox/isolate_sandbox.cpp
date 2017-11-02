#ifndef _WIN32

#include "isolate_sandbox.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <string>
#define BOOST_FILESYSTEM_NO_DEPRECATED
#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;


isolate_sandbox::isolate_sandbox(std::shared_ptr<sandbox_config> sandbox_config,
	sandbox_limits limits,
	size_t id,
	const std::string &temp_dir,
	std::shared_ptr<spdlog::logger> logger)
	: sandbox_config_(sandbox_config), limits_(limits), logger_(logger), id_(id), isolate_binary_("isolate")
{
	if (sandbox_config == nullptr) {
		throw sandbox_exception("No sandbox configuration provided.");
	}

	if (logger_ == nullptr) {
		logger_ = helpers::create_null_logger();
	}

	// Set backup limit (for killing isolate if it hasn't finished yet)
	max_timeout_ = limits_.wall_time > limits_.cpu_time ? limits_.wall_time : limits_.cpu_time;
	max_timeout_ += 300; // plus 5 minutes (for short tasks)
	max_timeout_ *= 1.2; // 20% time more than necessary (better have some spare time)

	temp_dir_ = (fs::path(temp_dir) / std::to_string(id_)).string();
	try {
		fs::create_directories(temp_dir_);
	} catch (fs::filesystem_error &e) {
		auto message = std::string("Failed to create directory for isolate meta file. Error: ") + e.what();
		logger_->warn(message);
		throw sandbox_exception(message);
	}

	meta_file_ = (fs::path(temp_dir_) / "meta.log").string();

	try {
		isolate_init();
	} catch (...) {
		fs::remove_all(temp_dir_);
		throw;
	}
}

isolate_sandbox::~isolate_sandbox()
{
	try {
		isolate_cleanup();
		fs::remove_all(temp_dir_);
	} catch (...) {
		// We don't care if this failed. We can't fix it either. Just don't throw an exception in destructor.
	}
}

sandbox_results isolate_sandbox::run(const std::string &binary, const std::vector<std::string> &arguments)
{
	isolate_run(binary, arguments);
	return process_meta_file();
}

void isolate_sandbox::isolate_init()
{
	int fd[2];
	pid_t childpid;

	logger_->debug("Initializing isolate...");

	// Create unnamend pipe
	if (pipe(fd) == -1) {
		auto message = std::string("Cannot create pipe: ") + strerror(errno);
		logger_->warn(message);
		throw sandbox_exception(message);
	}

	childpid = fork();

	switch (childpid) {
	case -1: {
		auto message = std::string("Fork failed: ") + strerror(errno);
		logger_->warn(message);
		throw sandbox_exception(message);
	} break;
	case 0: isolate_init_child(fd[0], fd[1]); break;
	default:
		//---Parent---
		// Close up input side of pipe
		close(fd[1]);

		char buf[256];
		int ret;
		while ((ret = read(fd[0], (void *) buf, 256)) > 0) {
			if (buf[ret - 1] == '\n') {
				buf[ret - 1] = '\0';
			}
			sandboxed_dir_ += std::string(buf);
		}
		if (ret == -1) {
			auto message = "Read from pipe error.";
			logger_->warn(message);
			throw sandbox_exception(message);
		}

		int status;
		waitpid(childpid, &status, 0);
		if (WEXITSTATUS(status) != 0) {
			auto message = "Isolate init error. Return value: " + std::to_string(WEXITSTATUS(status));
			logger_->warn(message);
			throw sandbox_exception(message);
		}
		logger_->debug("Isolate initialized in {}", sandboxed_dir_);
		close(fd[0]);
		break;
	}
}

void isolate_sandbox::isolate_init_child(int fd_0, int fd_1)
{
	// Close up output side of pipe
	close(fd_0);

	// Close stdout, duplicate the input side of pipe to stdout
	dup2(fd_1, 1);

	// Redirect stderr to /dev/null file
	int devnull;
	devnull = open("/dev/null", O_WRONLY);
	if (devnull == -1) {
		auto message = "Cannot open /dev/null file for writing.";
		logger_->warn(message);
		throw sandbox_exception(message);
	}
	dup2(devnull, 2);

	// Exec isolate init command
	const char *args[5];

	args[0] = isolate_binary_.c_str();
	args[1] = "--cg";
	args[2] = ("--box-id=" + std::to_string(id_)).c_str();
	args[3] = "--init";
	args[4] = NULL;

	// const_cast is ugly, but this is working with C code - execv does not modify its arguments
	execvp(isolate_binary_.c_str(), const_cast<char **>(args));

	// Never reached
	{
		auto message = std::string("Exec returned to child: ") + strerror(errno);
		logger_->warn(message);
		throw sandbox_exception(message);
	}
}

void isolate_sandbox::isolate_cleanup()
{
	pid_t childpid;

	logger_->debug("Cleaning up isolate...");

	childpid = fork();

	switch (childpid) {
	case -1: {
		auto message = std::string("Fork failed: ") + strerror(errno);
		logger_->warn(message);
		throw sandbox_exception(message);
	} break;
	case 0:
		//---Child---
		// Redirect stderr to /dev/null file
		int devnull;
		devnull = open("/dev/null", O_WRONLY);
		if (devnull == -1) {
			auto message = "Cannot open /dev/null file for writing.";
			logger_->warn(message);
			throw sandbox_exception(message);
		}
		dup2(devnull, 2);

		// Exec isolate cleanup command
		const char *args[5];
		args[0] = isolate_binary_.c_str();
		args[1] = "--cg";
		args[2] = ("--box-id=" + std::to_string(id_)).c_str();
		args[3] = "--cleanup";
		args[4] = NULL;
		// const_cast is ugly, but this is working with C code - execv does not modify its arguments
		execvp(isolate_binary_.c_str(), const_cast<char **>(args));

		// Never reached
		{
			auto message = std::string("Exec returned to child: ") + strerror(errno);
			logger_->warn(message);
			throw sandbox_exception(message);
		}
		break;
	default:
		//---Parent---
		int status;
		waitpid(childpid, &status, 0);
		if (WEXITSTATUS(status) != 0) {
			auto message = "Isolate cleanup error. Return value: " + std::to_string(WEXITSTATUS(status));
			logger_->warn(message);
			throw sandbox_exception(message);
		}
		logger_->debug("Isolate box {} cleaned up.", id_);
		break;
	}
}

void isolate_sandbox::isolate_run(const std::string &binary, const std::vector<std::string> &arguments)
{
	pid_t childpid;

	logger_->debug("Running isolate...");
	logger_->debug("Running the first fork");

	childpid = fork();

	switch (childpid) {
	case -1: {
		auto message = std::string("Fork failed: ") + strerror(errno);
		logger_->warn(message);
		throw sandbox_exception(message);
	} break;
	case 0: {
		//---Child---
		logger_->debug("Returned from the first fork as child");

		// Redirect stderr and stdout to /dev/null file
		int devnull;
		devnull = open("/dev/null", O_WRONLY);
		if (devnull == -1) {
			auto message = "Cannot open /dev/null file for writing.";
			logger_->warn(message);
			throw sandbox_exception(message);
		}
		dup2(devnull, 0); // Don't allow process inside isolate to read from current standard input
		dup2(devnull, 1);
		dup2(devnull, 2);

		auto args = isolate_run_args(binary, arguments);
		execvp(isolate_binary_.c_str(), args);

		// Never reached
		auto message = std::string("Exec returned to child: ") + strerror(errno);
		logger_->warn(message);
		throw sandbox_exception(message);
	} break;
	default: {
		//---Parent---
		/* Spawn a control process, that will wait given timeout and then kills isolate process.
		 * When a isolate process finishes before the timeout, parent thread kills control process
		 * and calls waitpid() to remove zombie from system.
		 */

		logger_->debug("Returned from the first fork as parent");

		pid_t controlpid;
		logger_->debug("Running the second fork");
		controlpid = fork();
		switch (controlpid) {
		case -1: {
			auto message = std::string("Fork failed: ") + strerror(errno);
			logger_->warn(message);
			throw sandbox_exception(message);
		} break;
		case 0:
			// Child---
			{
				logger_->debug("Returned from the second fork as child (control process)");

				int remaining = max_timeout_;
				// Sleep can be interrupted by signal, so make sure to sleep whole time
				while (remaining > 0) {
					remaining = sleep(remaining);
				}
				kill(childpid, SIGKILL);
			}
			break;
		default:
			// Parent---
			logger_->debug("Returned from the second fork as parent");

			int status;
			// Wait for isolate process. Waitpid returns no much longer than timeout if not earlier.
			waitpid(childpid, &status, 0);
			// Kill control process. If it already exits, nothing will be done
			kill(controlpid, SIGKILL);
			// Remove zombie from controll process.
			waitpid(controlpid, NULL, 0);

			// isolate was killed
			if (WIFSIGNALED(status)) {
				auto message =
					"Isolate process was killed by signal " + std::to_string(WTERMSIG(status)) + " due to timeout.";
				logger_->warn(message);
				throw sandbox_exception(message);
			}
			// isolate exited, but with return value signify internal error
			if (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 1) {
				auto message = "Isolate run into internal error. Return value: " + std::to_string(WEXITSTATUS(status));
				logger_->warn(message);
				throw sandbox_exception(message);
			}
			logger_->debug("Isolate box {} ran successfully.", id_);
			break;
		}
	} break;
	}
}

char **isolate_sandbox::isolate_run_args(const std::string &binary, const std::vector<std::string> &arguments)
{
	std::vector<std::string> vargs;

	vargs.push_back(isolate_binary_); // First argument must be binary name
	vargs.push_back("--cg");
	vargs.push_back("--cg-timing");
	vargs.push_back("--box-id=" + std::to_string(id_));

	vargs.push_back("--cg-mem=" + std::to_string(limits_.memory_usage + limits_.extra_memory));
	// vargs.push_back("--mem=" + std::to_string(limits_.memory_usage));
	vargs.push_back("--time=" + std::to_string(limits_.cpu_time));
	vargs.push_back("--wall-time=" + std::to_string(limits_.wall_time));
	vargs.push_back("--extra-time=" + std::to_string(limits_.extra_time));
	if (limits_.stack_size != 0) {
		vargs.push_back("--stack=" + std::to_string(limits_.stack_size));
	}
	if (limits_.files_size != 0) {
		vargs.push_back("--fsize=" + std::to_string(limits_.files_size));
	}
	// Calculate number of required blocks - total number of bytes divided by block size (defined in sys/mount.h)
	auto disk_size_blocks = (limits_.disk_size * 1024) / BLOCK_SIZE;
	vargs.push_back("--quota=" + std::to_string(disk_size_blocks) + "," + std::to_string(limits_.disk_files));
	if (!sandbox_config_->std_input.empty()) {
		vargs.push_back("--stdin=" + sandbox_config_->std_input);
	}
	if (!sandbox_config_->std_output.empty()) {
		vargs.push_back("--stdout=" + sandbox_config_->std_output);
	}
	if (!sandbox_config_->std_error.empty()) {
		vargs.push_back("--stderr=" + sandbox_config_->std_error);
	}
	if (!sandbox_config_->chdir.empty()) {
		// path is relative to /box inside sandbox ... we want path to be relative to root (/)
		vargs.push_back("--chdir=" + (fs::path("..") / sandbox_config_->chdir).string());
	}
	if (limits_.processes == 0) {
		vargs.push_back("--processes");
	} else {
		vargs.push_back("--processes=" + std::to_string(limits_.processes));
	}
	if (limits_.share_net) {
		vargs.push_back("--share-net");
	}
	for (auto &i : limits_.environ_vars) {
		vargs.push_back("--env=" + i.first + "=" + i.second);
	}
	for (auto &i : limits_.bound_dirs) {
		std::string mode = "";
		auto flags = std::get<2>(i);
		if (flags & sandbox_limits::dir_perm::RW) {
			mode += ":rw";
		}
		if (flags & sandbox_limits::dir_perm::NOEXEC) {
			mode += ":noexec";
		}
		if (flags & sandbox_limits::dir_perm::FS) {
			mode += ":fs";
		}
		if (flags & sandbox_limits::dir_perm::MAYBE) {
			mode += ":maybe";
		}
		if (flags & sandbox_limits::dir_perm::DEV) {
			mode += ":dev";
		}
		vargs.push_back(std::string("--dir=") + std::get<1>(i) + "=" + std::get<0>(i) + mode);
	}
	// Bind /etc/alternatives directory if exists
	vargs.push_back("--dir=etc/alternatives=/etc/alternatives:maybe");

	vargs.push_back("--meta=" + meta_file_);

	vargs.push_back("--run");
	vargs.push_back("--");
	vargs.push_back(binary);
	for (auto &i : arguments) {
		vargs.push_back(i);
	}

	// Convert string to char ** for execv call
	char **c_args = new char *[vargs.size() + 1];
	int i = 0;
	for (auto &it : vargs) {
		c_args[i++] = strdup(it.c_str());
		logger_->debug("  {}", it);
	}
	c_args[i] = NULL;
	return c_args;
}

sandbox_results isolate_sandbox::process_meta_file()
{
	sandbox_results results;

	std::ifstream meta_stream;
	meta_stream.open(meta_file_);
	if (meta_stream.is_open()) {
		std::string line;
		while (std::getline(meta_stream, line)) {
			size_t pos = line.find(':');
			size_t value_size = line.size() - (pos + 1);
			auto first = line.substr(0, pos);
			auto second = line.substr(pos + 1, value_size);
			if (first == "time") {
				results.time = std::stof(second);
			} else if (first == "time-wall") {
				results.wall_time = std::stof(second);
			} else if (first == "killed") {
				results.killed = true;
			} else if (first == "status") {
				if (second == "RE") {
					results.status = isolate_status::RE;
				} else if (second == "SG") {
					results.status = isolate_status::SG;
				} else if (second == "TO") {
					results.status = isolate_status::TO;
				} else if (second == "XX") {
					results.status = isolate_status::XX;
				}
			} else if (first == "message") {
				results.message = second;
			} else if (first == "exitsig") {
				results.exitsig = std::stoi(second);
			} else if (first == "exitcode") {
				results.exitcode = std::stoi(second);
			} else if (first == "cg-mem") {
				results.memory = std::stoul(second);
			} else if (first == "max-rss") {
				results.max_rss = std::stoul(second);
			}
		}
		return results;
	} else {
		auto message = "Cannot open " + meta_file_ + " for reading.";
		logger_->warn(message);
		throw sandbox_exception(message);
	}
}

#endif
