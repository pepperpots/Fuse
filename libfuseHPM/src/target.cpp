#include "target.h"
#include "analysis.h"
#include "config.h"
#include "fuse.h"
#include "instance.h"
#include "profiling.h"
#include "statistics.h"
#include "util.h"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <fstream>
#include <iomanip>

void Fuse::Target::parse_json_mandatory(nlohmann::json& j){

	this->binary = j["binary"];
	this->binary_directory = j["binary_directory"];

	std::string runtime_str = j["runtime"];
	this->runtime = Fuse::convert_string_to_runtime(runtime_str);

	for(auto event : j["target_events"])
		this->target_events.push_back(Fuse::Util::lowercase(event));

	std::sort(this->target_events.begin(), this->target_events.end());

	this->references_directory = j["references_directory"];
	this->tracefiles_directory = j["tracefiles_directory"];
	this->combinations_directory = j["combinations_directory"];
	this->papi_directory = j["papi_directory"];
	this->results_directory = j["results_directory"];

	/* Hard-coded files */

	this->statistics_filename = "event_statistics.csv";
	this->pairwise_mi_filename = "pairwise_mi_values.csv";
	this->sequence_generator_profile_mappings_filename = "profiled_event_sets.csv";
	this->sequence_generator_combination_mappings_filename = "combinations.csv";
	this->sequence_generator_tracefiles_directory = "tracefiles";
	this->sequence_generator_combined_profiles_directory = "combined_profiles";
	this->logs_directory = "logs";

}

void Fuse::Target::parse_json_optional(nlohmann::json& j){

	if(j.count("args"))
		this->args = j["args"];
	else
		this->args = "";

	if(j.count("should_clear_cache"))
		this->should_clear_cache = j["should_clear_cache"];
	else
		this->should_clear_cache = false;

	if(j.count("combined_indexes") && j["combined_indexes"].is_null() == false){

		auto strategy_objects = j["combined_indexes"];
		for(auto strat_iter = strategy_objects.begin(); strat_iter != strategy_objects.end(); strat_iter++){

			for(auto indexes_iter = strat_iter->begin(); indexes_iter != strat_iter->end(); indexes_iter++){

				std::string strategy_string = indexes_iter.key();
				Fuse::Strategy strategy = Fuse::convert_string_to_strategy(strategy_string);
				std::vector<unsigned int> combined_indexes = indexes_iter.value();

				this->combined_indexes[strategy] = combined_indexes;
				spdlog::debug("There are {} combined target profiles for strategy '{}'.", combined_indexes.size(), strategy_string);

			}
		}
	}

	this->sequence_generation_directory = "sequence_generation";
	if(j.count("sequence_generation_directory"))
		this->sequence_generation_directory = j["sequence_generation_directory"];
	
	this->num_reference_repeats = 0;
	if(j.count("num_reference_repeats"))
		this->num_reference_repeats = j["num_reference_repeats"];

	this->num_bc_sequence_repeats = 0;
	if(j.count("num_bc_sequence_repeats"))
		this->num_bc_sequence_repeats = j["num_bc_sequence_repeats"];

	this->num_minimal_sequence_repeats = 0;
	if(j.count("num_minimal_sequence_repeats"))
		this->num_minimal_sequence_repeats = j["num_minimal_sequence_repeats"];

	if(j.count("reference_sets") && j["reference_sets"].is_null() == false){
		for(auto set_iter : j["reference_sets"])
			this->reference_sets.push_back(Fuse::Util::vector_to_lowercase(set_iter));
	}

	Event_set unique_events_so_far;

	// Sequences are lists of JSON objects, and thus their order in the JSON is maintained
	if(j.count("bc_sequence") && j["bc_sequence"].is_null() == false){
		for(auto sequence_part : j["bc_sequence"]){

			if(sequence_part.count("overlapping") == false || sequence_part["overlapping"].is_null())
				throw std::invalid_argument("BC sequence in target JSON does not contain a valid set of overlapping events.");

			if(sequence_part.count("unique") == false || sequence_part["unique"].is_null())
				throw std::invalid_argument("BC sequence in target JSON does not contain a valid set of unique events.");

			struct Fuse::Sequence_part part;

			Fuse::Event_set overlapping = sequence_part["overlapping"];
			overlapping = Fuse::Util::vector_to_lowercase(overlapping);
			Fuse::Event_set unique = sequence_part["unique"];
			unique = Fuse::Util::vector_to_lowercase(unique);

			/* Check that overlapping events are not-unique */
			Fuse::Event_set intersection_check;
			std::set_difference(overlapping.begin(),overlapping.end(),
				unique_events_so_far.begin(),unique_events_so_far.end(),
				std::back_inserter(intersection_check));
			if(intersection_check.size() > 0){
				throw std::invalid_argument(
					fmt::format("BC sequence contains the overlapping events ({}) that were not previously profiled.",
					Fuse::Util::vector_to_string(intersection_check)));
			}

			/* Check that unique events are unique */
			intersection_check.clear();
			std::set_intersection(unique_events_so_far.begin(),unique_events_so_far.end(),
				unique.begin(),unique.end(),
				std::back_inserter(intersection_check));

			if(intersection_check.size() > 0){
				throw std::invalid_argument(
					fmt::format("BC sequence contains the same unique events ({}) in different profiles.",
					Fuse::Util::vector_to_string(intersection_check)));
			}

			unique_events_so_far.insert(unique_events_so_far.end(), unique.begin(), unique.end());

			part.part_idx = this->bc_sequence.size();
			part.overlapping = overlapping;
			part.unique = unique;

			this->bc_sequence.push_back(part);
		}
	}

	unique_events_so_far.clear();

	if(j.count("minimal_sequence") && j["minimal_sequence"].is_null() == false){
		for(auto sequence_part : j["minimal_sequence"]){

			if(sequence_part.count("overlapping"))
				throw std::invalid_argument("Minimal sequence in target JSON contains overlapping events. This is not valid.");

			if(sequence_part.count("unique") == false || sequence_part["unique"].is_null())
				throw std::invalid_argument("Minimal sequence in target JSON does not contain a valid set of unique events.");

			Fuse::Event_set unique = sequence_part["unique"];
			unique = Fuse::Util::vector_to_lowercase(unique);

			/* Check that unique events are unique */
			Fuse::Event_set intersection_check;
			std::set_intersection(unique_events_so_far.begin(),unique_events_so_far.end(),
				unique.begin(),unique.end(),
				std::back_inserter(intersection_check));

			if(intersection_check.size() > 0){
				throw std::invalid_argument(
					fmt::format("Minimal sequence contains the same unique events ({}) in different profiles.",
					Fuse::Util::vector_to_string(intersection_check)));
			}

			unique_events_so_far.insert(unique_events_so_far.end(), unique.begin(), unique.end());

			struct Fuse::Sequence_part part;
			// part.overlapping is empty by default
			part.part_idx = this->minimal_sequence.size();
			part.unique = unique;

			this->minimal_sequence.push_back(part);
		}
	}

}

