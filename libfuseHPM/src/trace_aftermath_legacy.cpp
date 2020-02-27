#include "trace.h"
#include "profile.h"
#include "instance.h"
#include "util.h"

#include "trace_aftermath_legacy.h"

#include "spdlog/spdlog.h"
#include "boost/icl/interval_map.hpp"

#include <fstream>
#include <limits>
#include <queue>
#include <set>
#include <sstream>

// External C code generates lots of warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-arith"
extern "C" {
	// linux-kernel/list uses 'new' as a variable name, so must be redefined to avoid conflict with c++ keyword
	#define new extern_new
	#include "aftermath/core/multi_event_set.h"
	#include "aftermath/core/debug.h"
	#undef new
}
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop

Fuse::Trace_aftermath_legacy::Trace_aftermath_legacy(Fuse::Execution_profile& profile) :
		Fuse::Trace(profile){

}

Fuse::Trace_aftermath_legacy::~Trace_aftermath_legacy(){
}

void Fuse::Trace_aftermath_legacy::parse_trace(Fuse::Runtime runtime, bool load_communication_matrix){

  struct multi_event_set* mes = new multi_event_set;
  multi_event_set_init(mes);

	off_t bytes_read = 0;

	if(read_trace_sample_file(mes, Fuse::Trace::profile.tracefile.c_str(), &bytes_read) != 0)
		throw std::runtime_error(fmt::format("There was an error reading the tracefile '{}' after {} bytes read.", Fuse::Trace::profile.tracefile, bytes_read));

	if(debug_read_task_symbols(Fuse::Trace::profile.benchmark.c_str(),mes) != 0)
		throw std::runtime_error(fmt::format("There was an error reading symbols from the binary '{}'.", Fuse::Trace::profile.benchmark));

	this->parse_instances_from_mes(mes, runtime, load_communication_matrix);

	// We no longer need the MES, so free the memory
	multi_event_set_destroy(mes);
	delete mes;

}

void Fuse::Trace_aftermath_legacy::parse_instances_from_mes(struct multi_event_set* mes,
		Fuse::Runtime runtime,
		bool load_communication_matrix
		){

	// TODO add the events to the profile ahead of time, rather than each time we encounter a value
	if(runtime == Fuse::Runtime::ALL || runtime == Fuse::Runtime::OPENSTREAM)
		this->parse_openstream_instances(mes, load_communication_matrix);

	if(runtime == Fuse::Runtime::ALL || runtime == Fuse::Runtime::OPENMP)
		this->parse_openmp_instances(mes);
}

struct data_access_time_compare {
	bool operator()(
			const std::pair<unsigned int, Fuse::Instance_p>& lhs,
			const std::pair<unsigned int, Fuse::Instance_p>& rhs)
			const {
		return lhs.second->start < rhs.second->start;
	}
};

void Fuse::Trace_aftermath_legacy::parse_openstream_instances(struct multi_event_set* mes, bool load_communication_matrix){

	spdlog::debug("Parsing OpenStream instances.");

	unsigned int total_num_single_events = 0;
	unsigned int total_num_comm_events = 0;
	for(auto es = &mes->sets[0]; es < &mes->sets[mes->num_sets]; es++){
		total_num_single_events += es->num_single_events;
		total_num_comm_events += es->num_comm_events;
	}

	spdlog::debug("There are {} OpenStream single events.", total_num_single_events);
	spdlog::debug("There are {} OpenStream communication events.", total_num_comm_events);

	if(total_num_single_events == 0){
		return;
	}

	std::vector<struct single_event*> all_single_events;
	all_single_events.reserve(total_num_single_events);

	std::vector<struct comm_event*> all_comm_events;
	all_comm_events.reserve(total_num_comm_events);

	this->gather_sorted_openstream_parsing_events(mes, all_single_events, all_comm_events);

	/*
	* --------------------------
	* Data structures for parsing the trace events
	* --------------------------
	*/

	// Frame maps to a queue of instances waiting to start executing
	// The next TEXEC start with the appropriate frame will be the next instance in the queue
	std::map<uint64_t, std::queue<Fuse::Instance_p> > ready_instances_by_frame;

	// Each executing instance is paired with its label
	std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > > executing_instances_by_cpu; // contains the tasks
	std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > > executing_instances_by_cpu_it_set; // always empty for OpenStream

	// a particular interval is read or written by a particular instance
	boost::icl::interval_map<
			uint64_t,
			std::set<std::pair<unsigned int, Fuse::Instance_p>, data_access_time_compare>
			> data_accesses;

	// top level instances are handled differently because there is no bounding START and END for those TCREATES
	struct frame* top_level_frame = nullptr;
	for(auto se : all_single_events){
		if(se->type == SINGLE_TYPE_TCREATE){
			top_level_frame = se->active_frame;
			break;
		}
	}

	// As we iterate through the trace, keep a running counter event index for efficient searching
	std::vector<int> ces_hints_per_cpu(mes->max_cpu+1, 0);

	// Data structures for the 'runtime' instances, to track the behaviour of the 'non-work' execution
	std::vector<Fuse::Instance_p> runtime_instances_by_cpu;
	std::vector<uint64_t> runtime_starts_by_cpu;
	std::vector<uint64_t> partially_traced_state_time_by_cpu;
	std::vector<unsigned int> next_state_event_idx_by_cpu;
	for(int cpu_idx = mes->min_cpu; cpu_idx <= mes->max_cpu; cpu_idx++){

		Fuse::Instance_p runtime_instance(new Fuse::Instance());
		std::vector<int> label = {(-cpu_idx - 1)};
		runtime_instance->label = label;
		runtime_instance->cpu = cpu_idx;
		runtime_instance->symbol = "runtime";
		runtime_instance->start = 0;
		runtime_instance->is_gpu_eligible = 0;

		runtime_instances_by_cpu.push_back(runtime_instance);
		next_state_event_idx_by_cpu.push_back(0);
		partially_traced_state_time_by_cpu.push_back(0);
		runtime_starts_by_cpu.push_back(0);
	}

	/*
	* --------------------------
	* Parsing the trace events
	* --------------------------
	*/

	unsigned int top_level_instance_counter = 0;
	unsigned int next_comm_event_idx = 0;

	for(auto se : all_single_events){

		// Check if we have some state information still to allocate prior to this single event
		this->allocate_cycles_in_state(
			Fuse::Runtime::OPENSTREAM,
			mes,
			se->event_set,
			se->time,
			next_state_event_idx_by_cpu,
			runtime_instances_by_cpu,
			executing_instances_by_cpu,
			executing_instances_by_cpu_it_set,
			partially_traced_state_time_by_cpu,
			runtime_starts_by_cpu);

		// Check if there is any data communications still to allocate prior to this single event
		this->update_data_accesses(mes,
			se,
			data_accesses,
			all_comm_events,
			executing_instances_by_cpu,
			next_comm_event_idx,
			total_num_comm_events,
			load_communication_matrix);

		// Process the single event (task creation/start/end etc) into the appropriate data structures
		this->process_next_openstream_single_event(se,
			top_level_frame,
			ready_instances_by_frame,
			executing_instances_by_cpu,
			runtime_instances_by_cpu,
			runtime_starts_by_cpu,
			ces_hints_per_cpu,
			top_level_instance_counter);

	}

	// Add the runtime instances simply as instances with the symbol 'runtime' to the dataset
	for(int cpu_idx = mes->min_cpu; cpu_idx <= mes->max_cpu; cpu_idx++){
		Fuse::Trace::profile.add_instance(runtime_instances_by_cpu.at(cpu_idx));
	}

	spdlog::debug("Finished processing OpenStream trace events.");

	if(load_communication_matrix)
		this->load_openstream_instance_dependencies(all_comm_events, data_accesses);

	return;

}

template <typename T>
bool sort_struct_by_time(T one, T two){
	return one->time < two->time;
}

void Fuse::Trace_aftermath_legacy::gather_sorted_openstream_parsing_events(
		struct multi_event_set* mes,
		std::vector<struct single_event*>& all_single_events,
		std::vector<struct comm_event*>& all_comm_events
		){

	for(auto es = &mes->sets[0]; es < &mes->sets[mes->num_sets]; es++){
		for(unsigned int idx = 0; idx < es->num_single_events; idx++){
			all_single_events.push_back(&es->single_events[idx]);
		}
		for(unsigned int idx = 0; idx < es->num_comm_events; idx++){
			all_comm_events.push_back(&es->comm_events[idx]);
		}
	}

	std::sort(all_single_events.begin(), all_single_events.end(), sort_struct_by_time<struct single_event*>);
	std::sort(all_comm_events.begin(), all_comm_events.end(), sort_struct_by_time<struct comm_event*>);

}

