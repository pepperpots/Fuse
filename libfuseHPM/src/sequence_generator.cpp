#include "sequence_generator.h"
#include "analysis.h"
#include "combination.h"
#include "config.h"
#include "target.h"
#include "profile.h"
#include "profiling.h"
#include "fuse_types.h"
#include "statistics.h"
#include "util.h"

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using Node_p = Fuse::Sequence_generator::Node_p; // To reduce boilerplate

bool compare_by_mi(const std::pair<double, Fuse::Event>&i, const std::pair<double, Fuse::Event>&j){
    return i.first < j.first;
}

bool compare_nodes(const std::pair<double, Node_p>& i, const std::pair<double, Node_p>& j){
		// sort by number of combined events first, so we try to generate complete profiles as fast as possible
		/*
		if(i.second->sorted_combined_events.size() != j.second->sorted_combined_events.size())
			return i.second->sorted_combined_events.size() > j.second->sorted_combined_events.size(); 
		*/
    return i.first < j.first;
}

std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > Fuse::Sequence_generator::generate_bc_sequence(
		Fuse::Target& target,
		unsigned int max_linking_events,
		unsigned int num_repeat_combinations
		){

	spdlog::info("Running BC sequence generator.");

	// Get events
	auto events = target.get_target_events();
	auto reference_pairs = target.get_reference_pairs();

	// Get from disk or calculate and save to disk the pairwise mutual information for the target events
	std::map<unsigned int, double> pairwise_mi_values = target.get_or_load_pairwise_mis(reference_pairs);

	// Get the AMI for the possible combinations of max_linking_events events to each event
	std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > > best_possible_amis_per_event
		 = Fuse::Sequence_generator::compute_best_possible_amis(target, reference_pairs, pairwise_mi_values, max_linking_events);

	// Load the records of any previous profiled executions
	auto profile_mappings_filename = target.get_sequence_generation_profile_mappings_filename();
	std::vector<std::pair<Event_set, std::string> > profiled_event_sets
		= Fuse::Sequence_generator::load_previous_profiled_event_sets(profile_mappings_filename);

	// Load the records of any previous profile combinations
	auto combination_mappings_filename = target.get_sequence_generation_combination_mappings_filename();
	std::vector<std::pair<std::string, std::string> > previous_combinations
		= Fuse::Sequence_generator::load_previous_combinations(combination_mappings_filename);

	// Get the root node of the branch and bound algorithm
	auto root_node = Fuse::Sequence_generator::get_tree_root(target, events, reference_pairs, pairwise_mi_values);
	
	unsigned int num_pmc = root_node->sorted_combined_events.size();

	root_node->find_or_execute_profiles(target, num_repeat_combinations, profiled_event_sets);

	// This loads the profiles of the root node (i.e. simply the tracefiles)
	root_node->load_node_profiles(target, num_repeat_combinations);

	/* Compute the accuracy stats of the root node */

	// The analyse function receives the set of already combined events so it knows which TMDs to compute
	Fuse::Event_set all_combined_events = root_node->sorted_combined_events; 

	/*
	*  The accuracy function produces tasks, but for this initial node we have not yet defined a parallel team
	*  So make one just for this function
	*/
	#pragma omp parallel
	#pragma omp single
	root_node->analyse_accuracy_and_compute_metrics(
		target,
		all_combined_events,
		reference_pairs
	);

	/* Build the priority list */

	// these must be mutually exclusive with the current priority list's nodes
	std::vector<std::pair<Fuse::Event_set, double> > previously_evaluated_nodes;

	std::vector<std::pair<double, Node_p> > nodes_by_epd;
	std::vector<std::pair<double, Node_p> > nodes_by_cross_profile_epd;
	std::vector<std::pair<double, Node_p> > nodes_by_tmd_mse;
	std::vector<std::pair<double, Node_p> > nodes_by_cross_profile_tmd_mse;

	std::pair<double, Node_p> node_by_epd = std::make_pair(root_node->epd, root_node);
	std::pair<double, Node_p> node_by_cross_epd = std::make_pair(root_node->cross_profile_epd, root_node);
	std::pair<double, Node_p> node_by_tmd_mse = std::make_pair(root_node->tmd_mse, root_node);
	std::pair<double, Node_p> node_by_cross_tmd_mse = std::make_pair(root_node->cross_profile_tmd_mse, root_node);

	nodes_by_epd.push_back(node_by_epd);
	nodes_by_cross_profile_epd.push_back(node_by_cross_epd);
	nodes_by_tmd_mse.push_back(node_by_tmd_mse);
	nodes_by_cross_profile_tmd_mse.push_back(node_by_cross_tmd_mse);

	// target_priority_list is the one that we use (change this if needed)
	std::vector<std::pair<double, Node_p> >* target_priority_list = &nodes_by_cross_profile_tmd_mse;
	std::string priority_type = "cross-profile TMD MSE";

	Node_p best_node;
	double best_value = 999.0;

	// Now process the priority list until empty
	while(target_priority_list->size() > 0){
		
		unsigned int initial_num_executed_profiles = profiled_event_sets.size();
		unsigned int initial_num_combinations = previous_combinations.size();

		spdlog::info("Getting new leaf to compute from list of {} leaves.", target_priority_list->size());

		std::vector<std::string> priorities;
		for(auto leaf_info : *target_priority_list){
			std::stringstream ss;
			ss << "[" << leaf_info.second->sorted_combined_events.size() << "," << leaf_info.first << "]";
			priorities.push_back(ss.str());
		}
		spdlog::debug("Leaves in order have num_combined_events and {}: {}.", priority_type, Fuse::Util::vector_to_string(priorities));

		// Get the lowest value (i.e. most accurate)
		Node_p node_to_compute = target_priority_list->front().second;
		target_priority_list->erase(target_priority_list->begin());
		
		// The selected node is about to have its child nodes created and evaluated (which will be added to the priority list)
		// Therefore it is no more - so add it to the previously evaluated nodes
		previously_evaluated_nodes.push_back(std::make_pair(node_to_compute->sorted_combined_events, node_to_compute->tmd_mse));

		std::vector<Node_p> completed_nodes = Fuse::Sequence_generator::compute_leaf_node(
			target,
			node_to_compute,
			events,
			num_pmc,
			max_linking_events,
			num_repeat_combinations,
			reference_pairs,
			pairwise_mi_values,
			best_possible_amis_per_event,
			nodes_by_epd,
			nodes_by_cross_profile_epd,
			nodes_by_tmd_mse,
			nodes_by_cross_profile_tmd_mse,
			profiled_event_sets,
			previous_combinations);

		spdlog::info("Finished computing a node. There are currently {} leaves in the tree to compute.", target_priority_list->size());

		if(completed_nodes.size() > 0){
			for(auto node : completed_nodes){
				if(node->tmd_mse < best_value){
					best_node = node;
					best_value = node->tmd_mse;
				}
			}
		}

		// Here, we should remove any nodes for which we have *already* found a better alternative, and thus shouldn't proceed with
		Fuse::Sequence_generator::prune_priority_list(target_priority_list, previously_evaluated_nodes);

		// Save after each node is computed, if we have executed some profiles
		if(profiled_event_sets.size() > initial_num_executed_profiles)
			Fuse::Sequence_generator::save_previously_profiled_event_sets(profile_mappings_filename, profiled_event_sets);

		if(previous_combinations.size() > initial_num_combinations)
			Fuse::Sequence_generator::save_previous_combinations(combination_mappings_filename, previous_combinations);
	 
	}
	
	spdlog::info("Finished BC sequence generator.");

	spdlog::info("The best BC sequence found had EPD {}, Cross EPD {}, TMD MSE {}, Cross TMD MSE {}, and was {}.",
		best_node->epd,
		best_node->cross_profile_epd,
		best_node->tmd_mse,
		best_node->cross_profile_tmd_mse,
		best_node->get_combination_spec_as_string());

	return best_node->combination_spec;

}

