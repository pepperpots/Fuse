#ifndef FUSE_TRACE_AFTERMATH_LEGACY_H
#define FUSE_TRACE_AFTERMATH_LEGACY_H

/*
 * 	As there are different versions of the OpenStream and OpenMP aftermath (legacy) trace files,
 *  the parsing code needs to agree with the trace file format
 * 	Therefore we have multiple preprocessor options that enable the different parts of the trace:
 * 	
 *  OMP_LABELLING_DS_ENABLED
 *  OS_GPU_ENABLED
 *  OS_NUMA_DIST_ENABLED
 *  SYSCALL_ENABLED
 *  
 * 	If any of these are performed by the runtime, then they must be enabled in the aftermath install,
 *  and they must also be enabled here 
 */

#include "fuse_types.h"
#include "trace.h"

#include <map>
#include <queue>
#include <set>
#include <string>

struct multi_event_set;
struct single_event;
struct comm_event;
struct frame;
struct event_set;
struct omp_pregion_enter;
struct omp_pregion_leave;
struct omp_for_instance;
struct omp_for_chunk_set;
struct omp_for_chunk_set_part;
struct omp_task_part;
struct omp_task_instance_finish;
struct omp_task_instance_creation;
struct omp_task_instance;
struct omp_single;
struct omp_single_leave;

namespace Fuse {

	enum Omp_construct_type {
		PREGION_ENTER,
		PREGION_LEAVE,
		CHUNK_SET_ENTER,
		CHUNK_SET_LEAVE,
		CHUNK_SET_PART_ENTER,
		CHUNK_SET_PART_LEAVE,
		SYSCALL,
		TASK_CREATION,
		TASK_PART_ENTER,
		TASK_PART_LEAVE,
		TASK_END,
		SINGLE,
		SINGLE_LEAVE
	};

	// One data structure for all the different types, so that we can have a sorted list of their union
	struct Aftermath_omp_construct {

		Omp_construct_type type;
		uint32_t cpu;
		uint64_t time;
		union {
			struct omp_pregion_enter* ope;
			struct omp_pregion_leave* opl;
			struct omp_for_chunk_set* cs;
			struct omp_for_chunk_set_part* csp;
			struct omp_task_part* tp;
			struct omp_task_instance_finish* tif;
			struct omp_task_instance_creation* tic;
			struct omp_task_instance* ti;
			struct omp_single* os;
			struct omp_single_leave* osl;
			struct single_event* se;
		} ptr;

	};

	struct csp_compare {
		bool operator()(struct omp_for_chunk_set* a, struct omp_for_chunk_set* b);
	};
	struct tp_compare {
		bool operator()(struct omp_task_instance* a, struct omp_task_instance* b);
	};

	class Trace_aftermath_legacy : public Fuse::Trace {

		public:
			Trace_aftermath_legacy(Fuse::Execution_profile& profile);
			~Trace_aftermath_legacy();
			void parse_trace(Fuse::Runtime runtime, bool load_communication_matrix) override;

		private:
			void parse_instances_from_mes(
				struct multi_event_set* mes,
				Fuse::Runtime runtime,
				bool load_communication_matrix
			);
			void parse_openstream_instances(struct multi_event_set* mes, bool load_communication_matrix);
			void parse_openmp_instances(struct multi_event_set* mes);

			/* Both runtimes */

			void interpolate_and_append_counter_values(
				Fuse::Instance_p instance,
				uint64_t start_time,
				uint64_t end_time,
				struct event_set* es,
				int& start_index_hint
			);

			void allocate_cycles_in_state(
				Fuse::Runtime runtime,
				struct multi_event_set* mes,
				struct event_set* es,
				uint64_t event_time,
				std::vector<unsigned int>& next_state_event_idx_by_cpu,
				std::vector<Fuse::Instance_p>& runtime_instances_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu_task,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu_it_set,
				std::vector<uint64_t>& partially_traced_state_time_by_cpu,
				std::vector<uint64_t>& runtime_starts_by_cpu
			);

			/* OpenStream trace functions */

			template <typename Compare>
			void load_openstream_instance_dependencies(std::vector<struct comm_event*> all_comm_events,
				boost::icl::interval_map<
					uint64_t,
					std::set<std::pair<unsigned int, Fuse::Instance_p>, Compare>
					>& data_accesses
			);

			void gather_sorted_openstream_parsing_events(
				struct multi_event_set* mes,
				std::vector<struct single_event*>& all_single_events,
				std::vector<struct comm_event*>& all_comm_events
			);

			template <typename Compare>
			void update_data_accesses(
				struct multi_event_set* mes,
				struct single_event* se,
				boost::icl::interval_map<
					uint64_t,
					std::set<std::pair<unsigned int, Fuse::Instance_p>, Compare>
					>& data_accesses,
				std::vector<struct comm_event*>& all_comm_events,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				unsigned int& next_comm_event_idx,
				unsigned int total_num_comm_events,
				bool load_communication_matrix
			);

