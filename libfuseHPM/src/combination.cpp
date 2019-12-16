#include "combination.h"
#include "fuse_types.h"
#include "profile.h"
#include "instance.h"
#include "util.h"
#include "statistics.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <ctime>
#include <limits>
#include <vector>
#include <random>

Fuse::Profile_p Fuse::Combination::combine_profiles_via_strategy(
		std::vector<Fuse::Profile_p> sequence_profiles,
		Fuse::Strategy strategy,
		std::string combined_filename,
		std::string binary_filename,
		std::vector<Fuse::Event_set> overlapping_per_profile,
		Fuse::Statistics_p statistics
		){

	if(sequence_profiles.size() < 2)
		throw std::runtime_error(fmt::format("Fuse combination requires at least two execution profiles (found {}).",
			sequence_profiles.size()));

	std::vector<Fuse::Instance_p> combined_instances;

	switch(strategy){

		case Fuse::Strategy::RANDOM:
		case Fuse::Strategy::RANDOM_MINIMAL:
		case Fuse::Strategy::LGL:
		case Fuse::Strategy::LGL_MINIMAL:
			combined_instances = Fuse::Combination::generate_combined_instances_from_unordered_profiles(
				sequence_profiles,
				strategy,
				false
			);
			break;
		case Fuse::Strategy::RANDOM_TT:
		case Fuse::Strategy::RANDOM_TT_MINIMAL:
		case Fuse::Strategy::CTC:
		case Fuse::Strategy::CTC_MINIMAL:
			combined_instances = Fuse::Combination::generate_combined_instances_from_unordered_profiles(
				sequence_profiles,
				strategy,
				true
			);
			break;
		case Fuse::Strategy::BC:
			combined_instances = Fuse::Combination::generate_combined_instances_bc(
				sequence_profiles,
				strategy,
				statistics,
				overlapping_per_profile
			);
			break;
		case Fuse::Strategy::HEM:
		default:
			throw std::logic_error("Combination logic failure.");

	}

	// Create the new profile
	Fuse::Profile_p combined_execution_profile(new Fuse::Execution_profile(combined_filename, binary_filename));

	std::set<Fuse::Event> unique_events;
	for(auto instance : combined_instances){
		combined_execution_profile->add_instance(instance);
		for(auto event : instance->get_events())
			unique_events.insert(event);
	}

	// Also incoporate the runtime instances from the first profile
	std::vector<Fuse::Symbol> runtime_symbol = {"runtime"};
	auto runtime_instances = sequence_profiles.at(0)->get_instances(true,runtime_symbol);

	for(auto instance : runtime_instances){
		combined_execution_profile->add_instance(instance);
		for(auto event : instance->get_events())
			unique_events.insert(event);
	}

	for(auto event : unique_events)
		combined_execution_profile->add_event(event);

	return combined_execution_profile;

}

std::vector<Fuse::Instance_p> Fuse::Combination::generate_combined_instances_from_unordered_profiles(
		std::vector<Fuse::Profile_p> sequence_profiles,
		Fuse::Strategy strategy,
		bool per_symbol
		){

	std::vector<Fuse::Instance_p> resulting_instances;

	std::vector<Fuse::Symbol> symbols;
	if(per_symbol){
		for(auto profile : sequence_profiles)
			for(auto symbol : profile->get_unique_symbols(false))
				if(std::find(symbols.begin(), symbols.end(), symbol) == symbols.end())
					symbols.push_back(symbol);
	} else {
		symbols.push_back("all");
	}

	for(auto symbol : symbols){

		std::vector<Fuse::Symbol> restricted_symbols_list;
		if(per_symbol)
			restricted_symbols_list = {symbol};

		std::vector<std::vector<Fuse::Instance_p> > instances_per_profile;
		for(auto profile : sequence_profiles)
			instances_per_profile.push_back(profile->get_instances(false, restricted_symbols_list));

		auto combined_instances = Fuse::Combination::combine_instances_via_strategy(instances_per_profile, strategy);

		resulting_instances.insert(resulting_instances.end(), combined_instances.begin(), combined_instances.end());

	}

	return resulting_instances;
}

