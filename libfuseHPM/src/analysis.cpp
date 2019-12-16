#include "analysis.h"
#include "config.h"
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
