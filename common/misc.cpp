#include "misc.hpp"


int RandomID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    return dis(gen);
}

std::vector<std::string> strSplit(const std::string& strString, std::string strDelimited, int iMax) {
	std::vector<std::string> vcOut;
	std::string strTemp = strString;
	while (iMax-- > 0) {
		size_t newpos = strTemp.find(strDelimited);
		if (newpos == std::string::npos) {
			if (strTemp.size() > 0) {
				vcOut.push_back(strTemp);
			}
			break;
		}
		std::string temp = strTemp.substr(0, newpos);
		vcOut.push_back(temp);

		strTemp.erase(0, newpos + strDelimited.size());
	}
	return vcOut;
}