void Fuse::Trace_aftermath_legacy::allocate_cycles_in_state(
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
		){

	spdlog::trace("Allocating {} state cycles prior to single event at timestamp {}",
		Fuse::convert_runtime_to_string(runtime),
		event_time);

	unsigned int single_event_cpu = es->cpu;
	unsigned int next_state_event_idx = next_state_event_idx_by_cpu.at(single_event_cpu);

	bool handling_states = true;
	while(handling_states){
		if(next_state_event_idx < es->num_state_events &&
			es->state_events[next_state_event_idx].start < event_time
			){
			// We have a state to allocate

			struct state_event* state_event = &es->state_events[next_state_event_idx];
			char* state_name = multi_event_set_find_state_description(mes, state_event->state_id)->name;

			std::stringstream ss;
			ss << "cycles_" << state_name;

			std::string event_name = ss.str();
			event_name = Fuse::Util::lowercase(event_name);
			Fuse::Trace::profile.add_event(event_name);

			// So first find what instance I should allocate the state cycles to
			Fuse::Instance_p responsible_instance;

			auto executing_iter_task = executing_instances_by_cpu_task.find(single_event_cpu);
			auto executing_iter_is = executing_instances_by_cpu_it_set.find(single_event_cpu);
			bool should_add = true;
			if(executing_iter_task != executing_instances_by_cpu_task.end()
					&& executing_iter_is != executing_instances_by_cpu_it_set.end())
				spdlog::warn("Both a task and an iteration set are currently executing on cpu {}.", single_event_cpu);

			if(executing_iter_task == executing_instances_by_cpu_task.end()
					&& executing_iter_is == executing_instances_by_cpu_it_set.end()){
				// There are no executing instances on this CPU, so allocate to the runtime
				responsible_instance = runtime_instances_by_cpu.at(single_event_cpu);

				// if we haven't yet started the instances, don't trace the runtime state
				if(runtime_starts_by_cpu.at(single_event_cpu) == 0)
					should_add = false;

			} else if(executing_iter_task != executing_instances_by_cpu_task.end()){
				responsible_instance = executing_iter_task->second.first;
			} else {
				responsible_instance = executing_iter_is->second.first;
			}

			// Now find the amount of cycles of this state to allocate

			if(event_time >= state_event->end){
				// the current state ended before the single event timestamp, so allocate all remaining cycles of the state

				uint64_t partially_traced_state_time = partially_traced_state_time_by_cpu.at(single_event_cpu);
				int64_t additional_time_in_state = state_event->end - state_event->start - partially_traced_state_time;

				// we have now traced everything for this state, and will be moving on to the next state
				partially_traced_state_time_by_cpu.at(single_event_cpu) = 0;

				if(should_add)
					responsible_instance->append_event_value(event_name,additional_time_in_state,true);

				next_state_event_idx++;

			} else {
				// the current state ended after the single event timestamp

				// so:
					// trace the state up to the single event
					// record that we have partially traced this state in the per-cpu vector
					// then next time, subtract what we have already partially traced from what we intend to trace

				uint64_t partially_traced_state_time = partially_traced_state_time_by_cpu.at(single_event_cpu);
				int64_t additional_partial_time_in_state = event_time - state_event->start - partially_traced_state_time;

				partially_traced_state_time_by_cpu.at(single_event_cpu) += additional_partial_time_in_state;

				if(should_add)
					responsible_instance->append_event_value(event_name,additional_partial_time_in_state,true);

				// do not continue to the next state
				handling_states = false;

			}

		} else {
			// We have no more states to allocate
			handling_states = false;
		}

	}

	// update to wherever we got to for this cpu's states
	// next state event idx might be the same, but we will have updated the partially_trace_state_time
	next_state_event_idx_by_cpu.at(single_event_cpu) = next_state_event_idx;

	spdlog::trace("Finished allocating {} state cycles prior to single event at timestamp {}",
		Fuse::convert_runtime_to_string(runtime),
		event_time);

}

template <typename Compare>
void Fuse::Trace_aftermath_legacy::update_data_accesses(
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
		bool load_communication_matrix){

	spdlog::trace("Updating OpenStream data accesses prior to single event at timestamp {}", se->time);

	bool handling_comm = true;

	while(handling_comm){
		if(next_comm_event_idx < total_num_comm_events && all_comm_events[next_comm_event_idx]->time < se->time){

			// We only care about READ and WRITE communication events, so iterate forward until we get one
			// or until we are past the current single event timestamp
			struct comm_event* ce = all_comm_events[next_comm_event_idx];

			if(!(ce->type == COMM_TYPE_DATA_READ || ce->type == COMM_TYPE_DATA_WRITE)){
				next_comm_event_idx++;
				continue;
			}

			// We have a comm event to handle
			switch(ce->type){

				case COMM_TYPE_DATA_READ: {

					auto executing_iter = executing_instances_by_cpu.find(ce->dst_cpu);
					if(executing_iter == executing_instances_by_cpu.end())
						throw std::runtime_error("There is no executing instance for a read communication event");

					Fuse::Instance_p responsible_instance = executing_iter->second.first;

					if(load_communication_matrix){
						// add the access to the interval map to later determine dependencies
						std::set<std::pair<unsigned int, Fuse::Instance_p>, data_access_time_compare> access;
						access.insert(std::make_pair((unsigned int)ce->type,responsible_instance));

						data_accesses += std::make_pair(boost::icl::interval<uint64_t>::right_open((uint64_t)ce->what->addr,((uint64_t)ce->what->addr)+(ce->size)), access);
					}

					// Add the communication data to the instance
					std::stringstream ss;
					ss << "data_read_" << ce->numa_dist << "_hops";
					Fuse::Trace::profile.add_event(ss.str());

					responsible_instance->append_event_value(ss.str(),ce->size,true);

					break;
				}
				case COMM_TYPE_DATA_WRITE: {

					auto executing_iter = executing_instances_by_cpu.find(ce->src_cpu);
					if(executing_iter == executing_instances_by_cpu.end())
						throw std::runtime_error("There is no executing instance for a write communication event");

					Fuse::Instance_p responsible_instance = executing_iter->second.first;

					if(load_communication_matrix){
						// add the access to the interval map to later determine dependencies
						std::set<std::pair<unsigned int, Fuse::Instance_p>, data_access_time_compare> access;
						access.insert(std::make_pair((unsigned int)ce->type,responsible_instance));

						data_accesses += std::make_pair(boost::icl::interval<uint64_t>::right_open((uint64_t)ce->what->addr,((uint64_t)ce->what->addr)+(ce->size)), access);
					}

					std::stringstream ss;
					ss << "data_write_" << ce->numa_dist << "_hops";
					Fuse::Trace::profile.add_event(ss.str());

					responsible_instance->append_event_value(ss.str(),ce->size,true);

					break;
				}
				default:
					break;

			}

			// continue on to handle the next communication event
			next_comm_event_idx++;

		} else {

			// any remaining communication events are after the current single event,
			// so we want to process the current single event first
			handling_comm = false;
		}

	}

	spdlog::trace("Finished updating OpenStream data accesses prior to single event at timestamp {}", se->time);

}

void Fuse::Trace_aftermath_legacy::process_openstream_instance_creation(
			struct single_event* se,
			struct frame* top_level_frame,
			std::map<uint64_t, std::queue<Fuse::Instance_p> >& ready_instances_by_frame,
			std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
			unsigned int& top_level_instance_counter
			){

	spdlog::trace("Processing an OpenStream TCREATE on cpu {} at timestamp {}", se->event_set->cpu, se->time);

	Fuse::Instance_p instance(new Fuse::Instance());

	// Set the appropriate label for this newly created instance
	if(se->active_frame == top_level_frame){
		//std::vector<int> label = {top_level_instance_counter++};
		instance->label = {(int) top_level_instance_counter++};

	} else {
		// The parent instance data structure maintains the correct rank of child instances
		auto executing_iter = executing_instances_by_cpu.find(se->event_set->cpu);
		instance->label = executing_iter->second.second;

		// Update the rank for my later siblings
		executing_iter->second.second.at(executing_iter->second.second.size()-1)++;
	}

	// Add the instance to the queue of unstarted instances
	// (We do not yet know which CPU this instance will be executed on) via the what frame
	auto frame_iter = ready_instances_by_frame.find(se->what->addr);
	if(frame_iter == ready_instances_by_frame.end()){

		// This is the first unstarted instance produced by the parent instance
		// create the queue for the new frame and add it

		std::queue<Fuse::Instance_p> instances_waiting_for_execution({instance});
		ready_instances_by_frame.insert(std::make_pair(se->what->addr, instances_waiting_for_execution));

	} else {

		// Add this instance as an one that is waiting to begin execution
		frame_iter->second.push(instance);
	}

	return;

}

void Fuse::Trace_aftermath_legacy::process_openstream_instance_start(
		struct single_event* se,
		std::map<uint64_t, std::queue<Fuse::Instance_p> >& ready_instances_by_frame,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu
		){

	spdlog::trace("Processing an OpenStream TEXEC_START on cpu {} at timestamp {}", se->event_set->cpu, se->time);

	// Find the instance that I have just started
	auto frame_iter = ready_instances_by_frame.find(se->what->addr);
	if(frame_iter == ready_instances_by_frame.end())
		{}
		//LOG(ERROR) << "Parsing tracefile error: Cannot find a waiting (created) instance for a TEXEC_START."; // Has the OpenStream TSC fix been implemented?

	// Get the waiting instance and remove it from queue
	auto instances_waiting_for_execution = frame_iter->second;
	Fuse::Instance_p my_instance = instances_waiting_for_execution.front();
	instances_waiting_for_execution.pop();
	frame_iter->second = instances_waiting_for_execution;

	// Give it its execution details
	Fuse::Symbol symbol("unknown_symbol_name");
	if(se->active_task->symbol_name != nullptr){
		symbol = se->active_task->symbol_name;
		std::replace(symbol.begin(), symbol.end(), ',', '_');
	}

	my_instance->symbol = symbol;
	my_instance->cpu = se->event_set->cpu;
	my_instance->start = se->time;
	my_instance->is_gpu_eligible = se->what->is_gpu_eligible;

	// Set the next child label for this CPU to be this instance's label, with an appended 0 (for the first child rank)
	auto label_for_potential_child = my_instance->label; // calls copy constructor
	label_for_potential_child.push_back(0);

	auto executing_pair = std::make_pair(my_instance,label_for_potential_child);
	executing_instances_by_cpu.insert(std::make_pair(se->event_set->cpu,executing_pair));

	// Now that the instance has been added as executing, update all of the currently executing instances to have updated realised parallelism
	// TODO this should be a much more sophisticated metric for parallelism
	unsigned int num_executing_instances = executing_instances_by_cpu.size();
	for(auto executing_iter : executing_instances_by_cpu){
		auto instance = executing_iter.second.first;
		instance->append_max_event_value("realised_parallelism",num_executing_instances);
	}

}

