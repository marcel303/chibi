#include "chibi-internal.h"
#include "stringbuilder.h"

//

#if defined(__GNUC__)
	#define sprintf_s(s, ss, f, ...) snprintf(s, ss, f, __VA_ARGS__)
	#define vsprintf_s(s, ss, f, a) vsnprintf(s, ss, f, a)
	#define strcpy_s(d, ds, s) strcpy(d, s)
	#define sscanf_s sscanf
#endif

//

static void report_error(const char * line, const char * format, ...)
{
	char text[1024];
	va_list ap;
	va_start(ap, format);
	vsprintf_s(text, sizeof(text), format, ap);
	va_end(ap);
	
	//
	
	printf("error: %s\n", text);
}

#include <sys/stat.h>
#include <unistd.h> // todo : platform compatibility

static FILE * f = nullptr;

struct S
{
	S & operator>>(const char * text)
	{
		fprintf(f, "%s", text);
		return *this;
	}
	
	S & operator<<(const char * text)
	{
		fprintf(f, "%s\n", text);
		return *this;
	}
};

static void push_dir(const char * path)
{
// todo : error checks

	mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	chdir(path);
}

static void pop_dir()
{
	chdir("..");
}

static const char * s_androidManifestTemplateForApp =
R"MANIFEST(<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.chibi.generated.lib" android:versionCode="1" android:versionName="1.0" android:installLocation="auto">

	<!-- Tell the system this app requires OpenGL ES 3.1. -->
	<uses-feature android:glEsVersion="0x00030001" android:required="true"/>

	<!-- Tell the system this app works in either 3dof or 6dof mode -->
	<uses-feature android:name="android.hardware.vr.headtracking" android:required="false" />

	<!-- Network access needed for OVRMonitor -->
	<uses-permission android:name="android.permission.INTERNET" />

	<!-- Volume Control -->
	<uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS" />
	<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

	<application android:allowBackup="false" android:label="${appName}">
		<meta-data android:name="com.samsung.android.vr.application.mode" android:value="vr_only"/>
		<!-- launchMode is set to singleTask because there should never be multiple copies of the app running -->
		<!-- Theme.Black.NoTitleBar.Fullscreen gives solid black instead of a (bad stereoscopic) gradient on app transition -->
        <!-- If targeting API level 24+, configChanges should additionally include 'density'. -->
        <!-- If targeting API level 24+, android:resizeableActivity="false" should be added. -->
		<activity
				android:name="com.oculus.sdk.GLES3JNIActivity"
				android:theme="@android:style/Theme.Black.NoTitleBar.Fullscreen"
				android:launchMode="singleTask"
				android:screenOrientation="landscape"
				android:excludeFromRecents="false"
				android:configChanges="screenSize|screenLayout|orientation|keyboardHidden|keyboard|navigation|uiMode">
      <!-- Indicate the activity is aware of VrApi focus states required for system overlays  -->
      <meta-data android:name="com.oculus.vr.focusaware" android:value="true"/>
      <!-- This filter lets the apk show up as a launchable icon. -->
			<intent-filter>
				<action android:name="android.intent.action.MAIN" />
				<category android:name="android.intent.category.LAUNCHER" />
			</intent-filter>
		</activity>
	</application>
</manifest>)MANIFEST";

static const char * s_androidManifestTemplateForLib =
R"MANIFEST(<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.chibi.generated.lib" android:versionCode="1" android:versionName="1.0" android:installLocation="auto">

	<!-- Tell the system this app requires OpenGL ES 3.1. -->
	<uses-feature android:glEsVersion="0x00030001" android:required="true"/>

	<!-- Tell the system this app works in either 3dof or 6dof mode -->
	<uses-feature android:name="android.hardware.vr.headtracking" android:required="false" />

	<!-- Network access needed for OVRMonitor -->
	<uses-permission android:name="android.permission.INTERNET" />

	<!-- Volume Control -->
	<uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS" />
	<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
</manifest>)MANIFEST";

static S s;

