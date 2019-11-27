#include "target.h"
#include "config.h"
#include "fuse.h"
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

	if(j["runtime"] == "openstream")
		this->runtime = Fuse::Runtime::OPENSTREAM;
	else if(j["runtime"] == "openmp")
		this->runtime = Fuse::Runtime::OPENMP;
	else
		throw std::invalid_argument(
				fmt::format("Runtime '{}' is not supported. Requires 'openstream' or 'openmp'.",
					static_cast<std::string>(j["runtime"])));

	for(auto event : j["target_events"])
		this->target_events.push_back(event);

	this->references_directory = j["references_directory"];
	this->tracefiles_directory = j["tracefiles_directory"];
	this->combinations_directory = j["combinations_directory"];
	this->papi_directory = j["papi_directory"];

	/* Hard-coded directories */

	this->statistics_filename = this->references_directory + "/event_statistics.csv";
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
			this->reference_sets.push_back(set_iter);
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

	if(this->runtime == Fuse::Runtime::OPENSTREAM)
		j["runtime"] = "openstream";
	else if(this->runtime == Fuse::Runtime::OPENMP)
		j["runtime"] = "openmp";
	else
		throw std::logic_error(
				fmt::format("Could not resolve a runtime (integer enum value is {}) to a string representation.",
					static_cast<int>(this->runtime)));

	j["references_directory"] = this->references_directory;
	j["tracefiles_directory"] = this->tracefiles_directory;
	j["combinations_directory"] = this->combinations_directory;
	j["papi_directory"] = this->papi_directory;

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

	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->references_directory);
	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->tracefiles_directory);
	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->combinations_directory);
	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->logs_directory);

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

std::string Fuse::Target::get_tracefiles_directory(){
	return (this->target_directory + "/" + this->tracefiles_directory);
}

std::string Fuse::Target::get_references_directory(){
	return (this->target_directory + "/" + this->references_directory);
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
	if(sequence.size() < 1)
		throw std::runtime_error(
			fmt::format("No {} sequence has been defined in the target JSON, so cannot combine the sequence profiles.", minimal_str));

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
		execution_profile->load_from_tracefile(false);

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

	std::string saved_statistics_filename = this->target_directory + "/event_statistics.csv";
	Fuse::Statistics_p stats(new Fuse::Statistics(saved_statistics_filename));
	stats->load();
	this->statistics = stats;

}

