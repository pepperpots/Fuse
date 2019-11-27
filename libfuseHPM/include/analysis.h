#ifndef FUSE_ANALYSIS_H
#define FUSE_ANALYSIS_H

#include "fuse_types.h"

#include <vector>

namespace Fuse {

	namespace Analysis {

		double calculate_uncalibrated_tmd(
			std::vector<std::vector<int64_t> > distribution_one,
			std::vector<std::vector<int64_t> > distribution_two,
			std::vector<std::pair<int64_t, int64_t> > bounds_per_event
		);



	}

}

#endif



