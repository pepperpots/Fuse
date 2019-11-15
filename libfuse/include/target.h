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

			/* Map from repeat index to map of (part index) to (in-memory profile) */
			std::map<unsigned int, std::map<unsigned int, Fuse::Profile_p> > loaded_minimal_sequence_profiles;
			std::map<unsigned int, std::map<unsigned int, Fuse::Profile_p> > loaded_non_minimal_sequence_profiles;

			std::map<Fuse::Strategy, std::vector<unsigned int> > combined_indexes;
			std::map<Fuse::Strategy, std::map<unsigned int, Fuse::Profile_p> > loaded_combined_profiles;

			bool should_clear_cache;

			Fuse::Statistics_p statistics;
			/* If non-empty, target profiles should only operate on these events at most */
			Fuse::Event_set filtered_events;

			bool modified;

		public:
			Target(std::string target_dir);

			Fuse::Combination_sequence get_sequence(bool minimal);
			unsigned int get_num_sequence_repeats(bool minimal);
			void increment_num_sequence_repeats(bool minimal);

			void store_loaded_sequence_profile(
				unsigned int repeat_index,
				Fuse::Sequence_part part,
				Fuse::Profile_p execution_profile,
				bool minimal
			);

			/* These will be ordered by their part index */
			std::vector<Fuse::Profile_p> load_and_retrieve_sequence_profiles(
				unsigned int repeat_idx,
				bool minimal
			);

			void store_combined_profile(
				unsigned int repeat_idx,
				Fuse::Strategy strategy,
				Fuse::Profile_p combined_profile
			);

			void register_new_combined_profile(
				Fuse::Strategy strategy,
				unsigned int repeat_idx,
				Fuse::Profile_p execution_profile
			);

			bool combined_profile_exists(Fuse::Strategy strategy, unsigned int repeat_idx);
			unsigned int get_num_combined_profiles(Fuse::Strategy strategy);

			Fuse::Statistics_p get_statistics();
			std::vector<Fuse::Event_set> get_bc_overlapping_events();
			Fuse::Event_set get_filtered_events();
			void set_filtered_events(Fuse::Event_set filter_to_events);
			bool get_should_clear_cache();
			Event_set get_target_events();
			Fuse::Runtime get_target_runtime();
			std::string get_target_binary();
			std::string get_target_args();
			std::string get_logs_directory();
			std::string get_tracefiles_directory();
			std::string get_combination_filename(Fuse::Strategy strategy, unsigned int repeat_idx);
			void save();

		private:
			void parse_json_mandatory(nlohmann::json& j);
			void parse_json_optional(nlohmann::json& j);
			void generate_json_mandatory(nlohmann::json& j);
			void generate_json_optional(nlohmann::json& j);
			void check_or_create_directories();
			void initialize_statistics();

	};

}

#endif
