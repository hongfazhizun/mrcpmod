#pragma once
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <map>

using std::map;
using std::string;

class IniParser {
public:
    // IniParser(string filename);
    IniParser();
    ~IniParser();
    int setFileName(string fileName);
    int get(string section, string name, string& value);
    int get(string section, string name, int& value);
    int get(string section, string name, bool& value);
    int get(string section, string name, double& value);
    int reloadContent(string fileName);
    map<string, map<string, string>> get();

    bool updateItem(string section, string name, string value);

private:
    boost::property_tree::ptree mPt;
    string mFileName;
};
