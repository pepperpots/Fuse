#ifndef FUSE_COMBINATION_H
#define FUSE_COMBINATION_H

#include "fuse_types.h"

#include <map>
#include <string>
#include <vector>

namespace Fuse {

	class Statistics;

	namespace Combination {

		Fuse::Profile_p combine_profiles_via_strategy(
			std::vector<Fuse::Profile_p> sequence_profiles,
			Fuse::Strategy strategy,
			std::string combined_filename,
			std::string binary_filename,
			std::vector<Fuse::Event_set> overlapping_per_profile,
			Fuse::Statistics_p statistics
		);

		std::vector<Fuse::Instance_p> generate_combined_instances_from_unordered_profiles(
			std::vector<Fuse::Profile_p> sequence_profiles,
			Fuse::Strategy strategy,
			bool per_symbol
		);

		std::vector<Fuse::Instance_p> combine_instances_via_strategy(
			std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
			Fuse::Strategy strategy,
			Fuse::Statistics_p statistics = nullptr,
			Fuse::Event_set overlapping_events = Fuse::Event_set()
		);

		Fuse::Instance_p combine_instances(
			std::vector<Fuse::Instance_p> instances_to_combine
		);

		/* Strategy specific */

		std::vector<Fuse::Instance_p> generate_combined_instances_bc(
			std::vector<Fuse::Profile_p> sequence_profiles,
			Fuse::Strategy strategy,
			Fuse::Statistics_p statistics,
			std::vector<Fuse::Event_set> overlapping_per_profile
		);

		std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > bc_allocate_to_clusters(
			std::vector<Fuse::Instance_p> instances,
			Fuse::Event_set overlapping_events,
			std::vector<std::pair<int64_t,int64_t> > bounds,
			unsigned int granularity
		);

		unsigned int bc_find_maximum_granularity(
			std::vector<Fuse::Instance_p> a,
			std::vector<Fuse::Instance_p> b,
			Fuse::Event_set overlapping_events,
			std::vector<std::pair<int64_t,int64_t> > bounds
		);

		unsigned int relax_similarity_constraint(
			unsigned int current_granularity,
			std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_a,
			std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_b,
			std::vector<Fuse::Instance_p> already_combined_a,
			std::vector<Fuse::Instance_p> already_combined_b,
			Fuse::Event_set overlapping_events,
			std::vector<std::pair<int64_t,int64_t> > event_bounds
		);

		std::vector<std::vector<Fuse::Instance_p> > extract_matched_instances_random(
			std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
			bool remove_combined_instances,
			bool expect_matching = true
		);

		std::vector<std::vector<Fuse::Instance_p> > extract_matched_instances_chronological(
			std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
			bool remove_combined_instances,
			bool expect_matching = true
		);

		std::vector<std::vector<Fuse::Instance_p> > extract_matched_instances_by_label(
			std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
			bool remove_combined_instances,
			bool expect_matching = true
		);

		std::vector<std::vector<Fuse::Instance_p> > extract_matched_instances_bc(
			std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
			bool remove_combined_instances,
			Fuse::Statistics_p statistics,
			Fuse::Event_set overlapping_events
		);

	}

}


#endif
