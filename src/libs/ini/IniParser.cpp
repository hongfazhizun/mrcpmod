#include "IniParser.h"

/*IniParser::IniParser(string filename)
{
    mIni = iniparser_load(filename.c_str());
}*/

IniParser::IniParser()
{
}

IniParser::~IniParser()
{
}

int IniParser::setFileName(string fileName)
{
    boost::property_tree::ini_parser::read_ini(fileName, mPt);
    mFileName = fileName;
    return 0;
}

int IniParser::get(string section, string name, string& value)
{
    std::ostringstream oss;
    oss << section << "." << name;
    value = mPt.get<std::string>(oss.str(), "");
    return 0;
}

int IniParser::get(string section, string name, bool& value)
{
    std::ostringstream oss;
    oss << section << "." << name;
    value = mPt.get<bool>(oss.str(), false);
    return 0;
}

int IniParser::get(string section, string name, int& value)
{
    std::ostringstream oss;
    oss << section << "." << name;
    value = mPt.get<int>(oss.str(), 0);
    return 0;
}

int IniParser::get(string section, string name, double& value)
{
    std::ostringstream oss;
    oss << section << "." << name;
    value = mPt.get<double>(oss.str(), 0.0);
    return 0;
}

int IniParser::reloadContent(string fileName)
{
    mPt.clear();
    boost::property_tree::ini_parser::read_ini(fileName, mPt);
    return 0;
}

map<string, map<string, string>> IniParser::get()
{
    map<string, map<string, string>> sections;
    auto begin = mPt.begin();
    for (auto it = mPt.begin(); it != mPt.end(); it++) {
        map<string, string> pairs;
        string section = it->first;
        auto val = it->second;
        for (auto it = val.begin(); it != val.end(); it++) {
            string name = it->first;
            auto val = it->second;
            pairs[name] = val.data();
        }
        sections[section] = pairs;
    }
    return sections;
}

bool IniParser::updateItem(string section, string name, string value)
{
    std::ostringstream oss;
    oss << section << "." << name;
    mPt.put<string>(oss.str(), value);
    write_ini(mFileName, mPt);
    return true;
}
