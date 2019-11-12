#ifndef FUSE_TARGET_H
#define FUSE_TARGET_H

#include "fuse_types.h"

#include "nlohmann/json_fwd.hpp"

#include <map>
#include <string>

namespace Fuse {

	class Target {

		public:

		private:
			std::string target_directory;

			Fuse::Runtime runtime;
			std::string binary;
			std::string args;
			std::string binary_directory;
			std::string references_directory;
			std::string tracefiles_directory;
			std::string combinations_directory;
			std::string papi_directory;
			std::string logs_directory;
			std::string statistics_filename;

			std::vector<Event_set> reference_sets;
			unsigned int num_reference_repeats;

			Event_set target_events;
			Fuse::Combination_sequence bc_sequence;
			Fuse::Combination_sequence minimal_sequence;
			unsigned int num_bc_sequence_repeats;
			unsigned int num_minimal_sequence_repeats;
			std::map<Fuse::Strategy, unsigned int> combination_counts;

			bool should_clear_cache;

			bool modified;

		public:
			Target(std::string target_dir);

			Fuse::Combination_sequence get_sequence(bool minimal);
			unsigned int get_num_sequence_repeats(bool minimal);
			void increment_num_sequence_repeats(bool minimal);

			Fuse::Runtime get_target_runtime();
			std::string get_target_binary();
			std::string get_target_args();
			std::string get_logs_directory();
			std::string get_tracefiles_directory();
			void save();

		private:
			void parse_json_mandatory(nlohmann::json& j);
			void parse_json_optional(nlohmann::json& j);
			void generate_json_mandatory(nlohmann::json& j);
			void generate_json_optional(nlohmann::json& j);
			void check_or_create_directories();

	};

}

#endif
