#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "util.hpp"

constexpr size_t MaxBufferSize = 2048;

std::string util::Format(const char* fmt, ...)
{
	char buffer[MaxBufferSize] = { 0 };

	va_list argptr;
	va_start(argptr, fmt);
	// Standard vsnprintf wrapper
	vsnprintf(buffer, sizeof(buffer), fmt, argptr);
	va_end(argptr);

	return std::string(buffer);
}

std::string util::ChangeExtension(std::string_view view, std::string_view ext)
{
	if (view.empty())
		return "";

	std::string_view path_without_ext = view;
	for (auto it = view.rbegin(); it != view.rend(); it++)
	{
		if (*it == '.')
		{
			path_without_ext = view.substr(0, view.size() - (it - view.rbegin()) - 1);
			break;
		}
	}

	return std::string(path_without_ext) + std::string(ext);
}

bool util::StartsWith(std::string_view str, std::string_view prefix)
{
	if (prefix.size() > str.size())
		return false;

	return str.substr(0, prefix.size()) == prefix;
}

bool util::EndsWith(std::string_view str, std::string_view suffix)
{
	if (suffix.size() > str.size())
		return false;

	return str.substr(str.size() - suffix.size()) == suffix;
}

bool util::Compare(std::string_view str, std::string_view str2)
{
	return str == str2;
}

bool util::Contains(std::string_view str, std::string_view substr)
{
	if (substr.size() > str.size() || str.empty() || substr.empty())
		return false;

	return str.find(substr) != std::string_view::npos;
}
