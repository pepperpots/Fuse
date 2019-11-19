#include "fuse.h"
#include "config.h"
#include "target.h"
#include "combination.h"
#include "profiling.h"
#include "instance.h"
#include "statistics.h"

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

void Fuse::execute_references(
		Fuse::Target& target,
		unsigned int number_of_repeats
		){

	auto saved_filtered_events = target.get_filtered_events();

	auto reference_sets = target.get_or_generate_reference_sets();

	spdlog::info("Executing {} repeats of the {} reference profiles.", number_of_repeats, reference_sets.size());

	unsigned int current_idx = target.get_num_reference_repeats();
	for(unsigned int instance_idx = current_idx; instance_idx < (current_idx+number_of_repeats); instance_idx++){

		spdlog::debug("Executing reference profiles for repeat index {}.", instance_idx);
		std::vector<std::string> reference_tracefiles_for_repeat;
		reference_tracefiles_for_repeat.reserve(reference_sets.size());

		for(decltype(reference_sets.size()) ref_idx = 0; ref_idx < reference_sets.size(); ref_idx++){

			std::stringstream ss;
			ss << target.get_tracefiles_directory() << "/reference_profile_";
			ss << instance_idx << "-" << ref_idx << ".ost";
			auto tracefile = ss.str();

			reference_tracefiles_for_repeat.push_back(tracefile);

			Fuse::Event_set reference_set = reference_sets.at(ref_idx);

			target.set_filtered_events(reference_set);

			Fuse::Profile_p execution_profile = Fuse::Profiling::execute_and_load(
				target.get_filtered_events(),
				target.get_target_runtime(),
				target.get_target_binary(),
				target.get_target_args(),
				tracefile,
				reference_set,
				target.get_should_clear_cache()
			);

			Fuse::add_profile_event_values_to_statistics(execution_profile, target.get_statistics());

			auto reference_values = execution_profile->get_value_distribution(reference_set);

			target.save_reference_values(ref_idx, instance_idx, reference_set, reference_values);

		}

		target.compress_references_tracefiles(reference_tracefiles_for_repeat, instance_idx);
		target.increment_num_reference_repeats();

	}

	target.save();

	target.set_filtered_events(saved_filtered_events);

	spdlog::info("Finished executing {} repeats of the {} reference profiles. Target now has {} reference repeats.",
		number_of_repeats,
		reference_sets.size(),
		target.get_num_reference_repeats());

}

