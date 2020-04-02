#include "fuse.h"
#include "analysis.h"
#include "config.h"
#include "target.h"
#include "combination.h"
#include "profiling.h"
#include "instance.h"
#include "statistics.h"
#include "sequence_generator.h"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <spdlog/sinks/stdout_color_sinks.h>

#include <numeric>
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

			auto reference_values_per_symbol = execution_profile->get_value_distribution(reference_set, false);

			target.save_reference_values_to_disk(ref_idx, instance_idx, reference_set, reference_values_per_symbol);

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


void Fuse::analyse_sequence_combinations(
		Fuse::Target& target,
		std::vector<Fuse::Strategy> strategies,
		std::vector<unsigned int> repeat_indexes,
		Fuse::Accuracy_metric metric){

	if(Fuse::Config::lazy_load_references == false)
		target.load_reference_distributions();

	auto reference_pairs = target.get_reference_pairs();

	std::vector<unsigned int> reference_repeats_list(target.get_num_reference_repeats());
	std::iota(reference_repeats_list.begin(), reference_repeats_list.end(), 0);

	std::vector<Fuse::Symbol> symbols = {"all_symbols"};
	if(Fuse::Config::calculate_per_workfunction_tmds){
		auto all_symbols = target.get_statistics()->get_unique_symbols(false);
		symbols.insert(symbols.end(), all_symbols.begin(), all_symbols.end());
	}

	unsigned int strategy_idx = 0;
	for(auto strategy : strategies){

		for(auto repeat_idx : repeat_indexes){ // Combined profile repeats, we report value for each

			spdlog::info("Calculating {} accuracy for combination repeat {}/{} by strategy {} ({}/{}).",
				Fuse::convert_metric_to_string(metric),
				repeat_idx,
				repeat_indexes.size()-1,
				Fuse::convert_strategy_to_string(strategy),
				strategy_idx,
				strategies.size()-1
			);

			auto profile = target.get_or_load_combined_profile(strategy, repeat_idx);

			std::map<unsigned int, double> tmd_per_reference_pair;

			unsigned int pair_idx = 0;
			for(auto reference_pair : reference_pairs){

				/* I will get a list of uncalibrated tmds for each symbol for each reference repeat
				*  Then I'll average these, so we have one average tmd per symbol across the reference repeats
				*  Then I'll calibrate these to the per-symbol calibration tmds
				*  Then I'll do a weighted average across the symbols, to give a final TMD value for this pair
				*/

				double calibrated_tmd_wrt_pair = Fuse::Analysis::calculate_calibrated_tmd_for_pair(
					target,
					symbols,
					reference_pair,
					profile,
					reference_repeats_list,
					Fuse::Config::tmd_bin_count,
					Fuse::Config::weighted_tmd);

				tmd_per_reference_pair.insert(std::make_pair(pair_idx, calibrated_tmd_wrt_pair));
				pair_idx++;
			}

			// Calculate the overall epd
			std::vector<double> tmds;
			tmds.reserve(tmd_per_reference_pair.size());
			for(auto pair_result : tmd_per_reference_pair){
				tmds.push_back(pair_result.second);
			}
			double epd = Fuse::calculate_weighted_geometric_mean(tmds);

			spdlog::info("Overall {} of {} repeat {} is: {}.",
				Fuse::convert_metric_to_string(metric),
				Fuse::convert_strategy_to_string(strategy),
				repeat_idx,
				epd
			);

			target.save_accuracy_results_to_disk(metric, strategy, repeat_idx, epd, tmd_per_reference_pair);

		}

	}

	spdlog::info("Finished analysing the accuracy of the combined profiles.");

}

void Fuse::generate_bc_sequence(
		Fuse::Target& target
		){

	std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > combination_sequence =
		Fuse::Sequence_generator::generate_bc_sequence(target, 3, 3);

	// Convert to a proper combination sequence
	Fuse::Combination_sequence sequence;
	unsigned int next_part_idx = 0;
	for(auto pair : combination_sequence){

		Fuse::Sequence_part part;
		part.part_idx = next_part_idx;
		part.overlapping = pair.first;
		part.unique = pair.second;

		sequence.push_back(part);

		next_part_idx++;
	}
	
	target.set_combination_sequence(sequence);

}

