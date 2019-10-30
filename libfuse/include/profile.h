#ifndef FUSE_PROFILE_H
#define FUSE_PROFILE_H

#include "fuse_types.h"

#include "boost/icl/interval_map.hpp"

#include <queue>
#include <string>
#include <map>

struct multi_event_set;
struct single_event;
struct comm_event;
struct frame;

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

			void gather_sorted_openstream_parsing_events(
				struct multi_event_set* mes,
				std::vector<struct single_event*>& all_single_events,
				std::vector<struct comm_event*>& all_comm_events);
		
			void allocate_cycles_in_state(
				struct multi_event_set* mes,
				struct single_event* se,
				std::vector<unsigned int>& next_state_event_idx_by_cpu,
				std::vector<::Fuse::Instance_p>& runtime_instances_by_cpu,
				std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				std::vector<uint64_t>& partially_traced_state_time_by_cpu,
				std::vector<uint64_t>& runtime_starts_by_cpu
			);

			template <typename Compare>
			void update_data_accesses(
				struct multi_event_set* mes,
				struct single_event* se,
				::boost::icl::interval_map<
					uint64_t,
					std::set<std::pair<unsigned int, ::Fuse::Instance_p>, Compare>
					>& data_accesses,
				std::vector<struct comm_event*>& all_comm_events,
				std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				unsigned int& next_comm_event_idx,
				unsigned int total_num_comm_events);
		
			void process_next_openstream_single_event(
				struct single_event* se,
				struct frame* top_level_frame,
				std::map<uint64_t, std::queue<::Fuse::Instance_p> >& ready_instances_by_frame,
				std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				std::vector<::Fuse::Instance_p>& runtime_instances_by_cpu,
				std::vector<uint64_t>& runtime_starts_by_cpu,
				std::vector<int>& ces_hints_per_cpu,
				unsigned int& top_level_instance_counter);


	};


}

#endif