std::vector<std::pair<Fuse::Event_set, std::string> >
		Fuse::Sequence_generator::load_previous_profiled_event_sets(std::string filename){

	spdlog::trace("Sequence generator loading profiled event set mappings JSON file: {}.", filename);

	std::vector<std::pair<Event_set, std::string> > profiled_event_sets;

	// In filename, we have the events -> tracefile mappings, as JSON
	nlohmann::json j;
	std::ifstream ifs(filename);

	if(!ifs.is_open()){
		spdlog::warn("Could not find JSON file {}, so loading no previous tracefile mappings.", filename);
		return profiled_event_sets;
	}

	try {

		ifs >> j;
		
		nlohmann::json profile_objects = j["profiled_event_sets"];
		for(nlohmann::json::iterator it = profile_objects.begin(); it < profile_objects.end(); it++){
				for(nlohmann::json::iterator entry_it = it->begin(); entry_it != it->end(); entry_it++){
					// entry_it points to a "string,of,comma,separated,events":"tracefile_name"
						
					std::string event_list_str = entry_it.key();
					auto ordered_events = Fuse::Util::split_string_to_vector(event_list_str,',');

					spdlog::trace("Found previously executed profile for {} events: {}.",
						ordered_events.size(),
						event_list_str);

					std::string tracefile = entry_it.value();
					
					profiled_event_sets.push_back(std::make_pair(ordered_events, tracefile));
				}
		}

	} catch (const std::domain_error err){
		throw std::domain_error(
			fmt::format("The sequence generator's profiles file {} was incorrectly formatted (domain error). Exception was: {}.",
				err.what())
			);
	} catch (const std::invalid_argument err){
		throw std::invalid_argument(
			fmt::format("The sequence generator's profiles file {} was incorrectly formatted (invalid arg). Exception was: {}.",
				err.what())
			);
	}

	return profiled_event_sets;

}

std::vector<std::pair<std::string, std::string> >
		Fuse::Sequence_generator::load_previous_combinations(std::string filename){

	spdlog::trace("Sequence generator saving combinations mappings JSON file: {}.", filename);

	std::vector<std::pair<std::string, std::string> > combinations;

	// In filename, we have the events -> tracefile mappings, as JSON
	nlohmann::json j;
	std::ifstream ifs(filename);

	if(!ifs.is_open()){
		spdlog::warn("Could not find JSON file {}, so loading no previous combinations.", filename);
		return combinations;
	}

	try {

		ifs >> j;
		
		nlohmann::json profile_objects = j["combination_spec"];
		for(nlohmann::json::iterator it = profile_objects.begin(); it < profile_objects.end(); it++){
				for(nlohmann::json::iterator entry_it = it->begin(); entry_it != it->end(); entry_it++){
					// entry_it points to a "combination_spec_as_str_via_helper_function":"filename"
						
					std::string spec_str = entry_it.key();
					std::string saved_combined_filename = entry_it.value();
					
					combinations.push_back(std::make_pair(spec_str, saved_combined_filename));
				}
		}

	} catch (const std::domain_error err){
		throw std::domain_error(
			fmt::format("The sequence generator's combinations file {} was incorrectly formatted (domain error). Exception was: {}.",
				err.what())
			);
	} catch (const std::invalid_argument err){
		throw std::invalid_argument(
			fmt::format("The sequence generator's combinations file {} was incorrectly formatted (invalid arg). Exception was: {}.",
				err.what())
			);
	}

	return combinations;

}

std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > >
		Fuse::Sequence_generator::compute_best_possible_amis(
			Fuse::Target& target,
			std::vector<Fuse::Event_set> reference_pairs,
			std::map<unsigned int, double> pairwise_mi_values,
			unsigned int max_linking_events
		){
	
	auto events = target.get_target_events();
	std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > > best_possible_amis_per_event;

	for(unsigned int event_idx = 0; event_idx < events.size(); event_idx++){
		// for this event, find the best possible AMI for each linking set size

		Fuse::Event a = events.at(event_idx);
		std::vector<std::pair<double, Fuse::Event_set> > best_linking_events;

		for(int num_linking = 1; num_linking <= max_linking_events; num_linking++){

			Fuse::Event_set top_events; // best set of events of size (num_linking)
			std::vector<double> mi_list; // the mis between the current event and each of the events in the best set

			top_events.reserve(max_linking_events+1);
			mi_list.reserve(max_linking_events+1);

			// iterate over the other possible events
			for(unsigned int other_event_idx = 0; other_event_idx < events.size(); other_event_idx++){

				if(other_event_idx == event_idx)
					continue;

				Fuse::Event b = events.at(other_event_idx);
				Fuse::Event_set pair = {a,b};
	
				// Equivalencies should be disregarded
				// TODO have these equivalencies properly defined somewhere
				if((a == "PAPI_L2_DCA" && b == "PAPI_L1_DCM") or (a == "PAPI_L1_DCM" && b == "PAPI_L2_DCA"))
					continue;
				
				double mi = 0.0;

				// What is the mi between a and b?
				auto event_pair_idx = target.get_reference_pair_index_for_event_pair(pair);
				auto saved_mi_iter = pairwise_mi_values.find(event_pair_idx);
				if(saved_mi_iter != pairwise_mi_values.end())
					mi = saved_mi_iter->second;
				else
					spdlog::error("Cannot find MI for event pair [{},{}].", a, b);
				
				// insert the mi into the ordered list and then prune it to be of length max_linking
				auto it = std::lower_bound(mi_list.begin(), mi_list.end(), mi, std::greater<double>());
				int index = std::distance(mi_list.begin(), it);

				if(index < max_linking_events){
					mi_list.insert(it, mi);
					top_events.insert(top_events.begin()+index, b);
				}

				if(mi_list.size() > max_linking_events){
					mi_list.pop_back();
					top_events.pop_back();
				}

			}

			// save these events with the AMI
			double ami = Fuse::calculate_weighted_geometric_mean(mi_list); // because they are normalised

			best_linking_events.push_back(std::make_pair(ami, top_events));
		}

		// insert into saved map
		best_possible_amis_per_event.insert(std::make_pair(a,best_linking_events));

	}

	return best_possible_amis_per_event;

}

