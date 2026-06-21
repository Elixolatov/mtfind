#include <iostream>
#include "mtfind_manager.h"


int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "Usage: mtfind <file> <mask>\n";
		return 1;
	}
	std::string filename = argv[1];
	std::string mask = argv[2];

	MtFind mtfind;
	try {
		mtfind.FindMatches(filename, mask);
		//manager.FindMatches(filename, "???");
	}
	catch (const std::exception& e) {
		std::cerr << e.what();
	};
	mtfind.PrintMatches();
}
