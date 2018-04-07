#include "filesystem.h"
#include <iostream>

void helpers::copy_directory(const fs::path &src, const fs::path &dest)
{
	try {
		if (!fs::exists(src)) {
			throw filesystem_exception("Source directory does not exist");
		} else if (!fs::is_directory(src)) {
			throw filesystem_exception("Source directory is not a directory");
		} else if (!fs::exists(dest) && !fs::create_directories(dest)) {
			throw filesystem_exception("Destination directory cannot be created");
		}

		fs::directory_iterator endit;
		for (fs::directory_iterator it(src); it != endit; ++it) {
			if (fs::is_directory(it->status())) {
				helpers::copy_directory(it->path(), dest / it->path().filename());
			} else {
				fs::copy(it->path(), dest / it->path().filename());
			}
		}
	} catch (fs::filesystem_error &e) {
		throw filesystem_exception("Error in copying directories: " + std::string(e.what()));
	}

	return;
}

fs::path helpers::normalize_path(const fs::path &path)
{
	// prepare root and path chunks
	fs::path root = path.root_path();
	std::vector<fs::path> path_arr(path.begin(), path.end());

	// process path chunks
	std::vector<fs::path> result_arr;
	for (auto &chunk : path_arr) {
		if (chunk.string() == ".") {
			continue;
		} else if (chunk.string() == "..") {
			if (result_arr.empty() || (!root.empty() && result_arr.back().string() == root)) {
				// invalid path, either there is no more elements, or root was hit
				return fs::path();
			}
			result_arr.pop_back();
			continue;
		}

		result_arr.push_back(chunk);
	}

	// construct resulting path
	fs::path result;
	for (auto &chunk : result_arr) {
		result /= chunk;
	}
	return result;
}

fs::path helpers::find_path_outside_sandbox(const std::string &inside_path,
	const std::string &sandbox_chdir,
	std::vector<std::tuple<std::string, std::string, sandbox_limits::dir_perm>> &bound_dirs,
	const std::string &source_dir)
{
	auto file_path = fs::path(inside_path);
	if (!file_path.has_root_directory()) {
		// relative path to chdir, chdir should be absolute
		file_path = fs::path(sandbox_chdir) / file_path;
	} else {
		// for absolute paths remove /box prefix if any
		std::string box_path = "/box";
		if (inside_path.find(box_path) == 0) {
			file_path = fs::path(inside_path.substr(box_path.length()));
		}
	}
	file_path = normalize_path(file_path);
	auto file_path_string = file_path.string();

	// try to find the file in main source directory
	auto source_path = fs::path(source_dir) / file_path;
	if (fs::exists(source_path)) {
		return source_path;
	}

	// try to find the file in sandbox bound directories (absolute path in sandbox provided)
	for (auto &dir : bound_dirs) {
		std::string sandbox_dir_string = normalize_path(fs::path(std::get<1>(dir))).string();

		if (file_path_string.find(sandbox_dir_string) == 0) {
			std::string file_path_end = file_path_string.substr(sandbox_dir_string.length());
			return fs::path(std::get<0>(dir)) / fs::path(file_path_end);
		}
	}

	// not found
	return fs::path();
}