Node_p Fuse::Sequence_generator::get_tree_root(
		Fuse::Target& target,
		Fuse::Event_set target_events,
		std::vector<Fuse::Event_set> event_pairs,
		std::map<unsigned int, double> pairwise_mi_values
		){
	
	std::vector<Node_p> potential_nodes;
	
	spdlog::trace("Sequence generator finding root node.");

	Fuse::Event_set initial_set;
	auto papi_directory = target.get_papi_directory();

	double minimum_mi = 10.0;
	for(auto pair_value : pairwise_mi_values){

		double mi = pair_value.second;
		if(mi < minimum_mi){
			unsigned int reference_index = pair_value.first;
			Fuse::Event_set pair = event_pairs.at(reference_index);

			if(std::find(target_events.begin(), target_events.end(), pair.at(0)) == target_events.end() 
				|| std::find(target_events.begin(), target_events.end(), pair.at(1)) == target_events.end())
			{
				continue;
			}

			initial_set.clear();
			initial_set.push_back(pair.at(0));
			initial_set.push_back(pair.at(1));
			minimum_mi = mi;
		}

	}

	// Now add the events with minimal AMIs to the initial set

	while(true){

		bool added_an_event = false;

		double minimum_ami = 10.0;
		Fuse::Event next_event;
		
		for(auto potential_event : target_events){

			if(std::find(initial_set.begin(),initial_set.end(),potential_event) != initial_set.end()){
				// we already have this event!
				continue;
			}

			double summed_mi = 0.0;
			for(auto current_event : initial_set){

				Fuse::Event_set pair = {current_event, potential_event};
				unsigned int ref_idx = std::distance(event_pairs.begin(),std::find(event_pairs.begin(),event_pairs.end(),pair));
				if(ref_idx == event_pairs.size()){
					// then the pair is in the 'wrong' order to find it
					Fuse::Event_set event_pair = {pair.at(1),pair.at(0)};
					ref_idx = std::distance(event_pairs.begin(),std::find(event_pairs.begin(),event_pairs.end(),event_pair));
					if(ref_idx == event_pairs.size()){
						throw std::runtime_error(fmt::format(
							"Could not find corresponding reference index for the pair {}.", Fuse::Util::vector_to_string(pair)));
					}
				}
				auto it = pairwise_mi_values.find(ref_idx);
				if(it == pairwise_mi_values.end()){
					throw std::runtime_error(fmt::format(
						"Could not find corresponding MI value for the pair {}.", Fuse::Util::vector_to_string(pair)));
				}

				summed_mi += it->second;

			}
			double ami = summed_mi / initial_set.size();

			if(ami < minimum_ami){

				Fuse::Event_set profile_events = initial_set;
				profile_events.push_back(potential_event);
				if(Fuse::Profiling::compatibility_check(profile_events, papi_directory) == false){
					spdlog::trace("Incompatibility for root node events {}.", Fuse::Util::vector_to_string(profile_events));
					continue;
				}

				added_an_event = true;
				minimum_ami = ami;
				next_event = potential_event;

			}

		}

		if(added_an_event == false){
			break;
		}

		initial_set.push_back(next_event);

	}

	spdlog::info("Sequence generator initial event set (root node) is {}.", Fuse::Util::vector_to_string(initial_set));

	std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > combination_spec;
	Fuse::Event_set linking_set;
	combination_spec.push_back(std::make_pair(linking_set,initial_set));

	std::map<unsigned int, double> tmds;	
	
	Node_p root = std::make_shared<Fuse::Sequence_generator::Node>(initial_set, combination_spec, tmds);

	return root;

}

std::vector<Node_p> Fuse::Sequence_generator::compute_leaf_node(
		Fuse::Target& target,
		Node_p node,
		Fuse::Event_set target_events,
		unsigned int num_pmc,
		unsigned int max_linking_events,
		unsigned int num_repeat_combinations,
		std::vector<Fuse::Event_set> reference_pairs,
		std::map<unsigned int, double> pairwise_mi_values,
		std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > > best_possible_amis_per_event,
		std::vector<std::pair<double, Node_p> >& nodes_by_epd,
		std::vector<std::pair<double, Node_p> >& nodes_by_cross_profile_epd,
		std::vector<std::pair<double, Node_p> >& nodes_by_tmd_mse,
		std::vector<std::pair<double, Node_p> >& nodes_by_cross_profile_tmd_mse,
		std::vector<std::pair<Fuse::Event_set, std::string> >& profiled_event_sets,
		std::vector<std::pair<std::string, std::string> >& recorded_combinations
		){

	std::vector<Node_p> complete_nodes;

	if(node->sorted_combined_events.size() == target_events.size()){
		// This shouldn't happen, but don't abort
		spdlog::error("Trying to compute a leaf which already has a complete set of hardware events.");
		return complete_nodes;
	}

	/* load the parent node's (potentially combined) profile */

	// checking if loaded because the initial profile isn't unloaded
	if(node->loaded == false){
		node->load_node_profiles(target, num_repeat_combinations);
	}
	
	/* get the additional nodes that we think would be good to evaluate from this node */
	auto child_nodes = Fuse::Sequence_generator::get_child_nodes(
		node,
		target,
		target_events,
		static_cast<int>(num_pmc),
		static_cast<int>(max_linking_events),
		reference_pairs,
		pairwise_mi_values,
		best_possible_amis_per_event);

	/* for each child node, execute the benchmark or link a sufficient previously executed profile, sequentially */

	for(auto child_node : child_nodes){
		// find or execute the tracefile so that the filenames are updated
		child_node->find_or_execute_profiles(target, num_repeat_combinations, profiled_event_sets);
	}

	/* combine and evaluate all the nodes */
	#pragma omp parallel shared(nodes_by_epd, nodes_by_cross_profile_epd, nodes_by_tmd_mse, nodes_by_cross_profile_tmd_mse, recorded_combinations, complete_nodes)
	{
		#pragma omp single
		{
			#pragma omp taskloop 
			for(unsigned int child_idx=0; child_idx < child_nodes.size(); child_idx++){
				Node_p child_node = child_nodes.at(child_idx);

				/* load the corresponding tracefiles */
				spdlog::debug("Loading child node {} of {}.", child_idx+1, child_nodes.size());
			
				#pragma omp critical (loading)
				{
					child_node->load_node_profiles(target, num_repeat_combinations);
				}
			
				/* evaluate the node */

				spdlog::debug("Combination and evaluating child node {} of {}.", child_idx+1, child_nodes.size());
				auto number_of_combinations = child_node->combine_and_evaluate_node_profiles(
					node, // this is the parent to combine with
					target,
					recorded_combinations,
					reference_pairs);

				if(child_node->sorted_combined_events.size() == target_events.size()){

					spdlog::info("Finished computing a complete branch, with combined profiles: {}.",
						Fuse::Util::vector_to_string(child_node->filenames));

					#pragma omp critical (complete_nodes)
					{
						complete_nodes.push_back(child_node);
					}

				} else {

					// insert into priority lists
					std::pair<double, Node_p> node_by_epd = std::make_pair(child_node->epd, child_node);
					std::pair<double, Node_p> node_by_cross_epd = std::make_pair(child_node->cross_profile_epd, child_node);
					std::pair<double, Node_p> node_by_tmd_mse = std::make_pair(child_node->tmd_mse, child_node);
					std::pair<double, Node_p> node_by_cross_tmd_mse = std::make_pair(child_node->cross_profile_tmd_mse, child_node);

					/* add the node (which is a new leaf) to each sorted priority list */
					#pragma omp critical (priority_list)
					{
						auto it = std::lower_bound(nodes_by_epd.begin(), nodes_by_epd.end(), node_by_epd, compare_nodes);
						nodes_by_epd.insert(it, node_by_epd);

						it = std::lower_bound(nodes_by_cross_profile_epd.begin(), nodes_by_cross_profile_epd.end(), node_by_cross_epd, compare_nodes);
						nodes_by_cross_profile_epd.insert(it, node_by_cross_epd);

						it = std::lower_bound(nodes_by_tmd_mse.begin(), nodes_by_tmd_mse.end(), node_by_tmd_mse, compare_nodes);
						nodes_by_tmd_mse.insert(it, node_by_tmd_mse);

						it = std::lower_bound(nodes_by_cross_profile_tmd_mse.begin(), nodes_by_cross_profile_tmd_mse.end(), node_by_cross_tmd_mse, compare_nodes);
						nodes_by_cross_profile_tmd_mse.insert(it, node_by_cross_tmd_mse);
					}

				}
				
			} // child nodes (omp task)
		}
	}

	/* unload the parent node execution profiles */
	node->execution_profiles.clear();
	node->loaded = false;

	return complete_nodes;

}