std::vector<Fuse::Instance_p> Fuse::Combination::combine_instances_via_strategy(
		std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
		Fuse::Strategy strategy,
		Fuse::Statistics_p statistics,
		Fuse::Event_set overlapping_events
		){

	std::vector<std::vector<Fuse::Instance_p> > matched_instances;

	switch(strategy){
		case Fuse::Strategy::RANDOM:
		case Fuse::Strategy::RANDOM_MINIMAL:
		case Fuse::Strategy::RANDOM_TT:
		case Fuse::Strategy::RANDOM_TT_MINIMAL:
			// The instances have already been filtered per symbol if necessary
			matched_instances = extract_matched_instances_random(instances_per_profile, false);
			break;
		case Fuse::Strategy::CTC:
		case Fuse::Strategy::CTC_MINIMAL:
			matched_instances = extract_matched_instances_chronological(instances_per_profile, false);
			break;
		case Fuse::Strategy::LGL:
		case Fuse::Strategy::LGL_MINIMAL:
			matched_instances = extract_matched_instances_by_label(instances_per_profile, false);
			break;
		case Fuse::Strategy::BC:
			matched_instances = extract_matched_instances_bc(instances_per_profile, true, statistics, overlapping_events);
			break;
		case Fuse::Strategy::HEM:
		default:
			throw std::logic_error("Fuse combination logic failure.");
	}

	std::vector<Fuse::Instance_p> combined_instances;
	combined_instances.reserve(matched_instances.size());

	for(auto match : matched_instances)
		combined_instances.push_back(Fuse::Combination::combine_instances(match));

	return combined_instances;
}

Fuse::Instance_p Fuse::Combination::combine_instances(
		std::vector<Fuse::Instance_p> instances_to_combine
		){

	// Create a new instance
	Fuse::Instance_p combined_instance(new Fuse::Instance());
	combined_instance->label = instances_to_combine.at(0)->label;
	combined_instance->cpu = instances_to_combine.at(0)->cpu;
	combined_instance->symbol = instances_to_combine.at(0)->symbol;
	combined_instance->start = instances_to_combine.at(0)->start;
	combined_instance->end = instances_to_combine.at(0)->end; // not necessary
	combined_instance->is_gpu_eligible = instances_to_combine.at(0)->is_gpu_eligible;

	// Add all the event value data
	// We add the first value found for each event
	Fuse::Event_set added_events;
	for(auto instance : instances_to_combine){
		auto events = instance->get_events();
		for(auto event : events){

			if(std::find(added_events.begin(), added_events.end(), event) == added_events.end()){
				bool error = false;
				auto value = instance->get_event_value(event, error); // guarantee no error
				combined_instance->append_event_value(event, value, false);
				added_events.push_back(event);
			}

		}
	}

	return combined_instance;
}

std::vector<std::vector<Fuse::Instance_p> > Fuse::Combination::extract_matched_instances_random(
		std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
		bool remove_combined_instances,
		bool expect_matching
		){

	std::random_device rd;
	std::default_random_engine engine{rd()};

	std::vector<unsigned int> num_instances_per_profile;
	for(auto profile_instances : instances_per_profile){
		std::shuffle(profile_instances.begin(), profile_instances.end(), engine);
		num_instances_per_profile.push_back(profile_instances.size());
	}

	if(expect_matching)
		if(std::adjacent_find(num_instances_per_profile.begin(), num_instances_per_profile.end(), std::not_equal_to<unsigned int>())
				!= num_instances_per_profile.end())
			spdlog::warn(fmt::format("Found variable instance counts when combining instances from {} sequence profiles randomly: {}.",
				instances_per_profile.size(), Fuse::Util::vector_to_string(num_instances_per_profile)));

	unsigned int common_num_instances = *std::min_element(num_instances_per_profile.begin(), num_instances_per_profile.end());

	std::vector<std::vector<Fuse::Instance_p> > matched_instances;
	matched_instances.reserve(common_num_instances);

	for(unsigned int instance_idx = 0; instance_idx < common_num_instances; instance_idx++){
		std::vector<Fuse::Instance_p> match;
		match.reserve(instances_per_profile.size());
		for(auto profile_instances : instances_per_profile)
			match.push_back(profile_instances.at(instance_idx));
		matched_instances.push_back(match);
	}

	if(remove_combined_instances)
		for(auto profile_instances : instances_per_profile)
			profile_instances.erase(profile_instances.begin(), profile_instances.begin() + common_num_instances);

	return matched_instances;
}

bool comp_instances_by_time(Fuse::Instance_p a, Fuse::Instance_p b){
	return (a->start < b->start);
}

