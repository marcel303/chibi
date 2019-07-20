#include "plistgenerator.h"
#include "stringbuilder.h"
#include <stdio.h>
#include <sys/stat.h>

#define TEMPLATE_FROM_FILE 0

static const char * header_text = R"TEXT(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
)TEXT";

static const char * body_text = R"TEXT(<key>CFBundleDevelopmentRegion</key>
<string>English</string>
<key>CFBundleGetInfoString</key>
<string>${APPLE_GUI_INFO_STRING}</string>
<key>CFBundleIconFile</key>
<string>${APPLE_GUI_ICON}</string>
<key>CFBundleIdentifier</key>
<string>${APPLE_GUI_IDENTIFIER}</string>
<key>CFBundleInfoDictionaryVersion</key>
<string>6.0</string>
<key>CFBundleLongVersionString</key>
<string>${APPLE_GUI_LONG_VERSION_STRING}</string>
<key>CFBundleName</key>
<string>${APPLE_GUI_BUNDLE_NAME}</string>
<key>CFBundlePackageType</key>
<string>APPL</string>
<key>CFBundleShortVersionString</key>
<string>${APPLE_GUI_SHORT_VERSION_STRING}</string>
<key>CFBundleSignature</key>
<string>????</string>
<key>CFBundleVersion</key>
<string>${APPLE_GUI_BUNDLE_VERSION}</string>
<key>CSResourcesFileMapped</key>
<true/>
<key>NSHumanReadableCopyright</key>
<string>${APPLE_GUI_COPYRIGHT}</string>
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
		std::string & out_text)
	{
		bool result = false;

		FILE * template_file = nullptr;
		char * template_text = nullptr;
		size_t template_size = 0;

		StringBuilder sb;

	#if TEMPLATE_FROM_FILE
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
	#endif

		// add header

		sb.Append(header_text);
		
		// add body
		
		sb.Append(body_text);

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
		
		// done!

		out_text = sb.text;
		
		result = true;

	cleanup:
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