// TODO: This function is a bit mad and should be simplified!
std::vector<Node_p> Fuse::Sequence_generator::get_child_nodes(
		Node_p parent_node,
		Fuse::Target& target,
		Fuse::Event_set target_events,
		int num_pmc,
		int max_linking_events,
		std::vector<Fuse::Event_set> event_pairs,
		std::map<unsigned int, double> pairwise_mi_values,
		std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > > best_possible_amis_per_event
		){
	
	std::vector<Node_p> potential_nodes;
	auto papi_directory = target.get_papi_directory();
	
	Fuse::Event_set already_selected_events, remaining_events;
	already_selected_events = parent_node->sorted_combined_events;
	std::set_difference(target_events.begin(), target_events.end(), parent_node->sorted_combined_events.begin(), parent_node->sorted_combined_events.end(), std::back_inserter(remaining_events));

	spdlog::debug("Finding child nodes. Currently have {} combined events, and there are {} remaining events.",
		already_selected_events.size(),
		remaining_events.size());

	if(remaining_events.size() == 0){
		spdlog::error("There are no remaining events when trying to find child nodes, this shouldn't happen.");
		return potential_nodes;
	}

	// Ensure we always use all PMCs (unless we can't find any compatible sets)
	// However, we want a minimum threshold on the unique events (i.e. if we have 6 slots free, then 5 good and 1 bad unique event should be replaced by 5 good)
	// However, we can't do this on an absolute value because perhaps that unique event doesn't have *any* good MI)
	// Therefore, check that each event has no significantly better MI: if there is a significantly better linking event, then reduce the number of unique events

	std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > previous_combinations; // local to this function

	unsigned int upper_bound = max_linking_events;
	if(num_pmc-1 < upper_bound)
		upper_bound = num_pmc-1;

	// Get all possible linking sets at size
	// For each linking set
		// Find top unique events at size
	// Check that the top unique events don't include a bad combination
	// If it does, reduce size of unique event set

	//#pragma omp parallel for shared(potential_nodes, previous_combinations)
	for(unsigned int num_linking = 1; num_linking <= upper_bound; num_linking++){

		// Find all possible linking event sets, of size num_linking
		// TODO this can probably be done better by finding the highest pairwise between already-selected and remaining-events, and so on
		std::vector<Fuse::Event_set> potential_linking_sets = Fuse::Util::get_unique_combinations(already_selected_events,num_linking);
		spdlog::trace("Finding nodes corresponding to {} linking events. There are {} to consider.", num_linking, potential_linking_sets.size());

		// Attempt to find a unique set that fills all PMC slots, and also has a good AMI with the linking set
		unsigned int minimum_number_unique = num_pmc-num_linking;
		for (unsigned int num_unique = num_pmc-num_linking; num_unique >= minimum_number_unique; num_unique--){

			// get best unique set (of size num_unique) across for the linking sets (of size num_linking)
			// the best AMI set of size N is the N highest ranked MI events
			
			Fuse::Event_set best_unique_event_set;
			Fuse::Event_set best_linking_event_set;

			double best_ami = -1.0;

			// for each potential linking sets, find the best set of unique events with it, and then check if that is the best we've seen so far
			//#pragma omp parallel shared(best_unique_event_set,best_linking_event_set)
			//#pragma omp single
			//#pragma omp taskloop grainsize(50)
			#pragma omp parallel for shared(best_unique_event_set,best_linking_event_set)
			for(unsigned int linking_idx = 0; linking_idx < potential_linking_sets.size(); linking_idx++){

				auto potential_linking_set = potential_linking_sets.at(linking_idx);

				Fuse::Event_set best_unique_events_with_this_linking_set;
				std::vector<double> best_amis_with_this_linking_set;

				// go through each potential unique event and get its AMI with the potential_linking_set
				for(auto potential_unique_event : remaining_events){

					Fuse::Event_set potential_unique_set = {potential_unique_event};

					double ami = Fuse::Analysis::compute_ami(
						potential_linking_set,
						potential_unique_set,
						event_pairs,
						pairwise_mi_values);

					auto it = std::lower_bound(best_amis_with_this_linking_set.begin(), best_amis_with_this_linking_set.end(), ami, std::greater<double>());
					int index = std::distance(best_amis_with_this_linking_set.begin(), it);
				
					// if the index is in the top 'num_unique', then we should insert it
					if(index < num_unique){
						best_amis_with_this_linking_set.insert(it, ami);
						best_unique_events_with_this_linking_set.insert(best_unique_events_with_this_linking_set.begin()+index, potential_unique_event);
					}

					if(best_amis_with_this_linking_set.size() > num_unique){
						best_amis_with_this_linking_set.pop_back();
						best_unique_events_with_this_linking_set.pop_back();
					}

				}

				// so I have the best events ranked, let's get their final AMI with the linking set (I could just take the geometric mean)
				double overall_ami = Fuse::calculate_weighted_geometric_mean(best_amis_with_this_linking_set);

				// TODO there might be some other combination of num_unique events with a high AMI that are compatible with linking_event_set - but finding them would be highly combinatorial...
				if(overall_ami > best_ami){

					// check if this event_set is compatible!
					Fuse::Event_set profile_events = potential_linking_set;
					profile_events.insert(profile_events.end(), best_unique_events_with_this_linking_set.begin(), best_unique_events_with_this_linking_set.end());
					if(Fuse::Profiling::compatibility_check(profile_events, papi_directory) == false)
						continue;

					// make sure we haven't already selected this event set (this shouldn't really happen right?)
					bool already_have = false;
					std::sort(potential_linking_set.begin(), potential_linking_set.end());
					std::sort(best_unique_events_with_this_linking_set.begin(), best_unique_events_with_this_linking_set.end());
					#pragma omp critical
					{
						for(auto previous_combination : previous_combinations){
							if(potential_linking_set == previous_combination.first && best_unique_events_with_this_linking_set == previous_combination.second){
								already_have = true;
								break;
							}
						}
						if(already_have)
							spdlog::warn("Sequence generator found the same event sets as the best linking/unique sets! This probably shouldn't happen.");

						best_unique_event_set = best_unique_events_with_this_linking_set;
						best_linking_event_set = potential_linking_set;
						best_ami = overall_ami;
					}
				}

			}

			if(best_unique_event_set.size() == 0 && num_unique != 1){
				// We were not able to find a good (highest AMI) compatible event set at size (num_unique), try a smaller size
				minimum_number_unique--;
				continue;
			}

			if(num_unique != 1){
				// Check that the best unique event set is reasonable
				// (i.e. we haven't included a terrible MI event just because we want to fill all the PMCs, where this event could be combined significantly better another way)
				// If we have, reduce the number of unique events and try again

				bool likely_better_combination_exists = false;
				for(auto potential_unique_event : best_unique_event_set){

					// Check the AMI of this unique event to the linking events
					Fuse::Event_set unique_event_as_set = {potential_unique_event};

					double current_ami = Fuse::Analysis::compute_ami(
						best_linking_event_set,
						unique_event_as_set,
						event_pairs,
						pairwise_mi_values);
					
					// Check the best known AMI for this event to linking event sets of this size
					auto it = best_possible_amis_per_event.find(potential_unique_event);

					if(it == best_possible_amis_per_event.end())
						throw std::runtime_error(fmt::format("Unable to find top ranked AMIs for event {}.", potential_unique_event));
					if(it->second.size() < num_linking)
						throw std::runtime_error(fmt::format("Unable to find the best AMI for {} linking events.", num_linking));

					double best_ami = it->second.at(num_linking-1).first;
					
					// If the current proposed combination is *significantly* less than this best set, then we should probably combination it later using that best set
					// Significance is a hyperparameter... 0.3 better?
					double maximum_difference = 0.3;

					if(abs(best_ami - current_ami) >= maximum_difference){
						spdlog::debug("A potential node proposed combining the {} events ({}) via {} with AMI {}. {}.",
							num_unique,
							Fuse::Util::vector_to_string(best_unique_event_set), 
							fmt::format("However, the event {} can be combined with {} with a better AMI of {}.",
								potential_unique_event,
								Fuse::Util::vector_to_string(it->second.at(num_linking-1).second),
								best_ami)
							);

						likely_better_combination_exists = true;
						break; // don't bother checking other events
					}

				}

				if(likely_better_combination_exists){
					spdlog::debug("A better combination does exist. Trying fewer unique events."); 
					minimum_number_unique--;
					continue;
				}

			}
			
			if(best_unique_event_set.size() == 0){
				// if this occurs, we haven't found any compatible unique sets with this linking set,
				// even when looking for only 1 unique event
				// therefore, no node can be created
				break;
			}

			// This unique event set is good, so add it as a node

			spdlog::debug("Creating new child node leaf to include {} via linking set {}.",
				Fuse::Util::vector_to_string(best_unique_event_set),
				Fuse::Util::vector_to_string(best_linking_event_set));

			// Here, we have the best unique_set and linking_set at this particular unique_set size
			Fuse::Event_set updated_combined_events = already_selected_events;
			updated_combined_events.insert(updated_combined_events.end(), best_unique_event_set.begin(), best_unique_event_set.end());
			std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > updated_combination_spec = parent_node->combination_spec;
			updated_combination_spec.push_back(std::make_pair(best_linking_event_set, best_unique_event_set));
			std::map<unsigned int, double> tmds = parent_node->tmds;

			Node_p child_node = std::make_shared<Fuse::Sequence_generator::Node>(
				updated_combined_events,
				updated_combination_spec,
				tmds);

			child_node->cross_profile_reference_indexes = parent_node->cross_profile_reference_indexes;
			child_node->within_profile_reference_indexes = parent_node->within_profile_reference_indexes;

			#pragma omp critical
			{
				previous_combinations.push_back(std::make_pair(best_linking_event_set, best_unique_event_set));
				potential_nodes.push_back(child_node);
			}

		}
	}

	spdlog::debug("Found {} child nodes to combine with the current node and evaluate.", potential_nodes.size());

	return potential_nodes;

}
	