namespace chibi
{
	bool handle_library(const ChibiInfo & chibi_info, ChibiLibrary & library, std::set<std::string> & traversed_libraries, std::vector<ChibiLibrary*> & libraries)
	{
	#if 0
		if (library.isExecutable)
			printf("handle_app: %s\n", library.name.c_str());
		else
			printf("handle_lib: %s\n", library.name.c_str());
	#endif
	
		traversed_libraries.insert(library.name);
		
		// recurse library dependencies
		
		for (auto & library_dependency : library.library_dependencies)
		{
			if (traversed_libraries.count(library_dependency.name) != 0)
				continue;
			
			if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
			{
				ChibiLibrary * found_library = chibi_info.find_library(library_dependency.name.c_str());
				
				if (found_library == nullptr)
				{
					report_error(nullptr, "failed to find library dependency: %s for target %s", library_dependency.name.c_str(), library.name.c_str());
					return false;
				}
				else
				{
					if (handle_library(chibi_info, *found_library, traversed_libraries, libraries) == false)
						return false;
				}
			}
			else if (library_dependency.type == ChibiLibraryDependency::kType_Local)
			{
				// nothing to do here
			}
			else if (library_dependency.type == ChibiLibraryDependency::kType_Find)
			{
				// nothing to do here
			}
			else if (library_dependency.type == ChibiLibraryDependency::kType_Global)
			{
				// nothing to do here
			}
			else
			{
				report_error(nullptr, "internal error: unknown library dependency type");
				return false;
			}
		}
		
		libraries.push_back(&library);
		
		return true;
	}

