#include "fuse.h"

#include "cxxopts.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <numeric>

cxxopts::Options setup_options(char* argv, std::vector<std::string>& main_options, std::vector<std::string>& utility_options){

	cxxopts::Options options(argv, "Configuration is given with command line options:");
	options.add_options("Miscellaneous")
		("h,help", "Print this help.")
		("log_level", "Set minimum logging level. Argument is integer position in {trace, debug, info, warn}. Defaults to info.", cxxopts::value<unsigned int>()->default_value("2"));

	options.add_options("Main")
		("d,target_dir", "Target Fuse target directory (containing fuse.json).", cxxopts::value<std::string>())
		("e,execute_sequence", "Execute the sequence. Argument is number of repeat sequence executions. Conditioned by 'minimal', 'filter_events'.", cxxopts::value<unsigned int>())
		("m,combine_sequence", "Combine the sequence repeats. Conditioned by 'strategies', 'repeat_indexes', 'minimal', 'filter_events'.")
		("t,execute_hem", "Execute the HEM execution profile. Argument is number of repeat executions. Conditioned by 'filter_events'.", cxxopts::value<unsigned int>())
		("a,analyse_accuracy", "Analyse accuracy of combined execution profiles. Conditioned by 'strategies', 'repeat_indexes', 'minimal', 'accuracy_metric'.")
		("r,execute_references", "Execute the reference execution profiles.", cxxopts::value<unsigned int>())
		("c,run_calibration", "Run EPD calibration on the reference profiles.");

	options.add_options("Utility")
		("dump_instances", "Dumps an execution profile matrix. Argument is the output file. Requires 'tracefile', 'benchmark'.", cxxopts::value<std::string>())
		("dump_dag_adjacency", "Dumps the data-dependency DAG as a dense adjacency matrix. Argument is the output file. Requires 'tracefile', 'benchmark'.", cxxopts::value<std::string>())
		("dump_dag_dot", "Dumps the task-creation and data-dependency DAG as a .dot for visualization. Argument is the output file. Requires 'tracefile', 'benchmark'.", cxxopts::value<std::string>());

	options.add_options("Parameter")
		("strategies", "Comma-separated list of strategies from {'random','ctc','lgl','bc','hem'}.",cxxopts::value<std::string>())
		("repeat_indexes", "Comma-separated list of sequence repeat indexes to operate on, or 'all'. Defaults to all repeat indexes.",cxxopts::value<std::string>()->default_value("all"))
		("minimal", "Use minimal execution profiles (default is non-minimal). Strategies 'bc' and 'hem' cannot use minimal.", cxxopts::value<bool>()->default_value("false"))
		("filter_events", "Main options only load and dump data for the events defined in the target JSON (i.e. exclude non HPM events). Default is false.", cxxopts::value<bool>()->default_value("false"))
		("accuracy_metric", "Accuracy metric to use for analysis, out of {'epd', 'spearmans'}. Default is 'epd'.", cxxopts::value<std::string>()->default_value("epd"))
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

void initialize_logging(std::string logging_directory, unsigned int log_level, bool log_to_file){

	std::string logger_name = "fuse";
	auto logger = spdlog::get(logger_name);
	if(logger){
		spdlog::debug("Reinitializing logging to use log directory {}", logging_directory);
		spdlog::drop_all();
	}

	// Set up sinks for the logging
	std::vector<spdlog::sink_ptr> sinks;

	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	sinks.push_back(console_sink);

	if(log_to_file){
		// Filename is the current datetime
		auto t = std::time(nullptr);
		auto tm = *std::localtime(&t);

		std::ostringstream oss;
		oss << logging_directory << std::put_time(&tm, "/%Y%m%d.%H%M.log");
		auto log_filename = oss.str();

		auto directory = Fuse::Util::check_or_create_directory_from_filename(log_filename);
		auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_filename);
		sinks.push_back(file_sink);
	}

	// Initialize fuse library logging to those sinks
	logger = Fuse::initialize_logging(sinks, log_level);

	logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%^%l%$]: %v");

	// Set log level
	switch(log_level) {
		case 3:
			logger->set_level(spdlog::level::warn);
			break;
		case 2:
			logger->set_level(spdlog::level::info);
			break;
		case 1:
			logger->set_level(spdlog::level::debug);
			break;
		case 0:
			logger->set_level(spdlog::level::trace);
			break;
		default:
			spdlog::warn("Log level {} is invalid so defaulting to 2 (INFO). See help for log level options.",log_level);
	};

	// Initialize this client application logging using the same sinks
	spdlog::set_default_logger(logger);

}

