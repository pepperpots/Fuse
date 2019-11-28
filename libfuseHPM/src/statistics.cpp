#include "statistics.h"
#include "fuse_types.h"
#include "util.h"

#include <gmp.h>
#include <mpfr.h> // mpfr required for float exponents
#include <mpf2mpfr.h> // redefines already-written (GMP) mpf code for mpfr
#include "spdlog/spdlog.h"

#include <numeric>
#include <ostream>
#include <fstream>
#include <map>
#include <string>

Fuse::Statistics::Statistics(std::string statistics_filename):
		statistics_filename(statistics_filename),
		modified(false){

}

std::vector<Fuse::Symbol> Fuse::Statistics::get_unique_symbols(bool include_runtime){

	std::vector<Fuse::Symbol> symbols;
	symbols.reserve(this->stats_by_symbol.size()-1);
	for(auto symbol_iter : this->stats_by_symbol){

		if(include_runtime == false && symbol_iter.first == "runtime")
			continue;

		if(symbol_iter.first != "all_symbols")
			symbols.push_back(symbol_iter.first);
	}

	return symbols;
}

void Fuse::Statistics::load(){

	spdlog::debug("Loading statistics from {}.", this->statistics_filename);

	std::ifstream in(this->statistics_filename);

	if(in.is_open() == false){
		spdlog::debug("There were no event statistics to load.");
		return;
	}

	std::string header;
	in >> header;

	std::string line;

	while(in >> line){

		auto split_line = Fuse::Util::split_string_to_vector(line,',');

		if(split_line.size() != 11)
			throw std::runtime_error(
				fmt::format("Incorrect number of values in {}. Line was: '{}'",this->statistics_filename, line));

		Fuse::Symbol symbol = split_line.at(0);
		Fuse::Event event = split_line.at(1);

		double min = std::stod(split_line.at(2));
		double max = std::stod(split_line.at(3));
		double mean = std::stod(split_line.at(4));
		double std = std::stod(split_line.at(5));

		this->save_stats_for_symbol(event, symbol, min, max, mean, std);

		Fuse::Running_stats run_stats;

		mpf_init_set_str(run_stats.n, split_line.at(6).c_str(), 10);
		mpf_init_set_str(run_stats.old_m, split_line.at(7).c_str(), 10);
		mpf_init_set_str(run_stats.new_m, split_line.at(8).c_str(), 10);
		mpf_init_set_str(run_stats.old_s, split_line.at(9).c_str(), 10);
		mpf_init_set_str(run_stats.new_s, split_line.at(10).c_str(), 10);
		run_stats.min = min;
		run_stats.max = max;

		auto symbol_iter = this->running_stats_by_symbol.find(symbol);

		if(symbol_iter == this->running_stats_by_symbol.end()){

			std::map<Fuse::Event,Fuse::Running_stats> running_stats_per_event;
			running_stats_per_event.insert(std::make_pair(event, run_stats));
			this->running_stats_by_symbol.insert(std::make_pair(symbol, running_stats_per_event));

		} else {

			auto event_iter = symbol_iter->second.find(event);
			if(event_iter != symbol_iter->second.end())
				throw std::runtime_error(
					fmt::format("Loading statistics for symbol {} and event {}: these statistics already exist.",
					symbol, event));

			symbol_iter->second.insert(std::make_pair(event, run_stats));

		}

	}

	in.close();

}

void Fuse::Statistics::save(){

	if(this->modified == false)
		return;

	this->calculate_statistics_from_running();

	spdlog::debug("Saving statistics to {}.", this->statistics_filename);

	std::ofstream out(this->statistics_filename);

	if(out.is_open() == false)
		throw std::runtime_error(fmt::format("Failed to open {} to save event statistics.", this->statistics_filename));

	out << "symbol,event,minimum,maximum,mean,std,n,old_m,new_m,old_s,new_s\n";
	out << std::fixed;

	for(auto symbol_iter : this->stats_by_symbol){

		for(auto event_iter : symbol_iter.second){

			Fuse::Stats stats = event_iter.second;

			out << symbol_iter.first << ",";
			out << event_iter.first << ",";
			out << stats.min << ",";
			out << stats.max << ",";
			out << stats.mean << ",";
			out << stats.std << ",";

			Fuse::Running_stats run_stats = this->running_stats_by_symbol[symbol_iter.first][event_iter.first];

			out << mpf_get_ui(run_stats.n) << ",";
			out << mpf_get_d(run_stats.old_m) << ",";
			out << mpf_get_d(run_stats.new_m) << ",";
			out << mpf_get_d(run_stats.old_s) << ",";
			out << mpf_get_d(run_stats.new_s) << "\n";

		}

	}

	out.close();

}

