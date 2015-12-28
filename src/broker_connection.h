#ifndef CODEX_WORKER_BROKER_CONNECTION_H
#define CODEX_WORKER_BROKER_CONNECTION_H


#include <zmq.hpp>
#include <map>
#include <memory>
#include "spdlog/spdlog.h"
#include "config/worker_config.h"


struct receive_task_callback {
	bool operator() ()
	{
		std::cout << "Task received" << std::endl;
		return true;
	}
};


template <typename proxy, typename task_callback>
class broker_connection {
private:
	const worker_config &config;
	proxy *socket;
	std::shared_ptr<spdlog::logger> logger_;

	/**
	 * Send the ACK command to the broker
	 */
	void command_ack ()
	{

	}

public:
	broker_connection (const worker_config &config, proxy *socket, std::shared_ptr<spdlog::logger> logger = nullptr) :
		config(config), socket(socket)
	{
		if(logger != nullptr) {
			logger_ = logger;
		} else {
			//Create logger manually to avoid global registration of logger
			auto sink = std::make_shared<spdlog::sinks::stderr_sink_st>();
			logger_ = std::make_shared<spdlog::logger>("cache_manager_nolog", sink);
			//Set loglevel to 'off' cause no logging
			logger_->set_level(spdlog::level::off);
		}
	}

	/**
	 * Send the INIT command to the broker.
	 */
	void connect ()
	{
		const worker_config::header_map_t &headers = config.get_headers();

		logger_->debug() << "Connecting to " << config.get_broker_uri();
		socket->connect(config.get_broker_uri());
		socket->send("init", 4, ZMQ_SNDMORE);

		for (auto it = headers.begin(); it != headers.end(); ++it) {
			auto pair = *it;

			char buf[256];
			int count = snprintf(buf, sizeof(buf), "%s=%s", pair.first.c_str(), pair.second.c_str());

			if (count < 0) {
				logger_->emerg() << "Cannot create header for connecting to broker";
				throw;
			}

			socket->send(buf, (size_t) count, std::next(it) == headers.end() ? 0 : ZMQ_SNDMORE);
		}
	}

	/**
	 * Receive and process tasks
	 * Blocks execution until the underlying ZeroMQ context is terminated
	 */
	void receive_tasks ()
	{
		task_callback cb;
		bool terminate = false;
		zmq::message_t request;

		while (!terminate) {
			try {
				socket->recv(request);
				logger_->debug() << "Received request from broker";
				cb();
			} catch (zmq::error_t &e) {
				logger_->emerg() << "Terminating to receive messages. " << e.what();
				terminate = true;
			}
		}
	}
};

#endif //CODEX_WORKER_BROKER_CONNECTION_H