void Fuse::Sequence_generator::prune_priority_list(
		std::vector<std::pair<double, Node_p> >* priority_list,
		std::vector<std::pair<Fuse::Event_set, double> > previously_evaluated_nodes
		){

	/*
	* I can only do the pruning for nodes with exactly the same merged events
	* Because only in this case, it would be always fair to say the other one is better
	*/
	
	spdlog::debug("Pruning the sequence generator tree.");

	unsigned int num_nodes_pruned = 0;
	unsigned int current_node_idx = 0;
	while(true){

		bool removed = false;
		for(; current_node_idx<priority_list->size(); current_node_idx++){
	
			Node_p current_node = priority_list->at(current_node_idx).second;
			Fuse::Event_set current_events = current_node->sorted_combined_events;

			// check if the combination is really terrible
			/*
			if(priority_list->at(current_node_idx).first > 15.0){
				// A little arbitrary, but if the cross TMD MSE is 15.0 then we shouldn't even bother
				priority_list->erase(priority_list->begin()+current_node_idx);
				removed = true;
				break;
			}
			*/

			// check if another node later in the priority list has the same events - if so, remove the less-accurate one
			for(unsigned int later_node_idx=current_node_idx+1; later_node_idx<priority_list->size(); later_node_idx++){

				Node_p later_node = priority_list->at(later_node_idx).second;
				Fuse::Event_set later_node_events = later_node->sorted_combined_events;

				// the events should both be sorted, and therefore should be directly comparable
				if(current_events != later_node_events)
					continue;

				// the two nodes have the same events! which one is better?

				if(current_node->tmd_mse <= later_node->tmd_mse){
					// the current node has equal or better accuracy, so get rid of the later one

					priority_list->erase(priority_list->begin()+later_node_idx);
					removed = true;
					break;

				} else {
					// the later node has better accuracy, so get rid of this one
					
					priority_list->erase(priority_list->begin()+current_node_idx);
					removed = true;
					break;

				}

			}

			if(removed){
				break; // continue pruning from the same node (or if it was removed, the next node) onwards
			}

			// if we haven't removed anything yet, try pruning from this node with the previously completed nodes
			for(auto previous_node : previously_evaluated_nodes){

				if(current_events != previous_node.first)
					continue;
				
				if(current_node->tmd_mse >= previous_node.second){
					// we previously evaluated this exact same set of events, and that combination was better, so there's no point continuing this node

					priority_list->erase(priority_list->begin()+current_node_idx);
					removed = true;
					break;

				}

			}

			if(removed){
				break; // continue pruning from the next node (which now has the index 'current_node_idx') onwards
			}

			// we've never before combined the events of this node better, and we've cancelled any future nodes that are intending to use a provably non-optimal version of these events 
			// only now can we iterate to the next current_node_idx
	
		}

		if(removed == false){
			// we've been through the nodes and not removed any, so we can break out of this pruning algorithm
			break;
		}

		// otherwise, continue pruning
		num_nodes_pruned++;

	}

	spdlog::debug("Pruned {} leaves of the sequence generation tree. There are now {} leaf nodes in the priority list remaining.",
		num_nodes_pruned,
		priority_list->size());

}

