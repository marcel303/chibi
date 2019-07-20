#pragma once

#include <string>

namespace chibi
{
	enum PlistFlags
	{
		kPlistFlag_HighDpi = 1 << 0,
		kPlistFlag_AccessMicrophone = 1 << 1,
		kPlistFlag_AccessWebcam = 1 << 2
	};

	bool generate_plist(
		const char * template_filename,
		const char * app_name,
		const int flags,
		std::string & out_text);
}