void Fuse::Trace_aftermath_legacy::process_openstream_instance_end(
		struct single_event* se,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
		std::vector<uint64_t>& runtime_starts_by_cpu,
		std::vector<int>& ces_hints_per_cpu
		){

	spdlog::trace("Processing an OpenStream TEXEC_END on cpu {} at timestamp {}", se->event_set->cpu, se->time);

	// Find the executing instance on this CPU that has just ended
	auto executing_iter = executing_instances_by_cpu.find(se->event_set->cpu);
	if(executing_iter == executing_instances_by_cpu.end())
		{}
		//LOG(FATAL) << "Parsing tracefile error: Encountered a TEXEC_END trace event, but cannot find an executing task on the CPU (" << se->event_set->cpu << "). Aborting.";

	// Mark when it ended
	auto my_instance = executing_iter->second.first;
	my_instance->end = se->time;

	// Remove it from the executing list, also removing the label required to create its children
	executing_instances_by_cpu.erase(executing_iter);

	// Calculate the instance's counter values and append them
	this->interpolate_and_append_counter_values(
		my_instance,
		my_instance->start,
		my_instance->end,
		se->event_set,
		ces_hints_per_cpu.at(se->event_set->cpu)
	);

	// Add the instance to this execution profile
	Fuse::Trace::profile.add_instance(my_instance);

	// Save the start time for the following runtime-execution period
	runtime_starts_by_cpu.at(se->event_set->cpu) = se->time;

}

void Fuse::Trace_aftermath_legacy::process_next_openstream_single_event(
		struct single_event* se,
		struct frame* top_level_frame,
		std::map<uint64_t, std::queue<Fuse::Instance_p> >& ready_instances_by_frame,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
		std::vector<Fuse::Instance_p>& runtime_instances_by_cpu,
		std::vector<uint64_t>& runtime_starts_by_cpu,
		std::vector<int>& ces_hints_per_cpu,
		unsigned int& top_level_instance_counter){

	if(!(se->type == SINGLE_TYPE_TCREATE ||
			se->type == SINGLE_TYPE_TEXEC_START ||
			se->type == SINGLE_TYPE_TEXEC_END ||
			se->type == SINGLE_TYPE_SYSCALL)){
		return;
	}

	int single_event_cpu = se->event_set->cpu;

	switch(se->type){
		case SINGLE_TYPE_TCREATE: {

			this->process_openstream_instance_creation(se,
				top_level_frame,
				ready_instances_by_frame,
				executing_instances_by_cpu,
				top_level_instance_counter);

			break;
		}
		case SINGLE_TYPE_TEXEC_START: {

			// We assume that there was a period of 'non-work' (runtime system execution) immediately preceeding this instance start
			// Therefore, add counter values for the prior runtime instance
			if(runtime_starts_by_cpu.at(single_event_cpu) != 0){
				Fuse::Instance_p runtime_instance = runtime_instances_by_cpu.at(single_event_cpu);
				uint64_t start_time = runtime_starts_by_cpu.at(single_event_cpu);
				uint64_t end_time = se->time;

				// The start time is the end time of the previous one, so we can just use the same hint, and update it if necessary
				this->interpolate_and_append_counter_values(runtime_instance,
					start_time,
					end_time,
					se->event_set,
					ces_hints_per_cpu.at(single_event_cpu));
			}

			this->process_openstream_instance_start(se,
				ready_instances_by_frame,
				executing_instances_by_cpu);

			break;
		}
		case SINGLE_TYPE_TEXEC_END: {

			this->process_openstream_instance_end(se,
					executing_instances_by_cpu,
					runtime_starts_by_cpu,
					ces_hints_per_cpu);

			break;
		}
		case SINGLE_TYPE_SYSCALL: {

			spdlog::trace("Processing an OpenStream SYSCALL on cpu {} at timestamp {}", se->event_set->cpu, se->time);

			// Increment the instance (or runtime-instance) value for this syscall

			std::stringstream ss;
			ss << "syscall_" << se->sub_type_id;
			Fuse::Trace::profile.add_event(ss.str());

			auto executing_iter = executing_instances_by_cpu.find(single_event_cpu);
			if(executing_iter != executing_instances_by_cpu.end())
				executing_iter->second.first->append_event_value(ss.str(),1,true);
			else
				runtime_instances_by_cpu.at(se->event_set->cpu)->append_event_value(ss.str(),1,true);

			break;
		}
		default:
			break;
	}

}

template <typename Compare>
void Fuse::Trace_aftermath_legacy::load_openstream_instance_dependencies(std::vector<struct comm_event*> all_comm_events,
		boost::icl::interval_map<
			uint64_t,
			std::set<std::pair<unsigned int, Fuse::Instance_p>, Compare>
			>& data_accesses
		){

	spdlog::debug("Loading openstream instance dependencies.");

	std::vector<Fuse::Instance_p> all_instances = Fuse::Trace::profile.get_instances(false);
	std::sort(all_instances.begin(), all_instances.end(), comp_instances_by_label_dfs);

	spdlog::trace("There are {} data intervals that are accessed.", data_accesses.iterative_size());

	// iterate all the data, and create the links for dependent instances
	// Only record the producer instances for each *consumer*
	// Does *not* record the consumer instances for a given producer
	for(auto interval_iter : data_accesses){

		// this set is ordered by instance start time
		auto accesses = interval_iter.second;

		std::vector<Fuse::Instance_p> consumer_instances;
		std::vector<Fuse::Instance_p> producer_instances;

		for(auto interval_access_iter : accesses){
			switch(interval_access_iter.first){
				case COMM_TYPE_DATA_READ:
					consumer_instances.push_back(interval_access_iter.second);
					break;
				case COMM_TYPE_DATA_WRITE:
					producer_instances.push_back(interval_access_iter.second);
					break;
			};
		}

		std::stringstream ss;
		ss << interval_iter.first;
		auto interval_string = ss.str();

		spdlog::trace("There are {} producer instances and {} consumer instances for memory location interval {}.", producer_instances.size(), consumer_instances.size(), interval_string);

		unsigned int previous_producer_idx = 0;
		for(auto consumer : consumer_instances){

			if(producer_instances.size() == 0){
				spdlog::warn("The interval {} was read by a consumer instance, but no producer instance wrote to this interval.", interval_string);
				continue;
			}

			// As I consumed from the previous producer, iterate forward to get the one immediately preceeding this read
			while(previous_producer_idx+1 < producer_instances.size() and producer_instances.at(previous_producer_idx+1)->end < consumer->start)
				previous_producer_idx++;

			auto producer = producer_instances.at(previous_producer_idx);

			auto instance_dependency_iter = Fuse::Trace::profile.instance_dependencies.find(consumer);
			if(instance_dependency_iter == Fuse::Trace::profile.instance_dependencies.end()){
				std::set<Fuse::Instance_p> this_consumer_depends_on;
				std::set<Fuse::Instance_p> this_consumer_produces_for;
				this_consumer_depends_on.insert(producer);

				Fuse::Trace::profile.instance_dependencies[consumer] = std::make_pair(this_consumer_depends_on, this_consumer_produces_for);
			} else {
				instance_dependency_iter->second.first.insert(producer);
			}

		}

	}

	spdlog::debug("Finished loading openstream instance dependencies.");

}

void Fuse::Trace_aftermath_legacy::interpolate_and_append_counter_values(
		Fuse::Instance_p instance,
		uint64_t start_time,
		uint64_t end_time,
		struct event_set* es,
		int& start_index_hint){

	// Interpolate the counter values between start_time and end_time for each counter event set

	int64_t value_start, value_end;
	int num_errors = 0;

	int start_idx = start_index_hint;
	int end_idx = start_index_hint;
	bool init = false;

	// We are assuming that the execution that we are tracing between start_time and end_time occured on a single processing unit
	// We are assuming that all counter event sets receive a value at each trace-point
	// 	(i.e. position i in each counter event set was traced at the same timestamp)
	for(size_t ctr_ev_idx = 0; ctr_ev_idx < es->num_counter_event_sets; ctr_ev_idx++) {
		struct counter_event_set* ces = &es->counter_event_sets[ctr_ev_idx];

		std::string event_name(ces->desc->name);
		event_name = Fuse::Util::lowercase(event_name);

		if(Fuse::Trace::profile.filtered_events.size() > 0
				&& std::find(
						Fuse::Trace::profile.filtered_events.begin(),
						Fuse::Trace::profile.filtered_events.end(),
						event_name) == Fuse::Trace::profile.filtered_events.end()
				)
			continue;

		Fuse::Trace::profile.add_event(event_name);

		if(init){
			// We know the index of the correct position in the counter_event_set, so avoid the search
			if(counter_event_set_interpolate_value_using_index(ces, start_time, &value_start, &start_idx)) {
				num_errors++;
				continue;
			}

			if(counter_event_set_interpolate_value_using_index(ces, end_time, &value_end, &end_idx)) {
				num_errors++;
				continue;
			}

		} else {
			// We do not know the index, but we know that the next index to use is equal to or later than the last one we used
			// So use the last one as a hint to the search
			if(counter_event_set_interpolate_value_search_with_hint(ces, start_time, &value_start, &start_idx)) {
				num_errors++;
				continue;
			}

			// We start from this index to search for the end's corresponding index
			end_idx = start_idx;
			if(counter_event_set_interpolate_value_search_with_hint(ces, end_time, &value_end, &end_idx)) {
				num_errors++;
				continue;
			}

			// start_idx is now the correct index to use for the starting timestamp
			// end_idx is now the correct index to use for the ending timestamp
			init = true;

		}

		instance->append_event_value(event_name, value_end-value_start, true);

	}

	// Save the starting index that we found as the hint for a later start timestamp
	start_index_hint = start_idx;

	if (num_errors > 0)
		spdlog::warn("Found {} errors when interpolating counter events for an instance.", num_errors);

	// Append instance duration as an event
	Fuse::Trace::profile.add_event("duration");
	int64_t duration = end_time - start_time;
	instance->append_event_value("duration",duration,true);

}

