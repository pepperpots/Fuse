#include "analysis.h"
#include "config.h"
#include "profile.h"
#include "statistics.h"
#include "target.h"
#include "util.h"

/* Assertions within the EMD implementation cause it to fail when two distributions are equivalent
 * Removing the assertions via NDEBUG allows these situations to (intuitively) return 0.0
 * cstdef is required for fast_emd headers
 */
#include <cstddef>
#define NDEBUG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare" // Lots in this third party code
#include "fast_emd/emd_hat.hpp"
#include "fast_emd/emd_hat_signatures_interface.hpp"
#pragma GCC diagnostic pop
#undef NDEBUG
#include "spdlog/spdlog.h"

extern "C" {
#include "MIToolbox/MutualInformation.h"
#include "MIToolbox/Entropy.h"
}

#include <map>
#include <cmath>
#include <vector>

double distance_calculation(feature_tt* one, feature_tt* two){

	double square_distance = 0.0;

	for(decltype(one->coords.size()) i=0; i < one->coords.size(); i++)
		square_distance += std::pow(std::fabs(two->coords.at(i) - one->coords.at(i)),2);

	return std::sqrt(square_distance);
}

struct Bin {
	unsigned int num_instances;
	std::vector<int64_t> per_dimension_summed_values; // For mean calculation after one pass
	double coord_of_mean;
};

std::map<std::vector<double>, Bin> allocate_instances_to_bins(
		std::vector<std::vector<int64_t> > distribution,
		std::vector<std::pair<int64_t,int64_t> > bounds_per_dimension,
		unsigned int num_bins_per_dimension,
		std::vector<double> bin_size_per_dimension
		){

	std::map<std::vector<double>, Bin> populated_bins;

	// Allocate instances to bins:
	for(auto instance_values : distribution){

		// The coords identify which bin
		std::vector<double> coords;
		coords.reserve(bounds_per_dimension.size());

		for(decltype(bounds_per_dimension.size()) dim_idx=0; dim_idx<bounds_per_dimension.size(); dim_idx++){

			// If bin size is zero, then the values are constant, so allocate all to same bin in this dimension
			if(bin_size_per_dimension.at(dim_idx) == 0.0){
				coords.push_back(0.0);
				continue;
			}

			// Find which integer bin this instance should be allocated to
			int coord = static_cast<int>((instance_values.at(dim_idx) - bounds_per_dimension.at(dim_idx).first) / bin_size_per_dimension.at(dim_idx));

			// If the instance value is maximum, then coord will be equal to (num_bins_per_dimension)
			// As the bins are zero indexed, this will give more than we want, so reduce by 1 if maximum
			if(instance_values.at(dim_idx) == bounds_per_dimension.at(dim_idx).second)
				coord--;

			// Now, determine if the instance is allocated to an external bin (indexed by -1 and num_bins_per_dimension)
			if(coord < 0)
				coord = -1;
			else if(coord > static_cast<double>(num_bins_per_dimension))
				coord = num_bins_per_dimension;

			coords.push_back(static_cast<double>(coord));

		}

		// Add the instance to the identified bin
		auto bin_iter = populated_bins.find(coords);
		if(bin_iter == populated_bins.end()){

			Bin bin;
			bin.num_instances = 1;
			bin.per_dimension_summed_values.reserve(bounds_per_dimension.size());
			for(decltype(bounds_per_dimension.size()) dim_idx=0; dim_idx<bounds_per_dimension.size(); dim_idx++)
				bin.per_dimension_summed_values.push_back(instance_values.at(dim_idx));

			populated_bins.insert(std::make_pair(coords, bin));

		} else {

			bin_iter->second.num_instances++;
			for(decltype(bounds_per_dimension.size()) dim_idx=0; dim_idx<bounds_per_dimension.size(); dim_idx++)
				bin_iter->second.per_dimension_summed_values.at(dim_idx) += instance_values.at(dim_idx);

		}

	} // Finished allocating instances

	return populated_bins;

}

