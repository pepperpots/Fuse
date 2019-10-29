#include "fuse.h"
#include "helpers.h"

#include "cxxopts.hpp"
#include "easylogging++.h"

#include <iostream>

auto setup_options(char* argv){
	
	cxxopts::Options options(argv, "Configuration is given with command line options:");
	options.add_options("Miscellaneous")
		("h,help", "Print this help.")
		("debug", "Enable DEBUG logging.");

	options.add_options("Main")
		("f,case_folder", "Target Fuse case folder (containing fuse.json).", cxxopts::value<std::string>())
		("e,execute_sequence", "Execute the sequence. Argument is number of repeat sequence executions. Conditioned by 'minimal'.", cxxopts::value<unsigned int>())
		("m,combine_sequence", "Combine the sequence. Conditioned by 'strategies' and 'minimal'.")
		("a,analyse_accuracy", "Analyse accuracy of combined execution profiles. Conditioned by 'strategies', 'minimal', and 'accuracy_metric'.")
		("r,execute_references", "Execute the reference execution profiles.")
		("c,run_calibration", "Run EPD calibration on the reference profiles.");

	options.add_options("Utility")
		("dump_instances", "Dumps an execution profile matrix. Requires 'tracefile', 'benchmark' and 'output_file'.")
		("dump_dependency_matrix", "Dumps a data-dependency adjacency matrix. Requires 'tracefile', 'benchmark' and 'output_file'.");

	options.add_options("Parameter")
		("strategies", "Comma-separated list of strategies from {'random','ctc','lgl','bc','hem'}.",cxxopts::value<std::string>())
		("minimal", "Use minimal execution profiles (default is non-minimal). Strategy will override: 'bc' and 'hem' cannot use minimal).")
		("tracefile", "Argument is the tracefile to load for utility options.", cxxopts::value<std::string>())
		("benchmark", "Argument is the benchmark to use when loading tracefile for utility options.", cxxopts::value<std::string>())
		("output_file", "Argument is the output_file that utility options will create.", cxxopts::value<std::string>());

	std::vector<std::string> main_options;
	std::vector<std::string> utility_options;

	auto main_options_group = options.group_help("Main").options;
	for(auto opt : main_options_group)
		main_options.push_back(opt.s);
	auto utility_options_group = options.group_help("Utility").options;
	for(auto opt : utility_options_group)
		utility_options.push_back(opt.s);

	return std::make_tuple(options, main_options, utility_options);

}

void run_main_options(cxxopts::ParseResult options_parse_result){

	
}

void run_utility_options(cxxopts::ParseResult options_parse_result){
	
	Fuse::initialize("/tmp", options_parse_result.count("debug"));

	/* All of these options operate on a loaded tracefile, so let's do that first */
	
	/* What tracefile to load */
	if(!options_parse_result.count("tracefile")){
		LOG(FATAL) << "Must provide the tracefile filename as option 'tracefile'";
	}
	std::string tracefile = options_parse_result["tracefile"].as<std::string>();

	/* Binary to find symbols */	
	if(!options_parse_result.count("benchmark")){
		LOG(FATAL) << "Must provide the tracefile's binary via option 'benchmark'";
	}
	std::string benchmark = options_parse_result["benchmark"].as<std::string>();
	
	/* Where to output the results */	
	if(!options_parse_result.count("output_file")){
		LOG(FATAL) << "Must provide the file to output results into via option 'output_file'";
	}
	std::string output_file = options_parse_result["output_file"].as<std::string>();

	bool load_communication_matrix = false;
	if(options_parse_result.count("dump_dependency_matrix")){
		load_communication_matrix = true;
	}

	/* Now load */
	Fuse::Profile_p execution_profile(new Fuse::Execution_profile(tracefile, benchmark));
	execution_profile->load_from_tracefile(load_communication_matrix);

	if(options_parse_result.count("dump_instances")){

	}

	if(options_parse_result.count("dump_dependency_matrix")){

	}
	
}

int main(int argc, char** argv){

	auto [options, main_options, utility_options] = setup_options(argv[0]);

	const cxxopts::ParseResult options_parse_result = options.parse(argc, argv);
	if(options_parse_result.count("help")){
		std::cout << options.help({""}) << std::endl;
		exit(0);
	}

	bool opt_given = false;

	if(std::any_of(std::begin(utility_options), std::end(utility_options), [&](std::string opt) {
				return options_parse_result.count(opt);
			})){
		opt_given = true;
		run_utility_options(options_parse_result);
	}
		
	if(std::any_of(std::begin(main_options), std::end(main_options), [&](std::string opt) {
				return options_parse_result.count(opt);
			})){
		opt_given = true;
		run_main_options(options_parse_result);
	}

	if(opt_given == false){
		std::cout << "No valid option given." << std::endl;
		std::cout << options.help();
		exit(0);
	}

	LOG(INFO) << "Finished.";
	return 0;
}
