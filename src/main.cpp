#include "fuse.h"

#include "cxxopts.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <spdlog/sinks/stdout_color_sinks.h>

#include <iostream>
#include <iomanip>
#include <ctime>

cxxopts::Options setup_options(char* argv, std::vector<std::string>& main_options, std::vector<std::string>& utility_options){

	cxxopts::Options options(argv, "Configuration is given with command line options:");
	options.add_options("Miscellaneous")
		("h,help", "Print this help.")
		("log_level", "Set minimum logging level. Argument is integer position in {warn, info, debug, trace}. Defaults to info.", cxxopts::value<unsigned int>()->default_value("1"));

	options.add_options("Main")
		("f,target_folder", "Target Fuse target folder (containing fuse.json).", cxxopts::value<std::string>())
		("e,execute_sequence", "Execute the sequence. Argument is number of repeat sequence executions. Conditioned by 'minimal'.", cxxopts::value<unsigned int>())
		("m,combine_sequence", "Combine the sequence. Conditioned by 'strategies' and 'minimal'.")
		("a,analyse_accuracy", "Analyse accuracy of combined execution profiles. Conditioned by 'strategies', 'minimal', and 'accuracy_metric'.")
		("r,execute_references", "Execute the reference execution profiles.")
		("c,run_calibration", "Run EPD calibration on the reference profiles.");

	options.add_options("Utility")
		("dump_instances", "Dumps an execution profile matrix. Argument is the output file. Requires 'tracefile', 'benchmark'.", cxxopts::value<std::string>())
		("dump_dag_adjacency", "Dumps the data-dependency DAG as a dense adjacency matrix. Argument is the output file. Requires 'tracefile', 'benchmark'.", cxxopts::value<std::string>())
		("dump_dag_dot", "Dumps the task-creation and data-dependency DAG as a .dot for visualization. Argument is the output file. Requires 'tracefile', 'benchmark'.", cxxopts::value<std::string>());

	options.add_options("Parameter")
		("strategies", "Comma-separated list of strategies from {'random','ctc','lgl','bc','hem'}.",cxxopts::value<std::string>())
		("minimal", "Use minimal execution profiles (default is non-minimal). Strategy will override: 'bc' and 'hem' cannot use minimal).")
		("tracefile", "Argument is the tracefile to load for utility options.", cxxopts::value<std::string>())
		("benchmark", "Argument is the benchmark to use when loading tracefile for utility options.", cxxopts::value<std::string>());

	auto main_options_group = options.group_help("Main").options;
	for(auto opt : main_options_group){
		main_options.push_back(opt.s);
		main_options.push_back(opt.l);
	}
	auto utility_options_group = options.group_help("Utility").options;
	for(auto opt : utility_options_group){
		utility_options.push_back(opt.s);
		utility_options.push_back(opt.l);
	}

	return options;

}

void initialize_logging(std::string logging_directory, unsigned int log_level){

	std::string logger_name = "fuse";
	auto logger = spdlog::get(logger_name);
	if(logger)
		spdlog::drop_all();

	// Filename is the current datetime
	auto t = std::time(nullptr);
	auto tm = *std::localtime(&t);

	std::ostringstream oss;
	oss << logging_directory << std::put_time(&tm, "/%Y%m%d.%H%M.log");
	auto log_filename = oss.str();

	auto directory = Fuse::Util::check_or_create_directory_from_filename(log_filename);

	// Set up sinks for stdout and file logging
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_filename);

	std::vector<spdlog::sink_ptr> sinks;
	sinks.push_back(console_sink);
	sinks.push_back(file_sink);

	// Initialize fuse library logging to those sinks
	logger = Fuse::initialize(sinks, log_level);

	logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%^%l%$]: %v");

	// Set log level
	switch(log_level) {
		case 0:
			logger->set_level(spdlog::level::warn);
			break;
		case 1:
			logger->set_level(spdlog::level::info);
			break;
		case 2:
			logger->set_level(spdlog::level::debug);
			break;
		case 3:
			logger->set_level(spdlog::level::trace);
			break;
		default:
			spdlog::warn("Log level {} is invalid so defaulting to 1 (INFO). See help for log level options.",log_level);
	};

	// Initialize this client application logging using the same sinks
	spdlog::set_default_logger(logger);

	spdlog::debug("Logging to filename {}.", log_filename);

}

void run_main_options(cxxopts::ParseResult options_parse_result){


}

void run_utility_options(cxxopts::ParseResult options_parse_result){

	/* All of these options operate on a loaded tracefile, so let's do that first */

	/* What tracefile to load */
	if(!options_parse_result.count("tracefile")){
		spdlog::critical("Must provide the tracefile filename as option 'tracefile'");
		exit(1);
	}
	std::string tracefile = options_parse_result["tracefile"].as<std::string>();

	/* Binary to find symbols */
	if(!options_parse_result.count("benchmark")){
		spdlog::critical("Must provide the tracefile's binary via option 'benchmark'");
		exit(1);
	}
	std::string benchmark = options_parse_result["benchmark"].as<std::string>();

	bool load_communication_matrix = false;
	if(options_parse_result.count("dump_dag_adjacency") || options_parse_result.count("dump_dag_dot"))
		load_communication_matrix = true;

	/* Now load */

	Fuse::Profile_p execution_profile(new Fuse::Execution_profile(tracefile, benchmark));
	execution_profile->load_from_tracefile(load_communication_matrix);

	if(options_parse_result.count("dump_instances")){
		std::string output_file = options_parse_result["dump_instances"].as<std::string>();
		execution_profile->print_to_file(output_file);
	}

	if(options_parse_result.count("dump_dag_adjacency")){
		std::string output_file = options_parse_result["dump_dag_adjacency"].as<std::string>();
		execution_profile->dump_instance_dependencies(output_file);
	}

	if(options_parse_result.count("dump_dag_dot")){
		std::string output_file = options_parse_result["dump_dag_dot"].as<std::string>();
		execution_profile->dump_instance_dependencies_dot(output_file);
	}

	return;

}

int main(int argc, char** argv){

	std::vector<std::string> main_options, utility_options;
	cxxopts::Options options = setup_options(argv[0], main_options, utility_options);

	const cxxopts::ParseResult options_parse_result = options.parse(argc, argv);
	if(options_parse_result.count("help")){
		std::cout << options.help();
		exit(0);
	}

	// initialise logging (external to the target directory)
	initialize_logging("/tmp/libfuse_logs",options_parse_result["log_level"].as<unsigned int>());

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
		spdlog::error("No valid option given.");
		spdlog::info(options.help());
		exit(1);
	}

	spdlog::info("Finished.");

	return 0;
}
