#include "chibi-internal.h"
#include "filesystem.h"
#include "stringbuilder.h"

#include <algorithm>
#include <errno.h>
#include <stdarg.h>

#ifdef WIN32
	#include <direct.h> // _mkdir, _chdir
	#define chdir _chdir
	#define mkdir _mkdir
#else
	#include <sys/stat.h> // mkdir
	#include <unistd.h> // chdir
#endif

// note : we do not overwrite files when they did not change. Gradle/NDK build will rebuild targets when the build files are newer than the output files

//

#define ENABLE_LOGGING 0 // do not alter

#define NB_CMAKE 1 // use a separately generated CMakeLists.txt for building apps
#define NB_NDK   2 // use the NDK build system for building apps and libraries
#define NATIVE_BUILD_TYPE NB_CMAKE

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

struct S : chibi::StringBuilder
{
	S & operator>>(const char * text)
	{
		Append(text);
		return *this;
	}
	
	S & operator<<(const char * text)
	{
		Append(text);
		Append('\n');
		return *this;
	}
};

static S s;

static std::string fn;

static void beginFile(const char * filename)
{
	fn = filename;
}

static bool endFile()
{
	bool result = true;

	if (!chibi_filesystem::write_if_different(s.text.c_str(), fn.c_str()))
	{
		report_error(nullptr, "failed to write file contents");
		result = false;
	}
	
	s.text.clear();
	
	fn.clear();

	return result;
}

static bool push_dir(const char * path)
{
#ifdef WIN32
	if (mkdir(path) != 0 && errno != EEXIST)
	{
		report_error(nullptr, "failed to create directory");
		return false;
	}
#else
	if (mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0 && errno != EEXIST)
	{
		report_error(nullptr, "failed to create directory");
		return false;
	}
#endif
	
	if (chdir(path) != 0)
	{
		report_error(nullptr, "failed to change directory");
		return false;
	}

	return true;
}

static bool pop_dir()
{
	if (chdir("..") != 0)
	{
		report_error(nullptr, "failed to change directory");
		return false;
	}

	return true;
}

// todo : figure out how to generate/specify Manifest files. there's a lot of OculusVR/framework specific stuff in the manifest templates below

static const char * s_androidManifestTemplateForApp =
R"MANIFEST(<?xml version="1.0" encoding="utf-8"?>
<manifest
	xmlns:android="http://schemas.android.com/apk/res/android"
	package="%s"
	android:versionCode="1"
	android:versionName="1.0"
	android:installLocation="auto">

	<!-- Tell the system this app requires OpenGL ES 3.1. -->
	<uses-feature android:glEsVersion="0x00030001" android:required="true"/>

	<!-- Tell the system this app works in either 3dof or 6dof mode -->
	<uses-feature android:name="android.hardware.vr.headtracking" android:required="false" />

	<!-- Tell the system this app can handle tracked remotes and hands -->
	<uses-feature android:name="oculus.software.handtracking" android:required="false" />
	<uses-permission android:name="oculus.permission.handtracking" />

	<!-- Network access needed for OVRMonitor -->
	<uses-permission android:name="android.permission.INTERNET" />

	<!-- Volume Control -->
	<uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS" />
	<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

	<application
		android:allowBackup="false"
		android:fullBackupContent="false"
		android:hasCode="false">

		<meta-data android:name="com.samsung.android.vr.application.mode" android:value="vr_only"/>

		<!-- launchMode is set to singleTask because there should never be multiple copies of the app running -->
		<!-- Theme.Black.NoTitleBar.Fullscreen gives solid black instead of a (bad stereoscopic) gradient on app transition -->
        <!-- If targeting API level 24+, configChanges should additionally include 'density'. -->
        <!-- If targeting API level 24+, android:resizeableActivity="false" should be added. -->
        <!-- todo : add support to libraries to define a native activity -->
        <!-- note : com.oculus.sdk.GLES3JNIActivity for the Oculus VR native activity -->
        <!-- note : android.app.NativeActivity for the native app glue activity -->
		<activity
				android:name="android.app.NativeActivity"
				android:theme="@android:style/Theme.Black.NoTitleBar.Fullscreen"
				android:launchMode="singleTask"
				android:screenOrientation="landscape"
				android:excludeFromRecents="false"
				android:configChanges="screenSize|screenLayout|orientation|keyboardHidden|keyboard|navigation|uiMode">

			<meta-data android:name="android.app.lib_name" android:value="%s" />

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
<manifest
	xmlns:android="http://schemas.android.com/apk/res/android"
	package="%s"
	android:versionCode="1"
	android:versionName="1.0"
	android:installLocation="auto">

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

