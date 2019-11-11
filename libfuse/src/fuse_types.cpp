#include "fuse_types.h"

#include "spdlog/spdlog.h"

#include <string>

Fuse::Strategy Fuse::convert_string_to_strategy(std::string strategy_string){

	if(strategy_string == "random") return Fuse::Strategy::RANDOM;
	if(strategy_string == "random_tt") return Fuse::Strategy::RANDOM_TT;
	if(strategy_string == "ctc") return Fuse::Strategy::CTC;
	if(strategy_string == "lgl") return Fuse::Strategy::LGL;
	if(strategy_string == "bc") return Fuse::Strategy::BC;
	if(strategy_string == "hem") return Fuse::Strategy::HEM;

	throw std::invalid_argument(
			fmt::format("Could not resolve provided strategy '{}' to a supported combination strategy.", strategy_string));

}

std::string Fuse::convert_strategy_to_string(Fuse::Strategy strategy){

	switch(strategy){
		case Fuse::Strategy::RANDOM: return "random";
		case Fuse::Strategy::RANDOM_TT: return "random_tt";
		case Fuse::Strategy::CTC: return "ctc";
		case Fuse::Strategy::LGL: return "lgl";
		case Fuse::Strategy::BC: return "bc";
		case Fuse::Strategy::HEM: return "hem";
		default:
			throw std::logic_error(
					fmt::format("Could not resolve a configured strategy (integer enum value is {}) to a string representation.",
						static_cast<int>(strategy)));
	};

}
