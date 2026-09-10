#include <string>
#include <cctype> // for std::tolower
#include <sstream> // for std::ostringstream
#include <fstream> // for std::ifstream

std::string toLower(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
}

std::string toString(size_t value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

bool readFile(const std::string& path, std::string& content)
{
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    if (!file)
        return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    content = ss.str();
    return true;
}