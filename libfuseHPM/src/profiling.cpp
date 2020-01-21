#include "profiling.h"
#include "config.h"
#include "profile.h"
#include "util.h"

#include "spdlog/spdlog.h"

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

Fuse::Profile_p Fuse::Profiling::execute_and_load(
		Fuse::Event_set filtered_events,
		Fuse::Runtime runtime,
		std::string binary,
		std::string args,
		std::string tracefile,
		Fuse::Event_set profiled_events,
		bool clear_cache,
		bool multiplex
		){

	Fuse::Profiling::execute(runtime, binary, args, tracefile, profiled_events, clear_cache, multiplex);

	Fuse::Profile_p execution_profile(new Fuse::Execution_profile(tracefile, binary, filtered_events));
	execution_profile->load_from_tracefile(runtime, false);

	return execution_profile;

}

void Fuse::Profiling::execute(
		Fuse::Runtime runtime,
		std::string binary,
		std::string args,
		std::string tracefile,
		Fuse::Event_set profiled_events,
		bool clear_cache,
		bool multiplex
		){

	bool success = false;
	switch(runtime){
		case Fuse::Runtime::OPENSTREAM:

			for(decltype(Fuse::Config::max_execution_attempts) attempt_idx = 0; attempt_idx < Fuse::Config::max_execution_attempts; attempt_idx++){

				if(clear_cache)
					Fuse::Profiling::clear_system_cache();

				success = Fuse::Profiling::Openstream::execute(binary, args, tracefile, profiled_events, multiplex);
				if(success)
					break;

				std::this_thread::sleep_for(std::chrono::seconds(1));
			}

			break;
		case Fuse::Runtime::OPENMP:

			throw std::logic_error("OpenMP Fuse not yet implemented.");
			break;
	}

	if(success == false){
		throw std::runtime_error(fmt::format("Unable to successfully execute '{}' while monitoring {} after {} attempts.",
			binary + " " + args,
			Fuse::Util::vector_to_string(profiled_events),
			Fuse::Config::max_execution_attempts));
	}

}

bool Fuse::Profiling::Openstream::execute(
		std::string binary,
		std::string args,
		std::string tracefile,
		Fuse::Event_set profiled_events,
		bool multiplex
		){

	Fuse::Event_set uppercase_events = Fuse::Util::vector_to_uppercase(profiled_events); // Because PAPI events are always uppercase

	std::string events_str = Fuse::Util::vector_to_string(uppercase_events, false);

	std::stringstream ss;
	ss << "WS_PAPI_EVENTS=" << events_str;
	ss << " WQEVENT_SAMPLING_OUTFILE=" << tracefile;
	ss << " WS_PAPI_MULTIPLEX=" << multiplex; // Casts to integer
	ss << " " << binary << " " << args;
	std::string cmd = ss.str();

	spdlog::debug("Executing OpenStream program using: '{}'.", cmd);

	char buffer[256];
	std::string result = "";

	auto pipe = popen(cmd.c_str(), "r");
	if(pipe == nullptr)
		return false;

	while(!feof(pipe)){
		if(fgets(buffer, 256, pipe) != nullptr)
			result += buffer;
	}

	auto ret = pclose(pipe);

	if(ret == EXIT_SUCCESS)
		return true;
	else
		return false;

}

bool Fuse::Profiling::Openmp::execute(
		std::string binary,
		std::string args,
		std::string tracefile,
		Fuse::Event_set profiled_events,
		bool multiplex
		){

	// TODO

	return true;
}

void Fuse::Profiling::clear_system_cache(){

	system("sync && sudo /sbin/sysctl vm.drop_caches=3");

}

std::vector<Fuse::Event_set> Fuse::Profiling::greedy_generate_minimal_partitioning(
		Fuse::Event_set target_events,
		std::string papi_directory
		){

	std::vector<Fuse::Event_set> minimal_sets;

	target_events = Fuse::Util::vector_to_uppercase(target_events);
	std::reverse(target_events.begin(), target_events.end()); // so that I can pop_back to get the front

	Fuse::Event_set current_set;
	while(target_events.size() > 0){

		auto event = target_events.back();
		current_set.push_back(event);

		if(Fuse::Profiling::compatibility_check(current_set, papi_directory)){
			target_events.pop_back();
			// continue
		} else {
			current_set.pop_back();
			// save the current set and start a new one
			minimal_sets.push_back(Fuse::Util::vector_to_lowercase(current_set));
			current_set.clear();
		}

	}

	if(current_set.size() > 0)
		minimal_sets.push_back(current_set);

	return minimal_sets;

}

bool Fuse::Profiling::compatibility_check(Fuse::Event_set events, std::string papi_directory){

	std::stringstream ss;
	ss << papi_directory << "/papi_event_chooser PRESET ";
	for(auto event : events)
		ss << Fuse::Util::uppercase(event) << " ";

	std::string cmd = ss.str();

	for(decltype(Fuse::Config::max_execution_attempts) attempt_idx = 0; attempt_idx < Fuse::Config::max_execution_attempts; attempt_idx++){

		spdlog::trace("Executing compatibility check using: '{}'.", cmd);

		char buffer[256];
		std::string result = "";

		auto pipe = popen(cmd.c_str(), "r");
		if(pipe == nullptr){
			std::this_thread::sleep_for(std::chrono::seconds(1));
			continue;
		}

		while(!feof(pipe)){
			if(fgets(buffer, 256, pipe) != nullptr)
				result += buffer;
		}

		auto ret = pclose(pipe);

		if(ret == EXIT_SUCCESS)
			return true;
		else {

			if(events.size() <= 2)
				throw std::invalid_argument(
					fmt::format("The {} events {} are incompatible. Fuse assumes that all pairs of events can be simultaneously monitored. Aborting.",
						events.size(),
						Fuse::Util::vector_to_string(events)
					)
				);

			return false;
		}

	}

	throw std::runtime_error(fmt::format("Unable to execute command '{}' to determine PAPI events compatibility.", cmd));

}









