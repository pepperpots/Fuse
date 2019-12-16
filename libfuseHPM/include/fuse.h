#ifndef FUSE_H
#define FUSE_H

#include "fuse_types.h"
#include "util.h"
#include "target.h"
#include "profile.h"

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

	void execute_references(
		Fuse::Target& target,
		unsigned int number_of_executions
	);

	void execute_sequence_repeats(
		Fuse::Target& target,
		unsigned int number_of_executions,
		bool minimal,
		bool keep_in_memory = true
	);

	void execute_hem_repeats(
		Fuse::Target& target,
		unsigned int number_of_executions,
		bool keep_in_memory = true
	);

	void combine_sequence_repeats(
		Fuse::Target& target,
		std::vector<Fuse::Strategy> strategies,
		std::vector<unsigned int> repeat_indexes,
		bool minimal,
		bool keep_in_memory = true
	);

	void analyse_sequence_combinations(
		Fuse::Target& target,
		std::vector<Fuse::Strategy> strategies,
		std::vector<unsigned int> repeat_indexes,
		Fuse::Accuracy_metric metric
	);

	void calculate_calibration_tmds(
		Fuse::Target& target
	);

	/* Initialize or reinitialize library-managed logging */
	void initialize_logging(std::string log_directory, bool log_to_file, unsigned int log_level);

	/* Initialize or reinitialize logging using provided sinks, and return the logger to share by both client and library */
	std::shared_ptr<spdlog::logger> initialize_logging(std::vector<spdlog::sink_ptr> sinks,	unsigned int log_level);

	// TODO move these to private header to not pollute the library API
	void add_profile_event_values_to_statistics(
		Fuse::Profile_p profile,
		Fuse::Statistics_p statistics
	);


}

#endif
