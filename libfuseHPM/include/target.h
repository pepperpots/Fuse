#ifndef FUSE_TARGET_H
#define FUSE_TARGET_H

#include "fuse_types.h"

#include "nlohmann/json_fwd.hpp"

#include <map>
#include <string>

namespace Fuse {

	class Target {

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

			std::vector<Fuse::Event_set> reference_sets;
			unsigned int num_reference_repeats;

			// Map from reference set index to (map of repeat index to (map of symbol to list of value-vector for each instance))
			std::map<unsigned int,
					std::map<unsigned int,
						std::map<Fuse::Symbol, std::vector<std::vector<int64_t> >	>
					>
				> loaded_reference_distributions;

			Fuse::Event_set target_events;
			std::vector<Fuse::Symbol> symbols;

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

			unsigned int get_num_reference_repeats();
			void increment_num_reference_repeats();
			std::vector<Fuse::Event_set> get_or_generate_reference_sets();
			std::vector<Fuse::Event_set> get_reference_pairs();

			unsigned int get_reference_set_index_for_events(
				Fuse::Event_set events
			);

			std::string get_reference_filename_for(
				unsigned int reference_idx,
				unsigned int repeat_idx
			);

			void save_reference_values_to_disk(
				unsigned int reference_idx,
				unsigned int repeat_idx,
				Fuse::Event_set reference_set,
				std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > values_per_symbol
			);

			void save_reference_calibration_tmd_to_disk(
				Fuse::Symbol symbol,
				Fuse::Event_set events,
				unsigned int reference_idx,
				double min,
				double max,
				double mean,
				double median,
				double std,
				double mean_num_instances
			);

			std::map<Fuse::Symbol, std::map<unsigned int, std::pair<double, double> > >
				get_reference_calibration(
			);

			void load_reference_distributions(
				std::vector<unsigned int> reference_set_indexes_to_load = std::vector<unsigned int>(),
				std::vector<unsigned int> reference_repeats_to_load = std::vector<unsigned int>()
			);

			std::vector<std::vector<int64_t> > get_or_load_reference_distribution(
				Fuse::Event_set events,
				unsigned int repeat_idx,
				std::vector<Fuse::Symbol>& symbols
			);

			void compress_references_tracefiles(
				std::vector<std::string> reference_tracefiles,
				unsigned int repeat_idx
			);

			Fuse::Statistics_p get_statistics();
			std::vector<Fuse::Event_set> get_bc_overlapping_events();
			Fuse::Event_set get_filtered_events();
			void set_filtered_events(Fuse::Event_set filter_to_events);
			bool get_should_clear_cache();
			Fuse::Event_set get_target_events();
			Fuse::Runtime get_target_runtime();
			std::string get_target_binary();
			std::string get_target_args();
			std::string get_logs_directory();
			std::string get_tracefiles_directory();
			std::string get_references_directory();
			std::string get_combination_filename(Fuse::Strategy strategy, unsigned int repeat_idx);
			std::string get_calibration_tmds_filename();
			void save();

		private:
			std::map<Fuse::Symbol, std::vector<std::vector<int64_t> > > load_reference_distribution(
				unsigned int reference_idx,
				unsigned int repeat_idx
			);
			void parse_json_mandatory(nlohmann::json& j);
			void parse_json_optional(nlohmann::json& j);
			void generate_json_mandatory(nlohmann::json& j);
			void generate_json_optional(nlohmann::json& j);
			void check_or_create_directories();
			void initialize_statistics();

	};

}

#endif
