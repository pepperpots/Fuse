#include "target.h"
#include "config.h"
#include "util.h"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

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

	this->should_clear_cache = j["should_clear_cache"];

	for(auto event : j["target_events"])
		this->target_events.push_back(event);

	this->references_directory = j["references_directory"];
	this->tracefiles_directory = j["tracefiles_directory"];
	this->combinations_directory = j["combinations_directory"];
	this->papi_directory = j["papi_directory"];

	this->statistics_filename = this->references_directory + "/event_statistics.csv";

}

void Fuse::Target::parse_json_optional(nlohmann::json& j){

	if(j.count("args"))
		this->args = j["args"];
	else
		this->args = "";

	if(j.count("combination_counts") && j["combination_counts"].is_null() == false){
		auto strategy_objects = j["combination_counts"];
		for(auto strat_iter = strategy_objects.begin(); strat_iter != strategy_objects.end(); strat_iter++){

			for(auto count_iter = strat_iter->begin(); count_iter != strat_iter->end(); count_iter++){

				std::string strategy_string = count_iter.key();
				Fuse::Strategy strategy = Fuse::convert_string_to_strategy(strategy_string);

				this->combination_counts[strategy] = count_iter.value();
				spdlog::trace("There are {} combined target profiles for strategy '{}'.", static_cast<int>(count_iter.value()), strategy_string);
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

	// Sequences are lists of JSON objects, and thus their order in the JSON is maintained
	if(j.count("bc_sequence") && j["bc_sequence"].is_null() == false){
		for(auto sequence_part : j["bc_sequence"]){
			Event_set overlapping = sequence_part["overlapping_events"];
			Event_set unique = sequence_part["unique_events"];

			struct Fuse::Sequence_part part;
			part.overlapping = overlapping;
			part.unique = unique;

			this->bc_sequence.push_back(part);
		}
	}

	if(j.count("minimal_sequence") && j["minimal_sequence"].is_null() == false){
		for(auto sequence_part : j["minimal_sequence"]){
			Event_set unique = sequence_part["unique_events"];

			struct Fuse::Sequence_part part;
			// part.overlapping is empty by default
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
	spdlog::trace("Loading Fuse target from json: {}.", json_filename);

	std::ifstream ifs(json_filename);

	if(ifs.is_open() == false)
		throw std::runtime_error(fmt::format("Cannot open the Fuse target JSON at {}.", json_filename));

	nlohmann::json j;

	try {

		ifs >> j;
		parse_json_mandatory(j);
		parse_json_optional(j);

	} catch (const std::domain_error& e){
		throw std::domain_error(fmt::format("Could not load Fuse target JSON due to invalid or missing data. \
				Exception was: {}.", e.what()));
	} catch (const std::invalid_argument& e){
		throw std::invalid_argument(fmt::format("Could not load Fuse target JSON due to invalid JSON formatting. \
				Exception was: {}.", e.what()));
	}

	// Check that the directories are available for later
	this->check_or_create_directories();

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

	j["should_clear_cache"] = this->should_clear_cache;

	j["references_directory"] = this->references_directory;
	j["tracefiles_directory"] = this->tracefiles_directory;
	j["combinations_directory"] = this->combinations_directory;
	j["papi_directory"] = this->papi_directory;

	j["target_events"] = this->target_events;

}

void Fuse::Target::generate_json_optional(nlohmann::json& j){

	if(this->args != "")
		j["args"] = this->args;

	if(this->combination_counts.size() > 0){
		nlohmann::json counts_json;

		for(auto strat_iter : combination_counts){
			nlohmann::json count_json;

			std::string strategy_string = Fuse::convert_strategy_to_string(strat_iter.first);
			count_json[strategy_string] = strat_iter.second;

			counts_json.push_back(count_json);
		}

		j["combination_counts"] = counts_json;

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

			part_json["part_index"] = sequence_json.size();
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

			part_json["part_index"] = sequence_json.size();
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

}

void Fuse::Target::check_or_create_directories(){

	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->references_directory);
	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->tracefiles_directory);
	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->combinations_directory);

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







