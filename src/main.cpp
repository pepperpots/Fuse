#include "fuse.h"

#include "cxxopts.hpp"

#include <iostream>
#include <chrono>
#include <thread>

auto setup_options(char* argv){
	
	cxxopts::Options options(argv, "Configuration is given with command line options:");
	options.add_options()
		("h,help", "Print this help.")
		("t,target_dir", "Target Fuse folder (containing target.json).", cxxopts::value<std::string>())
		("debug", "Enable DEBUG logging.");

	// We are going to allow for:

	std::vector<std::string> main_options = {
		"execute_sequence",
		"combine_sequence",
		"execute_references"
	};

	std::vector<std::string> parameter_options = {
		"strategies"
	};

	std::vector<std::string> utility_options = {
		"print_instances",
		"print_dependency_matrix"
	};

	return std::make_tuple(options, main_options, parameter_options, utility_options);

}

int main(int argc, char** argv){

	auto [options, main_options, parameter_options, utility_options] = setup_options(argv[0]);

	const auto options_parse_result = options.parse(argc, argv);
	if(options_parse_result.count("help")){
		std::cout << options.help({""}) << std::endl;
		exit(0);
	}

		


	return 0;
}