			void process_next_openstream_single_event(
				struct single_event* se,
				struct frame* top_level_frame,
				std::map<uint64_t, std::queue<Fuse::Instance_p> >& ready_instances_by_frame,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				std::vector<Fuse::Instance_p>& runtime_instances_by_cpu,
				std::vector<uint64_t>& runtime_starts_by_cpu,
				std::vector<int>& ces_hints_per_cpu,
				unsigned int& top_level_instance_counter
			);

			void process_openstream_instance_creation(
				struct single_event* se,
				struct frame* top_level_frame,
				std::map<uint64_t, std::queue<Fuse::Instance_p> >& ready_instances_by_frame,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				unsigned int& top_level_instance_counter
			);

			void process_openstream_instance_start(
				struct single_event* se,
				std::map<uint64_t, std::queue<Fuse::Instance_p> >& ready_instances_by_frame,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu
			);

			void process_openstream_instance_end(
				struct single_event* se,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
				std::vector<uint64_t>& runtime_starts_by_cpu,
				std::vector<int>& ces_hints_per_cpu
			);

			/* OpenMP trace functions */

			std::vector<struct Aftermath_omp_construct> gather_openmp_parsing_constructs(
				struct multi_event_set* mes
			);

			void process_openmp_pregion_enter(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::set<struct omp_for_instance*> > >&
					seen_instances_within_region_by_cpu
			);

			void process_openmp_pregion_leave(
				unsigned int worker_cpu,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::set<struct omp_for_instance*> > >&
					seen_instances_within_region_by_cpu
			);

			void process_openmp_chunk_set_enter(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::set<struct omp_for_instance*> > >&
					seen_instances_within_region_by_cpu,
				std::map<struct omp_for_chunk_set*,
					std::pair<Fuse::Instance_p,std::vector<struct omp_for_chunk_set_part*> >, Fuse::csp_compare>&
					csps_in_cs
			);

			void process_openmp_chunk_set_leave(
				unsigned int worker_cpu,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu
			);

			void process_previous_time_as_runtime_execution(
				struct multi_event_set* mes,
				unsigned int worker_cpu,
				Fuse::Instance_p runtime_instance,
				uint64_t runtime_start,
				uint64_t runtime_end
			);

			void process_openmp_chunk_set_part_enter(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_it_sets_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_tasks_by_cpu,
				std::map<struct omp_for_chunk_set*,
					std::pair<Fuse::Instance_p,std::vector<struct omp_for_chunk_set_part*> >, Fuse::csp_compare>&
					csps_in_cs
			);

			void process_openmp_chunk_set_part_leave(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::vector<uint64_t>&
					runtime_starts_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_it_sets_by_cpu
			);

			void process_openmp_task_creation(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::vector<Fuse::Instance_p>&
					runtime_instances_by_cpu,
				std::vector<uint64_t>&
					runtime_starts_by_cpu,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_it_sets_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_tasks_by_cpu,
				std::map<struct omp_task_instance*, std::pair<std::vector<int>, Fuse::Instance_p> >&
					current_tasks,
				std::map<struct omp_task_instance*,
					std::pair<Fuse::Instance_p,std::vector<struct omp_task_part*> >, Fuse::tp_compare>&
					tps_in_t
			);

			void process_openmp_task_part_enter(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_it_sets_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_tasks_by_cpu,
				std::map<struct omp_task_instance*, std::pair<std::vector<int>, Fuse::Instance_p> >&
					current_tasks,
				std::map<struct omp_task_instance*,
					std::pair<Fuse::Instance_p,std::vector<struct omp_task_part*> >, Fuse::tp_compare>&
					tps_in_t
			);

			void process_openmp_task_part_leave(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::vector<uint64_t>&
					runtime_starts_by_cpu,
				std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
					executing_tasks_by_cpu
			);

			void process_openmp_task_end(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu,
				std::map<struct omp_task_instance*, std::pair<std::vector<int>, Fuse::Instance_p> >&
					current_tasks
			);

			void process_openmp_single_enter(
				struct Fuse::Aftermath_omp_construct&
					construct,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu
			);

			void process_openmp_single_leave(
				unsigned int worker_cpu,
				std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
					pregions_by_cpu,
				std::map<unsigned int,std::vector<std::vector<int> > >&
					execution_context_stack_by_cpu,
				std::map<unsigned int, std::vector<std::vector<int> > >&
					future_context_stack_by_cpu
			);

			void process_openmp_instance_parts(
				struct multi_event_set* mes,
				std::vector<std::vector<Fuse::Aftermath_omp_construct> > syscalls_by_cpu,
				std::map<struct omp_for_chunk_set*,
					std::pair<Fuse::Instance_p,std::vector<struct omp_for_chunk_set_part*> >, csp_compare>&
					csps_in_cs,
				std::map<struct omp_task_instance*,
					std::pair<Fuse::Instance_p,std::vector<struct omp_task_part*> >, Fuse::tp_compare>&
					tps_in_t
			);

			void process_openmp_syscalls(
				Fuse::Instance_p instance,
				std::vector<Fuse::Aftermath_omp_construct> syscalls,
				uint64_t start_time,
				uint64_t end_time,
				int& hint
			);

	};

}

#endif