void Fuse::Statistics::add_event_value(
		Fuse::Event event,
		int64_t value,
		Fuse::Symbol symbol
		){

	this->modified = true;

	// First, add it as a value for the event across all symbols
	// Then add it as a value for its particular symbol
	std::vector<Fuse::Symbol> add_to_symbols = {"all_symbols", symbol};

	for(auto current_symbol : add_to_symbols){

		auto symbol_iter = this->running_stats_by_symbol.find(current_symbol);

		if(symbol_iter == this->running_stats_by_symbol.end()){

			Fuse::Running_stats stats;
			mpf_init_set_ui(stats.n,1);
			mpf_init_set_d(stats.old_m,value);
			mpf_init_set_d(stats.new_m,value);
			mpf_init_set_d(stats.old_s,0.0);
			mpf_init_set_d(stats.new_s,0.0);

			stats.min = static_cast<double>(value);
			stats.max = static_cast<double>(value);

			std::map<Fuse::Event,Fuse::Running_stats> running_stats_per_event;
			running_stats_per_event.insert(std::make_pair(event, stats));
			this->running_stats_by_symbol.insert(std::make_pair(current_symbol, running_stats_per_event));

		} else {

			auto event_iter = symbol_iter->second.find(event);

			if(event_iter == symbol_iter->second.end()){

				Fuse::Running_stats stats;
				mpf_init_set_ui(stats.n,1);
				mpf_init_set_d(stats.old_m,value);
				mpf_init_set_d(stats.new_m,value);
				mpf_init_set_d(stats.old_s,0.0);
				mpf_init_set_d(stats.new_s,0.0);

				stats.min = static_cast<double>(value);
				stats.max = static_cast<double>(value);

				symbol_iter->second.insert(std::make_pair(event, stats));

			} else {

				Fuse::Running_stats stats = event_iter->second;

				mpf_add_ui(stats.n, stats.n, 1);

				mpf_t val;
				mpf_init_set_d(val, value); // value

				mpf_t parenthesis;
				mpf_init_set_d(parenthesis, 0.0);
				mpf_sub(parenthesis, val, stats.old_m); // (val - old_m)

				mpf_t temp;
				mpf_init_set_d(temp, 0.0);
				mpf_div(temp, parenthesis, stats.n); // (val - old_m) / n
				mpf_add(stats.new_m, stats.old_m, temp); // new_m = old_m + (val - old_m) / n

				mpf_t second_parenthesis;
				mpf_init_set_d(second_parenthesis, 0.0);
				mpf_sub(second_parenthesis, val, stats.new_m); // (val - new_m)

				mpf_mul(temp, parenthesis, second_parenthesis); // (val - old_m)*(val - new_m)
				mpf_add(stats.new_s, stats.old_s, temp); // old_s + (val - old_m)*(val - new_m)

				mpf_set(stats.old_m, stats.new_m);
				mpf_set(stats.old_s, stats.new_s);

				if(static_cast<double>(value) < stats.min)
					stats.min = static_cast<double>(value);

				if(static_cast<double>(value) > stats.max)
					stats.max = static_cast<double>(value);

				event_iter->second = stats;

				mpf_clear(val);
				mpf_clear(parenthesis);
				mpf_clear(second_parenthesis);
				mpf_clear(temp);

			}

		}

	}

}

std::pair<int64_t,int64_t> Fuse::Statistics::get_bounds(
		Fuse::Event event,
		Fuse::Symbol symbol
		){

	auto symbol_iter = this->stats_by_symbol.find(symbol);
	if(symbol_iter == this->stats_by_symbol.end())
		throw std::runtime_error(fmt::format("No bounds exist for symbol {} and event {}.", symbol, event));

	auto event_iter = symbol_iter->second.find(event);
	if(event_iter == symbol_iter->second.end())
		throw std::runtime_error(fmt::format("No bounds exist for symbol {} and event {}.", symbol, event));

	return std::make_pair(static_cast<int64_t>(event_iter->second.min), static_cast<int64_t>(event_iter->second.max));

}

double Fuse::Statistics::get_mean(
		Fuse::Event event,
		Fuse::Symbol symbol
		){

	auto symbol_iter = this->stats_by_symbol.find(symbol);
	if(symbol_iter == this->stats_by_symbol.end())
		throw std::runtime_error(fmt::format("No mean statistic exists for symbol {} and event {}.", symbol, event));

	auto event_iter = symbol_iter->second.find(event);
	if(event_iter == symbol_iter->second.end())
		throw std::runtime_error(fmt::format("No mean statistic exists for symbol {} and event {}.", symbol, event));

	return event_iter->second.mean;

}

double Fuse::Statistics::get_std(
		Fuse::Event event,
		Fuse::Symbol symbol
		){

	auto symbol_iter = this->stats_by_symbol.find(symbol);
	if(symbol_iter == this->stats_by_symbol.end())
		throw std::runtime_error(fmt::format("No std statistic exists for symbol {} and event {}.", symbol, event));

	auto event_iter = symbol_iter->second.find(event);
	if(event_iter == symbol_iter->second.end())
		throw std::runtime_error(fmt::format("No std statistic exists for symbol {} and event {}.", symbol, event));

	return event_iter->second.std;

}

