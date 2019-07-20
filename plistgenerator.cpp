#include "plistgenerator.h"
#include "stringbuilder.h"
#include <stdio.h>
#include <sys/stat.h>

static const char * header_text = R"TEXT(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
)TEXT";

static const char * footer_text = R"TEXT(</dict>
</plist>
)TEXT";

namespace chibi
{
	bool generate_plist(
		const char * template_filename,
		const char * app_name,
		const int flags,
		const char * output_filename)
	{
		bool result = false;

		FILE * template_file = nullptr;
		char * template_text = nullptr;
		size_t template_size = 0;

		StringBuilder sb;

		FILE * output_file = nullptr;

		if (template_filename != nullptr)
		{
			template_file = fopen(template_filename, "rt");

			if (template_file == nullptr)
				goto cleanup;

			struct stat buffer;
			if (fstat(fileno(template_file), &buffer) != 0)
				goto cleanup;
		
			template_size = (size_t)buffer.st_size;

			template_text = new char[template_size + 1];
			
			if (fread(template_text, 1, template_size, template_file) != template_size)
				goto cleanup;

			template_text[template_size] = 0;

			fclose(template_file);
			template_file = nullptr;
		}

		// add header

		sb.Append(header_text);

		// add template text

		if (template_text != nullptr)
		{
			sb.Append(template_text);
			sb.Append("\n");
		}

		// add the executable name key. this is needed for MacOS to find the executable
		// within an app bundle in case the app bundle gets renamed

		sb.Append("<key>CFBundleExecutable</key>\n");
		sb.AppendFormat("<string>%s</string>\n", app_name);

		// add keys based on flags

		if (flags & kPlistFlag_HighDpi)
		{
			sb.Append("<key>NSHighResolutionCapable</key>\n");
			sb.Append("<true/>\n");
		}

		if (flags & kPlistFlag_AccessMicrophone)
		{
			sb.Append("<key>NSMicrophoneUsageDescription</key>\n");
			sb.Append("<string>This application needs access to your Microphone.</string>\n");
		}

		if (flags & kPlistFlag_AccessWebcam)
		{
			sb.Append("<key>NSCameraUsageDescription</key>\n");
			sb.Append("<string>This application needs access to your Camera.</string>\n");
		}
		
		sb.Append("<key>LSMinimumSystemVersion</key>\n");
		sb.Append("<string>10.11</string>\n");

		// add footer

		sb.Append(footer_text);

		// write output file

		output_file = fopen(output_filename, "wt");

		if (output_file != nullptr)
		{
			if (fwrite(sb.text.c_str(), sb.text.size(), 1, output_file) != 1)
				goto cleanup;
			
			fclose(output_file);
			output_file = nullptr;

			result = true;
		}

	cleanup:
		if (output_file != nullptr)
		{
			fclose(output_file);
			output_file = nullptr;
		}

		delete [] template_text;
		template_text = nullptr;

		if (template_file != nullptr)
		{
			fclose(template_file);
			template_file = nullptr;
		}

		return result;
	}
}
