#include "profile.h"
#include "instance.h"
#include "util.h"

#ifdef AFTERMATH_LEGACY
	#include "trace_aftermath_legacy.h"
#else
	#include "trace_aftermath.h"
#endif

#include "spdlog/spdlog.h"
#include "boost/icl/interval_map.hpp"

#include <fstream>
#include <queue>
#include <set>
#include <sstream>

Fuse::Execution_profile::Execution_profile(
		std::string tracefile,
		std::string benchmark,
		Fuse::Event_set filtered_events):
			tracefile(tracefile),
			benchmark(benchmark),
			filtered_events(filtered_events)
		{

}

Fuse::Execution_profile::~Execution_profile(){}

void Fuse::Execution_profile::load_from_tracefile(
		Fuse::Runtime runtime,
		bool load_communication_matrix
		){

	// load the instances from the tracefile into the map, using the aftermath calls
	spdlog::info("Loading {} tracefile {}.", Fuse::convert_runtime_to_string(runtime), this->tracefile);

	bool exists = Fuse::Util::check_file_existance(this->tracefile);
	if(exists == false)
		throw std::runtime_error(fmt::format("The tracefile to be loaded '{}' does not exist.", this->tracefile));

#if defined AFTERMATH_LEGACY && AFTERMATH_LEGACY == 1
	std::unique_ptr<Fuse::Trace> trace_impl(new Trace_aftermath_legacy(*this));
#else
	#error New Aftermath traces not yet implemented
#endif

	trace_impl->parse_trace(runtime, load_communication_matrix);

}

std::string Fuse::Execution_profile::get_tracefile_name(){
	return this->tracefile;
}

Fuse::Event_set Fuse::Execution_profile::get_unique_events(){
	return this->events;
}

std::vector<Fuse::Symbol> Fuse::Execution_profile::get_unique_symbols(bool include_runtime){

	std::vector<Fuse::Symbol> unique_symbols;
	for(auto symbol_pair : this->instances){

		if(include_runtime == false && symbol_pair.first == "runtime")
			continue;

		if(std::find(unique_symbols.begin(), unique_symbols.end(), symbol_pair.first) == unique_symbols.end())
			unique_symbols.push_back(symbol_pair.first);

	}

	return unique_symbols;
}

void Fuse::Execution_profile::add_event(Fuse::Event event){

	if(std::find(this->events.begin(),this->events.end(),event) == events.end())
		this->events.push_back(event);

}

// If symbols is empty (or not provided), then this will return all instances for all symbols
std::vector<Fuse::Instance_p> Fuse::Execution_profile::get_instances(
		bool include_runtime,
		const std::vector<Fuse::Symbol> symbols
		){

	std::vector<Fuse::Instance_p> all_instances;

	for(auto symbol_pair : this->instances){

		if(include_runtime == false && symbol_pair.first == "runtime")
			continue;

		if(symbols.size() > 0)
			if(std::find(symbols.begin(), symbols.end(), symbol_pair.first) == symbols.end())
				continue;

		all_instances.reserve(all_instances.size() + std::distance(symbol_pair.second.begin(),symbol_pair.second.end()));
		all_instances.insert(all_instances.end(), symbol_pair.second.begin(), symbol_pair.second.end());
	}

	return all_instances;

}

void Fuse::Execution_profile::print_to_file(std::string output_file){

	spdlog::info("Dumping the execution profile {} to output file {}.", this->tracefile, output_file);

	Event_set events = this->get_unique_events();

	// Create the header
	std::stringstream header_ss;
	header_ss << "cpu,symbol,label"; // these are always included, regardless of filtering

	bool filtered = false;
	if(this->filtered_events.size() > 0){
		events = this->filtered_events;
		filtered = true;
	} else {
		header_ss << ",gpu_eligible";
	}

	// Finish header according to (filtered or not) events
	for(auto event : events)
		header_ss << "," << event;

	spdlog::debug("The execution profile contains {}events {}.", (filtered ? "filtered " : ""), Fuse::Util::vector_to_string(events));

	std::ofstream out(output_file);

	if(out.is_open()){

		out << header_ss.str() << "\n";

		std::vector<Fuse::Instance_p> all_instances = this->get_instances(true);
		std::sort(all_instances.begin(), all_instances.end(), comp_instances_by_label_dfs);

		for(auto instance : all_instances){

			std::stringstream ss;

			ss << instance->cpu << "," << instance->symbol << "," << Fuse::Util::vector_to_string(instance->label, true, "-");

			if(filtered == false)
				ss << "," << instance->is_gpu_eligible;

			for(auto event : events){

				bool error = false;
				int64_t value = 0;

				if(event == "gpu_eligible")
					value = instance->is_gpu_eligible;
				else
					value = instance->get_event_value(event,error);

				if(error == false) {
					ss << "," << value;
				} else {
					ss << ",unknown";
					//ss << ",0";
				}

			}

			out << ss.str() << "\n";

		}

		out.close();

	} else {
		spdlog::error("Could not open {} to dump instances to file.", output_file);
	}

}

