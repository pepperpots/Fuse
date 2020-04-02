#ifndef FUSE_ANALYSIS_H
#define FUSE_ANALYSIS_H

#include "fuse_types.h"

#include <map>
#include <vector>

namespace Fuse {

	namespace Analysis {

		double calculate_uncalibrated_tmd(
			std::vector<std::vector<int64_t> > distribution_one,
			std::vector<std::vector<int64_t> > distribution_two,
			std::vector<std::pair<int64_t, int64_t> > bounds_per_dimension,
			unsigned int num_bins_per_dimension
		);

		double compute_ami(
			Fuse::Event_set linking_set,
			Fuse::Event_set unique_events,
			std::vector<Fuse::Event_set> reference_pairs,
			std::map<unsigned int, double> pairwise_mi_values
		);

		double calculate_normalised_mutual_information(
			std::vector<std::vector<int64_t> > distribution
		);
				
		double calculate_calibrated_tmd_for_pair(
			Fuse::Target& target,
			std::vector<Fuse::Symbol> symbols,
			Fuse::Event_set reference_pair,
			Fuse::Profile_p profile,
			std::vector<unsigned int> reference_repeats_list,
			unsigned int bin_count,
			bool weighted_tmd
		);

	}

}

#endif



