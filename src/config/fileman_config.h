#ifndef CODEX_WORKER_FILEMAN_CONFIG_H
#define CODEX_WORKER_FILEMAN_CONFIG_H


struct fileman_config {
public:
	std::string hostname = "";
	size_t port = 0;
	std::string username = "";
	std::string password = "";
	std::string cache_dir = "";

	bool operator==(const fileman_config &second) const
	{
		if (hostname != second.hostname ||
				port != second.port ||
				username != second.username ||
				password != second.password ||
				cache_dir != second.cache_dir) {
			return false;
		}

		return true;
	}

	bool operator!=(const fileman_config &second) const
	{
		return !((*this) == second);
	}
};

#endif //CODEX_WORKER_FILEMAN_CONFIG_H