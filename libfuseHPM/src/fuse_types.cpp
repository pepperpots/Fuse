#include "fuse_types.h"
#include "util.h"

#include "spdlog/spdlog.h"

#include <string>

Fuse::Strategy Fuse::convert_string_to_strategy(std::string strategy_string, bool minimal){

	if(strategy_string == "random") return minimal ? Fuse::Strategy::RANDOM_MINIMAL : Fuse::Strategy::RANDOM;
	if(strategy_string == "random_tt") return minimal ? Fuse::Strategy::RANDOM_TT_MINIMAL : Fuse::Strategy::RANDOM_TT;
	if(strategy_string == "ctc") return minimal ? Fuse::Strategy::CTC_MINIMAL : Fuse::Strategy::CTC;
	if(strategy_string == "lgl") return minimal ? Fuse::Strategy::LGL_MINIMAL : Fuse::Strategy::LGL;

	if(strategy_string == "hem"){
		if(minimal)
			std::runtime_error("Combination strategy HEM cannot be performed with minimal profiles.");
		else
			return Fuse::Strategy::HEM;
	}

	if(strategy_string == "bc"){
		if(minimal)
			std::runtime_error("Combination strategy BC cannot be performed with minimal profiles.");
		else
			return Fuse::Strategy::BC;
	}

	throw std::invalid_argument(
		fmt::format("Could not resolve provided strategy '{}' with minimal={} to a supported combination strategy.",
			strategy_string,
			minimal)
	);

}

Fuse::Strategy Fuse::convert_string_to_strategy(std::string strategy_string){

	if(strategy_string == "random") return Fuse::Strategy::RANDOM;
	if(strategy_string == "random_minimal") return Fuse::Strategy::RANDOM_MINIMAL;
	if(strategy_string == "random_tt") return Fuse::Strategy::RANDOM_TT;
	if(strategy_string == "random_tt_minimal") return Fuse::Strategy::RANDOM_TT_MINIMAL;
	if(strategy_string == "ctc") return Fuse::Strategy::CTC;
	if(strategy_string == "ctc_minimal") return Fuse::Strategy::CTC_MINIMAL;
	if(strategy_string == "lgl") return Fuse::Strategy::LGL;
	if(strategy_string == "lgl_minimal") return Fuse::Strategy::LGL_MINIMAL;

	if(strategy_string == "hem") return Fuse::Strategy::HEM;
	if(strategy_string == "bc") return Fuse::Strategy::BC;

	throw std::invalid_argument(
		fmt::format("Could not resolve strategy '{}' to a supported combination strategy.", strategy_string)
	);

}

std::string Fuse::convert_strategy_to_string(Fuse::Strategy strategy){

	switch(strategy){
		case Fuse::Strategy::RANDOM: return "random";
		case Fuse::Strategy::RANDOM_MINIMAL: return "random_minimal";
		case Fuse::Strategy::RANDOM_TT: return "random_tt";
		case Fuse::Strategy::RANDOM_TT_MINIMAL: return "random_tt_minimal";
		case Fuse::Strategy::CTC: return "ctc";
		case Fuse::Strategy::CTC_MINIMAL: return "ctc_minimal";
		case Fuse::Strategy::LGL: return "lgl";
		case Fuse::Strategy::LGL_MINIMAL: return "lgl_minimal";
		case Fuse::Strategy::BC: return "bc";
		case Fuse::Strategy::HEM: return "hem";
		default:
			throw std::logic_error(
					fmt::format("Could not resolve a configured strategy (integer enum value is {}) to a string representation.",
						static_cast<int>(strategy))
			);
	};

}

Fuse::Accuracy_metric Fuse::convert_string_to_metric(std::string metric_string){

	if(metric_string == "epd") return Fuse::Accuracy_metric::EPD;
	if(metric_string == "epd_tt") return Fuse::Accuracy_metric::EPD_TT;
	if(metric_string == "spearmans") return Fuse::Accuracy_metric::SPEARMANS;

	throw std::invalid_argument(
		fmt::format("Could not resolve metric '{}' to a supported accuracy metric.", metric_string)
	);

}

std::string Fuse::convert_metric_to_string(Fuse::Accuracy_metric metric){

	switch(metric){
		case Fuse::Accuracy_metric::EPD: return "epd";
		case Fuse::Accuracy_metric::EPD_TT: return "epd_tt";
		case Fuse::Accuracy_metric::SPEARMANS: return "spearmans";
		default:
			throw std::logic_error(
				fmt::format("Could not resolve a configured metric (integer enum value is {}) to a string representation.",
					static_cast<int>(metric))
			);
	};

}

std::vector<int> Fuse::convert_label_str_to_label(std::string label_str){

	label_str = label_str.substr(1,label_str.size()); // assuming label of form "[int,int,...]"
	label_str = label_str.substr(0,label_str.size()-1);

	auto label_str_vector = Fuse::Util::split_string_to_vector(label_str, '-');

	std::vector<int> label_vec;
	if(label_str_vector.front() == ""){
		label_vec.push_back(0 - std::stoi(label_str_vector.at(1)));
	} else {
		label_vec.reserve(label_str_vector.size());
		for(auto rank_str : label_str_vector)
			label_vec.push_back(std::stoi(rank_str));
	}

	return label_vec;
}

std::string Fuse::convert_runtime_to_string(Fuse::Runtime runtime){

	std::string runtime_str;
	if(runtime == Fuse::Runtime::OPENSTREAM)
		runtime_str = "openstream";
	else if(runtime == Fuse::Runtime::OPENMP)
		runtime_str = "openmp";
	else if(runtime == Fuse::Runtime::ALL)
		runtime_str = "unknown";
	else
		throw std::logic_error(
				fmt::format("Could not resolve a runtime (integer enum value is {}) to a string representation.",
					static_cast<int>(runtime)));

	return runtime_str;
}

Fuse::Runtime Fuse::convert_string_to_runtime(std::string runtime_str){

	if(runtime_str == "openstream")
		return Fuse::Runtime::OPENSTREAM;
	else if(runtime_str == "openmp")
		return Fuse::Runtime::OPENMP;
	else if(runtime_str == "openmp")
		return Fuse::Runtime::ALL;
	else
		throw std::invalid_argument(
				fmt::format("Runtime '{}' is not supported. Requires 'openstream' or 'openmp'.",
					static_cast<std::string>(runtime_str)));

}