bool sort_omp_by_time(struct Fuse::Aftermath_omp_construct ompc_one, struct Fuse::Aftermath_omp_construct ompc_two){
	return ompc_one.time < ompc_two.time;
}

std::vector<struct Fuse::Aftermath_omp_construct> Fuse::Trace_aftermath_legacy::gather_openmp_parsing_constructs(struct multi_event_set* mes){
	
	std::vector<std::pair<uint64_t, uint64_t> > measurement_intervals;

	uint64_t measurement_start = 0;
	uint64_t measurement_end = std::numeric_limits<uint64_t>::max();

	// Check if there are user-declared measurement intervals
	// TODO what should labels be? Whole program or only measurement interval?
	if(mes->num_global_single_events > 0){

		spdlog::debug("There are {} global single events.", mes->num_global_single_events);

		bool within_interval = false;

		// Assuming that global single events are ordered
		// TODO (per-CPU? per-instance?) sub measurement intervals
		for(unsigned int gse_idx=0; gse_idx < mes->num_global_single_events; gse_idx++){
			struct global_single_event gse = mes->global_single_events[gse_idx];

			if(gse.type == GLOBAL_SINGLE_TYPE_MEASURE_START){

				if(within_interval)
					throw std::runtime_error("Invalid assumption: Found two consecutive measurement interval start events.");

				within_interval = true;

				measurement_start = gse.time;
				spdlog::debug("Found a measurement start event at time {}.", measurement_start);

			} else if(gse.type == GLOBAL_SINGLE_TYPE_MEASURE_END){

				if(!within_interval)
					throw std::runtime_error("Invalid assumption: Found a measurement interval end that didn't follow a measurement interval start.");

				spdlog::debug("Found a measurement end event at time {}.", gse.time);

				measurement_intervals.push_back(std::make_pair(measurement_start, gse.time));
				within_interval = false;

			}
		}
		
		if(within_interval)
			measurement_intervals.push_back(std::make_pair(measurement_start, std::numeric_limits<uint64_t>::max()));

	}

	std::vector<struct Fuse::Aftermath_omp_construct> omp_constructs;

	for(struct omp_for_chunk_set* cs = &mes->omp_for_chunk_sets[0]; cs < &mes->omp_for_chunk_sets[mes->num_omp_for_chunk_sets]; cs++){

		// check this event is within the measurement interval if they exist:
		if(measurement_intervals.size() > 0){
			// check if within one of the intervals
			bool process_construct = false;
			
			for(auto interval_pair : measurement_intervals){
				// if the construct is within the interval, it's good
				if(cs->min_start >= interval_pair.first && cs->max_end <= interval_pair.second){
					process_construct = true;
					break;
				}
				// if the construct started before the interval, we can break
				if(cs->min_start < interval_pair.first)
					break;
			}
			
			if(!process_construct)
				continue;
		}

		struct Fuse::Aftermath_omp_construct construct_enter;
		construct_enter.type = Fuse::Omp_construct_type::CHUNK_SET_ENTER;
		construct_enter.cpu = cs->cpu;
		construct_enter.time = cs->min_start + 1;
		construct_enter.ptr.cs = cs;
		omp_constructs.push_back(construct_enter);

		struct Fuse::Aftermath_omp_construct construct_leave;
		construct_leave.type = Fuse::Omp_construct_type::CHUNK_SET_LEAVE;
		construct_leave.cpu = cs->cpu;
		construct_leave.time = cs->max_end - 1;
		construct_leave.ptr.cs = cs;
		omp_constructs.push_back(construct_leave);

	}

	for(struct event_set* es = &mes->sets[0]; es < &mes->sets[mes->num_sets]; es++){

		for(unsigned int idx = 0; idx < es->num_omp_pregion_enters; idx++){

			struct omp_pregion_enter* pregion_enter = &es->omp_pregion_enters[idx];
		
			if(measurement_intervals.size() > 0){
				// check if within one of the intervals
				bool process_construct = false;
				
				for(auto interval_pair : measurement_intervals){
					if(pregion_enter->time >= interval_pair.first && pregion_enter->time <= interval_pair.second){
						process_construct = true;
						break;
					}
					if(pregion_enter->time < interval_pair.first)
						break;
				}
				
				if(!process_construct)
					continue;
			}

			struct Fuse::Aftermath_omp_construct construct;
			construct.type = Fuse::Omp_construct_type::PREGION_ENTER;
			construct.cpu = es->cpu;
			construct.time = pregion_enter->time;
			construct.ptr.ope = pregion_enter;
			omp_constructs.push_back(construct);

		}

		for(unsigned int idx = 0; idx < es->num_omp_pregion_leaves; idx++){

			struct omp_pregion_leave* pregion_leave = &es->omp_pregion_leaves[idx];

			if(measurement_intervals.size() > 0){
				// check if within one of the intervals
				bool process_construct = false;
				
				for(auto interval_pair : measurement_intervals){
					if(pregion_leave->time >= interval_pair.first && pregion_leave->time <= interval_pair.second){
						process_construct = true;
						break;
					}
					if(pregion_leave->time < interval_pair.first)
						break;
				}
				
				if(!process_construct)
					continue;
			}

			struct Fuse::Aftermath_omp_construct construct;
			construct.type = Fuse::Omp_construct_type::PREGION_LEAVE;
			construct.cpu = es->cpu;
			construct.time = pregion_leave->time;
			construct.ptr.opl = pregion_leave;
			omp_constructs.push_back(construct);

		}

		for(unsigned int idx = 0; idx < es->num_omp_for_chunk_set_parts; idx++){

			struct Fuse::Aftermath_omp_construct construct_enter;
			construct_enter.type = Fuse::Omp_construct_type::CHUNK_SET_PART_ENTER;
			construct_enter.cpu = es->omp_for_chunk_set_parts[idx].chunk_set->cpu;
			construct_enter.time = es->omp_for_chunk_set_parts[idx].start + 2;
			construct_enter.ptr.csp = &es->omp_for_chunk_set_parts[idx];

			if(measurement_intervals.size() > 0){
				// check if within one of the intervals
				bool process_construct = false;
				
				for(auto interval_pair : measurement_intervals){
					if(construct_enter.time >= interval_pair.first && construct_enter.time <= interval_pair.second){
						process_construct = true;
						break;
					}
					if(construct_enter.time < interval_pair.first)
						break;
				}
				
				if(!process_construct)
					continue;
			}

			omp_constructs.push_back(construct_enter);

			struct Fuse::Aftermath_omp_construct construct_leave;
			construct_leave.type = Fuse::Omp_construct_type::CHUNK_SET_PART_LEAVE;
			construct_leave.cpu = es->omp_for_chunk_set_parts[idx].chunk_set->cpu;
			construct_leave.time = es->omp_for_chunk_set_parts[idx].end;
			construct_leave.ptr.csp = &es->omp_for_chunk_set_parts[idx];

			if(measurement_intervals.size() > 0){
				// check if within one of the intervals
				bool process_construct = false;
				
				for(auto interval_pair : measurement_intervals){
					if(construct_leave.time >= interval_pair.first && construct_leave.time <= interval_pair.second){
						process_construct = true;
						break;
					}
					if(construct_leave.time < interval_pair.first)
						break;
				}
				
				if(!process_construct)
					continue;
			}

			omp_constructs.push_back(construct_leave);

		}

		for(unsigned int idx = 0; idx < es->num_omp_task_parts; idx++){

			struct Fuse::Aftermath_omp_construct construct_part_enter;
			construct_part_enter.type = Fuse::Omp_construct_type::TASK_PART_ENTER;
			construct_part_enter.cpu = es->omp_task_parts[idx].cpu;
			construct_part_enter.time = es->omp_task_parts[idx].start + 2;
			construct_part_enter.ptr.tp = &es->omp_task_parts[idx];

			if(measurement_intervals.size() > 0){
				// check if within one of the intervals
				bool process_construct = false;
				
				for(auto interval_pair : measurement_intervals){
					if(construct_part_enter.time >= interval_pair.first && construct_part_enter.time <= interval_pair.second){
						process_construct = true;
						break;
					}
					if(construct_part_enter.time < interval_pair.first)
						break;
				}
				
				if(!process_construct)
					continue;
			}

			omp_constructs.push_back(construct_part_enter);

			struct Fuse::Aftermath_omp_construct construct_part_leave;
			construct_part_leave.type = Fuse::Omp_construct_type::TASK_PART_LEAVE;
			construct_part_leave.cpu = es->omp_task_parts[idx].cpu;
			construct_part_leave.time = es->omp_task_parts[idx].end;
			construct_part_leave.ptr.tp = &es->omp_task_parts[idx];
			
			if(measurement_intervals.size() > 0){
				// check if within one of the intervals
				bool process_construct = false;
				
				for(auto interval_pair : measurement_intervals){
					if(construct_part_leave.time >= interval_pair.first && construct_part_leave.time <= interval_pair.second){
						process_construct = true;
						break;
					}
					if(construct_part_leave.time < interval_pair.first)
						break;
				}
				
				if(!process_construct)
					continue;
			}

			omp_constructs.push_back(construct_part_leave);

		}

		for(unsigned int idx = 0; idx < es->num_omp_singles; idx++){

			struct Fuse::Aftermath_omp_construct construct_single;
			construct_single.type = Fuse::Omp_construct_type::SINGLE;
			construct_single.cpu = es->cpu;
			construct_single.time = es->omp_singles[idx].time;
			construct_single.ptr.os = &es->omp_singles[idx];

			if(measurement_intervals.size() > 0){
				// check if within one of the intervals
				bool process_construct = false;
				
				for(auto interval_pair : measurement_intervals){
					if(construct_single.time >= interval_pair.first && construct_single.time <= interval_pair.second){
						process_construct = true;
						break;
					}
					if(construct_single.time < interval_pair.first)
						break;
				}
				
				if(!process_construct)
					continue;
			}

			omp_constructs.push_back(construct_single);

			if(es->omp_singles[idx].executed == 0){

				struct Fuse::Aftermath_omp_construct construct_single_leave;
				construct_single_leave.type = Fuse::Omp_construct_type::SINGLE_LEAVE;
				construct_single_leave.cpu = es->cpu;
				construct_single_leave.time = es->omp_singles[idx].osl->time;
				construct_single_leave.ptr.os = &es->omp_singles[idx];

				if(measurement_intervals.size() > 0){
					// check if within one of the intervals
					bool process_construct = false;
					
					for(auto interval_pair : measurement_intervals){
						if(construct_single_leave.time >= interval_pair.first && construct_single_leave.time <= interval_pair.second){
							process_construct = true;
							break;
						}
						if(construct_single_leave.time < interval_pair.first)
							break;
					}
				
					if(!process_construct)
						continue;
				}

				omp_constructs.push_back(construct_single_leave);

			}

		}

	}

	for(struct omp_task_instance* ti = &mes->omp_task_instances[0]; ti < &mes->omp_task_instances[mes->num_omp_task_instances]; ti++){

		struct Fuse::Aftermath_omp_construct construct_creation;
		construct_creation.type = Fuse::Omp_construct_type::TASK_CREATION;
		construct_creation.cpu = ti->ti_creation->cpu;
		construct_creation.time = ti->ti_creation->timestamp;
		construct_creation.ptr.ti = ti;

		if(measurement_intervals.size() > 0){
			// check if within one of the intervals
			bool process_construct = false;
			
			for(auto interval_pair : measurement_intervals){
				if(construct_creation.time >= interval_pair.first && construct_creation.time <= interval_pair.second){
					process_construct = true;
					break;
				}
				if(construct_creation.time < interval_pair.first)
					break;
			}
			
			if(!process_construct)
				continue;
		}

		omp_constructs.push_back(construct_creation);

		struct Fuse::Aftermath_omp_construct construct_part_end;
		construct_part_end.type = Fuse::Omp_construct_type::TASK_END;
		construct_part_end.cpu = ti->ti_finish->cpu;
		construct_part_end.time = ti->ti_finish->timestamp;
		construct_part_end.ptr.ti = ti;

		if(measurement_intervals.size() > 0){
			// check if within one of the intervals
			bool process_construct = false;
			
			for(auto interval_pair : measurement_intervals){
				if(construct_part_end.time >= interval_pair.first && construct_part_end.time <= interval_pair.second){
					process_construct = true;
					break;
				}
				if(construct_part_end.time < interval_pair.first)
					break;
			}
			
			if(!process_construct)
				continue;
		}

		omp_constructs.push_back(construct_part_end);

	}

	return omp_constructs;

}