std::vector<std::vector<Fuse::Instance_p> > Fuse::Combination::extract_matched_instances_chronological(
		std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
		bool remove_combined_instances,
		bool expect_matching
		){

	std::vector<unsigned int> num_instances_per_profile;
	for(auto profile_instances : instances_per_profile){
		std::sort(profile_instances.begin(), profile_instances.end(), comp_instances_by_time);
		num_instances_per_profile.push_back(profile_instances.size());
	}

	if(expect_matching)
		if(std::adjacent_find(num_instances_per_profile.begin(), num_instances_per_profile.end(), std::not_equal_to<unsigned int>())
				!= num_instances_per_profile.end())
			spdlog::warn(fmt::format("Found variable instance counts when combining instances from {} sequence profiles chronologically: {}.",
				instances_per_profile.size(), Fuse::Util::vector_to_string(num_instances_per_profile)));

	unsigned int common_num_instances = *std::min_element(num_instances_per_profile.begin(), num_instances_per_profile.end());

	std::vector<std::vector<Fuse::Instance_p> > matched_instances;
	matched_instances.reserve(common_num_instances);

	for(unsigned int instance_idx = 0; instance_idx < common_num_instances; instance_idx++){
		std::vector<Fuse::Instance_p> match;
		match.reserve(instances_per_profile.size());
		for(auto profile_instances : instances_per_profile)
			match.push_back(profile_instances.at(instance_idx));
		matched_instances.push_back(match);
	}

	if(remove_combined_instances)
		for(auto profile_instances : instances_per_profile)
			profile_instances.erase(profile_instances.begin(), profile_instances.begin() + common_num_instances);

	return matched_instances;
}

std::vector<std::vector<Fuse::Instance_p> > Fuse::Combination::extract_matched_instances_by_label(
		std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
		bool remove_combined_instances,
		bool expect_matching
		){

	std::vector<unsigned int> num_instances_per_profile;
	for(auto profile_instances : instances_per_profile){
		std::sort(profile_instances.begin(), profile_instances.end(), Fuse::comp_instances_by_label_dfs);
		num_instances_per_profile.push_back(profile_instances.size());
	}

	if(expect_matching)
		if(std::adjacent_find(num_instances_per_profile.begin(), num_instances_per_profile.end(), std::not_equal_to<unsigned int>())
				!= num_instances_per_profile.end())
			spdlog::warn(fmt::format("Found variable instance counts when combining instances from {} sequence profiles by matching label: {}.",
				instances_per_profile.size(), Fuse::Util::vector_to_string(num_instances_per_profile)));

	unsigned int common_num_instances = *std::min_element(num_instances_per_profile.begin(), num_instances_per_profile.end());

	std::vector<std::vector<Fuse::Instance_p> > matched_instances;
	matched_instances.reserve(common_num_instances);

	for(unsigned int instance_idx = 0; instance_idx < common_num_instances; instance_idx++){

		std::vector<std::string> matched_label_strs;
		std::vector<Fuse::Instance_p> match;

		match.reserve(instances_per_profile.size());
		matched_label_strs.reserve(instances_per_profile.size());

		for(auto profile_instances : instances_per_profile){
			match.push_back(profile_instances.at(instance_idx));
			matched_label_strs.push_back(Fuse::Util::vector_to_string(profile_instances.at(instance_idx)->label));
		}

		if(expect_matching)
			if(std::adjacent_find(matched_label_strs.begin(), matched_label_strs.end(), std::not_equal_to<std::string>()) != matched_label_strs.end())
				spdlog::warn("LGL strategy matched different labels across profiles: {}.", Fuse::Util::vector_to_string(matched_label_strs));

		matched_instances.push_back(match);
	}

	if(remove_combined_instances)
		for(auto profile_instances : instances_per_profile)
			profile_instances.erase(profile_instances.begin(), profile_instances.begin() + common_num_instances);

	return matched_instances;
}