std::vector<Fuse::Strategy> parse_strategies_option(
		const cxxopts::ParseResult& options_parse_result,
		bool minimal
		){

	if(options_parse_result.count("strategies") == false)
		throw std::invalid_argument("To run Fuse with this configuration, the 'strategies' option must be provided.");

	auto strategies_str = options_parse_result["strategies"].as<std::string>();

	std::vector<Fuse::Strategy> strategies;

	for(auto strategy_str : Fuse::Util::split_string_to_vector(strategies_str, ',')){
		auto strategy = Fuse::convert_string_to_strategy(strategy_str, minimal);
		strategies.push_back(strategy);
	}

	return strategies;
}

Fuse::Accuracy_metric parse_accuracy_metric_option(
		const cxxopts::ParseResult& options_parse_result
		){

	if(options_parse_result.count("accuracy_metric") == false)
		throw std::invalid_argument("To analyse accuracy of Fuse combinations, the 'accuracy_metric' option must be provided.");

	auto metric_str = options_parse_result["accuracy_metric"].as<std::string>();

	return Fuse::convert_string_to_metric(metric_str);

}

std::vector<unsigned int> parse_repeat_indexes_option(
		const cxxopts::ParseResult& options_parse_result,
		Fuse::Target& fuse_target,
		bool minimal,
		std::vector<Fuse::Strategy> strategies
		){

	std::vector<unsigned int> repeat_indexes;

	auto indexes_str = options_parse_result["repeat_indexes"].as<std::string>();

	auto num_executed_instances = fuse_target.get_num_sequence_repeats(minimal);

	std::string strategies_str = "";
	for(auto strategy : strategies)
		strategies_str += Fuse::convert_strategy_to_string(strategy) + ",";
	strategies_str = "[" + strategies_str.substr(0,strategies_str.size()-1) + "]";

	if(std::find(strategies.begin(), strategies.end(), Fuse::Strategy::HEM) != strategies.end()){
		auto num_hem_instances = fuse_target.get_num_combined_profiles(Fuse::Strategy::HEM);
		if(strategies.size() == 1)
			num_executed_instances = num_hem_instances;
		else if(num_hem_instances < num_executed_instances)
			num_executed_instances = num_hem_instances;
	}

	if(num_executed_instances == 0)
		throw std::runtime_error(fmt::format("There are no available repeat indexes common to strategies {}, so cannot operate on them.",
			strategies_str));

	if(indexes_str == "all"){

		repeat_indexes.assign(num_executed_instances,0);
		std::iota(repeat_indexes.begin(), repeat_indexes.end(), 0);

	} else {

		for(auto index_str : Fuse::Util::split_string_to_vector(indexes_str, ',')){
			unsigned int repeat_idx = 0;

			try {
				repeat_idx = std::stoul(index_str);
			} catch (std::exception& e) {
				throw std::invalid_argument(fmt::format("Could not resolve the given repeat index {} as an unsigned integer.",index_str));
			}

			if(repeat_idx >= num_executed_instances)
				throw std::invalid_argument(fmt::format("Cannot combine repeat index {} as only have {} available repeat indexes common to strategies {}.",
					repeat_idx, num_executed_instances, strategies_str));

			repeat_indexes.push_back(repeat_idx);

		}

	}

	spdlog::debug("Operating on {} provided repeat indexes {}.", repeat_indexes.size(), Fuse::Util::vector_to_string(repeat_indexes));

	return repeat_indexes;
}


