#include "statistics.h"
#include "fuse_types.h"
#include "util.h"

#include <gmp.h>
#include "spdlog/spdlog.h"

#include <ostream>
#include <fstream>
#include <map>
#include <string>

Fuse::Statistics::Statistics(std::string statistics_filename){

	this->statistics_filename = statistics_filename;

}

void Fuse::Statistics::load(){

	spdlog::debug("Loading statistics from {}.", this->statistics_filename);

	std::ifstream in(this->statistics_filename);

	if(in.is_open() == false){
		spdlog::trace("There were no event statistics to load.");
		return;
	}

	std::string header;
	in >> header;

	std::string line;
	in >> line;

	while(in.good()){

		auto split_line = Fuse::Util::split_string_to_vector(line,',');

		if(split_line.size() != 11)
			throw std::runtime_error(
				fmt::format("Incorrect number of values in {}. Line was: '{}'",this->statistics_filename, line));

		Fuse::Symbol symbol = split_line.at(0);
		Fuse::Event event = split_line.at(1);

		int64_t min = std::stoll(split_line.at(2));
		int64_t max = std::stoll(split_line.at(3));
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
			if(event_iter == symbol_iter->second.end())
				throw std::runtime_error(
					fmt::format("Loading statistics for symbol {} and event {}: these statistics already exist.",
					symbol, event));

			symbol_iter->second.insert(std::make_pair(event, run_stats));

		}

		in >> line;

	}

	in.close();

}

void Fuse::Statistics::save(){

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

			stats.min = value;
			stats.max = value;

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

				stats.min = value;
				stats.max = value;

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

				if(value < stats.min)
					stats.min = value;

				if(value > stats.max)
					stats.max = value;

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

	return std::make_pair(event_iter->second.min, event_iter->second.max);

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
		int64_t min,
		int64_t max,
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

