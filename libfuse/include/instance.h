#ifndef FUSE_INSTANCE_H
#define FUSE_INSTANCE_H

#include "fuse_types.h"

#include <map>

namespace Fuse {

	class Instance {

		public:

			std::map<::Fuse::Event,int64_t> event_values;
			::Fuse::Symbol symbol;

			Instance();
			~Instance();

	};

}

#endif
