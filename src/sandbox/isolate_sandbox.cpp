#include "isolate_sandbox.h"
#include <unistd.h>
#include <sys/types.h>
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


isolate_sandbox::isolate_sandbox(sandbox_limits limits, size_t id, std::shared_ptr<spdlog::logger> logger) :
	limits_(limits), id_(id), isolate_binary_("/usr/local/bin/isolate")
{
	if (logger != nullptr) {
		logger_ = logger;
	} else {
		//Create logger manually to avoid global registration of logger
		auto sink = std::make_shared<spdlog::sinks::stderr_sink_st>();
		logger_ = std::make_shared<spdlog::logger>("cache_manager_nolog", sink);
		//Set loglevel to 'off' cause no logging
		logger_->set_level(spdlog::level::off);
	}

	auto meta_dir = fs::temp_directory_path() / ("recodex_isolate_" + std::to_string(id_));
	try {
		fs::create_directories(meta_dir);
	} catch (fs::filesystem_error &e) {
		auto message = std::string("Failed to create directory for isolate meta file. Error: ") + e.what();
		logger_->warn() << message;
		throw sandbox_exception(message);
	}

	meta_file_ = (meta_dir / "meta.log").string();

	isolate_init();
}

isolate_sandbox::~isolate_sandbox()
{
	try {
		isolate_cleanup();
		fs::remove_all(fs::temp_directory_path() / ("recodex_isolate_" + std::to_string(id_)));
	} catch (...) {
		//We don't care if this failed. We can't fix it either. Just don't throw an exception in destructor.
	}
}

task_results isolate_sandbox::run(const std::string &binary, const std::vector<std::string> &arguments)
{
	isolate_run(binary, arguments);
	return process_meta_file();
}