std::vector<Fuse::Instance_p> Fuse::Combination::generate_combined_instances_bc(
		std::vector<Fuse::Profile_p> sequence_profiles,
		Fuse::Strategy strategy,
		Fuse::Statistics_p statistics,
		std::vector<Fuse::Event_set> overlapping_per_profile
	){

	std::vector<Fuse::Instance_p> resulting_instances;

	auto initial_profile = sequence_profiles.at(0);
	auto symbols = initial_profile->get_unique_symbols(false);
	std::map<Fuse::Symbol, std::vector<Fuse::Instance_p> > previous_instances_per_symbol;

	for(auto symbol : symbols){
		std::vector<Fuse::Symbol> symbol_list = {symbol};
		auto instances_for_symbol = initial_profile->get_instances(false, symbol_list);
		previous_instances_per_symbol.insert(std::make_pair(symbol, instances_for_symbol));
	}

	for(decltype(sequence_profiles.size()) combination_idx = 1; combination_idx < sequence_profiles.size(); combination_idx++){

		auto next_profile = sequence_profiles.at(combination_idx);

		spdlog::info("Running BC combination {} to incorporate {} using overlapping events {}.",
			combination_idx,
			next_profile->tracefile,
			Fuse::Util::vector_to_string(overlapping_per_profile.at(combination_idx))
		);

		std::map<Fuse::Symbol, std::vector<Fuse::Instance_p> > combined_instances_per_symbol;

		for(auto symbol : symbols){

			spdlog::debug("Clustering instances of symbol [{}] ({}/{}).",
				symbol, std::find(symbols.begin(), symbols.end(), symbol) - symbols.begin() + 1, symbols.size());

			std::vector<std::vector<Fuse::Instance_p> > instances_per_profile;

			// Add the instances from the previous combination
			instances_per_profile.push_back(previous_instances_per_symbol.find(symbol)->second);

			// Add the instances from the next profile
			std::vector<Fuse::Symbol> symbol_list = {symbol};
			auto next_profile_instances = next_profile->get_instances(false, symbol_list);
			instances_per_profile.push_back(next_profile_instances);

			if(instances_per_profile.at(0).size() != instances_per_profile.at(1).size())
				spdlog::debug("There are unequal number of instances ({} and {}) from the two profiles under BC combination.",
					instances_per_profile.at(0).size(), instances_per_profile.at(1).size());
			else
				spdlog::debug("Clustering {} instances from each profile via BC, for symbol {}.",
					instances_per_profile.at(0).size(), symbol);

			auto combined_instances = Fuse::Combination::combine_instances_via_strategy(
				instances_per_profile,
				strategy,
				statistics,
				overlapping_per_profile.at(combination_idx)
			);

			if(instances_per_profile.at(0).size() > 0 || instances_per_profile.at(1).size() > 0)
				spdlog::warn("There were uncombined instances for symbol '{}' remaining ({} and {}) after BC combination.",
					symbol,
					instances_per_profile.at(0).size(),
					instances_per_profile.at(1).size()
				);

			// Add the combined instances to the combined map of instances per symbol
			combined_instances_per_symbol.insert(std::make_pair(symbol, combined_instances));

		}

		// Set the results of this combination as the set of previous instances for the next combination
		previous_instances_per_symbol = combined_instances_per_symbol;

	}

	// The final combined instances per symbol are the results of the final combination, so aggregate them
	for(auto symbol_instances : previous_instances_per_symbol)
		resulting_instances.insert(resulting_instances.end(), symbol_instances.second.begin(), symbol_instances.second.end());

	return resulting_instances;
}

