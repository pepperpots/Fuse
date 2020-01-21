#ifndef FUSE_TRACE_H
#define FUSE_TRACE_H

#include "fuse_types.h"

namespace Fuse {

	class Trace {

		protected:
			// We hold a reference to the Execution_profile that we are populating
			Fuse::Execution_profile& profile;

		public:
			Trace(Fuse::Execution_profile& profile) : profile(profile) {};
			void virtual parse_trace(Fuse::Runtime runtime, bool load_communication_matrix) = 0;

	};

}


#endif