signature_tt<double> convert_distribution_to_bounded_signature(
		std::vector<std::vector<int64_t> > distribution,
		std::vector<std::pair<int64_t,int64_t> > bounds_per_dimension,
		unsigned int num_bins_per_dimension
		){

	// Instances are binned within the provided bounds
	// Any outside the reference ranges are placed into one of two external bins (below min bin, above max bin)

	// The coordinate of any bin (including external) corresponds to the mean event values of its instances
	// Weights are the bin's instance-count normalised to the total instances in the distribution

	std::vector<double> bin_size_per_dimension;
	bin_size_per_dimension.reserve(bounds_per_dimension.size());
	for(auto bound : bounds_per_dimension)
		bin_size_per_dimension.push_back((static_cast<double>(bound.second) - static_cast<double>(bound.first)) / num_bins_per_dimension);

	unsigned int total_num_instances = distribution.size();

	std::map<std::vector<double>, Bin> populated_bins = allocate_instances_to_bins(
		distribution,
		bounds_per_dimension,
		num_bins_per_dimension,
		bin_size_per_dimension
	);

	if(populated_bins.size() == 0)
		throw std::runtime_error(
			fmt::format("Cannot analyse a distribution with 0 populated bins. {}.",
				fmt::format("The distribution contained {} instances, with {} dimensions divided into {} bins per dimension.",
					distribution.size(),
					bounds_per_dimension.size(),
					num_bins_per_dimension
				)
			)
		);

	// The signature contains an array of Features (i.e. coordinates) and an array of Weights
	// Each coord and each weight is associated by position in the array
	signature_tt<double> signature;
	signature.n = populated_bins.size();
	signature.Features = new feature_tt[signature.n];
	signature.Weights = new double[signature.n];

	// Convert each bin into the signature format for fast_emd
	unsigned int bin_idx = 0;
	for(auto bin_iter : populated_bins){

		if(bin_iter.second.num_instances < 1)
			throw std::logic_error("When calculating TMD: found a bin containing no instances.");

		std::vector<double> coords = bin_iter.first;
		double weight = static_cast<double>(bin_iter.second.num_instances) / total_num_instances;

		// Convert each integer coordinate into a float coordinate, corresponding to the mean of the bin's values
		for(decltype(bounds_per_dimension.size()) dim_idx=0; dim_idx<bounds_per_dimension.size(); dim_idx++){

			// Continue if dimension is constant (meaning all same bin, and the coords don't need to be changed)
			if(bin_size_per_dimension.at(dim_idx) == 0.0)
				continue;

			double mean_value = static_cast<double>(bin_iter.second.per_dimension_summed_values.at(dim_idx)) / bin_iter.second.num_instances;

			// If the value is external bottom, then calculate bin distance below 0.0
			// Otherwise, calculate the fractional bin distance above 0.0

			if(coords.at(dim_idx) < 0.0){

				// How much lower that (min) is mean_value, in floating units of bin_size
				double value_displacement = mean_value - bounds_per_dimension.at(dim_idx).first; // negative under (min)
				double bin_displacement = value_displacement / bin_size_per_dimension.at(dim_idx); // convert to units of bin_size
				coords.at(dim_idx) = 0.0 + bin_displacement; // 0.0 is defined as coord of (min)

			} else {

				double fractional_bin_coord = (mean_value - bounds_per_dimension.at(dim_idx).first) / bin_size_per_dimension.at(dim_idx);
				coords.at(dim_idx) = fractional_bin_coord;

			}

		}

		signature.Features[bin_idx].coords = coords;
		signature.Weights[bin_idx] = weight;

		bin_idx++;

	}

	return signature;
}