static char id_buffer[256];
static const char * make_valid_id(const char * id)
{
	int i = 0;
	while (id[i] != 0)
	{
		if (id[i] == '-' || id[i] == '.')
			id_buffer[i] = '_';
		else
			id_buffer[i] = id[i];
		++i;
	}
	id_buffer[i++] = 0;
	return id_buffer;
}

static const char * libstrip_name(const char * name)
{
	// Android NDK build has the annoying behavior to strip 'lib' from the target name and later add it again for all targets
	// I haven't been able to make it preserve the lib prefix, so instead we will remove it where needed to mirror this behavior
	if (memcmp(name, "lib", 3) == 0)
		return name + 3;
	else
		return name;
}

#if NATIVE_BUILD_TYPE != NB_CMAKE
	#include <stack>
#endif

namespace chibi
{
#if NATIVE_BUILD_TYPE != NB_CMAKE
	static bool gather_all_library_dependencies(const ChibiInfo & chibi_info, const ChibiLibrary & library, std::vector<ChibiLibraryDependency> & library_dependencies)
	{
		std::set<std::string> traversed_libraries;
		std::deque<const ChibiLibrary*> stack;
		
		stack.push_back(&library);
		
		traversed_libraries.insert(library.name);
		
		while (stack.empty() == false)
		{
			const ChibiLibrary * library = stack.front();
			
			for (auto & library_dependency : library->library_dependencies)
			{
				if (traversed_libraries.count(library_dependency.name) == 0)
				{
					traversed_libraries.insert(library_dependency.name);
					
					library_dependencies.push_back(library_dependency);
					
					if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
					{
						const ChibiLibrary * resolved_library = chibi_info.find_library(library_dependency.name.c_str());
						
						if (resolved_library == nullptr)
						{
							report_error(nullptr, "failed to resolve library dependency: %s for library %s", library_dependency.name.c_str(), library->name.c_str());
							return false;
						}

						stack.push_back(resolved_library);
					}
				}
			}
			
			stack.pop_front();
		}
		
		return true;
	}
	
	static bool generate_translation_unit_linkage_files(const ChibiInfo & chibi_info, const char * generated_path, const std::vector<ChibiLibrary*> & libraries)
	{
		// generate translation unit linkage files

		for (auto * app : libraries)
		{
			if (app->isExecutable == false)
				continue;

			std::vector<ChibiLibraryDependency> all_library_dependencies;
			if (!gather_all_library_dependencies(chibi_info, *app, all_library_dependencies))
				return false;

			std::vector<std::string> link_translation_unit_using_function_calls;
			
			for (auto & library_dependency : all_library_dependencies)
			{
				if (library_dependency.type != ChibiLibraryDependency::kType_Generated)
					continue;

				auto * library = chibi_info.find_library(library_dependency.name.c_str());

				link_translation_unit_using_function_calls.insert(
					link_translation_unit_using_function_calls.end(),
					library->link_translation_unit_using_function_calls.begin(),
					library->link_translation_unit_using_function_calls.end());
			}

			if (link_translation_unit_using_function_calls.empty() == false)
			{
				// generate translation unit linkage file
				
				StringBuilder sb;
			
				sb.Append("// auto-generated. do not hand-edit\n\n");

				for (auto & function_name : link_translation_unit_using_function_calls)
					sb.AppendFormat("extern void %s();\n", function_name.c_str());
				sb.Append("\n");

				sb.Append("void linkTranslationUnits()\n");
				sb.Append("{\n");
				{
					for (auto & function_name : link_translation_unit_using_function_calls)
						sb.AppendFormat("\t%s();\n", function_name.c_str());
				}
				sb.Append("}\n");

				char full_path[PATH_MAX];
				if (!concat(full_path, sizeof(full_path), generated_path, "/translation_unit_linkage-", app->name.c_str(), ".cpp"))
				{
					report_error(nullptr, "failed to create absolute path");
					return false;
				}

				if (!chibi_filesystem::write_if_different(sb.text.c_str(), full_path))
				{
					report_error(nullptr, "failed to write conglomerate file. path: %s", full_path);
					return false;
				}
				
				// add the translation unit linkage file to the list of app files
				
				ChibiLibraryFile file;
				file.filename = full_path;

				app->files.push_back(file);
			}
		}

		return true;
	}
#endif

