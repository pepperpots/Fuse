#include "util.h"

#include <sys/stat.h>
#include <iostream>
#include <string>
#include <algorithm>

// cannot assume that spdlog has been initialized

bool Fuse::Util::check_file_existance(std::string filename){

	struct stat sb;

	if(stat(filename.c_str(), &sb) == 0){
		if(S_ISREG(sb.st_mode))
			return true;
		else
			return false;
	}

	return false;

}

void Fuse::Util::check_or_create_directory(std::string directory){

	struct stat sb;

	if(stat(directory.c_str(), &sb) == 0){

		if(S_ISDIR(sb.st_mode))
			return;
		else {
			std::string msg = "'" + directory + "' exists but is not a directory.";
			throw std::runtime_error(msg);
		}

	} else {

		std::stringstream ss;
		ss << "mkdir -p " << directory;
		system(ss.str().c_str());

	}

}

std::string Fuse::Util::check_or_create_directory_from_filename(std::string filename){

	auto directory = Fuse::Util::get_directory_from_filename(filename);

	struct stat sb;

	if(stat(directory.c_str(), &sb) == 0){

		if(S_ISDIR(sb.st_mode))
			return directory;
		else {
			std::string msg = "'" + directory + "' exists but is not a directory.";
			throw std::runtime_error(msg);
		}

	} else {

		std::stringstream ss;
		ss << "mkdir -p " << directory;
		system(ss.str().c_str());

	}

	return directory;

}

std::string Fuse::Util::get_directory_from_filename(const std::string& filename){

	auto found_index = filename.find_last_of("/\\");
	auto directory = filename.substr(0,found_index);

	return directory;

}

std::vector<std::string> split(const std::string& s, char delim, std::vector<std::string>& elems){

	std::stringstream ss(s);
	std::string item;

	while (std::getline(ss, item, delim))
		elems.push_back(item);

	return elems;
}

std::vector<std::string> Fuse::Util::split_string_to_vector(const std::string& s, char delim){
	std::vector<std::string> elems;
	split(s, delim, elems);
	return elems;
}

std::string Fuse::Util::lowercase(const std::string str){

	std::string lower = str;
	std::transform(lower.begin(), lower.end(), lower.begin(), tolower);
	return lower;

}