double Fuse::Analysis::calculate_uncalibrated_tmd(
		std::vector<std::vector<int64_t> > distribution_one,
		std::vector<std::vector<int64_t> > distribution_two,
		std::vector<std::pair<int64_t, int64_t> > bounds_per_dimension,
		unsigned int num_bins_per_dimension
		){

	signature_tt<double> signature_one = convert_distribution_to_bounded_signature(
		distribution_one,
		bounds_per_dimension,
		num_bins_per_dimension
	);

	signature_tt<double> signature_two = convert_distribution_to_bounded_signature(
		distribution_two,
		bounds_per_dimension,
		num_bins_per_dimension
	);

	double extra_mass_penalty = 0.0;
	double result = emd_hat_signature_interface<double>(&signature_one, &signature_two, distance_calculation, extra_mass_penalty);

	delete[](signature_one.Features);
	delete[](signature_one.Weights);
	delete[](signature_two.Features);
	delete[](signature_two.Weights);

	return result;

}

double Fuse::Analysis::compute_ami(
		Fuse::Event_set set_a,
		Fuse::Event_set set_b,
		std::vector<Fuse::Event_set> reference_pairs,
		std::map<unsigned int, double> pairwise_mi_values
		){

	std::vector<double> mi_list;

	for(auto a : set_a){
		for(auto b : set_b){

			Fuse::Event_set event_pair = {a,b};

			unsigned int event_pair_idx = std::distance(reference_pairs.begin(),std::find(reference_pairs.begin(),reference_pairs.end(),event_pair));
			if(event_pair_idx == reference_pairs.size()){
				// then the pair is in the 'wrong' order to find it
				event_pair = {b,a};
				event_pair_idx = std::distance(reference_pairs.begin(),std::find(reference_pairs.begin(),reference_pairs.end(),event_pair));
			}
			
			double mi = -1.0;

			auto saved_mi_iter = pairwise_mi_values.find(event_pair_idx);
			if(saved_mi_iter != pairwise_mi_values.end()){
				mi = saved_mi_iter->second;
			} else {
				spdlog::error("Cannot find MI between {} and {}.", a, b);
				mi = 0.0;
			}
			
			mi_list.push_back(mi);

		}
	}

	//double ami = std::accumulate(mi_list.begin(), mi_list.end(), 0.0) / mi_list.size(); 
	return Fuse::calculate_weighted_geometric_mean(mi_list); // because they are normalised

}