/* Assume I have already filtered instances to per-symbol */
std::vector<std::vector<Fuse::Instance_p> > Fuse::Combination::extract_matched_instances_bc(
		std::vector<std::vector<Fuse::Instance_p> >& instances_per_profile,
		bool remove_combined_instances,
		Fuse::Statistics_p statistics,
		Fuse::Event_set overlapping_events
		){

	std::vector<std::vector<Fuse::Instance_p> > matched_instances;

	if(instances_per_profile.size() != 2)
		throw std::logic_error(fmt::format("BC combination strategy can only combine two profiles at a time, but {} were provided.",
			instances_per_profile.size()));

	if(statistics == nullptr)
		throw std::logic_error("BC combination strategy requires event statistics, but none were provided.");

	if(overlapping_events.size() == 0)
		throw std::runtime_error("BC combination strategy requires overlapping events between profiles, but none were provided.");

	auto instances_a = instances_per_profile.at(0);
	auto instances_b = instances_per_profile.at(1);

	// Sort so that we can use set_difference
	std::sort(instances_a.begin(), instances_a.end());
	std::sort(instances_b.begin(), instances_b.end());

	if(instances_a.size() == 0 || instances_b.size() == 0)
		return matched_instances;

	Fuse::Symbol symbol;
	if(instances_a.size() > 0)
		symbol = instances_a.at(0)->symbol;
	else if(instances_b.size() > 0)
		symbol = instances_b.at(0)->symbol;
	else
		return matched_instances;

	std::vector<std::pair<int64_t,int64_t> > event_bounds;
	for(auto event : overlapping_events){
		event_bounds.push_back(statistics->get_bounds(event, symbol));
	}

	auto d_max = Fuse::Combination::bc_find_maximum_granularity(
		instances_a,
		instances_b,
		overlapping_events,
		event_bounds);

	spdlog::debug("Initial granularity for BC was {}.", d_max);

	bool refining = true;
	unsigned int g = d_max;
	while(refining){

		auto clustered_instances_a = Fuse::Combination::bc_allocate_to_clusters(
			instances_a,
			overlapping_events,
			event_bounds,
			g);

		auto clustered_instances_b = Fuse::Combination::bc_allocate_to_clusters(
			instances_b,
			overlapping_events,
			event_bounds,
			g);

		spdlog::trace("At granularity {}, there are {} clusters in a and {} clusters in b.",
			g,
			clustered_instances_a.size(),
			clustered_instances_b.size());

		std::vector<Fuse::Instance_p> remove_from_a, remove_from_b;

		// Go through one profile's set of clusters (we don't need to check both)
		for(auto cluster_a_iter : clustered_instances_a){

			// Check if there are any instances of this cluster in the other profile
			auto cluster_b_iter = clustered_instances_b.find(cluster_a_iter.first);
			if(cluster_b_iter == clustered_instances_b.end())
				continue;

			// We have cross-profile instances in the same cluster
			auto cluster_instances_a = cluster_a_iter.second;
			auto cluster_instances_b = cluster_b_iter->second;

			std::vector<std::vector<Fuse::Instance_p> > instances_per_profile_within_cluster = {cluster_instances_a, cluster_instances_b};
			auto within_cluster_matched_instances = extract_matched_instances_by_label(instances_per_profile_within_cluster, true, false);

			matched_instances.insert(matched_instances.end(), within_cluster_matched_instances.begin(), within_cluster_matched_instances.end());

			// Now need to make sure that these instances are excluded from later clustering
			// So save the ones that we have matched from each profile
			remove_from_a.reserve(remove_from_a.size() + within_cluster_matched_instances.size());
			remove_from_b.reserve(remove_from_b.size() + within_cluster_matched_instances.size());
			for(auto match : within_cluster_matched_instances){
				remove_from_a.push_back(match.at(0));
				remove_from_b.push_back(match.at(1));
			}

		}

		// Remove the newly combined instances for the next iteration
		std::sort(remove_from_a.begin(), remove_from_a.end());
		std::sort(remove_from_b.begin(), remove_from_b.end());

		std::vector<Fuse::Instance_p> remaining_instances_a, remaining_instances_b;

		std::set_difference(
			instances_a.begin(), instances_a.end(),
			remove_from_a.begin(), remove_from_a.end(),
			std::back_inserter(remaining_instances_a));

		std::set_difference(
			instances_b.begin(), instances_b.end(),
			remove_from_b.begin(), remove_from_b.end(),
			std::back_inserter(remaining_instances_b));

		instances_a = remaining_instances_a;
		instances_b = remaining_instances_b;

		// Relax the similarity constraint by reducing the granularity g
		if(instances_a.size() == 0 || instances_b.size() == 0){
			spdlog::debug("At final refinement with granularity {}, there are {} and {} instances remaining across the profiles.",
				g, instances_a.size(), instances_b.size());
			refining = false;
			break;
		}

		spdlog::debug("After clustering with granularity {}, there are {} and {} instances remaining across the profiles.",
			g, instances_a.size(), instances_b.size());

		g = Fuse::Combination::relax_similarity_constraint(g,
			clustered_instances_a,
			clustered_instances_b,
			remove_from_a,
			remove_from_b,
			overlapping_events,
			event_bounds
		);

	}

	if(remove_combined_instances){
		instances_per_profile.at(0) = instances_a;
		instances_per_profile.at(1) = instances_b;
	}

	return matched_instances;

}

