#ifndef CODEX_WORKER_JOB_EVALUATOR_HPP
#define CODEX_WORKER_JOB_EVALUATOR_HPP

#include <memory>
#include "spdlog/spdlog.h"

#define BOOST_FILESYSTEM_NO_DEPRECATED
#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#include "tasks/job.h"
#include "config/worker_config.h"
#include "fileman/file_manager_base.h"
#include "eval_request.h"


/**
 * Class which handles receiving job from broker_connection, construction of working tree and its evaluation.
 * Above stated run in loop, so this class is in program constructed only once.
 */
class job_evaluator {
public:
	job_evaluator() = delete;
	job_evaluator(const job_evaluator &source) = delete;
	job_evaluator& operator=(const job_evaluator &source) = delete;
	job_evaluator(const job_evaluator &&source) = delete;
	job_evaluator& operator=(const job_evaluator &&source) = delete;

	/**
	 * All other constructors are disabled, because of needed data.
	 * @param logger pointer to logger created in core
	 * @param config default configuration of worker loaded from yaml file
	 * @param fileman filemanager used to download and upload files
	 */
	job_evaluator(std::shared_ptr<spdlog::logger> logger,
				  std::shared_ptr<worker_config> config,
				  std::shared_ptr<file_manager_base> fileman);
	/**
	 * Theoretically not needed, but stated for completion.
	 */
	virtual ~job_evaluator();

	/**
	 * Process an "eval" request
	 */
	virtual void evaluate(eval_request request);

private:
	/**
	 * Download submission from remote source through filemanager given during construction.
	 */
	void download_submission();

	/**
	 * Downloaded submission has prepared for evaluation, that means:
	 * Decompress archive with submission and copy source codes to working directory.
	 */
	void prepare_submission();

	/**
	 * Build job structure from given job-configuration.
	 * Aka build working tree and its linear ordering, which will be executed.
	 * It means load yaml config and call job constructor.
	 */
	void build_job();

	/**
	 * Evaluates job itself. Basically means call function run on job instance.
	 */
	void run_job();

	/**
	 * Cleanup decompressed archive and all other temporary files.
	 */
	void cleanup_submission();

	/**
	 * Upload output files using the filemanager (if desired)
	 */
	void push_result();


	// PRIVATE DATA MEMBERS
	/** URL of remote archive in which is job configuration and source codes */
	std::string archive_url_;
	/** Path in which downloaded archive is stored */
	fs::path archive_local_;
	/** Path in which downloaded decompressed submission is stored */
	fs::path submission_path_;
	/** Path with only source codes, not job configuration */
	fs::path source_path_;

	/** ID of downloaded job obtained from broker */
	std::string job_id_;
	/** Structure of job itself, this will be evaluated */
	std::shared_ptr<job> job_;

	/** File manager which is used to download and upload submission related files */
	std::shared_ptr<file_manager_base> fileman_;
	/** Logger given during construction */
	std::shared_ptr<spdlog::logger> logger_;
	/** Default configuration of worker */
	std::shared_ptr<worker_config> config_;
};

#endif //CODEX_WORKER_JOB_EVALUATOR_HPP