std::vector<Fuse::Event_set> Fuse::Target::get_or_generate_reference_sets(){

	if(this->reference_sets.size() > 0)
		return this->reference_sets;

	// First, generate all the desired pairs: each event is mapped to a list of events it needs paired with
	std::vector<Fuse::Event_set> remaining_pairs;
	Fuse::Event_set events = this->target_events;
	std::reverse(events.begin(),events.end());

	while(events.size() > 1){
		Fuse::Event event = events.back();
		events.pop_back();
		remaining_pairs.push_back(events);
	}

	// Then, greedily generate compatible event sets that contain these pairs
	Fuse::Event_set current_set;
	unsigned int next_event_idx = 0;
	while(next_event_idx < remaining_pairs.size()){

		// Get the next event, and find out which other events it still needs to be paired with
		Fuse::Event event = this->target_events.at(next_event_idx);
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

			current_set.push_back(remaining_pairs.at(next_event_idx).back());
			if(Fuse::Profiling::compatibility_check(current_set, this->papi_directory) == false){
				current_set.pop_back();
				this->reference_sets.push_back(current_set);
				current_set = {event};
			} else {
				remaining_pairs.at(next_event_idx).pop_back();
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

			auto reference_distribution = this->load_reference_distribution(ref_set_idx, repeat);

			// Add the reference distribution to the map
			auto reference_set_iter = this->loaded_reference_distributions.find(ref_set_idx);
			if(reference_set_iter == this->loaded_reference_distributions.end()){

				std::map<unsigned int, std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > > repeats_for_set;
				repeats_for_set.insert(std::make_pair(repeat, reference_distribution));
				this->loaded_reference_distributions.insert(std::make_pair(ref_set_idx, repeats_for_set));

			} else {

				auto repeat_iter = reference_set_iter->second.find(repeat);
				if(repeat_iter != reference_set_iter->second.end())
					spdlog::warn("Loaded a reference distribution that was already loaded (reference {} and repeat {}).",
						ref_set_idx, repeat);

				reference_set_iter->second[repeat] = reference_distribution; // Replaces if exists

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

	auto reference_set_idx = this->get_reference_set_index_for_events(events);
	auto reference_set_iter = this->loaded_reference_distributions.find(reference_set_idx);

	// Load it if it is not loaded
	bool was_loaded = true;
	if(reference_set_iter == this->loaded_reference_distributions.end()){

		reference_distribution_per_symbol = this->load_reference_distribution(reference_set_idx, repeat_idx);
		was_loaded = false;

	} else {

		auto repeat_iter = reference_set_iter->second.find(repeat_idx);
		if(repeat_iter == reference_set_iter->second.end()){

			reference_distribution_per_symbol = this->load_reference_distribution(reference_set_idx, repeat_idx);
			was_loaded = false;

		} else {

			reference_distribution_per_symbol = repeat_iter->second;

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

	spdlog::trace("Storing reference calibration TMD for reference index {}.", reference_idx);

	auto filename = this->get_calibration_tmds_filename();

	auto requires_header = true;
	if(Fuse::Util::check_file_existance(filename))
		requires_header = false;

	auto file_stream = std::ofstream(filename, std::ios_base::ate);
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
		Fuse::Target::get_reference_calibration(
		){

	std::map<Fuse::Symbol, std::map<unsigned int, std::pair<double, double> > > calibration_per_reference_per_symbol;

	auto filename = this->get_calibration_tmds_filename();

	auto file_stream = std::ifstream(filename);
	if(file_stream.is_open() == false)
		throw std::runtime_error(fmt::format("Unable to open {} to load calibration tmds.", filename));

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

unsigned int Fuse::Target::get_reference_set_index_for_events(
		Fuse::Event_set events
		){

	std::sort(events.begin(), events.end());
	auto reference_pairs = this->get_reference_pairs();

	for(decltype(reference_pairs.size()) reference_idx=0; reference_idx<reference_pairs.size(); reference_idx++){

		auto reference_set = reference_pairs.at(reference_idx);

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

std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > Fuse::Target::load_reference_distribution(
		unsigned int reference_idx,
		unsigned int repeat_idx
		){

	std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > values_per_symbol;

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
			event.reserve(num_chars);
			file_stream.read(&event[0],num_chars);

			events.push_back(event);
		}

		unsigned int num_symbols = 0;
		file_stream.read(reinterpret_cast<char*>(&num_symbols), sizeof(num_symbols));

		for(unsigned int symbol_idx=0; symbol_idx<num_symbols; symbol_idx++){

			unsigned int num_chars = 0;
			file_stream.read(reinterpret_cast<char*>(&num_chars), sizeof(num_chars));

			Fuse::Symbol symbol;
			symbol.reserve(num_chars);
			file_stream.read(&symbol[0],num_chars);

			std::vector<std::vector<int64_t> > values;

			unsigned int num_instances = 0;
			file_stream.read(reinterpret_cast<char*>(&num_instances), sizeof(num_instances));
			values.reserve(num_instances);

			for(unsigned int instance_idx=0; instance_idx<num_instances; instance_idx++){
				std::vector<int64_t> instance_values;
				instance_values.reserve(num_events);
				file_stream.read(reinterpret_cast<char*>(&instance_values[0]), num_events*sizeof(instance_values[0]));
				values.push_back(instance_values);
			}

			values_per_symbol.insert(std::make_pair(symbol, values));

		}

		file_stream.close();

	} catch (std::ios_base::failure& e){
		throw std::runtime_error(fmt::format("Failed loading reference distribution from file {}. Error was: {}.",
			ref_filename, e.what()));
	}

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

	// Check pbzip2 exists
	std::string check_cmd = "which pbzip2";
	auto pipe = popen(check_cmd.c_str(), "r");
	if(pipe == nullptr){
		spdlog::warn("Unable to compress reference tracefiles for repeat index {}, as cannot open pipe for '{}'.",
			repeat_idx, check_cmd);
		return;
	} else if(pclose(pipe) != EXIT_SUCCESS){
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

	char buffer[256];
	std::string result = "";

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