unsigned int Fuse::Combination::bc_find_maximum_granularity(
		std::vector<Fuse::Instance_p> a,
		std::vector<Fuse::Instance_p> b,
		Fuse::Event_set overlapping_events,
		std::vector<std::pair<int64_t,int64_t> > bounds
		){

	unsigned int granularity = std::numeric_limits<unsigned int>::max();

	for(auto event_iter = overlapping_events.begin(); event_iter < overlapping_events.end(); event_iter++){

		std::vector<int64_t> values_a, values_b;
		values_a.reserve(a.size());
		values_b.reserve(b.size());

		bool error = false;
		for(auto instance : a)
			values_a.push_back(instance->get_event_value(*event_iter, error));
		for(auto instance : b)
			values_b.push_back(instance->get_event_value(*event_iter, error));

		std::sort(values_a.begin(), values_a.end());
		std::sort(values_b.begin(), values_b.end());

		if(values_a.size() == 1 || values_b.size() == 1)
			return 1;

		// Iterate over the sorted values, to find the minimum difference
		uint64_t minimum_difference = std::numeric_limits<uint64_t>::max();
		unsigned int a_idx = 0, b_idx = 0;

		while(a_idx < values_a.size() && b_idx < values_b.size()){

			int64_t difference = values_a.at(a_idx) - values_b.at(b_idx);
			uint64_t abs_difference = std::abs(difference);

			if(abs_difference < minimum_difference)
				minimum_difference = abs_difference;

			if(difference < 0)
				a_idx++; // value_b is greater than value_a, so increment a_idx to get closer to b
			else
				b_idx++; // value_b is lesser or equal to value_a, so increment b_idx to get closer to a

		}

		if(minimum_difference == 0)
			return 1;

		auto event_idx = event_iter - overlapping_events.begin();
		unsigned int num_cells_for_minimum_difference =
			(bounds.at(event_idx).second - bounds.at(event_idx).first) / minimum_difference;

		// We require no more than this many cells to match the closest two instances in this dimension
		// There's no point using any more than this number of cells
		// So save the minimum 'max num cells' across the dimensions

		if(num_cells_for_minimum_difference < granularity)
			granularity = num_cells_for_minimum_difference;

	}

	return granularity;

}

std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > Fuse::Combination::bc_allocate_to_clusters(
		std::vector<Fuse::Instance_p> instances,
		Fuse::Event_set overlapping_events,
		std::vector<std::pair<int64_t,int64_t> > bounds,
		unsigned int granularity
		){

	// A cluster is represented by a vector of cluster indexes
	// Each cluster maps to the instances that populate it
	std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances;

	if(granularity == 1){
		// Simply add all instances to one cluster
		std::vector<unsigned int> cluster = {0};
		clustered_instances.insert(std::make_pair(cluster, instances));
		return clustered_instances;
	}

	// Alloate each instance to its cluster
	for(auto instance : instances){

		std::vector<unsigned int> cluster;

		for(unsigned int event_idx = 0; event_idx < overlapping_events.size(); event_idx++){

			int64_t minimum = bounds.at(event_idx).first;
			int64_t maximum = bounds.at(event_idx).second;

			if(minimum == maximum){
				cluster.push_back(0);
				continue;
			}

			bool error = false;
			int64_t value = instance->get_event_value(overlapping_events.at(event_idx), error);

			unsigned int dim_coord = (((double) (value - minimum) / (maximum - minimum))) * granularity;

			// I don't want the instance with the maximum value being in its own cluster
			if(value == maximum && dim_coord > 0)
				dim_coord--;

			cluster.push_back(dim_coord);

		}

		// Now add the instance to this cluster

		auto cluster_iter = clustered_instances.find(cluster);
		if(cluster_iter == clustered_instances.end()){
			std::vector<Fuse::Instance_p> new_cluster_instances = {instance};
			clustered_instances.insert(std::make_pair(cluster, new_cluster_instances));
		} else {
			cluster_iter->second.push_back(instance);
		}

	}

	return clustered_instances;

}