void Fuse::execute_sequence_repeats(
		Fuse::Target& target,
		unsigned int number_of_repeats,
		bool minimal,
		bool keep_in_memory
		){

	auto minimal_str = minimal ? "minimal" : "non_minimal";
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
			ss << target.get_tracefiles_directory() << "/" << minimal_str << "_";
			ss << "sequence_profile_" << instance_idx << "-" << part.part_idx << ".ost";
			auto tracefile = ss.str();

			Fuse::Event_set profiled_events = part.unique;
			profiled_events.insert(profiled_events.end(), part.overlapping.begin(), part.overlapping.end());

			// Execute and load to ensure the profile loads successfully
			Fuse::Profile_p execution_profile = Fuse::Profiling::execute_and_load(
				target.get_filtered_events(),
				target.get_target_runtime(),
				target.get_target_binary(),
				target.get_target_args(),
				tracefile,
				profiled_events,
				target.get_should_clear_cache()
			);

			// Add the event statistics
			Fuse::add_profile_event_values_to_statistics(execution_profile, target.get_statistics());

			if(keep_in_memory)
				target.store_loaded_sequence_profile(instance_idx, part, execution_profile, minimal);

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

void Fuse::execute_hem_repeats(
		Fuse::Target& target,
		unsigned int number_of_repeats,
		bool keep_in_memory
		){

	spdlog::info("Executing {} repeats of the HEM profile.", number_of_repeats);

	unsigned int current_idx = target.get_num_combined_profiles(Fuse::Strategy::HEM);

	for(unsigned int instance_idx = current_idx; instance_idx < (current_idx+number_of_repeats); instance_idx++){

		spdlog::debug("Executing the HEM profile for repeat index {}.", instance_idx);

		std::stringstream ss;
		ss << target.get_tracefiles_directory() << "/hem_profile_" << instance_idx << ".ost";
		auto tracefile = ss.str();
		Fuse::Event_set profiled_events = target.get_target_events();

		bool should_multiplex = true;

		// Execute and load to ensure the profile loads successfully
		Fuse::Profile_p execution_profile = Fuse::Profiling::execute_and_load(
			target.get_filtered_events(),
			target.get_target_runtime(),
			target.get_target_binary(),
			target.get_target_args(),
			tracefile,
			profiled_events,
			target.get_should_clear_cache(),
			should_multiplex
		);

		target.register_new_combined_profile(Fuse::Strategy::HEM, instance_idx, execution_profile);

		if(keep_in_memory)
			target.store_combined_profile(instance_idx, Fuse::Strategy::HEM, execution_profile);

	}

	target.save();

	spdlog::info("Finished executing {} HEM profiles. Target now has {} HEM profiles.",
		number_of_repeats,
		target.get_num_combined_profiles(Fuse::Strategy::HEM)
	);

}

void Fuse::combine_sequence_repeats(
		Fuse::Target& target,
		std::vector<Fuse::Strategy> strategies,
		std::vector<unsigned int> repeat_indexes,
		bool minimal,
		bool keep_in_memory
		){

	auto minimal_str = minimal ? "minimal" : "non_minimal";
	spdlog::info("Running {} combinations (for the repeat indexes {}) of the {} sequence profiles.",
		repeat_indexes.size(),
		Fuse::Util::vector_to_string(repeat_indexes),
		minimal_str);

	for(auto repeat_idx : repeat_indexes){

		spdlog::debug("Getting {} sequence profiles for repeat index {}.", minimal_str, repeat_idx);
		std::vector<Fuse::Profile_p> sequence_profiles = target.load_and_retrieve_sequence_profiles(repeat_idx, minimal);

		for(auto strategy : strategies){

			if(strategy == Fuse::Strategy::HEM){
				spdlog::info("Cannot combine sequence profiles via HEM. Ignoring this strategy.");
				continue;
			}

			// Check if we have already combined this repeat index with this strategy
			if(target.combined_profile_exists(strategy, repeat_idx)){
				spdlog::info("The repeat index {} has already been combined via strategy {}. Continuing.", repeat_idx, Fuse::convert_strategy_to_string(strategy));
				continue;
			}

			spdlog::info("Combining sequence profiles for repeat index {} via strategy {}.", repeat_idx, Fuse::convert_strategy_to_string(strategy));

			std::vector<Fuse::Event_set> overlapping_events;
			if(strategy == Fuse::Strategy::BC){
				Fuse::Combination_sequence bc_sequence = target.get_sequence(minimal);
				for(auto part : bc_sequence)
					overlapping_events.push_back(part.overlapping);
			}

			Fuse::Profile_p combined_profile = Fuse::Combination::combine_profiles_via_strategy(
				sequence_profiles,
				strategy,
				target.get_combination_filename(strategy, repeat_idx),
				target.get_target_binary(),
				overlapping_events,
				target.get_statistics()
			);

			target.register_new_combined_profile(strategy, repeat_idx, combined_profile);

			if(keep_in_memory)
				target.store_combined_profile(repeat_idx, strategy, combined_profile);

			spdlog::info("Finished combining the sequence profiles for repeat index {} via strategy {}.", repeat_idx, Fuse::convert_strategy_to_string(strategy));

		}

		target.save();

	}

	spdlog::info("Completed all requested combinations.");

}

void Fuse::add_profile_event_values_to_statistics(
		Fuse::Profile_p profile,
		Fuse::Statistics_p statistics
		){

	auto instances = profile->get_instances();
	auto events = profile->get_unique_events();

	spdlog::debug("Adding event values to statistics for {} instances and {} events.", instances.size(), events.size());

	// If error, then we are assuming there were no events of that type during the instance
	bool error = false;
	for(auto instance : instances){
		for(auto event : events){

			auto value = instance->get_event_value(event, error);
			statistics->add_event_value(event, value, instance->symbol);

		}
	}
}