void Fuse::Trace_aftermath_legacy::parse_openmp_instances(struct multi_event_set* mes){

	spdlog::debug("Parsing OpenMP instances.");
	spdlog::debug("There are {} OpenMP for constructs.", mes->num_omp_fors);
	spdlog::debug("There are {} OpenMP for instances.", mes->num_omp_for_instances);
	spdlog::debug("There are {} OpenMP for iteration sets.", mes->num_omp_for_chunk_sets);
	spdlog::debug("There are {} OpenMP task constructs.", mes->num_omp_tasks);
	spdlog::debug("There are {} OpenMP task instances.", mes->num_omp_task_instances);

	// Get all omp constructs ordered by time
	std::vector<struct Fuse::Aftermath_omp_construct> omp_constructs = this->gather_openmp_parsing_constructs(mes);
	std::sort(omp_constructs.begin(), omp_constructs.end(), sort_omp_by_time);

	// Get the syscalls ordered by time
	std::vector<std::vector<Fuse::Aftermath_omp_construct> > syscalls_by_cpu;
	syscalls_by_cpu.reserve(mes->num_sets);

	for(unsigned int cpu_idx = 0; cpu_idx <= (unsigned int) mes->max_cpu; cpu_idx++){
		std::vector<Fuse::Aftermath_omp_construct> constructs;
		syscalls_by_cpu.push_back(constructs);
	}

	for(struct event_set* es = &mes->sets[0]; es < &mes->sets[mes->num_sets]; es++){
		for(unsigned int idx = 0; idx < es->num_single_events; idx++){
			if(es->single_events[idx].type == SINGLE_TYPE_SYSCALL){
				struct Fuse::Aftermath_omp_construct syscall_construct;
				syscall_construct.type = Fuse::Omp_construct_type::SYSCALL;
				syscall_construct.cpu = es->cpu;
				syscall_construct.time = es->single_events[idx].time;
				syscall_construct.ptr.se = &es->single_events[idx];
				syscalls_by_cpu.at(es->cpu).push_back(syscall_construct);
			}
		}
	}

	std::vector<Fuse::Instance_p> runtime_instances_by_cpu;
	std::vector<uint64_t> runtime_starts_by_cpu;
	std::vector<uint64_t> partially_traced_state_time_by_cpu;
	std::vector<unsigned int> next_state_event_idx_by_cpu;

	for(unsigned int cpu_idx = 0; cpu_idx <= (unsigned int) mes->max_cpu; cpu_idx++){
		std::sort(syscalls_by_cpu.at(cpu_idx).begin(), syscalls_by_cpu.at(cpu_idx).end(), sort_omp_by_time);

		Fuse::Instance_p runtime_instance(new Fuse::Instance());

		std::vector<int> label = {(-((int) cpu_idx) - 1)};
		runtime_instance->label = label;
		runtime_instance->cpu = cpu_idx;
		runtime_instance->symbol = "runtime";
		runtime_instance->start = 0;
		runtime_instance->is_gpu_eligible = 0;

		runtime_instances_by_cpu.push_back(runtime_instance);
		runtime_starts_by_cpu.push_back(0);
		partially_traced_state_time_by_cpu.push_back(0);
		next_state_event_idx_by_cpu.push_back(0);

	}

	/* Declare the necessary data structures */

	/* List of waiting tasks in the runtime (i.e. tasks that have been created, parts might be executing, but have not yet finished executing)
	*  TODO this can probably be merged with tps_in_t unless we care about knowing that a task has finished (aside from execution contexts)
	*  A task_instance maps to a pair<future_context,task> where the task already holds its correct label
	*/
	std::map<struct omp_task_instance*, std::pair<std::vector<int>, Fuse::Instance_p> > current_tasks;

	/* Map each CPU to a stack of contexts
	*	 The top of the stack is the context that the CPU is currently in
	*	 When we leave a task, we return the context before
	*		 This is because a task can be executed at any place,
	*		 i.e there is no connection between the current running context and the now executing task's context
	*		 So take the task's context as a new item in the stack, and return to our current one after it's complete
	*/
	std::map<unsigned int, std::vector<std::vector<int> > > execution_context_stack_by_cpu; // for the context that the current unit should take
	std::map<unsigned int, std::vector<std::vector<int> > > future_context_stack_by_cpu; // for the context that the next unit should take

	// Each execution unit has a list of parts that compose it
	std::map<struct omp_for_chunk_set*, std::pair<Fuse::Instance_p,std::vector<struct omp_for_chunk_set_part*> >, csp_compare> csps_in_cs;
	std::map<struct omp_task_instance*, std::pair<Fuse::Instance_p,std::vector<struct omp_task_part*> >, tp_compare> tps_in_t;

	/* Each nested parallel region has a set of for-instances which have occured inside it, so we know when we encounter a new for-loop at the same level
	*  Doesn't need to be split by CPU because the instance should only appear on one CPU
	*/
	std::map<unsigned int,std::vector<std::set<struct omp_for_instance*> > > seen_instances_within_region_by_cpu;

	/* Cpu maps to a stack of parallel region histories
	*  Each parallel region history is a stack of tuples
	*  Each tuple represents <context is directly in pregion, worker's TID in the region, total num workers in the region>
	*/
	std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > > pregions_by_cpu;

	// In keeping with the OpenStream data structure for these:
	std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > > executing_tasks_by_cpu; // contains the tasks
	std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > > executing_it_sets_by_cpu; // contains the iteration sets

	// Initialise context stacks
	for(unsigned int cpu = mes->min_cpu; cpu <= (unsigned int) mes->max_cpu; cpu++){
		std::vector<int> future_context = {0};
		std::vector<std::vector<int> > future_context_stack = {future_context};
		future_context_stack_by_cpu[cpu] = future_context_stack;

		std::vector<int> current_context;
		std::vector<std::vector<int> > current_context_stack = {current_context};
		execution_context_stack_by_cpu[cpu] = current_context_stack;

		std::vector<std::set<struct omp_for_instance*> > seen_instances;
		seen_instances_within_region_by_cpu[cpu] = seen_instances;

		std::vector<std::tuple<bool, unsigned int, unsigned int> > pregions;
		std::vector<std::vector<std::tuple<bool, unsigned int, unsigned int> > > stack_of_pregion_histories = {pregions};
		pregions_by_cpu[cpu] = stack_of_pregion_histories;
	}

	/*
	* --------------------------
	* Parsing the trace events
	* --------------------------
	*/

	for(auto construct = omp_constructs.begin(); construct < omp_constructs.end(); construct++){

		struct event_set* es = multi_event_set_find_cpu(mes, construct->cpu);

		this->allocate_cycles_in_state(
			Fuse::Runtime::OPENMP,
			mes,
			es,
			construct->time,
			next_state_event_idx_by_cpu,
			runtime_instances_by_cpu,
			executing_tasks_by_cpu,
			executing_it_sets_by_cpu,
			partially_traced_state_time_by_cpu,
			runtime_starts_by_cpu);

		switch(construct->type){

			case Fuse::Omp_construct_type::PREGION_ENTER:{

				this->process_openmp_pregion_enter(
					*construct,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu,
					seen_instances_within_region_by_cpu
				);

				break;
			}
			case Fuse::Omp_construct_type::PREGION_LEAVE:{

				this->process_openmp_pregion_leave(
					construct->cpu,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu,
					seen_instances_within_region_by_cpu
				);

				break;
			}
			case Fuse::Omp_construct_type::CHUNK_SET_ENTER:{

				this->process_openmp_chunk_set_enter(
					*construct,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu,
					seen_instances_within_region_by_cpu,
					csps_in_cs
				);

				break;
			}
			case Fuse::Omp_construct_type::CHUNK_SET_LEAVE:{

				this->process_openmp_chunk_set_leave(
					construct->cpu,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu
				);

				break;
			}
			case Fuse::Omp_construct_type::CHUNK_SET_PART_ENTER:{

				this->process_previous_time_as_runtime_execution(
					mes,
					construct->cpu,
					runtime_instances_by_cpu.at(construct->cpu),
					runtime_starts_by_cpu.at(construct->cpu),
					construct->time
				);

				this->process_openmp_chunk_set_part_enter(
					*construct,
					executing_it_sets_by_cpu,
					executing_tasks_by_cpu,
					csps_in_cs
				);

				break;
			}
			case Fuse::Omp_construct_type::CHUNK_SET_PART_LEAVE:{

				// save the end of this workload as the start of a subsequent runtime period
				runtime_starts_by_cpu.at(construct->cpu) = construct->time;

				this->process_openmp_chunk_set_part_leave(
					*construct,
					runtime_starts_by_cpu,
					executing_it_sets_by_cpu
				);

				break;
			}
			case Fuse::Omp_construct_type::TASK_CREATION:{

				this->process_openmp_task_creation(
					*construct,
					runtime_instances_by_cpu,
					runtime_starts_by_cpu,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu,
					executing_it_sets_by_cpu,
					executing_tasks_by_cpu,
					current_tasks,
					tps_in_t
				);

				break;
			}
			case Fuse::Omp_construct_type::TASK_PART_ENTER:{

				this->process_previous_time_as_runtime_execution(
					mes,
					construct->cpu,
					runtime_instances_by_cpu.at(construct->cpu),
					runtime_starts_by_cpu.at(construct->cpu),
					construct->time
				);

				this->process_openmp_task_part_enter(
					*construct,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu,
					executing_it_sets_by_cpu,
					executing_tasks_by_cpu,
					current_tasks,
					tps_in_t
				);

				break;
			}
			case Fuse::Omp_construct_type::TASK_PART_LEAVE:{

				// save the end of this workload as the start of a subsequent runtime period
				runtime_starts_by_cpu.at(construct->cpu) = construct->time;

				this->process_openmp_task_part_leave(
					*construct,
					runtime_starts_by_cpu,
					executing_tasks_by_cpu
				);

				break;
			}
			case Fuse::Omp_construct_type::TASK_END:{

				this->process_openmp_task_end(
					*construct,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu,
					current_tasks
				);

				break;
			}
			case Fuse::Omp_construct_type::SINGLE:{

				this->process_openmp_single_enter(
					*construct,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu
				);

				break;
			}
			case Fuse::Omp_construct_type::SINGLE_LEAVE:{

				this->process_openmp_single_enter(
					*construct,
					pregions_by_cpu,
					execution_context_stack_by_cpu,
					future_context_stack_by_cpu
				);

				break;
			}

		} // construct type switch

	} // finished iterating over omp constructs

	this->process_openmp_instance_parts(
		mes,
		syscalls_by_cpu,
		csps_in_cs,
		tps_in_t
	);

	// Add runtime instances
	for(auto instance : runtime_instances_by_cpu)
		Fuse::Trace::profile.add_instance(instance);

}

