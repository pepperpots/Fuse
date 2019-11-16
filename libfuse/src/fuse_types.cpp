#include "fuse_types.h"

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
			minimal));

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
			fmt::format("Could not resolve strategy '{}' to a supported combination strategy.", strategy_string));

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
						static_cast<int>(strategy)));
	};

}
