#ifndef FUSE_STATISTICS_H
#define FUSE_STATISTICS_H

#include "fuse_types.h"

#include <gmp.h>
#include <mpfr.h> // mpfr required for float exponents
#include <mpf2mpfr.h> // redefines already-written (GMP) mpf code for mpfr

#include <map>
#include <string>

namespace Fuse {

	struct Running_stats {
		mpf_t n;
		mpf_t old_m;
		mpf_t new_m;
		mpf_t old_s;
		mpf_t new_s;

		double min;
		double max;
	};

	struct Stats {
		double min;
		double max;
		double mean;
		double std;
	};

	Fuse::Stats calculate_stats_from_values(std::vector<double> values);
	double calculate_median_from_values(std::vector<double> values);
	double calculate_weighted_geometric_mean(std::vector<double> samples, std::vector<double> weights);

	class Statistics {

		private:
			std::string statistics_filename;
			std::map<Fuse::Symbol, std::map<Fuse::Event,Fuse::Running_stats> > running_stats_by_symbol;
			std::map<Fuse::Symbol, std::map<Fuse::Event,Fuse::Stats> > stats_by_symbol;
			bool modified;

		public:
			Statistics(std::string statistics_filename);

			void add_event_value(
				Fuse::Event event,
				int64_t value,
				Fuse::Symbol symbol
			);

			void calculate_statistics_from_running();

			std::pair<int64_t,int64_t> get_bounds(
				Fuse::Event event,
				Fuse::Symbol symbol = Fuse::Symbol("all_symbols")
			);
			double get_mean(
				Fuse::Event event,
				Fuse::Symbol symbol = Fuse::Symbol("all_symbols")
			);
			double get_std(
				Fuse::Event event,
				Fuse::Symbol symbol = Fuse::Symbol("all_symbols")
			);

			std::vector<Fuse::Symbol> get_unique_symbols();

			void load();
			void save();

		private:
			void save_stats_for_symbol(
				Fuse::Event event,
				Fuse::Symbol symbol,
				double min,
				double max,
				double mean,
				double std
			);
	};

}

#endif