Fuse::Target::Target(std::string target_dir):
			target_directory(target_dir),
			calibrations_loaded(false),
			pairwise_mi_loaded(false),
			modified(false)
		{

	// Load the JSON
	std::string json_filename = target_dir + "/fuse.json";
	spdlog::trace("Loading new Fuse target from {}.", json_filename);

	std::ifstream ifs(json_filename);

	if(ifs.is_open() == false)
		throw std::runtime_error(fmt::format("Cannot open the Fuse target JSON at {}.", json_filename));

	nlohmann::json j;

	try {

		ifs >> j;
		parse_json_mandatory(j);
		parse_json_optional(j);

	} catch (const nlohmann::json::exception& e){
		throw std::domain_error(fmt::format("Unable to parse the Fuse target JSON via nlohmann::json. Exception was: {}.", e.what()));
	} catch (const std::domain_error& e){
		throw std::domain_error(fmt::format("Could not load Fuse target JSON due to invalid or missing data. Exception was: {}.", e.what()));
	} catch (const std::invalid_argument& e){
		throw std::invalid_argument(fmt::format("Could not load Fuse target JSON due to invalid JSON formatting. Exception was: {}.", e.what()));
	}

	// Check that the directories are available for later
	this->check_or_create_directories();

	if(Fuse::Config::client_managed_logging == false){
		Fuse::initialize_logging(this->get_logs_directory(), true, Fuse::Config::fuse_log_level);
	}

	this->initialize_statistics();

	spdlog::info("Loaded Fuse target from {}.", json_filename);

}

void Fuse::Target::generate_json_mandatory(nlohmann::json& j){

	j["binary"] = this->binary;
	j["binary_directory"] = this->binary_directory;

	j["runtime"] = Fuse::convert_runtime_to_string(this->runtime);

	j["references_directory"] = this->references_directory;
	j["tracefiles_directory"] = this->tracefiles_directory;
	j["combinations_directory"] = this->combinations_directory;
	j["results_directory"] = this->results_directory;
	j["papi_directory"] = this->papi_directory;
	j["sequence_generation_directory"] = this->sequence_generation_directory;

	j["target_events"] = this->target_events;

}

void Fuse::Target::generate_json_optional(nlohmann::json& j){

	j["should_clear_cache"] = this->should_clear_cache;

	if(this->args != "")
		j["args"] = this->args;

	if(this->combined_indexes.size() > 0){
		nlohmann::json strat_json;

		for(auto strat_iter : this->combined_indexes){
			nlohmann::json indexes_json;

			std::string strategy_string = Fuse::convert_strategy_to_string(strat_iter.first);
			indexes_json[strategy_string] = strat_iter.second;

			strat_json.push_back(indexes_json);
		}

		j["combined_indexes"] = strat_json;
	}
	
	if(this->num_reference_repeats > 0)
		j["num_reference_repeats"] = this->num_reference_repeats;

	if(this->num_bc_sequence_repeats > 0)
		j["num_bc_sequence_repeats"] = this->num_bc_sequence_repeats;

	if(this->num_minimal_sequence_repeats > 0)
		j["num_minimal_sequence_repeats"] = this->num_minimal_sequence_repeats;

	if(this->reference_sets.size() > 0){
		nlohmann::json sets_json;

		for(auto reference_set : this->reference_sets){
			sets_json.push_back(reference_set);
		}

		j["reference_sets"] = sets_json;
	}

	if(this->bc_sequence.size() > 0){
		nlohmann::json sequence_json;

		for(auto sequence_part : this->bc_sequence){
			nlohmann::json part_json;

			//part_json["part_index"] = sequence_part.part_idx;
			part_json["overlapping"] = sequence_part.overlapping;
			part_json["unique"] = sequence_part.unique;

			sequence_json.push_back(part_json);
		}

		j["bc_sequence"] = sequence_json;
	}

	if(this->minimal_sequence.size() > 0){
		nlohmann::json sequence_json;

		for(auto sequence_part : this->minimal_sequence){
			nlohmann::json part_json;

			//part_json["part_index"] = sequence_part.part_idx;
			part_json["unique"] = sequence_part.unique;

			sequence_json.push_back(part_json);
		}

		j["minimal_sequence"] = sequence_json;
	}

}

void Fuse::Target::save(){

	if(modified == false){
		spdlog::warn("Attempted to save a Fuse target JSON that hasn't been modified.");
		return;
	}

	std::string json_filename = this->target_directory + "/fuse.json";
	spdlog::trace("Saving Fuse target to json: {}.", json_filename);

	nlohmann::json j;

	this->generate_json_mandatory(j);
	this->generate_json_optional(j);

	std::ofstream out(json_filename);

	if(out.is_open() == false)
		throw std::runtime_error(fmt::format("Cannot open the JSON file for writing: {}", json_filename));

	out << std::setw(2) << j;
	out.close();

	// Save statistics too, if necessary
	if(this->statistics != nullptr)
		this->statistics->save();

}

void Fuse::Target::check_or_create_directories(){

	Fuse::Util::check_or_create_directory(this->get_results_directory());
	Fuse::Util::check_or_create_directory(this->get_references_directory());
	Fuse::Util::check_or_create_directory(this->get_tracefiles_directory());
	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->combinations_directory);
	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->sequence_generation_directory);

	Fuse::Util::check_or_create_directory(this->get_sequence_generation_tracefiles_directory());
	Fuse::Util::check_or_create_directory(this->get_sequence_generation_combined_profiles_directory());
	Fuse::Util::check_or_create_directory(this->get_logs_directory());

	bool exists = Fuse::Util::check_file_existance(this->binary_directory + "/" + this->binary);
	if(exists == false)
		throw std::invalid_argument(fmt::format("The target binary {} could not be found.", this->binary_directory + "/" + this->binary));

	exists = Fuse::Util::check_file_existance(this->papi_directory + "/papi_avail");
	if(exists == false)
		throw std::invalid_argument(fmt::format("{} could not be found.", this->papi_directory + "/papi_avail"));

	exists = Fuse::Util::check_file_existance(this->papi_directory + "/papi_event_chooser");
	if(exists == false)
		throw std::invalid_argument(fmt::format("{} could not be found.", this->papi_directory + "/papi_event_chooser"));

}

std::string Fuse::Target::get_logs_directory(){
	return (this->target_directory + "/" + this->logs_directory);
}

std::string Fuse::Target::get_papi_directory(){
	return this->papi_directory;
}

std::string Fuse::Target::get_tracefiles_directory(){
	return (this->target_directory + "/" + this->tracefiles_directory);
}

std::string Fuse::Target::get_references_directory(){
	return (this->target_directory + "/" + this->references_directory);
}

std::string Fuse::Target::get_results_directory(){
	return (this->target_directory + "/" + this->results_directory);
}

std::string Fuse::Target::get_combination_filename(
		Fuse::Strategy strategy,
		unsigned int repeat_idx
		){

	std::stringstream ss;
	ss << this->target_directory << "/" << this->combinations_directory << "/";
	ss << Fuse::convert_strategy_to_string(strategy);

	Fuse::Util::check_or_create_directory(ss.str());

	ss << "/combination_" << repeat_idx << ".txt";
	return ss.str();
}