void run_main_options(const cxxopts::ParseResult& options_parse_result){

	/* All of these options operate on a target Fuse folder, so load its json */

	if(!options_parse_result.count("target_dir"))
		throw std::invalid_argument("Must provide the target fuse folder (containing fuse.json) as option 'target_dir'");

	std::string target_dir = options_parse_result["target_dir"].as<std::string>();

	Fuse::Target fuse_target(target_dir);
	initialize_logging(fuse_target.get_logs_directory(), spdlog::get("fuse")->level(), true); // We log the target stuff to file

	/* Now run the requested functions on the target */

	bool minimal = options_parse_result["minimal"].as<bool>();

	bool filter_to_events = options_parse_result["filter_events"].as<bool>();
	if(filter_to_events){
		fuse_target.set_filtered_events(fuse_target.get_target_events());
	}

	if(options_parse_result.count("execute_references")){
		unsigned int number_of_executions = options_parse_result["execute_references"].as<unsigned int>();
		Fuse::execute_references(fuse_target, number_of_executions);
	}

	if(options_parse_result.count("execute_sequence")){
		unsigned int number_of_executions = options_parse_result["execute_sequence"].as<unsigned int>();
		Fuse::execute_sequence_repeats(fuse_target, number_of_executions, minimal);
	}

	if(options_parse_result.count("execute_hem")){
		unsigned int number_of_executions = options_parse_result["execute_hem"].as<unsigned int>();
		Fuse::execute_hem_repeats(fuse_target, number_of_executions);
	}

	if(options_parse_result.count("combine_sequence")){
		auto strategies = parse_strategies_option(options_parse_result, minimal);
		auto repeat_indexes = parse_repeat_indexes_option(options_parse_result, fuse_target, minimal, strategies);
		Fuse::combine_sequence_repeats(fuse_target, strategies, repeat_indexes, minimal);
	}

	if(options_parse_result.count("run_calibration")){
		Fuse::calculate_calibration_tmds(fuse_target);
	}

	if(options_parse_result.count("analyse_accuracy")){
		auto strategies = parse_strategies_option(options_parse_result, minimal);
		auto repeat_indexes = parse_repeat_indexes_option(options_parse_result, fuse_target, minimal, strategies);
		Fuse::Accuracy_metric metric = parse_accuracy_metric_option(options_parse_result);
		Fuse::analyse_sequence_combinations(fuse_target, strategies, repeat_indexes, metric);
	}

}

void run_utility_options(const cxxopts::ParseResult& options_parse_result){

	/* All of these options operate on a loaded tracefile, so let's do that first */

	/* What tracefile to load */
	if(!options_parse_result.count("tracefile"))
		throw std::invalid_argument("Must provide the tracefile filename via option 'tracefile'");
	std::string tracefile = options_parse_result["tracefile"].as<std::string>();

	/* Binary to find symbols */
	if(!options_parse_result.count("benchmark"))
		throw std::invalid_argument("Must provide the tracefile's binary via option 'benchmark'");
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

void run_options(const cxxopts::Options& options,
		const cxxopts::ParseResult& options_parse_result,
		const std::vector<std::string> main_options,
		const std::vector<std::string> utility_options){

	spdlog::info("Running Fuse.");

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
		throw std::invalid_argument(fmt::format("No valid option given. {}", options.help()));
	}

}

int main(int argc, char** argv){

	std::vector<std::string> main_options, utility_options;
	cxxopts::Options options = setup_options(argv[0], main_options, utility_options);

	const cxxopts::ParseResult options_parse_result = options.parse(argc, argv);
	if(options_parse_result.count("help")){
		std::cout << options.help();
		return 0;
	}

	// initialize logging (external to the target directory)
	// For now, we don't log the non-target stuff to file
	initialize_logging("",options_parse_result["log_level"].as<unsigned int>(), false);

	try {

		run_options(options, options_parse_result, main_options, utility_options);

	} catch (const cxxopts::OptionException& e){
		spdlog::error("Options parsing exception: {}", e.what());
		return 1;
	} catch (const std::exception& e){
		spdlog::error("General exception: {}", e.what());
		return 1;
	}

	spdlog::info("Finished.");
	return 0;
}
