#pragma once
#include <functional>
#include <string>
#include <vector>
extern std::vector<std::vector<std::string>> g_log;
extern std::function<std::string(const std::vector<std::string>&, bool&)> g_hook;
extern std::vector<unsigned char> g_photo; extern int g_photo_w, g_photo_h;
std::string mock_invoke(const std::vector<std::string>& words);
bool mock_has_command(const std::string& n);
void mock_set_var(const std::string& n, const std::string& v);
std::string mock_get_var(const std::string& n);