std::string Fuse::Target::get_target_binary(){
	return (this->binary_directory + "/" + this->binary);
}

Fuse::Runtime Fuse::Target::get_target_runtime(){
	return this->runtime;
}

std::string Fuse::Target::get_target_args(){
	return this->args;
}

unsigned int Fuse::Target::get_num_sequence_repeats(bool minimal){
	if(minimal)
		return this->num_minimal_sequence_repeats;
	else
		return this->num_bc_sequence_repeats;
}

unsigned int Fuse::Target::get_num_reference_repeats(){
	return this->num_reference_repeats;
}

void Fuse::Target::increment_num_sequence_repeats(bool minimal){
	if(minimal)
		this->num_minimal_sequence_repeats++;
	else
		this->num_bc_sequence_repeats++;

	this->modified = true;
}

Fuse::Combination_sequence Fuse::Target::get_sequence(bool minimal){
	if(minimal)
		return this->minimal_sequence;
	else
		return this->bc_sequence;
}

void Fuse::Target::set_combination_sequence(Fuse::Combination_sequence sequence){
	this->bc_sequence = sequence;
}

Fuse::Event_set Fuse::Target::get_target_events(){
	return this->target_events;
}

Fuse::Event_set Fuse::Target::get_filtered_events(){
	return this->filtered_events;
}

void Fuse::Target::set_filtered_events(Fuse::Event_set filter_to_events){
	this->filtered_events = filter_to_events;
}

bool Fuse::Target::get_should_clear_cache(){
	return this->should_clear_cache;
}

std::string Fuse::Target::get_calibration_tmds_filename(){

	auto references_directory = this->get_references_directory();

	std::stringstream ss;
	ss << references_directory << "/calibration_tmds_" << Fuse::Config::tmd_bin_count << ".csv";

	return ss.str();
}

std::string Fuse::Target::get_results_filename(
		Fuse::Accuracy_metric metric
		){

	auto results_directory = this->get_results_directory();

	std::stringstream ss;
	ss << results_directory << "/" << Fuse::convert_metric_to_string(metric) << "_accuracy_results.txt";

	return ss.str();
}

Fuse::Statistics_p Fuse::Target::get_statistics(){
	if(this->statistics == nullptr)
		throw std::runtime_error("Tried to get event statistics, but they have not yet been initialized.");
	else
		return this->statistics;
}

void Fuse::Target::store_loaded_sequence_profile(
		unsigned int repeat_index,
		Fuse::Sequence_part part,
		Fuse::Profile_p execution_profile,
		bool minimal
		){

	std::map<unsigned int, std::map<unsigned int, Fuse::Profile_p> >::iterator repeat_map_iter;

	bool repeat_profiles_exist = false;

	if(minimal){
		repeat_map_iter = this->loaded_minimal_sequence_profiles.find(repeat_index);
		if(repeat_map_iter != this->loaded_minimal_sequence_profiles.end())
			repeat_profiles_exist = true;
	} else {
		repeat_map_iter = this->loaded_non_minimal_sequence_profiles.find(repeat_index);
		if(repeat_map_iter != this->loaded_non_minimal_sequence_profiles.end())
			repeat_profiles_exist = true;
	}

	if(repeat_profiles_exist){

		auto part_iter = repeat_map_iter->second.find(part.part_idx);
		if(part_iter == repeat_map_iter->second.end())
			repeat_map_iter->second.insert(std::make_pair(part.part_idx, execution_profile));
		else
			throw std::logic_error("Attempted to add a loaded sequence profile, which already exists.");

	} else {

		std::map<unsigned int, Fuse::Profile_p> profiles_for_repeat_index;
		profiles_for_repeat_index.insert(std::make_pair(part.part_idx, execution_profile));
		if(minimal)
			this->loaded_minimal_sequence_profiles.insert(std::make_pair(repeat_index, profiles_for_repeat_index));
		else
			this->loaded_non_minimal_sequence_profiles.insert(std::make_pair(repeat_index, profiles_for_repeat_index));

	}

}

std::vector<Fuse::Profile_p> Fuse::Target::load_and_retrieve_sequence_profiles(
		unsigned int repeat_idx,
		bool minimal
		){

	auto minimal_str = minimal ? "minimal" : "non_minimal";

	auto sequence = this->get_sequence(minimal);
	if(sequence.size() < 1){

		if(minimal){
			spdlog::info("The minimal event partitioning was not defined in the target JSON. {}",
				"Greedily generating a minimal sequence..."
			);

			auto sets = Fuse::Profiling::greedy_generate_minimal_partitioning(
				this->target_events,
				this->papi_directory
			);

			spdlog::info("Generated a minimal partitioning comprising {} profiles.", sets.size());

			for(auto set : sets){

				struct Fuse::Sequence_part part;

				Fuse::Event_set overlapping;
				Fuse::Event_set unique = set;
				unique = Fuse::Util::vector_to_lowercase(unique);

				part.part_idx = this->minimal_sequence.size();
				part.overlapping = overlapping;
				part.unique = unique;

				this->minimal_sequence.push_back(part);

			}

			sequence = this->minimal_sequence;
			this->modified = true;
			this->save();

		} else {
			throw std::runtime_error("No BC sequence has been defined in the target JSON.");
		}

	}

	spdlog::info("Loading the {} sequence profiles for repeat index {}.", minimal_str, repeat_idx);

	// Do I have any loaded profiles for this repeat index?
	bool repeat_profiles_exist = false;
	std::map<unsigned int, std::map<unsigned int, Fuse::Profile_p> >::iterator repeat_map_iter;
	if(minimal){
		repeat_map_iter = this->loaded_minimal_sequence_profiles.find(repeat_idx);
		if(repeat_map_iter != this->loaded_minimal_sequence_profiles.end())
			repeat_profiles_exist = true;
	} else {
		repeat_map_iter = this->loaded_non_minimal_sequence_profiles.find(repeat_idx);
		if(repeat_map_iter != this->loaded_non_minimal_sequence_profiles.end())
			repeat_profiles_exist = true;
	}

	std::vector<Fuse::Profile_p> sequence_profiles;

	for(auto part : sequence){

		// Have I already got the sequence profile loaded?
		if(repeat_profiles_exist){
			auto part_iter = repeat_map_iter->second.find(part.part_idx);
			if(part_iter != repeat_map_iter->second.end()){
				sequence_profiles.push_back(part_iter->second);
				continue;
			}
		}

		// If here, it is not loaded, so load it
		std::stringstream ss;
		ss << this->get_tracefiles_directory() << "/" << minimal_str << "_";
		ss << "sequence_profile_" << repeat_idx << "-" << part.part_idx << ".ost";
		auto tracefile = ss.str();

		Fuse::Profile_p execution_profile(new Fuse::Execution_profile(tracefile, this->get_target_binary(), this->filtered_events));
		execution_profile->load_from_tracefile(this->get_target_runtime(), false);

		sequence_profiles.push_back(execution_profile);

		// Add it to this target
		this->store_loaded_sequence_profile(repeat_idx, part, execution_profile, minimal);

	}

	return sequence_profiles;

}

