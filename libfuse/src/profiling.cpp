#include "profiling.h"
#include "config.h"
#include "profile.h"
#include "util.h"

#include "spdlog/spdlog.h"

#include <sstream>
#include <string>

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
	execution_profile->load_from_tracefile(false);

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

	std::string events_str = Fuse::Util::vector_to_string(uppercase_events); // Returns a list in the form [event,event,...]
	events_str = events_str.substr(1,events_str.size()-2); // Remove first and last brackets

	std::stringstream ss;
	ss << "WS_PAPI_EVENTS=" << events_str;
	ss << " WQEVENT_SAMPLING_OUTFILE=" << tracefile;
	ss << " WS_PAPI_MULTIPLEX=" << multiplex; // Casts to integer
	ss << " " << binary << " " << args;
	std::string cmd = ss.str();

	spdlog::trace("Executing OpenStream program using: '{}'.", cmd);

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

