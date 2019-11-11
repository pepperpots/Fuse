#include "target.h"
#include "config.h"
#include "util.h"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include <fstream>

void Fuse::Target::parse_json_mandatory(nlohmann::json j){

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

	this->cache_cleared= j["cache_cleared"];

	for(auto event : j["events"])
		this->target_events.push_back(event);

	this->references_directory = j["references_directory"];
	this->tracefiles_directory = j["tracefiles_directory"];
	this->combinations_directory = j["combinations_directory"];
	this->papi_directory = j["papi_directory"];

	this->statistics_filename = this->references_directory + "/event_statistics.csv";

}

void Fuse::Target::parse_json_optional(nlohmann::json j){

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
		target_directory(target_dir)
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

Fuse::Target::~Target(){}

void Fuse::Target::check_or_create_directories(){

	Fuse::Util::check_or_create_directory(this->target_directory + "/" + this->references_directory);

}