	bool write_gradle_files(const ChibiInfo & chibi_info, const char * output_path)
	{
		// gather the library targets to emit
		
		std::set<std::string> traversed_libraries;
		
		std::vector<ChibiLibrary*> libraries;
		
		for (auto & library : chibi_info.libraries)
		{
			if (traversed_libraries.count(library->name) != 0)
				continue;
			
			if (library->isExecutable && chibi_info.should_build_target(library->name.c_str()))
			{
				if (handle_library(chibi_info, *library, traversed_libraries, libraries) == false)
					return false;
			}
		}
		
		for (auto & library : chibi_info.libraries)
		{
			if (traversed_libraries.count(library->name) != 0)
				continue;
			
			if (chibi_info.should_build_target(library->name.c_str()))
			{
				if (handle_library(chibi_info, *library, traversed_libraries, libraries) == false)
					return false;
			}
		}

		// sort files by name
		
		for (auto & library : libraries)
		{
			std::sort(library->files.begin(), library->files.end(),
				[](const ChibiLibraryFile & a, const ChibiLibraryFile & b)
				{
					return a.filename < b.filename;
				});
		}

		push_dir(output_path);
		
		// todo : generate conglomerate files

		// todo : copy header files aliased through copy

		// todo : generate translation unit linkage files

		// generate root build.gradle file

		{
			f = fopen("build.gradle", "wt");
			{
				s << "buildscript {";
				s << "  repositories {";
				s << "    google()";
				s << "    jcenter()";
				s << "  }";
				s << "  ";
				s << "  dependencies {";
				s << "    classpath 'com.android.tools.build:gradle:3.2.0'";
				s << "  }";
				s << "}";
				s << "";
				s << "allprojects {";
				s << "    repositories {";
				s << "        google()";
				s << "      jcenter()";
				s << "    }";
				s << "}";
			}
			fclose(f);
			f = nullptr;
		}
		
		// generate root settings.gradle file

		{
			f = fopen("settings.gradle", "wt");
			{
				s << "rootProject.name = 'Project'";
				s << "";
				for (auto & library : libraries)
					s >> "include '" >> library->name.c_str() << ":Projects:Android'";
			}
			fclose(f);
			f = nullptr;
		}

		for (auto & library : libraries)
		{
		// todo : add chibi option to set Android app id. if not set, auto-generate one
		// todo : add chibi option to set Android manifest file. if not set, auto-generate one
			std::string appId = std::string("com.chibi.generated.") + library->name;
			for (auto & c : appId)
				if (c == '-')
					c = '_'; // valid Android app ids are [a-zA-Z0-9_]

			push_dir(library->name.c_str());
			push_dir("Projects");
			push_dir("Android");
			{
				// generate build.gradle file for each library and app

				f = fopen("build.gradle", "wt");
				{
					if (library->isExecutable)
						s << "apply plugin: 'com.android.application'";
					else
						s << "apply plugin: 'com.android.library'";
					s << "";
					s << "dependencies {";
					for (auto & library_dependency : library->library_dependencies)
						s >> "  implementation project(':" >> library_dependency.name.c_str() << ":Projects:Android')";
					s << "}";
					s << "";
					s << "android {";
					s << "  // This is the name of the generated apk file, which will have";
					s << "  // -debug.apk or -release.apk appended to it.";
					s << "  // The filename doesn't effect the Android installation process.";
					s << "  // Use only letters to remain compatible with the package name.";
					s >> "  project.archivesBaseName = \"" >> library->name.c_str() << "\"";
					s << "  ";
					s << "  defaultConfig {";
					if (library->isExecutable)
					{
					s << "    // Gradle replaces the manifest package with this value, which must";
					s << "    // be unique on a system.  If you don't change it, a new app";
					s << "    // will replace an older one.";
					s >> "    applicationId \"" >> appId.c_str() << "\"";
					}
					s << "    minSdkVersion 23";
					s << "    targetSdkVersion 25";
					s << "    compileSdkVersion 26";
					s << "    ";
					s >> "    manifestPlaceholders = [appId:\"" >> appId.c_str() >> "\", appName:\"" >> library->name.c_str() << "\"]";
					s << "    ";
					s << "    ";
					s << "    // override app plugin abiFilters for 64-bit support";
					s << "    externalNativeBuild {";
					s << "        ndk {";
					s << "          abiFilters 'arm64-v8a'";
					s << "        }";
					s << "        ndkBuild {";
					s << "          abiFilters 'arm64-v8a'";
					s << "        }";
					s << "    }";
					s << "  }";
					s << "";
					s << "  externalNativeBuild {";
					s << "    ndkBuild {";
					s << "      path 'jni/Android.mk'";
					s << "    }";
					s << "  }";
					s << "";
					s << "  sourceSets {";
					s << "    main {";
					s << "      manifest.srcFile 'AndroidManifest.xml'";
					s << "      java.srcDirs = ['../../../java']"; // fixme : make unshared by copying files
					s << "      jniLibs.srcDir 'libs'";
					s << "      res.srcDirs = ['../../res']";
					s << "      assets.srcDirs = ['../../assets']";
					s << "    }";
					s << "  }";
					s << "";
					s << "  lintOptions{";
					s << "      disable 'ExpiredTargetSdkVersion'";
					s << "  }";
					s << "}";
				}
				fclose(f);
				f = nullptr;

				push_dir("jni");
				{
					// generate Android.mk file for each library

					f = fopen("Android.mk", "wt");
					{
						s << "# auto-generated. do not edit by hand";
						s << "";
						s << "LOCAL_PATH := $(call my-dir)";
						s << "";
						s >> "# -- " << library->name.c_str();
						s << "";
						s << "# note : CLEAR_VARS is a built-in NDK makefile";
						s << "#        which attempts to clear as many LOCAL_XXX";
						s << "#        variables as possible";
						s << "include $(CLEAR_VARS)";
						s << "";
						s >> "LOCAL_MODULE            := " << library->name.c_str();

						// write source files

						s >> "LOCAL_SRC_FILES         :=";
						for (auto & file : library->files)
							s >> " " >> file.filename.c_str();
						s << "";

						// write header paths
						
						s >> "LOCAL_C_INCLUDES        :=";
						for (auto & header_path : library->header_paths)
							if (header_path.expose == false)
								s >> " " >> header_path.path.c_str();
						s << "";
						
						s >> "LOCAL_EXPORT_C_INCLUDES :=";
						for (auto & header_path : library->header_paths)
							if (header_path.expose == true)
								s >> " " >> header_path.path.c_str();
						s << "";
						
					// todo : remoove these hacky LDLIBS
						s << "LOCAL_LDLIBS            := -llog -landroid -lGLESv3 -lEGL";
						s << "";

						// write compile definitions
						
						s >> "LOCAL_CFLAGS            :=";
						for (auto & compile_definition : library->compile_definitions)
							if (compile_definition.expose == false)
							{
								if (compile_definition.value.empty())
									s >> " -D" >> compile_definition.name.c_str();
								else
									s >> " -D" >> compile_definition.name.c_str() >> "=" >> compile_definition.value.c_str();
							}
						if (library->isExecutable && library->resource_path.empty() == false)
							s >> " -DCHIBI_RESOURCE_PATH=\\\"" >> library->resource_path.c_str() >> "\\\"";
						s << "";
						
						s >> "LOCAL_EXPORT_CFLAGS     :=";
						for (auto & compile_definition : library->compile_definitions)
							if (compile_definition.expose == true)
							{
								if (compile_definition.value.empty())
									s >> " -D" >> compile_definition.name.c_str();
								else
									s >> " -D" >> compile_definition.name.c_str() >> "=" >> compile_definition.value.c_str();
							}
						s << "";

						// write library dependencies
						
						s >> "LOCAL_STATIC_LIBRARIES  :=";
						for (auto & library_dependency : library->library_dependencies)
							for (auto & dep_library : libraries)
								if (dep_library->name == library_dependency.name)
									if (dep_library->shared == false)
										s >> " " >> dep_library->name.c_str();
						s << "";

						s >> "LOCAL_SHARED_LIBRARIES  :=";
						for (auto & library_dependency : library->library_dependencies)
							for (auto & dep_library : libraries)
								if (dep_library->name == library_dependency.name)
									if (dep_library->shared == true)
										s >> " " >> dep_library->name.c_str();
						//if (library->isExecutable)
						//	s >> " vrapi"; // todo : remove
						s << "";
						s << "";
						
						s << "LOCAL_CPP_FEATURES      += exceptions";
						s << "LOCAL_CPP_FEATURES      += rtti";
						s << "";

						// include 'build library' script

						if (library->shared || library->isExecutable)
						{
							s << "# note : BUILD_SHARED_LIBRARY is a built-in NDK makefile";
							s << "#        which will generate a shared library from LOCAL_SRC_FILES";
							s << "include $(BUILD_SHARED_LIBRARY)";
						}
						else
						{
							
							s << "# note : BUILD_STATIC_LIBRARY is a built-in NDK makefile";
							s << "#        which will generate a static library from LOCAL_SRC_FILES";
							s << "include $(BUILD_STATIC_LIBRARY)";
						}
						s << "";

						// write library imports

						//s << "$(call import-module,VrApi/Projects/AndroidPrebuilt/jni)"; // todo : remove
						for (auto & library_dependency : library->library_dependencies)
							s >> "$(call import-module," >> library_dependency.name.c_str() << "/Projects/Android/jni)";
						s << "";

						// todo : write license files
					}
					fclose(f);
					f = nullptr;

					// generate Application.mk file for each app

					f = fopen("Application.mk", "wt");
					{
						s >> "NDK_MODULE_PATH := " << output_path;
						s << "APP_ABI         := arm64-v8a";
						s << "APP_STL         := c++_shared";
						s << "APP_DEBUG       := true";
					}
					fclose(f);
					f = nullptr;
				}
				pop_dir();

				// generate AndroidManifest.xml for each library
				
				f = fopen("AndroidManifest.xml", "wt");
				{
					if (library->isExecutable)
						fprintf(f, "%s", s_androidManifestTemplateForApp);
					else
						fprintf(f, "%s", s_androidManifestTemplateForLib);
				}
				fclose(f);
				f = nullptr;
			}
			pop_dir();
			pop_dir();
			pop_dir();
		}
		
		pop_dir();

		return true;
	}
}