void Fuse::Trace_aftermath_legacy::process_openmp_pregion_enter(
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
		){

	unsigned int execution_context_increment = 0; // i.e. how much to increment on top of whatever is in future_context
	unsigned int future_context_increment = 1;

	// if we are currently directly in a parallel region, then we need to
	// iterate the index by the number of threads that will execute this new pregion
	if(pregions_by_cpu[construct.cpu].back().size() != 0){
		auto current_pregion_context = pregions_by_cpu[construct.cpu].back().back();
		bool directly_in_pregion = std::get<0>(current_pregion_context);

		if(directly_in_pregion){
			auto tid = std::get<1>(current_pregion_context);
			auto num_workers = std::get<2>(current_pregion_context);

			execution_context_increment = tid;
			future_context_increment = num_workers;
		}
	}

	int next_rank = (int) future_context_stack_by_cpu[construct.cpu].back().back() + execution_context_increment;
	execution_context_stack_by_cpu[construct.cpu].back().push_back(next_rank);

	future_context_stack_by_cpu[construct.cpu].back().back() += future_context_increment;

	// Add the source addr of the pregion
	execution_context_stack_by_cpu[construct.cpu].back().push_back((int) construct.ptr.ope->region_src_addr);
	future_context_stack_by_cpu[construct.cpu].back().push_back(0); // this context position won't be used

	// Set up the for_instances that have already appeared within this region, currently empty
	std::set<struct omp_for_instance*> seen_fors;
	seen_instances_within_region_by_cpu[construct.cpu].push_back(seen_fors);

	// The first for_instance or task in the region should have label 0
	future_context_stack_by_cpu[construct.cpu].back().push_back(0);

	// Add the parallel region details to the pregion context
	pregions_by_cpu[construct.cpu].back().push_back(
		std::make_tuple(true,construct.ptr.ope->tid,construct.ptr.ope->num_workers)
	);

}

void Fuse::Trace_aftermath_legacy::process_openmp_pregion_leave(
		unsigned int worker_cpu,
		std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
			pregions_by_cpu,
		std::map<unsigned int,std::vector<std::vector<int> > >&
			execution_context_stack_by_cpu,
		std::map<unsigned int, std::vector<std::vector<int> > >&
			future_context_stack_by_cpu,
		std::map<unsigned int, std::vector<std::set<struct omp_for_instance*> > >&
			seen_instances_within_region_by_cpu
		){

	// Remove the region_src_addr from the execution context, and remove the future context position
	execution_context_stack_by_cpu[worker_cpu].back().pop_back();
	future_context_stack_by_cpu[worker_cpu].back().pop_back();

	// Then remove the region indexes
	execution_context_stack_by_cpu[worker_cpu].back().pop_back();
	future_context_stack_by_cpu[worker_cpu].back().pop_back();

	// Remove the parallel region details from the pregion context
	pregions_by_cpu[worker_cpu].back().pop_back();

	// And stop managing for_instances within the region
	seen_instances_within_region_by_cpu[worker_cpu].pop_back();

}

void Fuse::Trace_aftermath_legacy::process_openmp_chunk_set_enter(
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
		){

	auto cpu = construct.cpu;

	auto seen_iter = seen_instances_within_region_by_cpu[cpu].back().find(construct.ptr.cs->for_instance);
	if(seen_iter == seen_instances_within_region_by_cpu[cpu].back().end()){

		// This is the first of this loop that I've seen within this region
		seen_instances_within_region_by_cpu[cpu].back().insert(construct.ptr.cs->for_instance);

		execution_context_stack_by_cpu[cpu].back().push_back((int) future_context_stack_by_cpu[cpu].back().back());
		future_context_stack_by_cpu[cpu].back().back() += 1;
	}	else {

		// Revert to the previous index if we have already seen this instance
		// for example with dynamic scheduling we encounter and leave the same instance multiple times
		// (and the instance label should stay the same)
		execution_context_stack_by_cpu[cpu].back().push_back((int) future_context_stack_by_cpu[cpu].back().back() - 1);
	}

	// push the address of the omp for
	execution_context_stack_by_cpu[cpu].back().push_back((int) construct.ptr.cs->for_instance->for_loop->addr);
	future_context_stack_by_cpu[cpu].back().push_back(0); // this context position won't be used

	execution_context_stack_by_cpu[cpu].back().push_back((int) construct.ptr.cs->iter_start); // push start_iter onto context
	future_context_stack_by_cpu[cpu].back().push_back(0); // push 0 for any inner constructs

	// The two newly created constructs are not directly parallel regions
	pregions_by_cpu[cpu].back().push_back(std::make_tuple(false,0,0));
	pregions_by_cpu[cpu].back().push_back(std::make_tuple(false,0,0));

	std::stringstream ss;
	ss << construct.ptr.cs->for_instance->for_loop->addr;
	std::string symbol = ss.str(); // The symbol for the moment is just a string of the pointer value for the loop

	Fuse::Instance_p chunk_instance(new Fuse::Instance());
	chunk_instance->label = execution_context_stack_by_cpu[cpu].back();
	chunk_instance->cpu = cpu;
	chunk_instance->symbol = symbol;
	chunk_instance->start = 0; // proper duration will be determined as aggregation of the chunk's parts
	chunk_instance->is_gpu_eligible = 0;

	std::vector<struct omp_for_chunk_set_part*> csps;

	csps_in_cs[construct.ptr.cs] = std::make_pair(chunk_instance,csps);

}

