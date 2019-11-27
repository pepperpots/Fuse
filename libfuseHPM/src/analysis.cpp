#include "analysis.h"
#include "config.h"

/* Assertions within the EMD implementation cause it to fail when two distributions are equivalent
 * Removing the assertions via NDEBUG allows these situations to (intuitively) return 0.0
 * cstdef is required for fast_emd headers
 */
#include <cstddef>
#define NDEBUG
#include "fast_emd/emd_hat.hpp"
#include "fast_emd/emd_hat_signatures_interface.hpp"
#undef NDEBUG

double Fuse::Analysis::calculate_uncalibrated_tmd(
		std::vector<std::vector<int64_t> > distribution_one,
		std::vector<std::vector<int64_t> > distribution_two,
		std::vector<std::pair<int64_t, int64_t> > bounds_per_event
		){

	return 0.0;

}

// OLD
/*
double Analysis::calculate_tmd(std::vector<Event> events,
	 // these are now the min and max of the events for each symbol, which will be divided into bins for the histogram
	std::map<std::string,std::vector<std::pair<int64_t,int64_t> > > normalisation_ranges_by_tt,
	std::map<std::string,std::vector<std::vector<int64_t> > > reference_distribution_by_tt,
	std::map<std::string,std::vector<std::vector<int64_t> > > target_distribution_by_tt,
	std::map<std::string,unsigned int> reference_task_count_by_task_tt,
	Event_statistics& local_statistics,
	double calibration_tmd){

	LOG(DEBUG) << "Calculating EMD for " << events.size() << " events: " << Helpers::vector_to_string(events);

	std::map<std::string,double> tmds_by_symbol;
	std::map<std::string,uint64_t> num_tasks_by_symbol;

	LOG(DEBUG) << "There are " << reference_distribution_by_tt.size() << " reference symbols, and " << target_distribution_by_tt.size() << " target symbols";

	std::map<std::string,std::vector<std::vector<int64_t> > >::iterator symbol_iter;
	for(symbol_iter = target_distribution_by_tt.begin(); symbol_iter != target_distribution_by_tt.end(); symbol_iter++){

		std::string symbol = symbol_iter->first;
		std::vector<std::vector<int64_t> > reference_distribution = reference_distribution_by_tt[symbol];
		std::vector<std::vector<int64_t> > target_distribution = target_distribution_by_tt[symbol];
		std::vector<std::pair<int64_t,int64_t> > normalisation_ranges = normalisation_ranges_by_tt[symbol];
		unsigned int num_reference_tasks_in_single_execution = reference_task_count_by_task_tt[symbol];

		LOG(DEBUG) << "For symbol " << symbol_iter->first << " there are " << num_reference_tasks_in_single_execution << " reference tasks in single distribution.";

		std::vector<double> reference_bin_ranges;
		std::vector<double> target_bin_ranges;

		signature_tt<double> reference_signature = convert_distribution_to_bounded_signature(reference_distribution,normalisation_ranges, reference_bin_ranges, reference_distribution.size());
		signature_tt<double> target_signature = convert_distribution_to_bounded_signature(target_distribution,normalisation_ranges, target_bin_ranges, num_reference_tasks_in_single_execution);

		LOG(DEBUG) << "Working on target distribution symbol " << symbol << " that has " << reference_distribution.size() << " tasks in the reference and " << target_distribution.size() << " tasks in the target";
		LOG(DEBUG) << "The number of populated reference bins is " << reference_signature.n << ", target bins is " << target_signature.n;

		if((reference_signature.n <= 0) || (target_signature.n <= 0)){
			LOG(INFO) << "At least one of the signatures has 0 or less populated bins, so ignoring the this task type (" << symbol << ").";
			continue;
		}

		double extra_mass_penalty = 0.0;

		double emd_result = emd_hat_signature_interface<double>(&reference_signature, &target_signature, distance_calculation, extra_mass_penalty);
		LOG(DEBUG) << "Uncalibrated TMD result for " << symbol << " was " << emd_result;

		double tmd_result = emd_result / calibration_tmd;

		if(!std::isnan(tmd_result)){

			tmds_by_symbol[symbol] = tmd_result;
			num_tasks_by_symbol.insert(std::make_pair(symbol, target_distribution.size()));
			LOG(DEBUG) << "Calibrated TMD result for " << symbol << " was " << tmd_result;

		}

		delete[](reference_signature.Features);
		delete[](reference_signature.Weights);
		delete[](target_signature.Features);
		delete[](target_signature.Weights);

	}

	if(tmds_by_symbol.size() == 0)
		return -1.0;

	std::vector<double> tmds;
	std::vector<double> weights;

	for(std::map<std::string,double>::iterator iter = tmds_by_symbol.begin(); iter != tmds_by_symbol.end(); iter++){

		tmds.push_back(iter->second);
		weights.push_back((double)num_tasks_by_symbol[iter->first]);

	}

	if(tmds.size() == 1){
		return tmds.at(0);
	}

	// all weights are the same currently
	double weighted_geometric_mean_tmd = Helpers::weighted_geometric_mean(tmds,weights);

	return weighted_geometric_mean_tmd;

}
*/