/* Avoiding stringstream for efficiency
*  TODO: allow for sparse adjacency matrix
*/
void Fuse::Execution_profile::dump_instance_dependencies(std::string output_file){

	spdlog::info("Dumping the data-dependency DAG as a dense adjacency matrix to {}", output_file);

	std::vector<Fuse::Instance_p> all_instances = this->get_instances(false);
	std::sort(all_instances.begin(), all_instances.end(), comp_instances_by_label_dfs);

	spdlog::debug("Dumping the instance dependencies for {} instances, of which {} have dependencies.", all_instances.size(), this->instance_dependencies.size());

	std::ofstream dense_adj(output_file);

	// Prior to the adjacency matrix in the file, the number of instances and each label is provided
	// Trying to make this more efficient by avoiding (direct or indirect) streams
	char numstr[16];
	sprintf(numstr, "%lu", all_instances.size());

	std::string filestring;
	filestring.reserve(all_instances.size()*2);

	filestring += numstr;
	filestring += "\n";

	for(auto instance : all_instances){
		filestring += Fuse::Util::vector_to_string(instance->label,true,"-");
		filestring += "\n";
	}

	dense_adj << filestring;

	// Now print the adjacency matrix
	for(decltype(all_instances.size()) consumer_idx = 0; consumer_idx < all_instances.size(); consumer_idx++){

		filestring = "";

		// For each consumer, find the indexes of its producers in the all_instances list...
		auto consumer_instance = all_instances.at(consumer_idx);
		auto depend_iter = this->instance_dependencies.find(consumer_instance);

		if(depend_iter == this->instance_dependencies.end()){
			// There are no dependencies for this instance

			for(decltype(all_instances.size()) potential_producer_idx = 0; potential_producer_idx < all_instances.size(); potential_producer_idx++){
				filestring += "0,";
			}
			filestring.pop_back(); // get rid of the trailing delimiter
			filestring += "\n";

			dense_adj << filestring;
			continue;

		}

		std::set<std::size_t> ordered_producer_indexes_for_this_consumer;
		for(auto producer_instance : depend_iter->second.first){
			auto producer_ordered_index = std::find(all_instances.begin(), all_instances.end(), producer_instance) - all_instances.begin();
			ordered_producer_indexes_for_this_consumer.insert(producer_ordered_index);
		}

		// Go through all the instances that the consumer might depend on
		for(decltype(all_instances.size()) potential_producer_idx = 0; potential_producer_idx < all_instances.size(); potential_producer_idx++){

			// I am iterating the potential_producers in order, so check if this one is the next in my ordered producer list

			if(ordered_producer_indexes_for_this_consumer.size() > 0
					&& potential_producer_idx == *ordered_producer_indexes_for_this_consumer.begin()){

				filestring += "1,";

				// pop the front of the set so the next producer is always the front
				ordered_producer_indexes_for_this_consumer.erase(ordered_producer_indexes_for_this_consumer.begin());

			} else {
				filestring += "0,";
			}

		}
		filestring.pop_back(); // get rid of the trailing delimiter
		filestring += "\n";

		dense_adj << filestring;
	}

	dense_adj.close();

}