void Fuse::Target::register_new_combined_profile(
		Fuse::Strategy strategy,
		unsigned int repeat_idx,
		Fuse::Profile_p execution_profile
		){

	auto strategy_iter = this->combined_indexes.find(strategy);
	if(strategy_iter == this->combined_indexes.end()){
		std::vector<unsigned int> indexes = {repeat_idx};
		this->combined_indexes.insert(std::make_pair(strategy, indexes));
	}
	else
		strategy_iter->second.push_back(repeat_idx);

	// Now dump the execution profile to file
	std::string combined_instances_filename = this->get_combination_filename(strategy, repeat_idx);
	execution_profile->print_to_file(combined_instances_filename);

	this->modified = true;
}

unsigned int Fuse::Target::get_num_combined_profiles(Fuse::Strategy strategy){

	auto strategy_iter = this->combined_indexes.find(strategy);
	if(strategy_iter == this->combined_indexes.end())
		return 0;
	else
		return strategy_iter->second.size();

}

void Fuse::Target::store_combined_profile(
			unsigned int repeat_idx,
			Fuse::Strategy strategy,
			Fuse::Profile_p combined_profile
		){

	auto combined_iter = this->loaded_combined_profiles.find(strategy);
	if(combined_iter == this->loaded_combined_profiles.end()){

		std::map<unsigned int, Fuse::Profile_p> combined_profiles;
		combined_profiles.insert(std::make_pair(repeat_idx, combined_profile));
		this->loaded_combined_profiles.insert(std::make_pair(strategy, combined_profiles));

	} else {

		if(combined_iter->second.find(repeat_idx) != combined_iter->second.end())
			spdlog::warn("When storing the combined profile for repeat index {} via strategy {}, a previously combined profile was found. The old combination will be overwritten.",
				repeat_idx, Fuse::convert_strategy_to_string(strategy));

		combined_iter->second[repeat_idx] = combined_profile;

	}

}

bool Fuse::Target::combined_profile_exists(Fuse::Strategy strategy, unsigned int repeat_idx){

	auto strategy_iter = this->combined_indexes.find(strategy);
	if(strategy_iter == this->combined_indexes.end())
		return false;

	if(std::find(strategy_iter->second.begin(), strategy_iter->second.end(), repeat_idx) == strategy_iter->second.end())
		return false;

	return true;

}

void Fuse::Target::initialize_statistics(){

	Fuse::Statistics_p stats(new Fuse::Statistics(this->target_directory + "/" + this->statistics_filename));
	stats->load();
	this->statistics = stats;

}

std::vector<Fuse::Event_set> Fuse::Target::get_or_generate_reference_sets(){

	if(this->reference_sets.size() > 0)
		return this->reference_sets;

	// First, generate all the desired pairs: each event is mapped to a list of events it needs paired with
	std::vector<Fuse::Event_set> remaining_pairs;
	Fuse::Event_set events = this->target_events;

	while(events.size() > 1){
		Fuse::Event event = events.front();
		events.erase(events.begin());

		Fuse::Event_set target_list;
		target_list.insert(target_list.end(), events.begin(), events.end());

		remaining_pairs.push_back(target_list);
	}

	// Then, greedily generate compatible event sets that contain these pairs
	Fuse::Event_set current_set;
	unsigned int next_event_idx = 0;
	while(next_event_idx < remaining_pairs.size()){

		// Next event to get its needed pairings
		Fuse::Event event = this->target_events.at(next_event_idx);

		// Check if this event is already done
		if(remaining_pairs.at(next_event_idx).size() == 0){
			next_event_idx++;
			continue;
		}

		// Filter the remaining pairs to the pairs which haven't already been profiled

		std::vector<Fuse::Event_set> previous_event_sets = this->reference_sets; // Copy
		previous_event_sets.push_back(current_set); // Also includes what we currently have in the profile

		Fuse::Event_set complement_events_to_remove;
		for(auto complement_event : remaining_pairs.at(next_event_idx)){

			Fuse::Event_set pair = {event, complement_event}; // These are ordered
			for(auto previous_set : previous_event_sets){

				Fuse::Event_set difference;
				std::set_difference(pair.begin(), pair.end(), previous_set.begin(), previous_set.end(),
					std::back_inserter(difference)
				);

				if(difference.size() == 0){
					// We have already simultaneously profiled these events in a previous set!
					complement_events_to_remove.push_back(complement_event);
					break;
				}
			}
		}

		Fuse::Event_set filtered_target_events;
		std::set_difference(remaining_pairs.at(next_event_idx).begin(), remaining_pairs.at(next_event_idx).end(),
			complement_events_to_remove.begin(), complement_events_to_remove.end(),
			std::back_inserter(filtered_target_events)
		);
		remaining_pairs.at(next_event_idx) = filtered_target_events;

		// Check if this event is now done after filtering
		if(remaining_pairs.at(next_event_idx).size() == 0){
			next_event_idx++;
			continue;
		}

		current_set.push_back(event);
		if(Fuse::Profiling::compatibility_check(current_set, this->papi_directory) == false){
			current_set.pop_back(); // Remove the incompatible event
			this->reference_sets.push_back(current_set); // Save the event set
			current_set = {event}; // Start a fresh set with the event that we are currently fulfilling
		}

		// Keep adding until the current set is incompatible or we move to a new next_event_idx
		while(remaining_pairs.at(next_event_idx).size() > 0){

			current_set.push_back(remaining_pairs.at(next_event_idx).front());
			if(Fuse::Profiling::compatibility_check(current_set, this->papi_directory) == false){
				current_set.pop_back();
				this->reference_sets.push_back(current_set);
				current_set = {event};
			} else {
				remaining_pairs.at(next_event_idx).erase(remaining_pairs.at(next_event_idx).begin());
			}

		}

		// We no longer have any remaining for this event, so move onto the next
		next_event_idx++;
	}

	// We might have finished on a so-far compatible set, so save it
	if(current_set.size() > 1)
		this->reference_sets.push_back(current_set);

	this->modified = true;
	this->save();

	return this->reference_sets;
}

std::string Fuse::Target::get_reference_filename_for(
		unsigned int reference_idx,
		unsigned int repeat_idx
		){

	auto references_dir = this->get_references_directory();
	std::stringstream ss;
	ss << references_dir << "/distribution_" << reference_idx << "_" << repeat_idx << ".bin";

	return ss.str();
}

