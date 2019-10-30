#include "profile.h"
#include "instance.h"

#include "easylogging++.h"
#include "boost/icl/interval_map.hpp"

#include <queue>
#include <set>
#include <sstream>

extern "C" {
	// linux-kernel/list uses 'new' as a variable name, so must be redefined to avoid conflict with c++ keyword
	#define new extern_new
	#include "aftermath/core/multi_event_set.h"
	#include "aftermath/core/debug.h"
	#undef new
}

::Fuse::Execution_profile::Execution_profile(std::string tracefile, std::string benchmark)
	: tracefile(tracefile)
	, benchmark(benchmark){

}
::Fuse::Execution_profile::~Execution_profile(){
}

void ::Fuse::Execution_profile::load_from_tracefile(bool load_communication_matrix){

	// load the instances from the tracefile into the map, using the aftermath calls
	LOG(DEBUG) << "Loading tracefile: " << this->tracefile;
	
  struct multi_event_set* mes = new multi_event_set;
  multi_event_set_init(mes);

	off_t bytes_read = 0;
	
	if(read_trace_sample_file(mes, this->tracefile.c_str(), &bytes_read) != 0) {
			LOG(ERROR) << "There was an error reading the tracefile '" << this->tracefile << "' after " << bytes_read << " bytes.";
			return;
	}

	if(debug_read_task_symbols(this->benchmark.c_str(),mes) != 0){
			LOG(ERROR) << "There was an error reading symbols from the binary '" << this->benchmark << "'.";
	}
	
	this->parse_instances_from_mes(mes, load_communication_matrix);

	// We no longer need the MES, so free the memory
	multi_event_set_destroy(mes);
	delete mes;

}

void ::Fuse::Execution_profile::parse_instances_from_mes(struct multi_event_set* mes, bool load_communication_matrix){
	this->parse_openstream_instances(mes, load_communication_matrix);
	this->parse_openmp_instances(mes);
}

struct data_access_time_compare {
	bool operator()(
			const std::pair<unsigned int, ::Fuse::Instance_p>& lhs,
			const std::pair<unsigned int, ::Fuse::Instance_p>& rhs)
			const {
		return lhs.second->start < rhs.second->start;
	}
};