	static bool handle_library(const ChibiInfo & chibi_info, ChibiLibrary & library, std::set<std::string> & traversed_libraries, std::vector<ChibiLibrary*> & libraries)
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

		if (!push_dir(output_path))
			return false;
		
	#if NATIVE_BUILD_TYPE != NB_CMAKE
		// todo : generate conglomerate files

		// todo : copy header files aliased through copy

		// generate translation unit linkage files
		
		if (generate_translation_unit_linkage_files(chibi_info, output_path, libraries) == false)
			return false;
	#endif

		// generate root build.gradle file

		{
			beginFile("build.gradle");
			{
				s << "buildscript {";
				s << "  repositories {";
				s << "    google()";
				s << "    jcenter()";
				s << "  }";
				s << "  ";
				s << "  dependencies {";
				s << "    classpath 'com.android.tools.build:gradle:4.1.3'";
				s << "  }";
				s << "}";
				s << "";
				s << "allprojects {";
				s << "  repositories {";
				s << "    google()";
				s << "    jcenter()";
				s << "  }";
				s << "}";
			}
			if (!endFile())
				return false;
		}
		
		// generate root settings.gradle file

		{
			beginFile("settings.gradle");
			{
				s << "rootProject.name = 'Project'";
				s << "";
				for (auto & library : libraries)
					s >> "include ':" >> library->name.c_str() << "'";
			}
			if (!endFile())
				return false;
		}