void Fuse::Target::save_reference_values_to_disk(
		unsigned int reference_idx,
		unsigned int repeat_idx,
		Fuse::Event_set reference_set,
		std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > values_per_symbol
		){

	// I want to write the reference values as a compact binary format, to save space
	// TODO I should provide a utility function to dump a reference's values as csv, for debugging

	auto ref_filename = this->get_reference_filename_for(reference_idx, repeat_idx);
	spdlog::debug("Writing reference distribution for events {} to file {}.",
		Fuse::Util::vector_to_string(reference_set),
		ref_filename
	);

	auto file_stream = std::fstream(ref_filename, std::ios::out | std::ios::binary);
	if(file_stream.is_open() == false)
		throw std::runtime_error(fmt::format("Unable to open {} to write reference distribution data.", ref_filename));

	unsigned int num_events = reference_set.size();
	file_stream.write(reinterpret_cast<char*>(&num_events), sizeof(num_events));

	for(auto event : reference_set){
		unsigned int num_chars = event.size();
		file_stream.write(reinterpret_cast<char*>(&num_chars), sizeof(num_chars));
		file_stream.write(event.c_str(),num_chars);
	}

	unsigned int num_symbols = values_per_symbol.size();
	file_stream.write(reinterpret_cast<char*>(&num_symbols), sizeof(num_symbols));

	for(auto symbol_iter : values_per_symbol){

		auto symbol = symbol_iter.first;
		unsigned int num_chars = symbol.size();
		file_stream.write(reinterpret_cast<char*>(&num_chars), sizeof(num_chars));
		file_stream.write(symbol.c_str(),num_chars);

		auto values = symbol_iter.second;

		unsigned int num_instances = values.size();
		file_stream.write(reinterpret_cast<char*>(&num_instances), sizeof(num_instances));

		for(auto instance_values : values)
			file_stream.write(reinterpret_cast<char*>(&instance_values[0]), instance_values.size()*sizeof(instance_values[0]));

	}

	file_stream.close();

}

void Fuse::Target::load_reference_distributions(
		std::vector<unsigned int> reference_set_indexes_to_load,
		std::vector<unsigned int> reference_repeats_to_load
		){

	// TODO check if we have defined reference sets?

	if(reference_repeats_to_load.size() == 0){
		reference_repeats_to_load.assign(this->get_num_reference_repeats(),0);
		std::iota(reference_repeats_to_load.begin(), reference_repeats_to_load.end(), 0);
	}
	if(reference_set_indexes_to_load.size() == 0){
		reference_set_indexes_to_load.assign(this->reference_sets.size(),0);
		std::iota(reference_set_indexes_to_load.begin(), reference_set_indexes_to_load.end(), 0);
	}

	for(auto repeat : reference_repeats_to_load){

		for(auto ref_set_idx : reference_set_indexes_to_load){

			auto reference_set_iter = this->loaded_reference_distributions.find(ref_set_idx);
			if(reference_set_iter == this->loaded_reference_distributions.end()){

				auto reference_distribution = this->load_reference_distribution_from_disk(ref_set_idx, repeat);

				// Add the reference distribution to the map
				std::map<unsigned int, std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > > repeats_for_set;
				repeats_for_set.insert(std::make_pair(repeat, reference_distribution));
				this->loaded_reference_distributions.insert(std::make_pair(ref_set_idx, repeats_for_set));

			} else {

				auto repeat_iter = reference_set_iter->second.find(repeat);
				if(repeat_iter == reference_set_iter->second.end()){

					auto reference_distribution = this->load_reference_distribution_from_disk(ref_set_idx, repeat);
					reference_set_iter->second[repeat] = reference_distribution; // Replaces if exists

				} // else it already is loaded, so do nothing

			}

		}

	}

}

std::vector<std::vector<int64_t> > Fuse::Target::get_or_load_reference_distribution(
		Fuse::Event_set events,
		unsigned int repeat_idx,
		std::vector<Fuse::Symbol>& symbols
		){

	std::map<Fuse::Symbol, std::vector<std::vector<int64_t > > > reference_distribution_per_symbol;

	bool was_loaded = true; // by the end of this function, this bool is set to false if we had to load it from disk
	auto reference_set_idx = this->get_reference_set_index_for_events(events);

	#pragma omp critical (target_references)
	{
		auto reference_set_iter = this->loaded_reference_distributions.find(reference_set_idx);

		// Load it if it is not loaded
		if(reference_set_iter == this->loaded_reference_distributions.end()){

			reference_distribution_per_symbol = this->load_reference_distribution_from_disk(reference_set_idx, repeat_idx);
			was_loaded = false;

		} else {

			auto repeat_iter = reference_set_iter->second.find(repeat_idx);
			if(repeat_iter == reference_set_iter->second.end()){

				reference_distribution_per_symbol = this->load_reference_distribution_from_disk(reference_set_idx, repeat_idx);
				was_loaded = false;

			} else {

				reference_distribution_per_symbol = repeat_iter->second;
			}
		}
	}

	// Now filter for symbols
	if(symbols.size() == 0){
		symbols.reserve(reference_distribution_per_symbol.size());
		for(auto symbol_iter : reference_distribution_per_symbol){
			symbols.push_back(symbol_iter.first);
		}
	}

	std::vector<std::vector<int64_t> > concatenated_distribution;
	for(auto symbol : symbols){
		auto values_iter = reference_distribution_per_symbol.find(symbol);
		if(values_iter == reference_distribution_per_symbol.end())
			throw std::runtime_error(fmt::format("Cannot retrieve instances for symbol {} as this symbol does not exist.", symbol));

		concatenated_distribution.reserve(concatenated_distribution.size() + values_iter->second.size());
		concatenated_distribution.insert(concatenated_distribution.end(), values_iter->second.begin(), values_iter->second.end());
	}

	if(was_loaded == false && Config::lazy_load_references == false)
		spdlog::warn("The reference distribution for events {} (set {}) and for repeat {} was not loaded, but should have been.",
			Fuse::Util::vector_to_string(events),
			reference_set_idx,
			repeat_idx
		);

	return concatenated_distribution;

}

std::pair<double, double> Fuse::Target::get_or_load_calibration_tmd(
		Fuse::Event_set events,
		Fuse::Symbol symbol
		){

	auto reference_idx = this->get_reference_pair_index_for_event_pair(events);

	#pragma omp critical (target_calibrations)
	{
		if(this->calibrations_loaded == false){
			this->calibration_tmds = this->load_reference_calibrations_per_symbol();
		}
	}

	auto symbol_iter = this->calibration_tmds.find(symbol);
	if(symbol_iter == this->calibration_tmds.end())
		return std::make_pair(-1.0,-1.0);

	auto pair_iter = symbol_iter->second.find(reference_idx);
	if(pair_iter == symbol_iter->second.end())
		return std::make_pair(-1.0,-1.0);

	return pair_iter->second;

}