void Fuse::Statistics::calculate_statistics_from_running(){

	spdlog::debug("Calculating event statistics from the running stats.");

	for(auto symbol_iter : this->running_stats_by_symbol){

		for(auto event_iter : symbol_iter.second){

			Fuse::Running_stats stats = event_iter.second;

			if(mpf_get_ui(stats.n) < 2){
				spdlog::warn("Only {} values for symbol '{}' and event '{}' for stats calculation. Variance will be set to 0.0",
					mpf_get_ui(stats.n), event_iter.first, symbol_iter.first);

				this->save_stats_for_symbol(event_iter.first, symbol_iter.first, stats.min, stats.max, stats.min, 0.0);
				continue;
			}

			mpf_t num_samples_minus_one;
			mpf_init_set_ui(num_samples_minus_one, 0);
			mpf_sub_ui(num_samples_minus_one, stats.n, 1);

			mpf_t variance;
			mpf_init_set_d(variance, 0.0);
			mpf_div(variance, stats.new_s, num_samples_minus_one);

			mpf_t std_mpf;
			mpf_init_set_d(std_mpf, 0.0);
			mpf_sqrt(std_mpf, variance);

			double mean = mpf_get_d(stats.new_m);
			double stddev = mpf_get_d(std_mpf);

			this->save_stats_for_symbol(event_iter.first, symbol_iter.first, stats.min, stats.max, mean, stddev);

			mpf_clear(num_samples_minus_one);
			mpf_clear(variance);
			mpf_clear(std_mpf);

		}

	}

}

void Fuse::Statistics::save_stats_for_symbol(
		Fuse::Event event,
		Fuse::Symbol symbol,
		double min,
		double max,
		double mean,
		double std
		){

	Fuse::Stats stats;
	stats.min = min;
	stats.max = max;
	stats.mean = mean;
	stats.std = std;

	auto symbol_iter = this->stats_by_symbol.find(symbol);
	if(symbol_iter == this->stats_by_symbol.end()){

		std::map<Fuse::Event, Fuse::Stats> stats_by_event;
		stats_by_event.insert(std::make_pair(event, stats));
		this->stats_by_symbol.insert(std::make_pair(symbol, stats_by_event));

	} else {

		symbol_iter->second[event] = stats; // Will replace current values

	}
}

// Assuming no need for GMP
Fuse::Stats Fuse::calculate_stats_from_values(std::vector<double> values){

	auto n = values.size();
	if(n == 0)
		throw std::runtime_error("Cannot calculate stats from an empty value vector.");

	std::sort(values.begin(), values.end());
	double min = *values.begin();
	double max = *(values.end()-1);

	double mean = std::accumulate(values.begin(), values.end(), 0.0) / n;

	std::vector<double> variations_from_mean(n);
	std::transform(values.begin(), values.end(), variations_from_mean.begin(), [mean](double x) { return x - mean; });

	double variance = std::inner_product(
		variations_from_mean.begin(),
		variations_from_mean.end(),
		variations_from_mean.begin(),
		0.0
	);

	double std = std::sqrt(variance);

	Fuse::Stats stats;
	stats.min = min;
	stats.max = max;
	stats.mean = mean;
	stats.std = std;

	return stats;

}

double Fuse::calculate_median_from_values(std::vector<double> values){

	auto n = values.size();
	if(n == 0)
		throw std::runtime_error("Cannot calculate median value from an empty value vector.");

	std::sort(values.begin(), values.end());

	if(n % 2 == 0)
		return (values.at((n/2)-1) + values.at((n/2))) / 2.0;
	else
		return values.at((n/2)-1);

}

double Fuse::calculate_weighted_geometric_mean(std::vector<double> samples, std::vector<double> weights){

	mpfr_t product;
	mpfr_init_set_d(product, 1.0, MPFR_RNDD); // prod({sample_i}^{w_i} for i in num_samples)

	mpfr_t weights_summed;
	mpfr_init_set_d(weights_summed, 0.0, MPFR_RNDD); // sum({w_i} for i in num_samples)

	// geomean = product ^ (1 / weights_summed)

	for(decltype(samples.size()) i=0; i<samples.size(); i++){

		mpfr_t sample;
		mpfr_init_set_d(sample, samples.at(i), MPFR_RNDD);

		mpfr_t weight;
		mpfr_init_set_d(weight, weights.at(i), MPFR_RNDD);

		mpfr_add(weights_summed, weights_summed, weight, MPFR_RNDD);

		mpfr_pow(sample, sample, weight, MPFR_RNDD);

		mpfr_mul(product, product, sample, MPFR_RNDD);

		mpfr_clears(sample, weight, (mpfr_ptr) nullptr);
	}

	// Getting (1 / weights_summed)
	mpfr_t exponent_numerator;
	mpfr_init_set_d(exponent_numerator, 1.0, MPFR_RNDD);

	mpfr_t exponent;
	mpfr_init(exponent);
	mpfr_div(exponent, exponent_numerator, weights_summed, MPFR_RNDD);

	// Getting geomean
	mpfr_t geometric_mean;
	mpfr_init(geometric_mean);
	mpfr_pow(geometric_mean, product, exponent, MPFR_RNDD);

	double gmean = mpfr_get_d(geometric_mean, MPFR_RNDD);

	mpfr_clears(product, weights_summed, exponent_numerator, exponent, geometric_mean, (mpfr_ptr) nullptr);

	return gmean;

}



