#ifndef FUSE_PROFILE_H
#define FUSE_PROFILE_H

#include "fuse_types.h"

#include "boost/icl/interval_map.hpp"

#include <map>
#include <queue>
#include <set>
#include <string>

struct multi_event_set;
struct single_event;
struct comm_event;
struct frame;
struct event_set;

namespace Fuse {

	class Execution_profile {
	
		// class members
		public:
			std::map<::Fuse::Symbol, std::vector<::Fuse::Instance_p> > instances; // symbols mapped to instances of that symbol
			std::string tracefile;
			std::string benchmark;

		private:
			::Fuse::Event_set events;

			// if loaded, each instance maps to the pair [instances that it depends on, instances that depend on it]
			std::map<::Fuse::Instance_p, std::pair<std::set<::Fuse::Instance_p>,std::set<::Fuse::Instance_p> > > instance_dependencies;

		// class functions
		public:

			Execution_profile(std::string tracefile, std::string benchmark_binary);
			~Execution_profile();

			void load_from_tracefile(bool load_communication_matrix);
			void print_to_file(std::string output_file);
			void dump_instance_dependencies(std::string output_file);
			void dump_instance_dependencies_dot(std::string output_file);

			::Fuse::Event_set get_unique_events();
			std::vector<::Fuse::Instance_p> get_instances(const std::vector<::Fuse::Symbol> symbols);

		private:

			void add_instance(::Fuse::Instance_p instance);
			void add_event(::Fuse::Event event);

			void parse_instances_from_mes(struct multi_event_set* mes, bool load_communication_matrix);
			void parse_openstream_instances(struct multi_event_set* mes, bool load_communication_matrix);
			void parse_openmp_instances(struct multi_event_set* mes);

			template <typename Compare>
			void load_openstream_instance_dependencies(std::vector<struct comm_event*> all_comm_events,
				::boost::icl::interval_map<
					uint64_t,
					std::set<std::pair<unsigned int, ::Fuse::Instance_p>, Compare>
					>& data_accesses
				);

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
				unsigned int total_num_comm_events,
				bool load_communication_matrix);
		
			void process_next_openstream_single_event(
				struct single_event* se,
				struct frame* top_level_frame,
				std::map<uint64_t, std::queue<::Fuse::Instance_p> >& ready_instances_by_frame,
				std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				std::vector<::Fuse::Instance_p>& runtime_instances_by_cpu,
				std::vector<uint64_t>& runtime_starts_by_cpu,
				std::vector<int>& ces_hints_per_cpu,
				unsigned int& top_level_instance_counter);

			void process_openstream_instance_creation(
				struct single_event* se,
				struct frame* top_level_frame,
				std::map<uint64_t, std::queue<::Fuse::Instance_p> >& ready_instances_by_frame,
				std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				unsigned int& top_level_instance_counter);

			void process_openstream_instance_start(
				struct single_event* se,
				std::map<uint64_t, std::queue<::Fuse::Instance_p> >& ready_instances_by_frame,
				std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu);

			void process_openstream_instance_end(
				struct single_event* se,
				std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				std::vector<uint64_t>& runtime_starts_by_cpu,
				std::vector<int>& ces_hints_per_cpu);
			
			void interpolate_and_append_counter_values(
				::Fuse::Instance_p instance,
				uint64_t start_time,
				uint64_t end_time,
				struct event_set* es,
				int& start_index_hint);

	};

}

#endif