void ::Fuse::Execution_profile::parse_openstream_instances(struct multi_event_set* mes, bool load_communication_matrix){

	LOG(DEBUG) << "Parsing OpenStream instances.";

	unsigned int total_num_single_events = 0;
	unsigned int total_num_comm_events = 0;
	for(struct event_set* es = &mes->sets[0]; es < &mes->sets[mes->num_sets]; es++){
		total_num_single_events += es->num_single_events;
		total_num_comm_events += es->num_comm_events;
	}

	LOG(DEBUG) << "There are " << total_num_single_events << " OpenStream single events.";
	LOG(DEBUG) << "There are " << total_num_comm_events << " OpenStream communication events.";

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
	std::map<uint64_t, std::queue<::Fuse::Instance_p> > ready_instances_by_frame;

	// Each executing instance is paired with its label
	std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > > executing_instances_by_cpu; 

	// a particular interval is read or written by a particular instance
	::boost::icl::interval_map<
			uint64_t,
			std::set<std::pair<unsigned int, ::Fuse::Instance_p>, data_access_time_compare>
			> data_accesses;

	// top level instances are handled differently because there is no bounding START and END for those TCREATES
	struct frame* top_level_frame = nullptr;
	for(auto se : all_single_events){
		if(se->type == SINGLE_TYPE_TCREATE){
			top_level_frame = se->active_frame;
		}
	}

	// As we iterate through the trace, keep a running counter event index for efficient searching
	std::vector<int> ces_hints_per_cpu(mes->max_cpu, 0);

	// Data structures for the 'runtime' instances, to track the behaviour of the 'non-work' execution
	std::vector<::Fuse::Instance_p> runtime_instances_by_cpu;
	std::vector<uint64_t> runtime_starts_by_cpu;
	std::vector<uint64_t> partially_traced_state_time_by_cpu;
	std::vector<unsigned int> next_state_event_idx_by_cpu;
	for(int cpu_idx = mes->min_cpu; cpu_idx <= mes->max_cpu; cpu_idx++){
		
		Instance_p runtime_instance(new Instance());
		std::vector<int> label = {(-cpu_idx - 1)};
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

	for(int i=0; i<total_num_single_events; i++){

		struct single_event* se = all_single_events[i];

		// update the states on this cpu
		this->allocate_cycles_in_state(mes,
			se,
			next_state_event_idx_by_cpu,
			runtime_instances_by_cpu,
			executing_instances_by_cpu,
			partially_traced_state_time_by_cpu,
			runtime_starts_by_cpu);

		this->update_data_accesses(mes,
			se,
			data_accesses,
			all_comm_events,
			executing_instances_by_cpu,
			next_comm_event_idx,
			total_num_comm_events);

		this->process_next_openstream_single_event(se,
			top_level_frame,
			ready_instances_by_frame,
			executing_instances_by_cpu,
			runtime_instances_by_cpu,
			runtime_starts_by_cpu,
			ces_hints_per_cpu,
			top_level_instance_counter);
			


	}

	LOG(DEBUG) << "Finished iterating single events!";
	return;

}

template <typename T>
bool sort_struct_by_time(T one, T two){
    return one->time < two->time;
}

void ::Fuse::Execution_profile::gather_sorted_openstream_parsing_events(
		struct multi_event_set* mes,
		std::vector<struct single_event*>& all_single_events,
		std::vector<struct comm_event*>& all_comm_events
		){
	
	for(struct event_set* es = &mes->sets[0]; es < &mes->sets[mes->num_sets]; es++){
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

void ::Fuse::Execution_profile::allocate_cycles_in_state(
		struct multi_event_set* mes,
		struct single_event* se,
		std::vector<unsigned int>& next_state_event_idx_by_cpu,
		std::vector<::Fuse::Instance_p>& runtime_instances_by_cpu,
		std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
		std::vector<uint64_t>& partially_traced_state_time_by_cpu,
		std::vector<uint64_t>& runtime_starts_by_cpu
	){

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

			// So first find what instance I should allocate the state cycles to

			Instance_p responsible_instance;
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
					responsible_instance->append_event_value(ss.str(),additional_time_in_state,true);

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
					responsible_instance->append_event_value(ss.str(),additional_partial_time_in_state,true);

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

}

template <typename Compare>
void ::Fuse::Execution_profile::update_data_accesses(
		struct multi_event_set* mes,
		struct single_event* se,
		::boost::icl::interval_map<
			uint64_t,
			std::set<std::pair<unsigned int, ::Fuse::Instance_p>, Compare>
			>& data_accesses,
		std::vector<struct comm_event*>& all_comm_events,
		std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
		unsigned int& next_comm_event_idx,
		unsigned int total_num_comm_events){

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
						LOG(FATAL) << "There is no executing instance for the read communication event!";
					
					Instance_p responsible_instance = executing_iter->second.first;

					// add the access to the interval map to later determine dependencies
					std::set<std::pair<unsigned int, Instance_p>, data_access_time_compare> access;
					access.insert(std::make_pair((unsigned int)ce->type,responsible_instance));

					data_accesses += std::make_pair(::boost::icl::interval<uint64_t>::right_open((uint64_t)ce->what->addr,((uint64_t)ce->what->addr)+(ce->size)), access);

					// Add the communication data to the instance
					std::stringstream ss;
					ss << "data_read_" << ce->numa_dist << "_hops";
					responsible_instance->append_event_value(ss.str(),ce->size,true);

					break;
				}
				case COMM_TYPE_DATA_WRITE: {
					
					auto executing_iter = executing_instances_by_cpu.find(ce->src_cpu);
					if(executing_iter == executing_instances_by_cpu.end())
						LOG(FATAL) << "There is no executing instance for a write communication event!";
					
					Instance_p responsible_instance = executing_iter->second.first;

					// add the access to the interval map to later determine dependencies
					std::set<std::pair<unsigned int, Instance_p>, data_access_time_compare> access;
					access.insert(std::make_pair((unsigned int)ce->type,responsible_instance));

					data_accesses += std::make_pair(::boost::icl::interval<uint64_t>::right_open((uint64_t)ce->what->addr,((uint64_t)ce->what->addr)+(ce->size)), access);
					
					std::stringstream ss;
					ss << "data_write_" << ce->numa_dist << "_hops";
					responsible_instance->append_event_value(ss.str(),ce->size,true);
					
					break;
				}

			}
			
			// continue on to handle the next communication event
			next_comm_event_idx++;

		} else {
			
			// any remaining communication events are after the current single event,
			// so we want to process the current single event first
			handling_comm = false;
		}

	}

}
			
