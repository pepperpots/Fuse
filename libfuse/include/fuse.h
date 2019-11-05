#ifndef FUSE_H
#define FUSE_H

#include <memory>
#include <string>
#include <vector>

#include "fuse_types.h"
#include "util.h"

#include "instance.h"
#include "target.h"
#include "profile.h"

//#include "spdlog/spdlog.h"

// Forward declare necessary logging data structures, if user application does not use spdlog
namespace spdlog {
	class logger;
	namespace sinks {
		class sink;
	}
	using sink_ptr = std::shared_ptr<spdlog::sinks::sink>;
}


namespace Fuse {

	/*
	* Global functions
	*/

	/* Initialize logging within the library, connected to the same spdlog sinks used by the client application */
	std::shared_ptr<spdlog::logger> initialize(std::vector<spdlog::sink_ptr> sinks,	unsigned int log_level);

	/* Initialize logging within the library, disconnected from spdlogging (or lack thereof) in the client application */
	void initialize(std::string log_directory, unsigned int log_level, bool log_to_file);

}

#endif