void Fuse::Trace_aftermath_legacy::process_openmp_chunk_set_leave(
		unsigned int worker_cpu,
		std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
			pregions_by_cpu,
		std::map<unsigned int,std::vector<std::vector<int> > >&
			execution_context_stack_by_cpu,
		std::map<unsigned int, std::vector<std::vector<int> > >&
			future_context_stack_by_cpu
		){

	execution_context_stack_by_cpu[worker_cpu].back().pop_back();
	future_context_stack_by_cpu[worker_cpu].back().pop_back();

	// remove the address
	execution_context_stack_by_cpu[worker_cpu].back().pop_back();
	future_context_stack_by_cpu[worker_cpu].back().pop_back();

	execution_context_stack_by_cpu[worker_cpu].back().pop_back();

	// Go back to the previous parallel region state
	pregions_by_cpu[worker_cpu].back().pop_back();
	pregions_by_cpu[worker_cpu].back().pop_back();

}

void Fuse::Trace_aftermath_legacy::process_openmp_chunk_set_part_enter(
		struct Fuse::Aftermath_omp_construct&
			construct,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
			executing_it_sets_by_cpu,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
			executing_tasks_by_cpu,
		std::map<struct omp_for_chunk_set*,
			std::pair<Fuse::Instance_p,std::vector<struct omp_for_chunk_set_part*> >, csp_compare>&
			csps_in_cs
		){

	// Add a new part to the chunk set
	csps_in_cs[construct.ptr.csp->chunk_set].second.push_back(construct.ptr.csp);

	auto chunk_iter = executing_it_sets_by_cpu.find(construct.cpu);
	if(chunk_iter != executing_it_sets_by_cpu.end())
		throw std::runtime_error(
			"Assertion failed: Trying to start a new iteration set part when there is already one executing on the CPU.");

	auto task_iter = executing_tasks_by_cpu.find(construct.cpu);
	if(task_iter != executing_tasks_by_cpu.end())
		throw std::runtime_error(
			"Assertion failed: Trying to start a new iteration set part when there is already a task part executing on the CPU.");

	// the OpenStream parsing code maintains the label as a pair, so we do the same here
	auto instance_for_csp = csps_in_cs[construct.ptr.csp->chunk_set].first;
	auto label_of_instance = instance_for_csp->label;

	executing_it_sets_by_cpu.insert(std::make_pair(construct.cpu, std::make_pair(instance_for_csp,label_of_instance)));

}

void Fuse::Trace_aftermath_legacy::process_previous_time_as_runtime_execution(
		struct multi_event_set* mes,
		unsigned int worker_cpu,
		Fuse::Instance_p runtime_instance,
		uint64_t runtime_start,
		uint64_t runtime_end
		){

	// if we haven't started the workload yet, we don't trace any runtime periods
	if(runtime_start != 0){
		// We are assuming that we have entered a chunk set part from a state of non-work
		// so trace the previous period of non-work on this cpu

		// The counter values between the two times should also be traced
		struct event_set* es = multi_event_set_find_cpu(mes, worker_cpu);
		int hint = 0; // always binary search for OpenMP
		this->interpolate_and_append_counter_values(
			runtime_instance,
			runtime_start,
			runtime_end,
			es,
			hint
		);
	}

}


void Fuse::Trace_aftermath_legacy::process_openmp_chunk_set_part_leave(
		struct Fuse::Aftermath_omp_construct&
			construct,
		std::vector<uint64_t>&
			runtime_starts_by_cpu,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
			executing_it_sets_by_cpu
		){

	auto current_is = executing_it_sets_by_cpu.find(construct.cpu);
	if(current_is == executing_it_sets_by_cpu.end())
		throw std::runtime_error("Assertion failed: There was no iteration set executing on this CPU when its end was traced.");
	else
		executing_it_sets_by_cpu.erase(current_is);

}

void Fuse::Trace_aftermath_legacy::process_openmp_task_creation(
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
		){

	// Take whatever is in the current execution context when this task creation occurs,
	// push the latest future label to it, then iterate the latest future label

	// Do not modify the current execution context
	auto created_tasks_label = execution_context_stack_by_cpu[construct.cpu].back();

	std::tuple<bool,unsigned int,unsigned int> current_pregion_context = pregions_by_cpu[construct.cpu].back().back();
	bool directly_in_pregion = std::get<0>(current_pregion_context);

	if(directly_in_pregion){
		// The task is created directly in a parallel region that is not in a loop, therefore each thread spawns the same task
		// Therefore there is no ordering of the tasks, so use the worker's TID

		unsigned int tid = std::get<1>(current_pregion_context);
		unsigned int num_workers = std::get<2>(current_pregion_context);

		created_tasks_label.push_back(future_context_stack_by_cpu[construct.cpu].back().back() + tid);
		future_context_stack_by_cpu[construct.cpu].back().back() += (num_workers);

	} else {
		created_tasks_label.push_back(future_context_stack_by_cpu[construct.cpu].back().back());
		future_context_stack_by_cpu[construct.cpu].back().back()++;
	}

	// push the address of the entered task as a label rank
	created_tasks_label.push_back(construct.ptr.ti->task->addr);

	// Also save the future_context!
	std::vector<int> created_tasks_future_context = future_context_stack_by_cpu[construct.cpu].back();
	created_tasks_future_context.push_back(0); // for the index
	created_tasks_future_context.push_back(0); // for the addr

	std::stringstream ss;
	ss << construct.ptr.ti->task->addr;
	std::string symbol = ss.str(); // The symbol is just a string of the address of the task construct

	Fuse::Instance_p task_instance(new Fuse::Instance());
	task_instance->label = created_tasks_label;
	task_instance->cpu = construct.cpu; // this is **creation** CPU, not necessarily execution CPU
	task_instance->symbol = symbol;
	task_instance->start = 0; // proper duration will be determined as aggregation of the task's parts
	task_instance->is_gpu_eligible = 0;

	current_tasks[construct.ptr.ti] = std::make_pair(created_tasks_future_context,task_instance);

	std::vector<struct omp_task_part*> tps;
	tps_in_t[construct.ptr.ti] = std::make_pair(task_instance,tps);

	// increment the number of task creations that occured during the current instance
	// (these are proper task creations, not serialised task creations)
	auto chunk_iter = executing_it_sets_by_cpu.find(construct.cpu);
	auto task_iter = executing_tasks_by_cpu.find(construct.cpu);
	auto runtime_instance_iter = runtime_instances_by_cpu.begin() + construct.cpu;

	// add it to the correct instance (only add to runtime instance if we have started workload)
	if(chunk_iter == executing_it_sets_by_cpu.end() &&
			task_iter == executing_tasks_by_cpu.end()){

		if(runtime_starts_by_cpu.at(construct.cpu) > 0)
			(*runtime_instance_iter)->append_event_value("task_creations",1,true);

	} else if (chunk_iter != executing_it_sets_by_cpu.end()) {
		chunk_iter->second.first->append_event_value("task_creations",1,true);
	} else {
		task_iter->second.first->append_event_value("task_creations",1,true);
	}

}

void Fuse::Trace_aftermath_legacy::process_openmp_task_part_enter(
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
		){

	auto executing_task = current_tasks[construct.ptr.tp->task_instance].second;

	// Swap to this task's execution context by pushing it to the context stack
	execution_context_stack_by_cpu[construct.cpu].push_back(executing_task->label);
	future_context_stack_by_cpu[construct.cpu].push_back(current_tasks[construct.ptr.tp->task_instance].first);

	// Add a new stack to the pregions, starting with not-in-pregion
	std::vector<std::tuple<bool, unsigned int, unsigned int> > pregions;
	pregions.push_back(std::make_tuple(false,0,0));
	pregions_by_cpu[construct.cpu].push_back(pregions);

	// Trace this task part
	tps_in_t[construct.ptr.tp->task_instance].second.push_back(construct.ptr.tp);

	auto chunk_iter = executing_it_sets_by_cpu.find(construct.cpu);
	if(chunk_iter != executing_it_sets_by_cpu.end())
		throw std::runtime_error(
			"Assertion failed: Trying to start a new task part when there is already an iteration set executing on the CPU.");

	auto task_iter = executing_tasks_by_cpu.find(construct.cpu);
	if(task_iter != executing_tasks_by_cpu.end())
		throw std::runtime_error(
			"Assertion failed: Trying to start a new task part when there is already a task part executing on the CPU.");

	// the OpenStream parsing code maintains the label as a pair, so we do the same here
	auto instance_for_tp = tps_in_t[construct.ptr.tp->task_instance].first;
	auto label_of_instance = instance_for_tp->label;

	executing_tasks_by_cpu.insert(std::make_pair(construct.cpu, std::make_pair(instance_for_tp,label_of_instance)));

}