void ::Fuse::Execution_profile::process_next_openstream_single_event(
		struct single_event* se,
		struct frame* top_level_frame,
		std::map<uint64_t, std::queue<::Fuse::Instance_p> >& ready_instances_by_frame,
		std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
		std::vector<::Fuse::Instance_p>& runtime_instances_by_cpu,
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

			Instance_p instance(new Instance());

			// Set the appropriate label for this newly created instance

			if(se->active_frame == top_level_frame){
				//std::vector<int> label = {top_level_instance_counter++};
				instance->label = {top_level_instance_counter++};

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

				std::queue<Instance_p> instances_waiting_for_execution({instance});
				ready_instances_by_frame.insert(std::make_pair(se->what->addr, instances_waiting_for_execution));

			} else {

				// Add this instance as an one that is waiting to begin execution
				frame_iter->second.push(instance);
			}

			break;
		}
		case SINGLE_TYPE_TEXEC_START: {

			// We assume that there was a period of 'non-work' (runtime system execution) immediately preceeding this instance start
			// Therefore, add counter values for the prior runtime instance
			if(runtime_starts_by_cpu.at(single_event_cpu) != 0){
				Instance_p runtime_instance = runtime_instances_by_cpu.at(single_event_cpu);
				uint64_t start_time = runtime_starts_by_cpu.at(single_event_cpu);
				uint64_t end_time = se->time;

				// the start time is the end time of the previous one, so we can just use the same hint, and update it if necessary
				this->interpolate_and_append_counter_values(runtime_instance,
					start_time,
					end_time,
					se->event_set,
					ces_hints_per_cpu.at(single_event_cpu));
			}

			// find the first task in the queue of waiting tasks in the map by active_frame ? or via *what*?
			// this is my task
			//frame_iter = ready_tasks_by_frame.find(se->active_frame->addr);
			frame_iter = ready_tasks_by_frame.find(se->what->addr);
			if(frame_iter == ready_tasks_by_frame.end())
				LOG(ERROR) << "Parsing tracefile error: Cannot find a waiting task for my TEXEC_START. Has the OpenStream TSC offset fix been implemented?";

			// Remove from queue
			std::queue<Execution_unit_p> tasks_waiting_for_execution = frame_iter->second;
			Execution_unit_p my_task = tasks_waiting_for_execution.front();
			tasks_waiting_for_execution.pop();
			frame_iter->second = tasks_waiting_for_execution;

			// Give it its execution details
			if(se->active_task->symbol_name == nullptr){
				std::string symbol("unknown_symbol_name");
				my_task->symbol = symbol; 
			} else {
				std::string symbol(se->active_task->symbol_name);
				std::replace(symbol.begin(), symbol.end(), ',', '_');
				my_task->symbol = symbol; 
			}

			my_task->cpu = se->event_set->cpu;
			my_task->start = se->time;

			my_task->is_gpu_eligible = se->what->is_gpu_eligible;

			// This task is now the only one executing on this CPU, therefore set the next child label for this CPU to be this task's label + 1 depth 
			
			int* my_task_label = my_task->task_label.get();
			int my_task_depth = my_task->task_tree_depth;
			int* label_for_potential_child = new int[my_task_depth+2]; // One for the next depth in the tree, and one for the terminating character
		   
			std::copy(my_task_label, my_task_label+my_task_depth, label_for_potential_child);
			label_for_potential_child[my_task_depth] = 0;
			label_for_potential_child[my_task_depth+1] = -1;

			std::pair<Execution_unit_p,int*> executing_task = std::make_pair(my_task,label_for_potential_child);
			executing_tasks_by_cpu.insert(std::make_pair(se->event_set->cpu,executing_task));

			// Now that the task has been added as executing, update all of the currently executing tasks to have realised parallelism
			unsigned int num_executing_tasks = executing_tasks_by_cpu.size();
			for(executing_iter = executing_tasks_by_cpu.begin(); executing_iter != executing_tasks_by_cpu.end(); executing_iter++){
				Execution_unit_p task = executing_iter->second.first;
				task->append_event_value_nodecrease("realised_parallelism",num_executing_tasks);
			}

			//LOG(DEBUG) << se->time << ":" << se->event_set->cpu << ":TSTART " <<  se->what->addr;


			break;
		}
		case SINGLE_TYPE_TEXEC_END: {
			
			break;
		}
		case SINGLE_TYPE_SYSCALL: {

			break;
		}
	}
	

}

void ::Fuse::Execution_profile::parse_openmp_instances(struct multi_event_set* mes){

}

// define an explicit implementation of the templated update_data_accesses function for our comparator
template void ::Fuse::Execution_profile::update_data_accesses<data_access_time_compare>(
		struct multi_event_set* mes,
		struct single_event* se,
		::boost::icl::interval_map<
			uint64_t,
			std::set<std::pair<unsigned int, ::Fuse::Instance_p>, data_access_time_compare>
			>& data_accesses,
		std::vector<struct comm_event*>& all_comm_events,
		std::map<int, std::pair<::Fuse::Instance_p, std::vector<int> > >& executing_instances_by_cpu,
		unsigned int& next_comm_event_idx,
		unsigned int total_num_comm_events);