void Fuse::Target::save_reference_calibration_tmd_to_disk(
		Fuse::Symbol symbol,
		Fuse::Event_set events,
		unsigned int reference_idx,
		double min,
		double max,
		double mean,
		double std,
		double median,
		double mean_num_instances // to be used as a 'weight' for the symbol
		){

	spdlog::trace("Storing calibration TMD {} for symbol '{}' and reference {}:{}.",
		median,
		symbol,
		reference_idx,
		Fuse::Util::vector_to_string(events)
	);

	auto filename = this->get_calibration_tmds_filename();

	auto requires_header = true;
	if(Fuse::Util::check_file_existance(filename))
		requires_header = false;

	auto file_stream = std::ofstream(filename, std::ios_base::app);
	if(file_stream.is_open() == false)
		throw std::runtime_error(fmt::format("Unable to open {} to store calibration tmds.", filename));

	if(requires_header){
		std::string header("reference_idx,events,min,max,mean,std,median,mean_num_instances\n");
		file_stream << header;
	}

	auto events_str = Fuse::Util::vector_to_string(events,true,"-");

	file_stream << symbol;
	file_stream << "," << reference_idx;
	file_stream << "," << events_str;
	file_stream << "," << min;
	file_stream << "," << max;
	file_stream << "," << mean;
	file_stream << "," << std;
	file_stream << "," << median;
	file_stream << "," << mean_num_instances << std::endl;

	file_stream.close();

}

std::map<Fuse::Symbol, std::map<unsigned int, std::pair<double, double> > >
		Fuse::Target::load_reference_calibrations_per_symbol(
		){

	this->calibrations_loaded = true;
	std::map<Fuse::Symbol, std::map<unsigned int, std::pair<double, double> > > calibration_per_reference_per_symbol;

	auto filename = this->get_calibration_tmds_filename();

	auto file_stream = std::ifstream(filename);
	if(file_stream.is_open() == false)
		return calibration_per_reference_per_symbol;
		//throw std::runtime_error(fmt::format("Unable to open {} to load calibration tmds.", filename));

	std::string header;
	file_stream >> header;

	std::string line;
	while(file_stream >> line){

		auto split_line = Fuse::Util::split_string_to_vector(line, ',');

		Fuse::Symbol symbol = split_line.at(0);
		// There is no conversion to unsigned int, it must be caused from unsigned long!
		unsigned int reference_idx = static_cast<unsigned int>(std::stoul(split_line.at(1)));
		double median = std::stod(split_line.at(6));
		double num_instances = std::stod(split_line.at(7));

		std::pair<double, double> calibration_pair = std::make_pair(median, num_instances);

		auto symbol_iter = calibration_per_reference_per_symbol.find(symbol);
		if(symbol_iter == calibration_per_reference_per_symbol.end()){

			std::map<unsigned int, std::pair<double, double> > calibration_for_symbol;
			calibration_for_symbol.insert(std::make_pair(reference_idx, calibration_pair));

			calibration_per_reference_per_symbol.insert(std::make_pair(symbol, calibration_for_symbol));

		} else {

			auto reference_iter = symbol_iter->second.find(reference_idx);
			if(reference_iter != symbol_iter->second.end())
				spdlog::warn("When loading calibration TMDs from {}, inserted a calibration for '{}' and pair {} that already exists.",
					filename,
					symbol,
					reference_idx
				);

			symbol_iter->second[reference_idx] = calibration_pair; // Will replace if exists

		}

	}

	file_stream.close();
	return calibration_per_reference_per_symbol;

}

void Fuse::Target::save_accuracy_results_to_disk(
		Fuse::Accuracy_metric metric,
		Fuse::Strategy strategy,
		unsigned int repeat_idx,
		double epd,
		const std::map<unsigned int, double> tmd_per_reference_pair
		){

	spdlog::debug("Storing {} accuracy reuslts for strategy '{}' repeat {}.",
		Fuse::convert_metric_to_string(metric),
		Fuse::convert_strategy_to_string(strategy),
		repeat_idx
	);

	auto filename = this->get_results_filename(metric);

	auto requires_header = true;
	if(Fuse::Util::check_file_existance(filename))
		requires_header = false;

	auto file_stream = std::ofstream(filename, std::ios_base::app);
	if(file_stream.is_open() == false)
		throw std::runtime_error(fmt::format("Unable to open {} to store accuracy results.", filename));

	// TODO should condition on metric
	if(requires_header){
		std::string header("strategy,repeat,pair_idx,events,calibrated_tmd\n");
		file_stream << header;
	}

	auto strategy_str = Fuse::convert_strategy_to_string(strategy);
	auto event_pairs = this->get_reference_pairs();

	// Overall
	file_stream << strategy_str;
	file_stream << "," << repeat_idx;
	file_stream << ",-1";
	file_stream << ",overall";
	file_stream << "," << epd << std::endl;

	// Per reference pair
	for(auto pair : tmd_per_reference_pair){
		file_stream << strategy_str;
		file_stream << "," << repeat_idx;
		file_stream << "," << pair.first;
		file_stream << "," << Fuse::Util::vector_to_string(event_pairs.at(pair.first), true, "-");
		file_stream << "," << pair.second << std::endl;
	}

	file_stream.close();

}

unsigned int Fuse::Target::get_reference_pair_index_for_event_pair(
		Fuse::Event_set pair
		){

	std::sort(pair.begin(), pair.end());

	auto all_pairs = this->get_reference_pairs();
	for(decltype(all_pairs.size()) pair_idx=0; pair_idx<all_pairs.size(); pair_idx++){
		if(pair.at(0) == all_pairs.at(pair_idx).at(0) && pair.at(1) == all_pairs.at(pair_idx).at(1))
			return pair_idx;
	}

	throw std::runtime_error(fmt::format("Cannot find reference pair index for events {}.",
		Fuse::Util::vector_to_string(pair)));
}

unsigned int Fuse::Target::get_reference_set_index_for_events(
		Fuse::Event_set events
		){

	std::sort(events.begin(), events.end());
	auto reference_sets = this->get_or_generate_reference_sets();

	for(decltype(reference_sets.size()) reference_idx=0; reference_idx<reference_sets.size(); reference_idx++){

		auto reference_set = reference_sets.at(reference_idx);

		// Check if all events are in this reference set (there are none left over from set difference)
		Fuse::Event_set difference;
		std::set_difference(events.begin(), events.end(), reference_set.begin(), reference_set.end(),
			std::back_inserter(difference)
		);

		if(difference.size() == 0)
			return reference_idx;

	}

	throw std::runtime_error(fmt::format("Cannot find a reference set corresponding to events {}.",
		Fuse::Util::vector_to_string(events)));

}