std::vector<std::pair<std::vector<unsigned int>,std::vector<unsigned int> > > get_closest_clusters(
		std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_a,
		std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_b,
		std::vector<Fuse::Instance_p> already_combined_a,
		std::vector<Fuse::Instance_p> already_combined_b
		){

	// There may be multiple clusters at the same euclidean distance
	std::vector<std::pair<std::vector<unsigned int>,std::vector<unsigned int> > > closest_cluster_coordinates;

	std::vector<std::vector<unsigned int> > all_cluster_coords;
	for(auto cluster : clustered_instances_a)
		all_cluster_coords.push_back(cluster.first);
	for(auto cluster : clustered_instances_b)
		all_cluster_coords.push_back(cluster.first);

	double minimum_squared_distance = std::numeric_limits<double>::max();

	for(decltype(all_cluster_coords.size()) i = 0; i < all_cluster_coords.size(); i++){
		for(decltype(all_cluster_coords.size()) j = i+1; j < all_cluster_coords.size(); j++){
			// Find the euclidean distance between these clusters

			double squared_euclidean_distance = 0.0;
			for(decltype(all_cluster_coords.size()) k = 0; k < all_cluster_coords.at(i).size(); k++)
				squared_euclidean_distance += std::pow(std::fabs(all_cluster_coords.at(i).at(k)-all_cluster_coords.at(j).at(k)),2);

			bool should_add = false;
			if(squared_euclidean_distance <= minimum_squared_distance){

				auto cluster_a_iter = clustered_instances_a.find(all_cluster_coords.at(i));
				auto cluster_b_iter = clustered_instances_b.find(all_cluster_coords.at(j));

				if(cluster_a_iter != clustered_instances_a.end() && cluster_b_iter != clustered_instances_b.end()){

					std::vector<Fuse::Instance_p> non_merged_a, non_merged_b;

					// There are some in both! Check they haven't been merged?
					std::set_difference(
						cluster_a_iter->second.begin(), cluster_a_iter->second.end(),
						already_combined_a.begin(), already_combined_a.end(),
						std::back_inserter(non_merged_a));

					std::set_difference(
						cluster_b_iter->second.begin(), cluster_b_iter->second.end(),
						already_combined_b.begin(), already_combined_b.end(),
						std::back_inserter(non_merged_b));

					if(non_merged_a.size() > 0 && non_merged_b.size() > 0){
						should_add = true;
					}

				}

				if(should_add == false){
					// Check the other way around

					cluster_a_iter = clustered_instances_a.find(all_cluster_coords.at(j));
					cluster_b_iter = clustered_instances_b.find(all_cluster_coords.at(i));

					if(cluster_a_iter != clustered_instances_a.end() && cluster_b_iter != clustered_instances_b.end()){

						std::vector<Fuse::Instance_p> non_merged_a, non_merged_b;

						// There are some in both! Check they haven't been merged?
						std::set_difference(
							cluster_a_iter->second.begin(), cluster_a_iter->second.end(),
							already_combined_a.begin(), already_combined_a.end(),
							std::back_inserter(non_merged_a));

						std::set_difference(
							cluster_b_iter->second.begin(), cluster_b_iter->second.end(),
							already_combined_b.begin(), already_combined_b.end(),
							std::back_inserter(non_merged_b));

						if(non_merged_a.size() > 0 && non_merged_b.size() > 0){
							should_add = true;
						}

					}
				}
			}

			if(should_add == false)
				continue;

			if(squared_euclidean_distance < minimum_squared_distance){
				minimum_squared_distance = squared_euclidean_distance;
				closest_cluster_coordinates.clear();
			}

			auto pair = std::make_pair(all_cluster_coords.at(i),all_cluster_coords.at(j));
			closest_cluster_coordinates.push_back(pair);

		}

	}

	return closest_cluster_coordinates;
}


/* TODO Currently very inefficient!
* At the moment, I find the pair of instances that are closest in euclidean distance
* - I first find the closest pair of bins, by bin-distance, that have combinable instances
*	- Then enumerate the instances of these bins
* Then I find the largest single-dimension distance between these instances, and that's the span for the next bins
*/