void Fuse::Execution_profile::dump_instance_dependencies_dot(std::string output_file){

	spdlog::info("Dumping the instance-creation and data-dependency DAGs as .dot visualisation to {}", output_file);

	std::ofstream graph(output_file);

	std::string filestring = "digraph D {\n";
	graph << filestring;

	std::vector<Fuse::Instance_p> all_instances = this->get_instances(false);
	std::sort(all_instances.begin(), all_instances.end(), comp_instances_by_label_dfs);

	// Each label is associated with its ordered index in all_instances
	// This is necessary to later search for a parent instance directly from the child instance's truncated label
	std::map<std::string, int> node_label_to_node_index;

	filestring = "";

	// First, declare all the instances as nodes
	for(decltype(all_instances.size()) instance_idx = 0; instance_idx < all_instances.size(); instance_idx++){

		auto instance = all_instances.at(instance_idx);
		if(instance->symbol == "runtime")
			continue;

		auto label_string = Fuse::Util::vector_to_string(instance->label,true,"-");

		node_label_to_node_index.insert(std::make_pair(label_string,instance_idx));

		std::string name = "node_" + std::to_string(instance_idx);
		filestring += (name + " [label=\"" + std::to_string(instance_idx) + "\n" + label_string + "\n" + instance->symbol + "\"];\n");
	}

	graph << filestring;
	filestring = "";

	// Next, define all the instance-creation edges
	for(decltype(all_instances.size()) instance_idx = 0; instance_idx < all_instances.size(); instance_idx++){

		auto instance = all_instances.at(instance_idx);
		if(instance->symbol == "runtime")
			continue;

		// Find the instance with my parent's label
		// Then draw an edge between my parent instance and me

		auto my_label_string = Fuse::Util::vector_to_string(instance->label,true,"-");

		auto parent_label = instance->label; // copy constructor
		parent_label.pop_back(); // remove my child rank

		auto parent_label_string = Fuse::Util::vector_to_string(parent_label,true,"-");

		auto parent_node_iter = node_label_to_node_index.find(parent_label_string);
		if (parent_node_iter == node_label_to_node_index.end())
			continue; // The instance is a top-level instance (with no parent)

		std::string child_name = "node_" + std::to_string(instance_idx);
		std::string parent_name = "node_" + std::to_string(parent_node_iter->second);

		filestring += (parent_name + " -> " + child_name + "\n");

	}

	graph << filestring;

	// Next, define the data-dependencies
	for(decltype(all_instances.size()) consumer_idx = 0; consumer_idx < all_instances.size(); consumer_idx++){

		filestring = "";

		// For each consumer, find the indexes of its producers in the all_instances list...
		auto consumer_instance = all_instances.at(consumer_idx);
		if(consumer_instance->symbol == "runtime")
			continue;

		auto depend_iter = this->instance_dependencies.find(consumer_instance);
		if(depend_iter == this->instance_dependencies.end())
			continue; // This instance does not depend on any other instance

		std::string consumer_name = "node_" + std::to_string(consumer_idx);

		for(auto producer_instance : depend_iter->second.first){

			// Get ordered index for this producer instance, in order to determine the node name
			auto producer_ordered_index = std::find(all_instances.begin(), all_instances.end(), producer_instance) - all_instances.begin();

			std::string producer_name = "node_" + std::to_string(producer_ordered_index);

			filestring += (producer_name + " -> " + consumer_name + " [style=dotted, constraint=false];\n");

		}

		graph << filestring;

	}

	graph << "}\n";
	graph.close();

}

void Fuse::Execution_profile::add_instance(Fuse::Instance_p instance){

	auto symbol_iter = this->instances.find(instance->symbol);

	if(symbol_iter == this->instances.end()){

		std::vector<Fuse::Instance_p> symbol_instances = {instance};
		this->instances.insert(std::make_pair(instance->symbol,symbol_instances));

	} else {

		symbol_iter->second.push_back(instance);

	}

}

std::map<std::string, std::vector<std::vector<int64_t> > > Fuse::Execution_profile::get_value_distribution(
		Fuse::Event_set events,
		bool include_runtime,
		const std::vector<Fuse::Symbol> symbols
		){

	std::map<std::string, std::vector<std::vector<int64_t> > > distribution_per_symbol;

	auto requested_symbols = symbols;

	if(symbols.size() == 0)
		requested_symbols = this->get_unique_symbols(include_runtime);

	for(auto symbol : requested_symbols){

		if(include_runtime == false && symbol == "runtime")
			throw std::logic_error("Requested runtime instances, but include_runtime was false.");

		std::vector<Fuse::Symbol> constrained_symbols = {symbol};

		auto instances = this->get_instances(include_runtime, constrained_symbols);

		std::vector<std::vector<int64_t> > values;
		values.reserve(instances.size());
		for(auto instance : instances){

			bool error = false;

			std::vector<int64_t> instance_values;
			instance_values.reserve(events.size());
			for(auto event : events)
				instance_values.push_back(instance->get_event_value(event, error));

			if(error)
				throw std::runtime_error(
					fmt::format("Requested distribution for events {}, but instance {} in {} does not have values for them all. {}",
						Fuse::Util::vector_to_string(events),
						Fuse::Util::vector_to_string(instance->label),
						this->tracefile,
						fmt::format("The instance only contains values for events: {}", Fuse::Util::vector_to_string(instance->get_events()))
						)
				);

			values.push_back(instance_values);

		}

		distribution_per_symbol.insert(std::make_pair(symbol, values));

	}

	return distribution_per_symbol;

}

