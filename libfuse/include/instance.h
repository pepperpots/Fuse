#ifndef FUSE_INSTANCE_H
#define FUSE_INSTANCE_H

#include "fuse_types.h"

#include <map>

namespace Fuse {

	class Instance {

		public:

			std::map<::Fuse::Event,int64_t> event_values;
			::Fuse::Symbol symbol;
			std::vector<int> label;
			uint64_t start;
			uint64_t end;
			unsigned int cpu;
			bool is_gpu_eligible;

			void append_event_value(::Fuse::Event event, int64_t value, bool additive);

			Instance();
			~Instance();

	};

}

#endif