void Fuse::Sequence_generator::save_previously_profiled_event_sets(
		std::string mappings_filename,
		std::vector<std::pair<Fuse::Event_set, std::string> > profiled_event_sets
		){

	spdlog::trace("Sequence generator saving profiled event set mappings JSON file: {}.", mappings_filename);

	std::ofstream out(mappings_filename);

	if(!out.is_open())
		throw std::domain_error(
			fmt::format("Sequence generator could not save the profiled event set mappings JSON file: {}.", mappings_filename));

	nlohmann::json j;
	nlohmann::json saved_list;

	for(auto it : profiled_event_sets){

		nlohmann::json obj;

		std::string events_str = Fuse::Util::vector_to_string(it.first, false);
		obj[events_str] = it.second;
		saved_list.push_back(obj);
	}
	
	j["profiled_event_sets"] = saved_list;
	out << std::setw(4) << j;
	
	out.close();
}

void Fuse::Sequence_generator::save_previous_combinations(
		std::string mappings_filename,
		std::vector<std::pair<std::string, std::string> > recorded_combinations
		){

	spdlog::trace("Sequence generator saving combination mappings JSON file: {}.", mappings_filename);

	std::ofstream out(mappings_filename);

	if(!out.is_open())
		throw std::domain_error(
			fmt::format("Sequence generator could not save the combination mappings JSON file: {}.", mappings_filename));

	nlohmann::json j;
	nlohmann::json saved_list;

	for(auto it : recorded_combinations){

		nlohmann::json obj;

		std::string comb_str = it.first;
		obj[comb_str] = it.second;
		saved_list.push_back(obj);
	}
	
	j["combination_spec"] = saved_list;
	out << std::setw(4) << j;
	
	out.close();
}

/* Node class */

Fuse::Sequence_generator::Node::Node(
		Fuse::Event_set all_combined_events,
		std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > combination_spec,
		std::map<unsigned int, double> tmds
		): 
			sorted_combined_events(all_combined_events),
			combination_spec(combination_spec),
			tmds(tmds),
			executed(false),
			loaded(false),
			combined(false),
			evaluated(false),
			epd(0.0),
			tmd_mse(0.0),
			cross_profile_epd(0.0),
			cross_profile_tmd_mse(0.0)
		{

	std::sort(sorted_combined_events.begin(), sorted_combined_events.end());

}

void Fuse::Sequence_generator::Node::find_or_execute_profiles(
		Fuse::Target& target,
		unsigned int num_repeat_combinations,
		std::vector<std::pair<Event_set, std::string> >& profiled_event_sets
		){

	spdlog::trace("Finding or executing the profiles for a node.");

	std::pair<Fuse::Event_set, Fuse::Event_set> spec = this->combination_spec.at(this->combination_spec.size()-1);
	Fuse::Event_set required_events = spec.first;
	required_events.insert(required_events.end(), spec.second.begin(), spec.second.end());

	std::sort(required_events.begin(), required_events.end()); // must be sorted to do std::includes

	for(auto profiled_event_set : profiled_event_sets){

		bool found_previous_profile = std::includes(profiled_event_set.first.begin(),
			profiled_event_set.first.end(),
			required_events.begin(),
			required_events.end());

		if(found_previous_profile){
			this->filenames.push_back(profiled_event_set.second);

			// There may be a large number of profiles that have this subset of events, so just load the first num_repeats 
			if(this->filenames.size() == num_repeat_combinations)
				break;
		}

	}

	while(this->filenames.size() < num_repeat_combinations){
		// I need more profiles, so I must execute the benchmark
		
		std::stringstream ss;
		ss << target.get_sequence_generation_tracefiles_directory() << "/";
		ss << std::chrono::steady_clock::now().time_since_epoch().count() << ".ost";

		this->filenames.push_back(ss.str());

		spdlog::debug("There were insufficient profiles for a node, so executing events {} into tracefile {}.",
			Fuse::Util::vector_to_string(required_events),
			ss.str());

		Fuse::Profiling::execute(
			target.get_target_runtime(),
			target.get_target_binary(),
			target.get_target_args(),
			ss.str(),
			required_events,
			target.get_should_clear_cache());

		// now add this as a previously executed profile
		profiled_event_sets.push_back(std::make_pair(required_events, ss.str()));
		
	}

	this->executed = true;

}

void Fuse::Sequence_generator::Node::load_node_profiles(
		Fuse::Target& target,
		unsigned int num_repeat_combinations
		){

	for(unsigned int i=0; i<num_repeat_combinations; i++){

		if(this->combined){
			// load from a combined profile

			auto profile = target.load_combined_profile_from_disk(this->filenames.at(i));
			this->execution_profiles.push_back(profile);
			this->loaded = true;

		} else {
			// if not combined, then these are execution tracefiles

			auto profile_spec = this->combination_spec.at(this->combination_spec.size()-1);
			Fuse::Event_set filter_to_events = profile_spec.first;
			filter_to_events.insert(filter_to_events.end(), profile_spec.second.begin(), profile_spec.second.end());

			Fuse::Profile_p profile(new Fuse::Execution_profile(
				this->filenames.at(i),
				target.get_target_binary(),
				filter_to_events));

			profile->load_from_tracefile(target.get_target_runtime());

			this->execution_profiles.push_back(profile);
			this->loaded = true;
		}

	}


}

