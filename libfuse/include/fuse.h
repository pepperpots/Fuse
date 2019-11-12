#ifndef FUSE_H
#define FUSE_H

#include "fuse_types.h"
#include "util.h"
#include "instance.h"
#include "target.h"
#include "profile.h"
#include "profiling.h"

#include <memory>
#include <string>
#include <vector>

// Forward declare necessary logging data structures, if user application does not use spdlog
namespace spdlog {
	class logger;
	namespace sinks {
		class sink;
	}
	using sink_ptr = std::shared_ptr<spdlog::sinks::sink>;
}


namespace Fuse {

	/* Initialize or reinitialize library-managed logging */
	void initialize_logging(std::string log_directory, bool log_to_file, unsigned int log_level);

	/* Initialize or reinitialize logging using provided sinks, and return the logger to share by both client and library */
	std::shared_ptr<spdlog::logger> initialize_logging(std::vector<spdlog::sink_ptr> sinks,	unsigned int log_level);

	void execute_sequence(Fuse::Target target, unsigned int number_of_executions, bool minimal);

}

#endif