void Fuse::Trace_aftermath_legacy::process_openmp_task_part_leave(
		struct Fuse::Aftermath_omp_construct&
			construct,
		std::vector<uint64_t>&
			runtime_starts_by_cpu,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >&
			executing_tasks_by_cpu
		){

	auto current_task = executing_tasks_by_cpu.find(construct.cpu);
	if(current_task == executing_tasks_by_cpu.end())
		throw std::runtime_error("Assertion failed: There was no task executing on this CPU when its end was traced.");
	else
		executing_tasks_by_cpu.erase(current_task);

}

void Fuse::Trace_aftermath_legacy::process_openmp_task_end(
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
		){

	execution_context_stack_by_cpu[construct.cpu].pop_back();
	future_context_stack_by_cpu[construct.cpu].pop_back();
	pregions_by_cpu[construct.cpu].pop_back();
	current_tasks.erase(construct.ptr.ti);

}

void Fuse::Trace_aftermath_legacy::process_openmp_single_enter(
		struct Fuse::Aftermath_omp_construct&
			construct,
		std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
			pregions_by_cpu,
		std::map<unsigned int,std::vector<std::vector<int> > >&
			execution_context_stack_by_cpu,
		std::map<unsigned int, std::vector<std::vector<int> > >&
			future_context_stack_by_cpu
		){

	// Assume that all workers in a team see/trace the encountered single construct (even if worker does not execute)

	if(construct.ptr.os->executed == 0){
		// if this worker executes the single region, add to the execution stack

		int rank_to_push = (int) future_context_stack_by_cpu[construct.cpu].back().back();
		execution_context_stack_by_cpu[construct.cpu].back().push_back(rank_to_push);
		future_context_stack_by_cpu[construct.cpu].back().back()++;

		// push 0 ready for any constructs inside the single construct
		future_context_stack_by_cpu[construct.cpu].back().push_back(0);

	} else {
		// otherwise, just update my future rank so that we don't repeat ranks
		future_context_stack_by_cpu[construct.cpu].back().back()++;
	}

	pregions_by_cpu[construct.cpu].back().push_back(std::make_tuple(false,0,0));

}

void Fuse::Trace_aftermath_legacy::process_openmp_single_leave(
		unsigned int worker_cpu,
		std::map<unsigned int,std::vector<std::vector<std::tuple<bool,unsigned int,unsigned int> > > >&
			pregions_by_cpu,
		std::map<unsigned int,std::vector<std::vector<int> > >&
			execution_context_stack_by_cpu,
		std::map<unsigned int, std::vector<std::vector<int> > >&
			future_context_stack_by_cpu
		){

	// If the worker recorded this event then it executed the single region
	// so pop the execution context indexes for it
	execution_context_stack_by_cpu[worker_cpu].back().pop_back();
	future_context_stack_by_cpu[worker_cpu].back().pop_back();
	pregions_by_cpu[worker_cpu].back().pop_back();

}

void Fuse::Trace_aftermath_legacy::process_openmp_instance_parts(
		struct multi_event_set* mes,
		std::vector<std::vector<Fuse::Aftermath_omp_construct> > syscalls_by_cpu,
		std::map<struct omp_for_chunk_set*,
			std::pair<Fuse::Instance_p,std::vector<struct omp_for_chunk_set_part*> >, csp_compare>&
			csps_in_cs,
		std::map<struct omp_task_instance*,
			std::pair<Fuse::Instance_p,std::vector<struct omp_task_part*> >, Fuse::tp_compare>&
			tps_in_t
		){

	// TODO not using hints currently because one instance may have later parts than the next instance's first part?
	std::vector<int> ces_hints_per_cpu;
	std::vector<unsigned int> syscall_hints_per_cpu;
	ces_hints_per_cpu.assign(mes->max_cpu+1,0);
	syscall_hints_per_cpu.assign(mes->max_cpu+1,0);

	/* Iteration sets */
	for(auto instance_iter : csps_in_cs){

		auto instance = instance_iter.second.first;
		
		//auto iteration_space_size = instance_iter.first->iter_end - instance_iter.first->iter_start;
		auto iteration_space_size = instance_iter.first->for_instance->iter_end - instance_iter.first->for_instance->iter_start;
		iteration_space_size += 1; // inclusive of lower bound

		// Record size of the iteration space
		Fuse::Trace::profile.add_event("iteration_space_size");
		instance->append_event_value("iteration_space_size",iteration_space_size,true);

		// Parts should be chronologically ordered within each instance
		int syscall_hint = -1;
		for(auto part_iter : instance_iter.second.second){

			auto executed_on_cpu = part_iter->cpu;
			struct event_set* es = multi_event_set_find_cpu(mes, executed_on_cpu);

			int hint = 0;
			this->interpolate_and_append_counter_values(
				instance,
				part_iter->start,
				part_iter->end,
				es,
				hint
			);

			Fuse::Trace::profile.add_event("serialized_subtasks");
			instance->append_event_value("serialized_subtasks",part_iter->serialized_tcreates,true);

			this->process_openmp_syscalls(
				instance,
				syscalls_by_cpu.at(executed_on_cpu),
				part_iter->start,
				part_iter->end,
				syscall_hint
			);

		}

		// Add the instance to this execution profile
		Fuse::Trace::profile.add_instance(instance);

	}

	/* Tasks */
	for(auto instance_iter : tps_in_t){

		auto instance = instance_iter.second.first;

		// Parts should be chronologically ordered within each instance
		int syscall_hint = -1;
		for(auto part_iter : instance_iter.second.second){

			auto executed_on_cpu = part_iter->cpu;
			struct event_set* es = multi_event_set_find_cpu(mes, executed_on_cpu);

			int hint = 0;
			this->interpolate_and_append_counter_values(
				instance,
				part_iter->start,
				part_iter->end,
				es,
				hint
			);

			Fuse::Trace::profile.add_event("serialized_subtasks");
			instance->append_event_value("serialized_subtasks",part_iter->serialized_tcreates,true);

			this->process_openmp_syscalls(
				instance,
				syscalls_by_cpu.at(executed_on_cpu),
				part_iter->start,
				part_iter->end,
				syscall_hint
			);

		}

		// Add the instance to this execution profile
		Fuse::Trace::profile.add_instance(instance);

	}

}

void Fuse::Trace_aftermath_legacy::process_openmp_syscalls(
		Fuse::Instance_p instance,
		std::vector<Fuse::Aftermath_omp_construct> syscalls,
		uint64_t start_time,
		uint64_t end_time,
		int& hint
		){

	if(syscalls.size() == 0)
		return;

	if(hint == -1){
		// binary search to find first syscall that might occur in the part
		int start_idx = 0;
		int end_idx = syscalls.size();
		int center_idx = 0;

		while(end_idx - start_idx >= 0) {
			center_idx = (start_idx + end_idx) / 2;

			if(syscalls.at(center_idx).time > start_time)
				end_idx = center_idx-1;
			else if(syscalls.at(center_idx).time < start_time)
				start_idx = center_idx+1;
			else
				break;
		}

		// iterate backwards until center_idx is the first after the part's start time
		while(center_idx-1 >= 0 && syscalls.at(center_idx-1).time > start_time)
			center_idx--;

		hint = center_idx;

	} else {

		// iterate forward until we are at the first syscall after the start
		while(hint < syscalls.size() && syscalls.at(hint).time < start_time)
			hint++;

		// there may be no syscalls after the start
		if(hint >= syscalls.size())
			return;

	}

	// Then move forward one syscall at a time until the end of the part
	while(syscalls.at(hint).time >= start_time && syscalls.at(hint).time < end_time){

		// add this syscall
		auto syscall = syscalls.at(hint);
		std::stringstream ss;
		ss << "syscall_" << syscall.ptr.se->sub_type_id;
		instance->append_event_value(ss.str(),1,true);

		hint++;
	}

}


bool Fuse::csp_compare::operator()(struct omp_for_chunk_set* a, struct omp_for_chunk_set* b) {
	if(a->min_start == b->min_start)
		return a < b;
	else
		return a->min_start < b->min_start;
};
bool Fuse::tp_compare::operator()(struct omp_task_instance* a, struct omp_task_instance* b) {
	if(a->min_start == b->min_start)
		return a < b;
	else
		return a->min_start < b->min_start;
};

// define an explicit implementation of the templated update_data_accesses function for our comparator
template void Fuse::Trace_aftermath_legacy::update_data_accesses<data_access_time_compare>(
	struct multi_event_set* mes,
	struct single_event* se,
	boost::icl::interval_map<
		uint64_t,
		std::set<std::pair<unsigned int, Fuse::Instance_p>, data_access_time_compare>
		>& data_accesses,
	std::vector<struct comm_event*>& all_comm_events,
	std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
	unsigned int& next_comm_event_idx,
	unsigned int total_num_comm_events,
	bool load_communication_matrix);

template void Fuse::Trace_aftermath_legacy::load_openstream_instance_dependencies<data_access_time_compare>(
	std::vector<struct comm_event*> all_comm_events,
	boost::icl::interval_map<
		uint64_t,
		std::set<std::pair<unsigned int, Fuse::Instance_p>, data_access_time_compare>
		>& data_accesses
	);