void isolate_sandbox::isolate_init()
{
	int fd[2];
	pid_t childpid;

	logger_->debug() << "Initializing isolate...";

	//Create unnamend pipe
	if(pipe(fd) == -1) {
		auto message = std::string("Cannot create pipe: ") + strerror(errno);
		logger_->warn() << message;
		throw sandbox_exception(message);
	}

	childpid = fork();

	switch (childpid) {
	case -1:
		{
			auto message = std::string("Fork failed: ") + strerror(errno);
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		break;
	case 0:
		//---Child---
		//Close up output side of pipe
		close(fd[0]);

		//Close stdout, duplicate the input side of pipe to stdout
		dup2(fd[1], 1);

		//Redirect stderr to /dev/null file
		int devnull;
		devnull = open("/dev/null", O_WRONLY);
		if (devnull == -1) {
			auto message = "Cannot open /dev/null file for writing.";
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		dup2(devnull, 2);

		//Exec isolate init command
		const char *args[5];
		args[0] = isolate_binary_.c_str();
		args[1] = "--cg";
		args[2] = ("--box-id=" + std::to_string(id_)).c_str();
		args[3] = "--init";
		args[4] = NULL;
		//const_cast is ugly, but this is working with C code - execv does not modify its arguments
		execvp(isolate_binary_.c_str(), const_cast<char **>(args));

		//Never reached
		{
			auto message = std::string("Exec returned to child: ") + strerror(errno);
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		break;
	default:
		//---Parent---
		//Close up input side of pipe
		close(fd[1]);

		char buf[256];
		int ret;
		while ((ret = read(fd[0], (void *)buf, 256)) > 0) {
			if (buf[ret - 1] == '\n') {
				buf[ret - 1] = '\0';
			}
			sandboxed_dir_ += std::string(buf);
		}
		if (ret == -1) {
			auto message = "Read from pipe error.";
			logger_->warn() << message;
			throw sandbox_exception(message);
		}

		int status;
		waitpid(childpid, &status, 0);
		if(WEXITSTATUS(status) != 0) {
			auto message = "Isolate init error. Return value: " + std::to_string(WEXITSTATUS(status));
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		logger_->debug() << "Isolate initialized in " << sandboxed_dir_;
		break;
	}
}

void isolate_sandbox::isolate_cleanup()
{
	pid_t childpid;

	logger_->debug() << "Cleaning up isolate...";

	childpid = fork();

	switch (childpid) {
	case -1:
		{
			auto message = std::string("Fork failed: ") + strerror(errno);
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		break;
	case 0:
		//---Child---
		//Redirect stderr to /dev/null file
		int devnull;
		devnull = open("/dev/null", O_WRONLY);
		if (devnull == -1) {
			auto message = "Cannot open /dev/null file for writing.";
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		dup2(devnull, 2);

		//Exec isolate cleanup command
		const char *args[5];
		args[0] = isolate_binary_.c_str();
		args[1] = "--cg";
		args[2] = ("--box-id=" + std::to_string(id_)).c_str();
		args[3] = "--cleanup";
		args[4] = NULL;
		//const_cast is ugly, but this is working with C code - execv does not modify its arguments
		execvp(isolate_binary_.c_str(), const_cast<char **>(args));

		//Never reached
		{
			auto message = std::string("Exec returned to child: ") + strerror(errno);
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		break;
	default:
		//---Parent---
		int status;
		waitpid(childpid, &status, 0);
		if (WEXITSTATUS(status) != 0) {
			auto message = "Isolate cleanup error. Return value: " + std::to_string(WEXITSTATUS(status));
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		logger_->debug() << "Isolate box " << id_ << " cleaned up.";
		break;
	}
}

void isolate_sandbox::isolate_run(const std::string &binary, const std::vector<std::string> &arguments)
{
	pid_t childpid;

	logger_->debug() << "Running isolate...";

	childpid = fork();

	switch (childpid) {
	case -1:
		{
			auto message = std::string("Fork failed: ") + strerror(errno);
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		break;
	case 0:
		{
			//---Child---
			//Redirect stderr and stdout to /dev/null file
			int devnull;
			devnull = open("/dev/null", O_WRONLY);
			if (devnull == -1) {
				auto message = "Cannot open /dev/null file for writing.";
				logger_->warn() << message;
				throw sandbox_exception(message);
			}
			dup2(devnull, 1);
			dup2(devnull, 2);

			auto args = isolate_run_args(binary, arguments);
			execvp(isolate_binary_.c_str(), args);

			//Never reached
			auto message = std::string("Exec returned to child: ") + strerror(errno);
			logger_->warn() << message;
			throw sandbox_exception(message);
		}
		break;
	default:
		{
			//---Parent---
			int status;
			waitpid(childpid, &status, 0);
			if (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 1) {
				auto message = "Isolate run internal error. Return value: " + std::to_string(WEXITSTATUS(status));
				logger_->warn() << message;
				throw sandbox_exception(message);
			}
			logger_->debug() << "Isolate box " << id_ << " ran successfuly.";
		}
		break;
	}
}

char **isolate_sandbox::isolate_run_args(const std::string &binary, const std::vector<std::string> &arguments)
{
	std::vector<std::string> vargs;

	vargs.push_back("--cg");
	vargs.push_back("--cg-timing");
	vargs.push_back("--box-id=" + std::to_string(id_));

	vargs.push_back("--cg-mem=" + std::to_string(limits_.memory_usage));
	vargs.push_back("--time=" + std::to_string(limits_.cpu_time));
	vargs.push_back("--wall-time=" + std::to_string(limits_.wall_time));
	vargs.push_back("--extra-time=" + std::to_string(limits_.extra_time));
	if (limits_.stack_size != 0) {
		vargs.push_back("--stack=" + std::to_string(limits_.stack_size));
	}
	if (limits_.files_size != 0) {
		vargs.push_back("--fsize=" + std::to_string(limits_.files_size));
	}
	vargs.push_back("--quota=" + std::to_string(limits_.disk_blocks) + "," + std::to_string(limits_.disk_inodes));
	if (!limits_.stdin.empty()) {
		vargs.push_back("--stdin=" + limits_.stdin);
	}
	if (!limits_.stdout.empty()) {
		vargs.push_back("--stdout=" + limits_.stdout);
	}
	if (!limits_.stderr.empty()) {
		vargs.push_back("--stderr=" + limits_.stderr);
	}
	if (!limits_.chdir.empty()) {
		vargs.push_back("--chdir=" + limits_.chdir);
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
	vargs.push_back("--meta=" + meta_file_);

	vargs.push_back("--run");
	vargs.push_back("--");
	vargs.push_back(binary);
	for (auto &i : arguments) {
		vargs.push_back(i);
	}

	//Convert string to char ** for execv call
	char **c_args = new char*[vargs.size() + 1];
	int i = 0;
	for (auto &it : vargs) {
		c_args[i++] = strdup(it.c_str());
		//std::cerr << it << " ";
	}
	c_args[i] = NULL;
	return c_args;
}

task_results isolate_sandbox::process_meta_file()
{
	task_results results;

	//Set reasonable defaults
	results.exitcode = 0;
	results.exitsig = 0; //no signal
	results.killed = false;
	results.memory = 0;
	results.message = "";
	results.status = isolate_status::NOTSET;
	results.time = 0;
	results.wall_time = 0;

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
			}
		}
		return results;
	} else {
		auto message = "Cannot open " + meta_file_ + " for reading.";
		logger_->warn() << message;
		throw sandbox_exception(message);
	}
}