		for (auto & library : libraries)
		{
		// todo : add chibi option to set Android app id. if not set, auto-generate one
		// todo : add chibi option to set Android manifest file. if not set, auto-generate one
			std::string appId = std::string("com.chibi.generated.lib.") + make_valid_id(library->name.c_str());

			if (!push_dir(library->name.c_str()))
				return false;
			{
				// generate build.gradle file for each library and app

				beginFile("build.gradle");
				{
					// disable lint because it's sloooow. this needs to be before 'apply plugin'
					// disabling lint commonly reduces the build time by more than 50%
					s << "tasks.whenTaskAdded { task ->";
    				s << "  if (task.name.equals('lint')) {";
        			s << "    task.enabled = false";
    				s << "  }";

// mergeDebugJniLibFolders
// stripDebugDebugSymbols

				#if 0
					const char * disabledTasks[] =
					{
						"generate%sUnitTestSources",
						"pre%sUnitTestBuild",
						"javaPreCompile%sUnitTest",
						"compile%sUnitTestJavaWithJavac",
						"process%sUnitTestJavaRes",
						"test%sUnitTest",
						"test",
						"check",
						"lint",
						//"transformResourcesWithMergeJavaResFor%s",
						//"process%sJavaRes",
						//"check%sLibraries",
						"compile%sShaders",
						"merge%sShaders",
						//"compile%sSources",
						"prepareLintJar",
						"compile%sRenderscript",
						//"transformClassesAndResourcesWithSyncLibJarsFor%s",
						//"transformNativeLibsWithStripDebugSymbolFor%s",
						//"transformNativeLibsWithMergeJniLibsFor%s",
						//"extract%sAnnotations",
						//"generate%sResValues",
						"compile%sNdk",
						""
					};
					
					const char * configs[] =
					{
						"Debug",
						"Release",
						""
					};

					for (int i = 0; disabledTasks[i][0] != 0; ++i)
					{
						for (int c = 0; configs[c][0] != 0; ++c)
						{
							char taskName[64];
							sprintf_s(taskName, sizeof(taskName), disabledTasks[i], configs[c]);
							s >> "  if (task.name.equals('" >> taskName << "')) { task.enabled = false }";
						}
					}
				#endif

					s << "}";
					s << "";

					if (library->isExecutable)
						s << "apply plugin: 'com.android.application'";
					else
						s << "apply plugin: 'com.android.library'";
					s << "";
					s << "dependencies {";
					// note : we need the dependencies for asset merging to work correctly
					for (auto & library_dependency : library->library_dependencies)
						if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
							s >> "  implementation project(':" >> library_dependency.name.c_str() << "')";
					s << "}";
					s << "";
					s << "android {";
					if (library->isExecutable)
					{
					s << "  // This is the name of the generated apk file, which will have";
					s << "  // -debug.apk or -release.apk appended to it.";
					s << "  // The filename doesn't effect the Android installation process.";
					s << "  // Use only letters to remain compatible with the package name.";
					s >> "  project.archivesBaseName = \"" >> make_valid_id(library->name.c_str()) << "\""; // todo : do we need to fixup archive base name ?
					s << "  ";
					}
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
					s << "    aaptOptions {";
					//s << "      // turn off asset compression for .cache, .png and .ogg files";
					//s << "      noCompress 'cache', 'png', 'ogg'";
					s << "      // turn off asset compression for all files";
					s << "      noCompress ''";
    				s << "    }";
    				s << "    ";
				#if NATIVE_BUILD_TYPE == NB_NDK
					if (library->isExecutable)
					{
					s << "    // override app plugin abiFilters for 64-bit support";
					s << "    externalNativeBuild {";
					s << "        ndk {";
					s << "          //abiFilters 'arm64-v8a'";
					s << "        }";
					s << "        ndkBuild {";
					s << "          //abiFilters 'arm64-v8a'";
					s << "          arguments '-j4'";
					s << "        }";
					s << "    }";
					s << "    ";
					}
				#endif
				#if NATIVE_BUILD_TYPE == NB_CMAKE
					if (library->isExecutable)
					{
					s << "    externalNativeBuild {";
					s << "        cmake {";
					s >> "          targets '" >> library->name.c_str() << "'";
					s << "        }";
					s << "    }";
					s << "    ";
					}
				#endif
					s << "  }";
					s << "";
				#if NATIVE_BUILD_TYPE == NB_NDK
					s << "  externalNativeBuild {";
					s << "    ndkBuild {";
					s << "      path 'jni/Android.mk'";
					s << "    }";
					s << "  }";
				#endif
				#if NATIVE_BUILD_TYPE == NB_CMAKE
					if (library->isExecutable)
					{
					s << "  externalNativeBuild {";
					s << "    cmake {";
					s << "      path '../CMakeLists.txt'";
					s << "      buildStagingDirectory '../cmake-build'";
					s << "    }";
					s << "  }";
					}
				#endif
					s << "";
					s << "  sourceSets {";
					s << "    main {";
					s << "      manifest.srcFile 'AndroidManifest.xml'";
				#if NATIVE_BUILD_TYPE == NB_CMAKE
					s << "      jniLibs.srcDirs = [] // explicitly disable libs dir for prebuilt .so files";
					s << "      jni.srcDirs = [] // explicitly disable NDK build";
      			#endif
					if (library->resource_path.empty() == false)
					s >> "      assets.srcDirs = ['" >> library->resource_path.c_str() << "']";
					s << "    }";
					s << "  }";
					s << "";
					if (library->isExecutable)
					{
					// splits
					s << "  splits {";
					s << "    abi { // splits allows us to build separate APKs for each ABI";
					s << "      enable true // enable splits based on abi";
					s << "      universalApk false // we do not want to also generate a universal APK that includes all ABIs";
					s << "      reset() // clear the default list from all ABIs to no ABIs";
					s << "      // explicitly build for the following ABIs:";
					if (true) // is_platform("android.ovr-mobile")
					s << "      include 'arm64-v8a'";
					else
					s << "      include 'arm64-v8a', 'x86'";
					s << "    }";
					s << "    density {";
					s << "      enable false";
					s << "    }";
					s << "  }";
					s << "  ";
					s << "  buildTypes {";
					s << "    release {";
					// signing config
					s << "      // sign with debug keys, so release builds can be run on physical devices";
					s << "      signingConfig signingConfigs.debug";
					// crunchPngs
					s << "      // crunching pngs takes forever.. we don't want to wait ages when testing on the device";
					s << "      crunchPngs false";
					s << "    }";
					s << "    debug {";
					s << "      ext.enableCrashlytics = false";
					s << "    }";
					s << "  }";
					}
					if (true)
					{
					s << "  lintOptions {";
					s << "      disable 'ExpiredTargetSdkVersion'";
					s << "  }";
					s << "  ";
					}
					s << "}";
				}
				if (!endFile())
					return false;

			#if NATIVE_BUILD_TYPE == NB_NDK
				if (!push_dir("jni"))
					return false;
				{
					// generate Android.mk file for each library

					beginFile("Android.mk");
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
						{
							// the Android NDK build will complain about files it doesn't know how to compile,
							// so we must take care to only include c and c++ source files here
							auto extension = get_path_extension(file.filename, true);
							if (library->prebuilt || (extension == "c" || extension == "cc" || extension == "cpp"))
								s >> " " >> file.filename.c_str();
						#if ENABLE_LOGGING
							else
								printf("silently dropping source file %s, since the NDK build would complain about it not recognizing it", file.filename.c_str());
						#endif
						}
						s << "";

						// write header paths
						
						s >> "LOCAL_C_INCLUDES        :=";
						for (auto & header_path : library->header_paths)
							s >> " " >> header_path.path.c_str();
						s << "";
						
						s >> "LOCAL_EXPORT_C_INCLUDES :=";
						for (auto & header_path : library->header_paths)
							if (header_path.expose == true)
								s >> " " >> header_path.path.c_str();
						s << "";
						
						s >> "LOCAL_LDLIBS            :=";
						for (auto & library_dependency : library->library_dependencies)
							if (library_dependency.type == ChibiLibraryDependency::kType_Global)
								s >> " -l" >> library_dependency.name.c_str();
						s << "";
						s << "";

						// write compile definitions
						
						s >> "LOCAL_CFLAGS            :=";
						for (auto & compile_definition : library->compile_definitions)
							if (compile_definition.value.empty())
								s >> " -D" >> compile_definition.name.c_str();
							else
								s >> " -D" >> compile_definition.name.c_str() >> "=" >> compile_definition.value.c_str();
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
							if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
								for (auto & dep_library : libraries)
									if (dep_library->name == library_dependency.name)
										if (dep_library->shared == false)
											s >> " " >> dep_library->name.c_str();
						if (library->isExecutable)
							s >> " android_native_app_glue";
						s << "";

						s >> "LOCAL_SHARED_LIBRARIES  :=";
						for (auto & library_dependency : library->library_dependencies)
							if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
								for (auto & dep_library : libraries)
									if (dep_library->name == library_dependency.name)
										if (dep_library->shared == true)
											s >> " " >> dep_library->name.c_str();
						s << "";
						
					// todo : completely separate out prebuilt libraries. there are too many differences
						if (library->prebuilt == false)
						{
							s << "LOCAL_CPP_FEATURES      += exceptions";
							s << "LOCAL_CPP_FEATURES      += rtti";
							s << "";
						}

						// include 'build library' script

						if (library->isExecutable)
						{
							s << "# note : BUILD_SHARED_LIBRARY is a built-in NDK makefile";
							s << "#        which will generate a shared library from LOCAL_SRC_FILES";
							s << "include $(BUILD_SHARED_LIBRARY)";
						}
						else
						{
							if (library->prebuilt)
							{
								if (library->shared)
								{
									s << "# note : PREBUILT_SHARED_LIBRARY is a built-in NDK makefile";
									s << "#        which will link with the shared library files from LOCAL_SRC_FILES";
									s << "include $(PREBUILT_SHARED_LIBRARY)";
								}
								else
								{
									
									s << "# note : PREBUILT_STATIC_LIBRARY is a built-in NDK makefile";
									s << "#        which will link with the static library files from LOCAL_SRC_FILES";
									s << "include $(PREBUILT_STATIC_LIBRARY)";
								}
							}
							else
							{
								if (library->shared)
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
							}
						}
						s << "";

						// write library imports

						if (library->library_dependencies.empty() == false)
						{
							for (auto & library_dependency : library->library_dependencies)
								if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
									s >> "$(call import-module," >> library_dependency.name.c_str() << "/jni)";
							s << "";
						}
						
						if (library->isExecutable)
						{
							s >> "$(call import-module,android/native_app_glue)";
							s << "";
						}
						
						// todo : write license files
					}
					if (!endFile())
						return false;

					// generate Application.mk file for each app

					beginFile("Application.mk");
					{
						s >> "NDK_MODULE_PATH := " << output_path;
						s << "APP_STL         := c++_shared";
						s << "APP_DEBUG       := true";
					}
					if (!endFile())
						return false;
				}
				if (!pop_dir())
					return false;
			#endif

				// generate AndroidManifest.xml for each library
				
				beginFile("AndroidManifest.xml");
				{
					if (library->isExecutable)
						s.AppendFormat(s_androidManifestTemplateForApp, appId.c_str(), libstrip_name(library->name.c_str()));
					else
						s.AppendFormat(s_androidManifestTemplateForLib, appId.c_str());
				}
				if (!endFile())
					return false;
			}
			if (!pop_dir())
				return false;
		}
		
		if (!pop_dir())
			return false;

		return true;
	}
}