double find_minimum_pairwise_distance_brute_force(
		std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_a,
		std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_b,
		std::vector<Fuse::Instance_p> already_combined_a,
		std::vector<Fuse::Instance_p> already_combined_b,
		Fuse::Event_set overlapping_events,
		std::vector<std::pair<int64_t,int64_t> > event_bounds,
		unsigned int granularity
		){

	// Find the closest clusters that have combinable instances
	auto closest_cluster_coordinates = get_closest_clusters(
		clustered_instances_a,
		clustered_instances_b,
		already_combined_a,
		already_combined_b
	);

	double minimum_euclidean_squared_distance = std::numeric_limits<double>::max();
	double closest_instances_largest_distance_in_single_event = 0;

	for(auto cluster_pair : closest_cluster_coordinates){

		std::vector<Fuse::Instance_p> all_instances_in_one, all_instances_in_two;

		// Collect all the non-combined instances into a single list for each cluster pair
		auto cluster_one = cluster_pair.first;

		auto one_iter = clustered_instances_a.find(cluster_one);
		if(one_iter != clustered_instances_a.end())
			for(auto instance : one_iter->second){
				if(std::find(already_combined_a.begin(), already_combined_a.end(), instance) == already_combined_a.end())
					all_instances_in_one.push_back(instance);
			}
		one_iter = clustered_instances_b.find(cluster_one);
		if(one_iter != clustered_instances_b.end())
			for(auto instance : one_iter->second){
				if(std::find(already_combined_b.begin(), already_combined_b.end(), instance) == already_combined_b.end())
					all_instances_in_one.push_back(instance);
			}

		auto cluster_two = cluster_pair.second;

		auto two_iter = clustered_instances_a.find(cluster_two);
		if(two_iter != clustered_instances_a.end())
			for(auto instance : two_iter->second){
				if(std::find(already_combined_a.begin(), already_combined_a.end(), instance) == already_combined_a.end())
					all_instances_in_two.push_back(instance);
			}
		two_iter = clustered_instances_b.find(cluster_two);
		if(two_iter != clustered_instances_b.end())
			for(auto instance : two_iter->second){
				if(std::find(already_combined_b.begin(), already_combined_b.end(), instance) == already_combined_b.end())
					all_instances_in_two.push_back(instance);
			}

		for(auto instance_one : all_instances_in_one){
			for(auto instance_two : all_instances_in_two){

				bool error = false;
				double squared_bin_euclidean_distance = 0.0;
				double local_largest_distance_in_single_event = 0.0;
				for(decltype(overlapping_events.size()) k = 0; k < overlapping_events.size(); k++){

					int64_t count_from_one = instance_one->get_event_value(overlapping_events.at(k),error);
					int64_t count_from_two = instance_two->get_event_value(overlapping_events.at(k),error);

					uint64_t difference = std::abs(((double) count_from_one) - ((double) count_from_two));

					int64_t range = event_bounds.at(k).second - event_bounds.at(k).first;

					double bin_distance = ((double) difference) / (((double)(range)) / granularity);

					if(bin_distance > local_largest_distance_in_single_event){
						local_largest_distance_in_single_event = bin_distance;
					}

					squared_bin_euclidean_distance += std::pow(bin_distance,2);
				}

				if(squared_bin_euclidean_distance == minimum_euclidean_squared_distance){
					if(local_largest_distance_in_single_event < closest_instances_largest_distance_in_single_event){
						closest_instances_largest_distance_in_single_event = local_largest_distance_in_single_event;
					}
				} else if(squared_bin_euclidean_distance < minimum_euclidean_squared_distance){
					minimum_euclidean_squared_distance = squared_bin_euclidean_distance;
					closest_instances_largest_distance_in_single_event = local_largest_distance_in_single_event;
				}

			}
		}

	}

	return closest_instances_largest_distance_in_single_event;
}

unsigned int Fuse::Combination::relax_similarity_constraint(
		unsigned int current_granularity,
		std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_a,
		std::map<std::vector<unsigned int>, std::vector<Fuse::Instance_p> > clustered_instances_b,
		std::vector<Fuse::Instance_p> already_combined_a,
		std::vector<Fuse::Instance_p> already_combined_b,
		Fuse::Event_set overlapping_events,
		std::vector<std::pair<int64_t,int64_t> > event_bounds
		){

	double minimum_bin_distance = find_minimum_pairwise_distance_brute_force(
		clustered_instances_a,
		clustered_instances_b,
		already_combined_a,
		already_combined_b,
		overlapping_events,
		event_bounds,
		current_granularity
	);

	unsigned int next_granularity = std::ceil(((double) ((double) 1.0/(1.0+minimum_bin_distance))) * current_granularity);

	if(next_granularity == current_granularity)
		next_granularity--;

	// Just in case
	if(next_granularity < 1 || minimum_bin_distance == 0.0)
		next_granularity = 1;

	spdlog::debug("Minimum bin distance is {} after granularity {}, relaxing granularity to {}.", minimum_bin_distance, current_granularity, next_granularity);

	return next_granularity;

}