std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > Fuse::Target::load_reference_distribution_from_disk(
		unsigned int reference_idx,
		unsigned int repeat_idx
		){

	std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > values_per_symbol;

	unsigned int num_instances_loaded = 0;

	auto ref_filename = this->get_reference_filename_for(reference_idx, repeat_idx);
	spdlog::debug("Reading reference distribution for reference index {} and repeat {} from file {}.",
		reference_idx,
		repeat_idx,
		ref_filename
	);

	auto file_stream = std::fstream(ref_filename, std::ios::in | std::ios::binary);
	if(file_stream.is_open() == false)
		throw std::runtime_error(fmt::format("Unable to open {} to read reference distribution data.", ref_filename));

	try {

		unsigned int num_events = 0;
		file_stream.read(reinterpret_cast<char*>(&num_events), sizeof(num_events));

		Fuse::Event_set events;

		for(unsigned int event_idx=0; event_idx<num_events; event_idx++){

			unsigned int num_chars = 0;
			file_stream.read(reinterpret_cast<char*>(&num_chars), sizeof(num_chars));

			std::string event;
			event.resize(num_chars);
			file_stream.read(reinterpret_cast<char*>(&event[0]),num_chars);

			events.push_back(event);
		}

		unsigned int num_symbols = 0;
		file_stream.read(reinterpret_cast<char*>(&num_symbols), sizeof(num_symbols));

		for(unsigned int symbol_idx=0; symbol_idx<num_symbols; symbol_idx++){

			unsigned int num_chars = 0;
			file_stream.read(reinterpret_cast<char*>(&num_chars), sizeof(num_chars));

			Fuse::Symbol symbol;
			symbol.resize(num_chars);
			file_stream.read(reinterpret_cast<char*>(&symbol[0]),num_chars);

			std::vector<std::vector<int64_t> > values;

			unsigned int num_instances = 0;
			file_stream.read(reinterpret_cast<char*>(&num_instances), sizeof(num_instances));
			values.reserve(num_instances);

			for(unsigned int instance_idx=0; instance_idx<num_instances; instance_idx++){
				std::vector<int64_t> instance_values;
				instance_values.resize(num_events);
				file_stream.read(reinterpret_cast<char*>(&instance_values[0]), num_events*sizeof(instance_values[0]));
				values.push_back(instance_values);
			}

			spdlog::trace("Loaded a reference distribution for a symbol '{}' containing {} instances of {} events.",
				symbol,
				values.size(),
				num_events
			);

			num_instances_loaded += values.size();

			values_per_symbol.insert(std::make_pair(symbol, values));

		}

		file_stream.close();

	} catch (std::ios_base::failure& e){
		throw std::runtime_error(fmt::format("Failed loading reference distribution from file {}. Error was: {}.",
			ref_filename, e.what()));
	}

	spdlog::debug("Loaded a reference distribution consisting of {} instances.", num_instances_loaded);

	return values_per_symbol;

}

void Fuse::Target::increment_num_reference_repeats(){

	this->num_reference_repeats++;
	this->modified = true;

}

void Fuse::Target::compress_references_tracefiles(
		std::vector<std::string> reference_tracefiles,
		unsigned int repeat_idx
		){

	char buffer[256];
	std::string result = "";

	// Check pbzip2 exists
	std::string check_cmd = "which pbzip2";
	auto pipe = popen(check_cmd.c_str(), "r");
	if(pipe == nullptr){
		spdlog::warn("Unable to compress reference tracefiles for repeat index {}, as cannot open pipe for '{}'.",
			repeat_idx, check_cmd);
		return;
	}

	while(!feof(pipe)){
		if(fgets(buffer, 256, pipe) != nullptr)
			result += buffer;
	}

	auto pbzip2_ret = pclose(pipe);

	if(pbzip2_ret != EXIT_SUCCESS){
		spdlog::warn("Unable to compress reference tracefiles for repeat index {}, as pbzip2 cannot be found.",
			repeat_idx);
		return;
	}

	std::stringstream ss;
	ss << this->get_tracefiles_directory() << "/references_" << repeat_idx << ".tar.bz2";
	auto compressed_filename = ss.str();

	std::string tracefiles_str;
	for(auto reference_name : reference_tracefiles)
		tracefiles_str += " " + Fuse::Util::get_filename_from_full_path(reference_name);

	// to compress:
	// tar -cf tracefiles/comp.tar.bz2 --use-compress-prog=pbzip2 --remove-files -C tracefiles/ a.txt b.txt
	// to decompress:
	// tar -C tracefiles/ -xf tracefiles/comp.tar.bz2 --remove-files

	std::string compress_cmd = "tar -cf " + compressed_filename + " --use-compress-prog=pbzip2 --remove-files";
	compress_cmd += " -C " + this->get_tracefiles_directory() + "/ ";
	compress_cmd += tracefiles_str;

	result = "";

	pipe = popen(compress_cmd.c_str(), "r");
	if(pipe == nullptr){
		spdlog::warn("Unable to compress reference tracefiles for repeat index {}, as cannot open pipe for '{}'.",
			repeat_idx, compress_cmd);
		return;
	}

	while(!feof(pipe)){
		if(fgets(buffer, 256, pipe) != nullptr)
			result += buffer;
	}

	auto ret = pclose(pipe);

	if(ret != EXIT_SUCCESS)
		spdlog::warn("Failed to compress reference tracefiles for repeat index {}, command '{}' returned '{}'.",
			repeat_idx, compress_cmd, result);

}

std::vector<Fuse::Event_set> Fuse::Target::get_reference_pairs(){

	Fuse::Event_set sorted_events = this->target_events;
	std::sort(sorted_events.begin(), sorted_events.end());

	auto reference_pairs = Fuse::Util::get_unique_combinations(sorted_events, 2);
	return reference_pairs;

}

Fuse::Profile_p Fuse::Target::get_or_load_combined_profile(
		Fuse::Strategy strategy,
		unsigned int repeat_idx
		){

	// Check if it has been combined and thus able to be loaded
	auto indexes_iter = this->combined_indexes.find(strategy);
	if(indexes_iter == this->combined_indexes.end()
			|| std::find(indexes_iter->second.begin(), indexes_iter->second.end(), repeat_idx)
				== indexes_iter->second.end()){

		throw std::invalid_argument(fmt::format(
			"Cannot load combined profile for strategy {} and repeat {}, as this combination does not exist.",
			Fuse::convert_strategy_to_string(strategy),
			repeat_idx)
		);

	}

	Fuse::Profile_p profile;

	auto strategy_iter = this->loaded_combined_profiles.find(strategy);
	if(strategy_iter == this->loaded_combined_profiles.end()){

		std::string combined_instances_filename = this->get_combination_filename(strategy, repeat_idx);
		profile = this->load_combined_profile_from_disk(combined_instances_filename);
	
		spdlog::debug("Loaded combined profile for strategy {} and repeat {} from disk.",
			Fuse::convert_strategy_to_string(strategy),
			repeat_idx
		);

		std::map<unsigned int, Fuse::Profile_p> profiles;
		profiles.insert(std::make_pair(repeat_idx, profile));

		this->loaded_combined_profiles.insert(std::make_pair(strategy, profiles));

	} else {

		auto repeat_iter = strategy_iter->second.find(repeat_idx);
		if(repeat_iter == strategy_iter->second.end()){

			std::string combined_instances_filename = this->get_combination_filename(strategy, repeat_idx);
			profile = this->load_combined_profile_from_disk(combined_instances_filename);
		
			spdlog::debug("Loaded combined profile for strategy {} and repeat {} from disk.",
				Fuse::convert_strategy_to_string(strategy),
				repeat_idx
			);

			strategy_iter->second.insert(std::make_pair(repeat_idx, profile));

		} else {

			profile = repeat_iter->second;

		}

	}

	return profile;

}