void Fuse::Sequence_generator::Node::analyse_accuracy_and_compute_metrics(
		Fuse::Target& target,
		Fuse::Event_set all_combined_events,
		std::vector<Fuse::Event_set> reference_pairs
		){

	auto spec = this->combination_spec.at(this->combination_spec.size()-1);
	auto linking_events = spec.first;
	auto unique_events = spec.second;
	
	spdlog::trace("Analysing the accuracy of a processed node.");

	/* Find which pair-projections we want to analyse */

	Fuse::Event_set previously_combined_events = all_combined_events; // unique_events are going to be removed from this!

	// I want to calculate only the new TMDs (there's no point calculating pairs that I already had)
	// So that's the pair combinations within the unique set
	// Together with each event of the unique set, matched with each event of the previously merged set
	std::vector<Fuse::Event_set> new_combined_event_pairs;
	if(unique_events.size() > 1)
		new_combined_event_pairs = Fuse::Util::get_unique_combinations(unique_events,2);

	std::sort(previously_combined_events.begin(), previously_combined_events.end());

	// if the event is contained in the unique event set, then remove it from previous events
	previously_combined_events.erase(
		std::remove_if(previously_combined_events.begin(), previously_combined_events.end(),
				[unique_events](const Fuse::Event& previous_event_candidate) {
			return std::find(unique_events.begin(), unique_events.end(), previous_event_candidate) != unique_events.end();
		}),
		previously_combined_events.end());

	for(auto unique_event : unique_events){
		for(auto previous_event : previously_combined_events){
			Fuse::Event_set new_pair = {unique_event, previous_event};
			new_combined_event_pairs.push_back(new_pair);
		}
	}
	
	spdlog::debug("There are {} newly combined event pairs to analyse.", new_combined_event_pairs.size());

	// map a reference index to a vector of tmds, one per merged profile repeat
	// so that we can average them later to produce a single value
	std::map<unsigned int, std::vector<double> > tmd_per_reference_per_repeat;

	// Pre-allocate the vectors so that I can access thread-safely
	// (i.e. no mututation to the map structure, only appending to the vectors)
	for(unsigned int combined_pair_idx = 0; combined_pair_idx < new_combined_event_pairs.size(); combined_pair_idx++){

		auto map_iter = new_combined_event_pairs.begin();
		std::advance(map_iter,combined_pair_idx);

		Fuse::Event_set combined_pair = (*map_iter);
		if(combined_pair.size() != 2)
			throw std::runtime_error(fmt::format("Found a combined pair that does not have two events: {}.",
				Fuse::Util::vector_to_string(combined_pair)));

		// Get the reference pair index of this merged event pair
		auto it = std::find(reference_pairs.begin(),reference_pairs.end(),combined_pair);
		if(it == reference_pairs.end()){
			std::reverse(combined_pair.begin(), combined_pair.end());
			it = std::find(reference_pairs.begin(),reference_pairs.end(),combined_pair);
			if(it == reference_pairs.end())
				throw std::runtime_error("Could not find the event pair in the reference pairs.");
		}

		unsigned int reference_pair_index = std::distance(reference_pairs.begin(),it);
		std::vector<double> empty_tmd_set;
		tmd_per_reference_per_repeat.insert(std::make_pair(reference_pair_index, empty_tmd_set));

	}
	
	std::vector<Fuse::Symbol> symbols = {"all_symbols"};
	if(Fuse::Config::calculate_per_workfunction_tmds){
		auto all_symbols = target.get_statistics()->get_unique_symbols(false);
		symbols.insert(symbols.end(), all_symbols.begin(), all_symbols.end());
	}

	if(this->combined == false && this->combination_spec.size() > 1)
		throw std::runtime_error("Assertion failed: the profiles should be combined before evaluated.");

	for(auto combined_profile : this->execution_profiles){

		/* Analyse the acccuracy of each of these pair-projections, within this profile */

		#pragma omp taskloop shared(tmd_per_reference_per_repeat)
		for(unsigned int combined_pair_idx = 0; combined_pair_idx < new_combined_event_pairs.size(); combined_pair_idx++){

			auto map_iter = new_combined_event_pairs.begin();
			std::advance(map_iter,combined_pair_idx);
	
			Fuse::Event_set event_pair = (*map_iter);
		
			// Get the reference pair index of this merged event pair
			auto it = std::find(reference_pairs.begin(),reference_pairs.end(),event_pair);
			if(it == reference_pairs.end()){
				std::reverse(event_pair.begin(), event_pair.end());
				it = std::find(reference_pairs.begin(),reference_pairs.end(),event_pair);
			}
			
			unsigned int reference_pair_index = std::distance(reference_pairs.begin(),it);
			unsigned int reference_execution_index = target.get_reference_set_index_for_events(event_pair);

			// Now, calculate the TMD for this pair

			std::vector<unsigned int> reference_repeats_list = {0}; // just use the first reference repeat to analyse accuracy
				
			double calibrated_tmd_wrt_pair = Fuse::Analysis::calculate_calibrated_tmd_for_pair(
				target,
				symbols,
				event_pair,
				combined_profile,
				reference_repeats_list,
				Config::tmd_bin_count,
				Config::weighted_tmd);

			// we have pre-allocated the vectors so we are guaranteed to find the reference index
			auto iter = tmd_per_reference_per_repeat.find(reference_pair_index);
			iter->second.push_back(calibrated_tmd_wrt_pair);

		} // for each combined pair (implicit task wait follows)

	} // for each combined profile
	
	/* Now average the TMDs over each repeat combination and save in this node's tmds */

	for(auto results_pair : tmd_per_reference_per_repeat){

		std::vector<double> tmd_values = results_pair.second;
		// take the mean because this an expectation of what we get when repeat the measurement,
		// where the measurement itself is the median across reference instances

		double mean_tmd = std::accumulate(tmd_values.begin(), tmd_values.end(), 0.0) / tmd_values.size(); 
		this->tmds.insert(std::make_pair(results_pair.first, mean_tmd));

	}

	/* Now compute the results of the node */
	
	this->compute_resulting_metrics(reference_pairs);
	this->evaluated = true;
	
	spdlog::trace("Finished analysing the necessary event pairs for a node.");

}

unsigned int Fuse::Sequence_generator::Node::combine_and_evaluate_node_profiles(
		Node_p parent_node,
		Fuse::Target& target,
		std::vector<std::pair<std::string, std::string> >& recorded_combinations,
		std::vector<Fuse::Event_set> reference_pairs
		){

	// Now see if we have already done this combination before:

	auto spec = this->combination_spec.back();
	auto linking_events = spec.first;
	auto unique_events = spec.second;

	std::string combination_string = this->get_combination_spec_as_string();

	if(parent_node->execution_profiles.size() != this->execution_profiles.size())
		throw std::runtime_error("Attempting to combine profiles, but a parent node has a different number of loaded execution profiles than a child node.");
	
	std::vector<Fuse::Profile_p> combined_profiles;

	#pragma omp critical (recorded_combinations)
	{
		for(auto record : recorded_combinations){
			if(record.first == combination_string){
				// We have already done this merge before, so let's just load it

				std::string combined_filename = record.second;
				auto combined_profile = target.load_combined_profile_from_disk(combined_filename);

				combined_profiles.push_back(combined_profile);

				// swap the filename from the execution to the merge, for children of this child branch to use
				this->filenames.at(combined_profiles.size()-1) = combined_filename;

				// stop if we have enough combined profiles
				if(combined_profiles.size() == parent_node->execution_profiles.size())
					break;
			}
		}
	}

	int number_of_required_combinations = parent_node->execution_profiles.size() - combined_profiles.size();

	unsigned int initial_idx = combined_profiles.size();

	// Combine the remaining profiles in parallel

	#pragma omp taskloop shared(combined_profiles)
	for(unsigned int profile_idx=initial_idx; profile_idx<number_of_required_combinations+initial_idx; profile_idx++){

		std::vector<Fuse::Profile_p> profiles_to_combine;
		profiles_to_combine.push_back(parent_node->execution_profiles.at(profile_idx));
		profiles_to_combine.push_back(this->execution_profiles.at(profile_idx));

		std::vector<Fuse::Event_set> linking_events_list = {linking_events};

		std::stringstream ss;
		#pragma omp critical (time)
		{
			ss << "combination_" << std::chrono::steady_clock::now().time_since_epoch().count() << ".csv";
		}
		std::string combined_filename = ss.str();
		this->filenames.at(profile_idx) = combined_filename;
		
		auto strategy = Fuse::Strategy::BC;
		Fuse::Profile_p combined_profile = Fuse::Combination::combine_profiles_via_strategy(
			profiles_to_combine,
			strategy,
			combined_filename,
			target.get_target_binary(),
			linking_events_list,
			target.get_statistics());

		// Now save to disk (can be done in parallel)
		combined_profile->print_to_file(combined_filename);

		spdlog::debug("Combined profile index {} into {}.", profile_idx, combined_filename);

		#pragma omp critical (recorded_combinations)
		{
			recorded_combinations.push_back(std::make_pair(combination_string, combined_filename));
			combined_profiles.push_back(combined_profile);
		}

	}

	this->execution_profiles = combined_profiles;
	this->combined = true;
	
	// Now evaluate each profile

	this->analyse_accuracy_and_compute_metrics(
		target,
		this->sorted_combined_events,
		reference_pairs
	);

	return number_of_required_combinations;

}