void Fuse::calculate_calibration_tmds(
		Fuse::Target& target
		){

	if(Fuse::Config::lazy_load_references == false)
		target.load_reference_distributions();

	auto reference_pairs = target.get_reference_pairs();

	std::vector<unsigned int> reference_repeats_list(target.get_num_reference_repeats());
	std::iota(reference_repeats_list.begin(), reference_repeats_list.end(), 0);
	auto reference_repeat_combinations = Fuse::Util::get_unique_combinations(reference_repeats_list, 2);

	spdlog::info("Calculating calibration TMDs for {} reference pairs and {} combinations of the reference repeats.",
		reference_pairs.size(),
		reference_repeat_combinations.size()
	);

	// We always get calibration TMDs for all symbols, and optionally for each individual symbol
	std::vector<Fuse::Symbol> symbols = {"all_symbols"};
	if(Fuse::Config::calculate_per_workfunction_tmds){
		auto all_symbols = target.get_statistics()->get_unique_symbols(false);
		symbols.insert(symbols.end(), all_symbols.begin(), all_symbols.end());
	}

	unsigned int pair_idx = 0;
	for(auto reference_pair : reference_pairs){

		// Map of symbol to (list of the symbol's reference tmds for each combination of repeats)
		std::map<Fuse::Symbol, std::vector<double> > reference_tmd_per_combination_per_symbol;

		// Map of symbol to (list of the number of instances of that symbol for each combination of repeats)
		std::map<Fuse::Symbol, std::vector<double> > num_instances_per_combination_per_symbol;

		// Check if we have already calibrated this reference pair (assume if we have one symbol, we have all)
		auto calibration_tmd_pair = target.get_or_load_calibration_tmd(reference_pair, "all_symbols");
		if(calibration_tmd_pair.first >= 0.0){
			spdlog::debug("Already calibrated the event pair {}:{}.", pair_idx, Fuse::Util::vector_to_string(reference_pair));
			pair_idx++;
			continue;
		}

		spdlog::debug("Running calibration for the event pair {}:{}.", pair_idx, Fuse::Util::vector_to_string(reference_pair));
		for(auto combination : reference_repeat_combinations){

			for(auto symbol : symbols){

				std::vector<Fuse::Symbol> constrained_symbols;
				if(symbol != "all_symbols")
					constrained_symbols = {symbol};

				auto distribution_one = target.get_or_load_reference_distribution(reference_pair, combination.at(0), constrained_symbols);
				auto distribution_two = target.get_or_load_reference_distribution(reference_pair, combination.at(1), constrained_symbols);

				std::vector<std::pair<int64_t, int64_t> > bounds_per_event;
				for(auto event : reference_pair)
					bounds_per_event.push_back(target.get_statistics()->get_bounds(event, symbol));

				auto tmd = Fuse::Analysis::calculate_uncalibrated_tmd(distribution_one, distribution_two, bounds_per_event, Fuse::Config::tmd_bin_count);

				auto symbol_iter = reference_tmd_per_combination_per_symbol.find(symbol);
				if(symbol_iter == reference_tmd_per_combination_per_symbol.end()){

					std::vector<double> tmds_per_combination_for_this_symbol = {tmd};
					std::vector<double> num_instances_per_combination_for_this_symbol = {static_cast<double>(distribution_one.size())};

					reference_tmd_per_combination_per_symbol.insert(std::make_pair(symbol, tmds_per_combination_for_this_symbol));
					num_instances_per_combination_per_symbol.insert(std::make_pair(symbol, num_instances_per_combination_for_this_symbol));

				} else {

					symbol_iter->second.push_back(tmd);
					num_instances_per_combination_per_symbol.find(symbol)->second.push_back(static_cast<double>(distribution_one.size()));

				}

			}

		}

		// Now, for each symbol, average the tmds across the combinations to give the calibration tmd for the symbol for the pair

		for(auto symbol : symbols){

			auto tmds = reference_tmd_per_combination_per_symbol.find(symbol)->second;
			auto num_instances_list = num_instances_per_combination_per_symbol.find(symbol)->second;

			Fuse::Stats tmd_stats = Fuse::calculate_stats_from_values(tmds);
			auto median_tmd = Fuse::calculate_median_from_values(tmds);

			Fuse::Stats num_instances_stats = Fuse::calculate_stats_from_values(num_instances_list);

			if(num_instances_stats.min != num_instances_stats.max)
				spdlog::warn("Reference distribution for {} and symbol '{}' has variable instance counts across combinations (from {} to {}).",
					Fuse::Util::vector_to_string(reference_pair),
					symbol,
					num_instances_stats.min,
					num_instances_stats.max
				);

			target.save_reference_calibration_tmd_to_disk(
				symbol,
				reference_pair,
				pair_idx,
				tmd_stats.min,
				tmd_stats.max,
				tmd_stats.mean,
				tmd_stats.std,
				median_tmd,
				num_instances_stats.mean
			);

		}

		pair_idx++;
	}

	spdlog::info("Finished calculating calibration TMDs.");

}

void Fuse::add_profile_event_values_to_statistics(
		Fuse::Profile_p profile,
		Fuse::Statistics_p statistics
		){

	auto instances = profile->get_instances(true);
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





