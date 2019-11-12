#include "fuse.h"
#include "config.h"
#include "target.h"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <spdlog/sinks/stdout_color_sinks.h>

#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

std::shared_ptr<spdlog::logger> Fuse::initialize_logging(std::vector<spdlog::sink_ptr> sinks, unsigned int log_level){

	std::string logger_name = "fuse";

	// check if we already have a logger in this library
	// If we do, then drop it so we can re-initialize
	auto logger = spdlog::get(logger_name);
	if(logger){
		spdlog::debug("Dropping libFuse logging at client request.");
		spdlog::drop_all();
	}

	if(sinks.size() > 0){
		// use the provided sinks in this library

		logger = std::make_shared<spdlog::logger>(logger_name, std::begin(sinks), std::end(sinks));

		switch (log_level) {
			case 3:
				logger->set_level(spdlog::level::warn);
				break;
			case 1:
				logger->set_level(spdlog::level::debug);
				break;
			case 0:
				logger->set_level(spdlog::level::trace);
				break;
			default:
				logger->set_level(spdlog::level::info);
				break;
		};

		logger->set_pattern("[%Y-%m-%d %H:%M:%S] [libFuse] %^%l%$ %v");

		spdlog::set_default_logger(logger);

	} else {

		std::cerr << "No spdlog sinks were provided to libFuse for logging. Use Fuse::intialize_logging(std::string log_directory, bool log_to_file,";
		std::cerr << "unsigned int log_level) to have the library generate its own logging." << std::endl;
		exit(1);

	}

	Fuse::Config::fuse_log_level = log_level;
	Fuse::Config::client_managed_logging = true;
	Fuse::Config::initialized = true;

	return logger;
}

 void Fuse::initialize_logging(std::string log_directory, bool log_to_file, unsigned int log_level){

	std::string logger_name = "fuse";

	auto logger = spdlog::get(logger_name);
	if(logger){
		spdlog::debug("Reinitializing libFuse logging to use log directory {}", log_directory);
		spdlog::drop_all();
	}

	std::vector<spdlog::sink_ptr> sinks;

	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	sinks.push_back(console_sink);

	if(log_to_file){

		// Filename is the current datetime
		auto t = std::time(nullptr);
		auto tm = *std::localtime(&t);

		std::ostringstream oss;
		oss << log_directory << std::put_time(&tm, "/%Y%m%d.%H%M.log");
		auto log_filename = oss.str();

		Fuse::Util::check_or_create_directory_from_filename(log_filename);

		auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_filename);
		sinks.push_back(file_sink);

	}

	logger = std::make_shared<spdlog::logger>(logger_name, std::begin(sinks), std::end(sinks));

	switch (log_level) {
		case 0:
			logger->set_level(spdlog::level::warn);
			break;
		case 2:
			logger->set_level(spdlog::level::debug);
			break;
		case 3:
			logger->set_level(spdlog::level::trace);
			break;
		default:
			logger->set_level(spdlog::level::info);
			break;
	};

	logger->set_pattern("[%Y-%m-%d %H:%M:%S] [libFuse] [%^%l%$] %v");

	spdlog::set_default_logger(logger);

	Fuse::Config::fuse_log_level = log_level;
	Fuse::Config::client_managed_logging = false;
	Fuse::Config::initialized = true;

}

void Fuse::execute_sequence(Fuse::Target target, unsigned int number_of_repeats, bool minimal){

	auto minimal_str = minimal ? "minimal" : "non-minimal";
	spdlog::info("Executing {} repeats of the {} sequence profiles.", number_of_repeats, minimal_str);

	unsigned int current_idx = target.get_num_sequence_repeats(minimal);

	for(unsigned int instance_idx = current_idx; instance_idx < (current_idx+number_of_repeats); instance_idx++){

		spdlog::debug("Executing sequence profiles for repeat index {}.", instance_idx);

		auto sequence = target.get_sequence(minimal);
		if(sequence.size() < 1)
			throw std::runtime_error(
				fmt::format("No {} sequence has been defined in the target JSON, so cannot execute the sequence profiles.", minimal_str));

		for(auto part : sequence){

			std::stringstream ss;
			ss << target.get_tracefiles_directory() << "/combined_profile_" << instance_idx << "-" << part.part_idx << ".ost";
			auto tracefile = ss.str();

			Fuse::Event_set profiled_events = part.unique;
			profiled_events.insert(profiled_events.end(), part.overlapping.begin(), part.overlapping.end());

			Fuse::Profile_p execution_profile = Fuse::Profiling::execute_and_load(
				target.get_target_runtime(),
				target.get_target_binary(),
				target.get_target_args(),
				tracefile,
				profiled_events
			);

			// TODO Do I want to keep this profile in memory?
			// target.add_loaded_sequence_profile(part, execution_profile, ...)

		}

		target.increment_num_sequence_repeats(minimal);

	}

	target.save();

	spdlog::info("Finished executing {} {} sequence profiles. Target now has {} {} sequence profiles.",
		number_of_repeats,
		minimal_str,
		target.get_num_sequence_repeats(minimal),
		minimal_str);

}