void Fuse::Sequence_generator::Node::update_profile_indexes(
		std::vector<Fuse::Event_set> reference_pairs
		){

	std::vector<Fuse::Event_set> new_cross_profile_pairs;
	std::vector<Fuse::Event_set> new_within_profile_pairs;

	Fuse::Event_set latest_linking_events = combination_spec.back().first;
	Fuse::Event_set latest_unique_events = combination_spec.back().second;
	
	Fuse::Event_set previously_combined_events;
	for(unsigned int i=0; i<combination_spec.size()-1; i++){
		previously_combined_events.insert(previously_combined_events.end(), combination_spec.at(i).second.begin(), combination_spec.at(i).second.end());
	}		

	for(auto previous_event : previously_combined_events){
		
		if(std::find(latest_linking_events.begin(), latest_linking_events.end(), previous_event) == latest_linking_events.end()){
			// so the unique events (below) were never profiled with 'previous_event'

			for(auto unique_event : latest_unique_events){
				Fuse::Event_set pair = {previous_event, unique_event};
				new_cross_profile_pairs.push_back(pair);
			}

		}
	}

	// the within profile pairs are the pair combinations of the unique events,
	// together with each unique event and each linking event
	new_within_profile_pairs = Fuse::Util::get_unique_combinations(latest_unique_events,2);
	for(auto linking_event : latest_linking_events){
		for(auto unique_event : latest_unique_events){
			Fuse::Event_set pair = {linking_event, unique_event};
			new_within_profile_pairs.push_back(pair);
		}
	}
	
	// Now find corresponding reference indexes and update the data structures

	for(auto pair : new_cross_profile_pairs){
		
		unsigned int ref_idx = std::distance(reference_pairs.begin(),std::find(reference_pairs.begin(),reference_pairs.end(),pair));
		if(ref_idx == reference_pairs.size()){
			// then the pair is in the 'wrong' order to find it
			Fuse::Event_set event_pair = {pair.at(1), pair.at(0)};
			ref_idx = std::distance(reference_pairs.begin(),std::find(reference_pairs.begin(),reference_pairs.end(),event_pair));
			if(ref_idx == reference_pairs.size())
				throw std::runtime_error(fmt::format(
					"Could not find the event pair {} in the reference pairs.", Fuse::Util::vector_to_string(pair)));
		}

		if(std::find(this->cross_profile_reference_indexes.begin(),
				this->cross_profile_reference_indexes.end(),
				ref_idx) != this->cross_profile_reference_indexes.end())
			spdlog::error("A new reference index {} is going to be added to a node, but it already exists as a cross profile index.", ref_idx);

		cross_profile_reference_indexes.push_back(ref_idx);
	}

	for(auto pair : new_within_profile_pairs){
		
		unsigned int ref_idx = std::distance(reference_pairs.begin(),std::find(reference_pairs.begin(),reference_pairs.end(),pair));

		if(ref_idx == reference_pairs.size()){
			Fuse::Event_set event_pair = {pair.at(1), pair.at(0)};
			ref_idx = std::distance(reference_pairs.begin(),std::find(reference_pairs.begin(),reference_pairs.end(),event_pair));

			if(ref_idx == reference_pairs.size())
				throw std::runtime_error(fmt::format(
					"Could not find the event pair {} in the reference pairs.", Fuse::Util::vector_to_string(pair)));
		}

		if(std::find(this->within_profile_reference_indexes.begin(),
				this->within_profile_reference_indexes.end(),
				ref_idx) != this->within_profile_reference_indexes.end())
			spdlog::error("A new reference index {} is going to be added to a node, but it already exists as a within profile index.", ref_idx);

		this->within_profile_reference_indexes.push_back(ref_idx);
	}

}

void Fuse::Sequence_generator::Node::compute_resulting_metrics(
		std::vector<Fuse::Event_set> reference_pairs
		){
	
	this->update_profile_indexes(reference_pairs);

	std::vector<double> cross_profile_tmds;
	double summed_squared_tmds = 0.0;

	for(auto ref_idx : cross_profile_reference_indexes){

		auto it = this->tmds.find(ref_idx);
		if(it == this->tmds.end())
			throw std::runtime_error(fmt::format(
				"Cannot find TMD for the cross profile reference pair {} with index {}.",
				Fuse::Util::vector_to_string(reference_pairs.at(ref_idx),
				ref_idx)));

		double tmd = it->second;

		cross_profile_tmds.push_back(tmd);
		summed_squared_tmds += std::pow((tmd),2.0);

	}

	if(cross_profile_reference_indexes.size() > 0){
		this->cross_profile_epd = Fuse::calculate_weighted_geometric_mean(cross_profile_tmds);
		this->cross_profile_tmd_mse = summed_squared_tmds / cross_profile_reference_indexes.size();
	}

	std::vector<double> all_profile_tmds;
	all_profile_tmds = cross_profile_tmds;

	for(auto ref_idx : within_profile_reference_indexes){

		auto it = this->tmds.find(ref_idx);
		if(it == this->tmds.end())
			throw std::runtime_error(fmt::format(
				"Cannot find TMD for the cross profile reference pair {} with index {}.",
				Fuse::Util::vector_to_string(reference_pairs.at(ref_idx),
				ref_idx)));

		double tmd = it->second;

		all_profile_tmds.push_back(tmd);
		summed_squared_tmds += std::pow((tmd),2.0);

	}
	
	this->epd = Fuse::calculate_weighted_geometric_mean(all_profile_tmds);
	this->tmd_mse = summed_squared_tmds / (cross_profile_reference_indexes.size() + within_profile_reference_indexes.size());

	evaluated = true;

	spdlog::info("Accuracy results for {} events with combination sequence {}. epd:{}, cross_epd:{}, tmd_mse:{}, cross_tmd_mse:{}, computed on {} TMDs.",
		this->sorted_combined_events.size(),
		this->get_combination_spec_as_string(),
		this->epd,
		this->cross_profile_epd,
		this->tmd_mse,
		this->cross_profile_tmd_mse);

}
	
std::string Fuse::Sequence_generator::Node::get_combination_spec_as_string(){

	std::stringstream ss;
	for(auto spec : this->combination_spec){
		Fuse::Event_set link = spec.first;
		Fuse::Event_set uniq = spec.second;
		std::sort(link.begin(),link.end());
		std::sort(uniq.begin(),uniq.end());

		ss << "{link:" << Fuse::Util::vector_to_string(link) << ",uniq:" << Fuse::Util::vector_to_string(uniq) << "}";
	}
	return ss.str();

}