std::map<unsigned int, double> Fuse::Target::load_pairwise_mis_from_disk(){

	std::map<unsigned int, double> pairwise_values;

	auto filename = this->get_sequence_generation_pairwise_mi_filename();

	auto file_stream = std::ifstream(filename);
	if(file_stream.is_open() == false)
		return pairwise_values;

	std::string header;
	file_stream >> header;

	std::string line;
	while(file_stream >> line){

		auto split_line = Fuse::Util::split_string_to_vector(line, ',');

		// There is no conversion to unsigned int, it must be caused from unsigned long!
		unsigned int reference_idx = static_cast<unsigned int>(std::stoul(split_line.at(0)));
		double mi = std::stod(split_line.at(1));

		auto reference_iter = pairwise_values.find(reference_idx);
		if(reference_iter == pairwise_values.end()){

			pairwise_values.insert(std::make_pair(reference_idx, mi));

		} else {

			spdlog::warn("When loading pairwise MI from {}, inserted an MI for pair {} that already exists.",
				filename,
				reference_idx
			);

			pairwise_values[reference_idx] = mi; // Will replace if exists

		}

	}

	file_stream.close();
	
	this->pairwise_mi_loaded = true;
	return pairwise_values;

}

std::string Fuse::Target::get_sequence_generation_pairwise_mi_filename(){
	return this->target_directory
		+ "/" + this->sequence_generation_directory
		+ "/" + this->pairwise_mi_filename;
}

std::string Fuse::Target::get_sequence_generation_profile_mappings_filename(){
	return this->target_directory
		+ "/" + this->sequence_generation_directory
		+ "/" + this->sequence_generator_profile_mappings_filename;
}

std::string Fuse::Target::get_sequence_generation_combination_mappings_filename(){
	return this->target_directory
		+ "/" + this->sequence_generation_directory
		+ "/" + this->sequence_generator_combination_mappings_filename;
}

std::string Fuse::Target::get_sequence_generation_tracefiles_directory(){
	return this->target_directory
		+ "/" + this->sequence_generation_directory
		+ "/" + this->sequence_generator_tracefiles_directory;
}

std::string Fuse::Target::get_sequence_generation_combined_profiles_directory(){
	return this->target_directory
		+ "/" + this->sequence_generation_directory
		+ "/" + this->sequence_generator_combined_profiles_directory;
}

void Fuse::Target::save_pairwise_mis_to_disk(std::map<unsigned int, double> pairwise_mis){

	auto filename = this->get_sequence_generation_pairwise_mi_filename();

	spdlog::debug("Saving pairwise MI reuslts for {} event pairs to {}.",
		pairwise_mis.size(),
		filename
	);

	auto file_stream = std::ofstream(filename);
	if(file_stream.is_open() == false)
		throw std::runtime_error(fmt::format("Unable to open {} to store calculated pairwise MI.", filename));

	std::string header("reference_pair_index,mutual_information");
	file_stream << header << std::endl;

	for(auto result : pairwise_mis){
		file_stream << result.first << "," << result.second << std::endl;
	}

	file_stream.close();

}

std::map<unsigned int, double> Fuse::Target::get_or_load_pairwise_mis(
		std::vector<Fuse::Event_set> reference_pairs
		){

	// try to load MI for each pair
	if(this->pairwise_mi_loaded == false)
		this->loaded_pairwise_mis = this->load_pairwise_mis_from_disk();
	
	if(this->pairwise_mi_loaded == false){

		// If they are still not loaded, then they don't exist and we need to calculate them
			
		spdlog::info("Calculating pairwise MI values for {} event pairs.", reference_pairs.size());

		unsigned int repeat_index = 0; // this is the repeat reference index that we'll use to calculate MI

		#pragma omp parallel for
		for(unsigned int event_pair_idx = 0; event_pair_idx < reference_pairs.size(); event_pair_idx++){
			
			auto event_pair = reference_pairs.at(event_pair_idx);

			// for this pair, find the reference execution that I should load
			auto reference_execution_index = this->get_reference_set_index_for_events(event_pair);

			std::vector<Fuse::Symbol> symbols; // Empty to return a joint distribution
			std::vector<std::vector<int64_t> > reference_values = this->get_or_load_reference_distribution(
				event_pair,
				repeat_index,
				symbols
			);

			double mi = Fuse::Analysis::calculate_normalised_mutual_information(reference_values);

			#pragma omp critical
			this->loaded_pairwise_mis.insert(std::make_pair(event_pair_idx, mi));

		}

		this->pairwise_mi_loaded = true;

		this->save_pairwise_mis_to_disk(this->loaded_pairwise_mis);
		
	} else {

		if(this->loaded_pairwise_mis.size() != reference_pairs.size()){
			spdlog::error("Loading from disk, found only {} pairwise MI values, but expected {}.",
				this->loaded_pairwise_mis.size(),
				reference_pairs.size());
		}

	}

	return this->loaded_pairwise_mis;

}

Fuse::Profile_p Fuse::Target::load_combined_profile_from_disk(
		std::string filename
		){

	Fuse::Profile_p combined_profile(new Fuse::Execution_profile(filename, this->get_target_binary()));

	auto file_stream = std::ifstream(filename);
	if(file_stream.is_open() == false)
		throw std::runtime_error(fmt::format("Cannot open '{}' to load a combined profile.", filename));

	std::string header;
	file_stream >> header;

	// These are captured as instance member variables, rather than the instance's vector
	std::vector<std::string> non_events = {
		"cpu",
		"label",
		"symbol",
		"start",
		"end",
		"gpu_eligible"
	};

	auto header_vec = Fuse::Util::split_string_to_vector(header, ',');
	for(auto event : header_vec){
		if(std::find(non_events.begin(), non_events.end(), event) == non_events.end())
			combined_profile->add_event(event);
	}

	std::string line;
	while(file_stream >> line){

		Fuse::Instance_p instance(new Fuse::Instance());
		auto split_line = Fuse::Util::split_string_to_vector(line, ',');

		for(decltype(split_line.size()) event_idx=0; event_idx<split_line.size(); event_idx++){

			auto event = header_vec.at(event_idx);
			auto value_str = split_line.at(event_idx);

			if(value_str == "unknown")
				continue;

			if(event == "cpu")
				instance->cpu = static_cast<unsigned int>(std::stoul(value_str));
			else if(event == "label")
				instance->label = Fuse::convert_label_str_to_label(value_str);
			else if(event == "symbol")
				instance->symbol = value_str;
			else if(event == "start")
				instance->start = std::stoul(value_str);
			else if(event == "end")
				instance->end = std::stoul(value_str);
			else if(event == "gpu_eligible")
				instance->is_gpu_eligible = std::stoi(value_str);
			else
				instance->append_event_value(event, std::stoul(value_str), true);

		}

		// Add the instance to the profile and continue
		combined_profile->add_instance(instance);

	}

	spdlog::trace("Loaded combined profile from {}.", filename);

	return combined_profile;

}

