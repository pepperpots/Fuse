#ifndef FUSE_PROFILE_H
#define FUSE_PROFILE_H

#include "fuse_types.h"

#include <string>
#include <map>

struct multi_event_set;

namespace Fuse {

	class Execution_profile {
	
		public:
			std::map<::Fuse::Symbol, std::vector<::Fuse::Instance_p> > instances; // symbols mapped to instances of that symbol
			std::string tracefile;
			std::string benchmark;

			Execution_profile(std::string tracefile, std::string benchmark_binary);
			~Execution_profile();

			void load_from_tracefile(bool load_communication_matrix);

		private:
			void parse_instances_from_mes(struct multi_event_set* mes, bool load_communication_matrix);
			void parse_openstream_instances(struct multi_event_set* mes, bool load_communication_matrix);
			void parse_openmp_instances(struct multi_event_set* mes);

	};


}

#endif
