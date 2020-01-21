#include "trace.h"
#include "profile.h"
#include "instance.h"
#include "util.h"

#include "trace_aftermath_legacy.h"

#include "spdlog/spdlog.h"
#include "boost/icl/interval_map.hpp"

#include <fstream>
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
	std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > > executing_instances_by_cpu;

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
		this->allocate_cycles_in_state(mes,
			se,
			next_state_event_idx_by_cpu,
			runtime_instances_by_cpu,
			executing_instances_by_cpu,
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
		struct multi_event_set* mes,
		struct single_event* se,
		std::vector<unsigned int>& next_state_event_idx_by_cpu,
		std::vector<Fuse::Instance_p>& runtime_instances_by_cpu,
		std::map<int, std::pair<Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
		std::vector<uint64_t>& partially_traced_state_time_by_cpu,
		std::vector<uint64_t>& runtime_starts_by_cpu
		){

	spdlog::trace("Allocating OpenStream state cycles prior to single event at timestamp {}",se->time);

	unsigned int single_event_cpu = se->event_set->cpu;
	unsigned int next_state_event_idx = next_state_event_idx_by_cpu.at(single_event_cpu);

	bool handling_states = true;
	while(handling_states){
		if(next_state_event_idx < se->event_set->num_state_events &&
			se->event_set->state_events[next_state_event_idx].start < se->time
			){
			// We have a state to allocate

			struct state_event* state_event = &se->event_set->state_events[next_state_event_idx];
			char* state_name = multi_event_set_find_state_description(mes, state_event->state_id)->name;

			std::stringstream ss;
			ss << "cycles_" << state_name;

			std::string event_name = ss.str();
			event_name = Fuse::Util::lowercase(event_name);
			Fuse::Trace::profile.add_event(event_name);

			// So first find what instance I should allocate the state cycles to

			Fuse::Instance_p responsible_instance;
			auto executing_iter = executing_instances_by_cpu.find(single_event_cpu);
			bool should_add = true;
			if(executing_iter == executing_instances_by_cpu.end()){
				// There are no executing instances on this CPU, so allocate to the runtime

				responsible_instance = runtime_instances_by_cpu.at(single_event_cpu);

				// if we haven't yet started the instances, don't trace the runtime state
				if(runtime_starts_by_cpu.at(single_event_cpu) == 0)
					should_add = false;

			} else {
				responsible_instance = executing_iter->second.first;
			}

			// Now find the amount of cycles of this state to allocate

			if(se->time >= state_event->end){
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
				int64_t additional_partial_time_in_state = se->time - state_event->start - partially_traced_state_time;

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

	spdlog::trace("Finished allocating OpenStream state cycles prior to single event at timestamp {}",se->time);

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
	this->interpolate_and_append_counter_values(my_instance,my_instance->start,my_instance->end,se->event_set,ces_hints_per_cpu.at(se->event_set->cpu));

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
		spdlog::warn("Found {} errors when interpolating counter events for an OpenStream instance.", num_errors);

	// Append instance duration as an event
	int64_t duration = end_time - start_time;
	instance->append_event_value("duration",duration,true);

}

void Fuse::Trace_aftermath_legacy::parse_openmp_instances(struct multi_event_set* mes){

	// TODO

}

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