double Fuse::Analysis::calculate_calibrated_tmd_for_pair(
		Fuse::Target& target,
		std::vector<Fuse::Symbol> symbols,
		Fuse::Event_set reference_pair,
		Fuse::Profile_p profile,
		std::vector<unsigned int> reference_repeats_list,
		unsigned int bin_count,
		bool weighted_tmd
		){

	std::map<Fuse::Symbol, double> uncalibrated_tmd_per_symbol;

	for(auto symbol : symbols){

		std::vector<double> uncalibrated_tmds_per_reference_repeat;
		uncalibrated_tmds_per_reference_repeat.reserve(reference_repeats_list.size());

		std::vector<Fuse::Symbol> constrained_symbols;
		if(symbol != "all_symbols")
			constrained_symbols = {symbol};

		std::vector<std::pair<int64_t, int64_t> > bounds_per_event;
		for(auto event : reference_pair)
			bounds_per_event.push_back(target.get_statistics()->get_bounds(event, symbol));

		// We are guaranteed one if no exception
		std::map<std::string, std::vector<std::vector<int64_t> > > distribution_per_symbol =
			profile->get_value_distribution(
				reference_pair,
				false,
				constrained_symbols
			);
		auto distribution = distribution_per_symbol.begin()->second;

		for(auto reference_repeat_idx : reference_repeats_list){

			auto reference_distribution = target.get_or_load_reference_distribution(
				reference_pair,
				reference_repeat_idx,
				constrained_symbols
			);

			auto uncalibrated_tmd = Fuse::Analysis::calculate_uncalibrated_tmd(
				reference_distribution,
				distribution,
				bounds_per_event,
				bin_count
			);

			uncalibrated_tmds_per_reference_repeat.push_back(uncalibrated_tmd);

		}

		double median_uncalibrated_tmd = Fuse::calculate_median_from_values(
			uncalibrated_tmds_per_reference_repeat
		);

		uncalibrated_tmd_per_symbol.insert(std::make_pair(symbol, median_uncalibrated_tmd));

	}

	// Now calibrate and average the tmds across symbol to get the calibrated tmd w.r.t reference pair
	std::vector<double> calibrated_tmds_per_symbol;
	std::vector<double> weights_per_symbol;
	calibrated_tmds_per_symbol.reserve(uncalibrated_tmd_per_symbol.size());
	weights_per_symbol.reserve(uncalibrated_tmd_per_symbol.size());

	for(auto symbol_pair : uncalibrated_tmd_per_symbol){

		auto calibration_tmd_pair = target.get_or_load_calibration_tmd(reference_pair, symbol_pair.first);
		auto calibration_tmd = calibration_tmd_pair.first;
		auto weight = calibration_tmd_pair.second;

		if(calibration_tmd == -1.0)
			throw std::runtime_error(fmt::format(
				"Cannot find calibration tmd for reference pair {} and symbol '{}'",
				Fuse::Util::vector_to_string(reference_pair),
				symbol_pair.first)
			);

		if(calibration_tmd == 0.0){
			spdlog::warn("Calibration TMD for reference pair {} and symbol '{}' was 0.0.",
				Fuse::Util::vector_to_string(reference_pair),
				symbol_pair.first
			);
			calibration_tmd = 1.0; // Let it be equal to the uncalibrated TMD in this case
		}

		double calibrated_tmd = symbol_pair.second / calibration_tmd;
		calibrated_tmds_per_symbol.push_back(calibrated_tmd);

		if(weighted_tmd)
			weights_per_symbol.push_back(weight);

	}

	// Apply a weighted average across symbols
	// If weights_per_symbol is empty, then the standard geometric mean will be calculated
	double calibrated_tmd_wrt_pair = Fuse::calculate_weighted_geometric_mean(
		calibrated_tmds_per_symbol,
		weights_per_symbol
	);

	return calibrated_tmd_wrt_pair;

}

double Fuse::Analysis::calculate_normalised_mutual_information(
		std::vector<std::vector<int64_t> > distribution
		){

	unsigned int event1_counts[distribution.size()];
	unsigned int event2_counts[distribution.size()]; // must be same length

	int64_t min_e1 = 99999;
	int64_t max_e1 = 0;
	int64_t min_e2 = 99999;
	int64_t max_e2 = 0;
	for(unsigned int task_idx = 0; task_idx < distribution.size(); task_idx++){
	
		std::vector<int64_t> task_counts = distribution.at(task_idx);
		if(task_counts.at(0) < min_e1)
			min_e1 = task_counts.at(0);
		if (task_counts.at(0) > max_e1)
			max_e1 = task_counts.at(0);
		
		if(task_counts.at(1) < min_e2)
			min_e2 = task_counts.at(1);
		if (task_counts.at(1) > max_e2)
			max_e2 = task_counts.at(1);

	}

	for(unsigned int task_idx = 0; task_idx < distribution.size(); task_idx++){

		event1_counts[task_idx] = (unsigned int) ((((double)(distribution.at(task_idx).at(0) - min_e1)) / (max_e1 - min_e1))*1000);
		event2_counts[task_idx] = (unsigned int) ((((double)(distribution.at(task_idx).at(1) - min_e2)) / (max_e2 - min_e2))*1000);

	}

	// normalised as http://www.jmlr.org/papers/volume3/strehl02a/strehl02a.pdf (page 589)
	double event1_entropy = calcEntropy(&event1_counts[0], distribution.size());
	double event2_entropy = calcEntropy(&event2_counts[0], distribution.size());
	double denominator = std::sqrt(event1_entropy * event2_entropy);

	double mi = calcMutualInformation(&event1_counts[0], &event2_counts[0], distribution.size());

	if(denominator == 0.0){
		// The values are constant, so there is 0 entropy, meaning 0 entropy difference, meaning no information
		return 0.0;
	}

	return (mi / denominator);

}

