# chibi build system
## chibi at a glance
chibi is a small, pragmatic, specification-based project file generator. chibi is built with the following design principles in mind:

* App and library specifications should use a minimal description. No turing complete language, and more control to the end user and to thw build system.
* The user should be in control of the environment. It is no longer up to the libraries and apps to dictate how projects are organized or how apps are compiled and packaged. Some control is there (like how to organize target files into folders); but the control ends there.
* chibi should embrace modern workflows using Git and open source collaborations. It should be trivial to compile and reference third party libraries directly from source, even if these libraries are checked out and stored somewhere else on disk. Such workflows help to foster open source collaborations, by making it a superior workflow as opposed to using system-wide precompiled and preinstalled libraries.

Below the surface chibi leverages CMake to generate project files. The enormous benefit of using CMake as an intermediate language is that chibi is able to leverage CMake's outstanding project/solution file generation, dependency tracking, and other such niceties, which have a proven track record of being a stable and robust system for developing and compiling code.

## A specification-based language
The minimal description principle means no turing complete programming language, where the app or library developer is invited (read: tempted) to specify _how_ to build things, instead of _what_ should be built. It's up to chibi and the user (through setting up the environment) to control _how_ things are built. This makes chibi a more robust, cross platform solution, and makes possible for chibi to facilitate the building of redistributable packages and to invest in workflow optimizations. It also enables chibi to optimize common tasks, such as copying resources into app bundles, and fixing up rpaths and packaging of shared libraries where the executable can find them. Also things like setting up a compiler cache (which is a great help when switching branches frequently; see the modern workflow section below) are trivial to do when the build system has an internal representation of things to build. Last but not least, having a minimal description of what to build, minimizes complexity and maximizes future longevity.

## User control: workspaces
todo: user control

## Modern workflows
chibi separates the specification of apps and libraries, from where to find these specifications. This is different from say CMake, which requires one to explicitly include references to other CMakeLists.txt files or packages. In addition, library authors often end up making assumptions about the environment, and making a prescription for how to access a library. For instance a lot of libraries contain scripts to install the libraries in order to use them. On Windows users usually have to resort to manually copying and pasting libraries and header files to a location where the compiler can find them. On Linux and OSX these scripts work; but the unanswered question is how to make self contained apps. Installed libraries complicate things when working on projects integrating (possibly different versions of) open source libraries. One often wants to isolate versions, and then you end up copying files around. A better solution exists, which doesn't assume or require the existence of a universally accessible storage space for libraries.

chibi uses a three-step process when generating projects. 

* First, it establishes a workspace. It will look for a chibi-root.txt file, starting at the current working directory, traversing up the file system tree. The top-level chibi-root.txt becomes authorative in setting up the workspace. The workspace defines global options, like whether to include a compiler cache. It also directs chibi to include other workspaces and chibi.txt files.
* The second step involves chibi building up an internal representation of all apps and libraries and how to build them. This step gives chibi a complete view of everything and to resolve dependencies in the next step.
* The third step in the process is to generate the CMakeLists.txt file. During this step, dependencies between targets (app and libraries) are resolved and the CMakeListst.txt file is output.

Since chibi will make the top-level chibi-root.txt authorative, this means it will always be possible to override the workspace in case a repository already defines one.

Suppose you have the following repositories checked out:

```
/Repositories/fancy/
                .../fancyLibrary/
                             .../chibi.txt
                .../fancyExample/
                             .../chibi.txt
                .../chibi-root.txt
/Repositories/myProject/
                .../chibi.txt
```

Perhaps the 'fancy' repository, which contains both a library and an example app, will already define a workspace. A workspace which allows it to build the fancy library and example app.

Suppose now we wish to set up a workspace which will build our own project too. We can do this by adding a chibi-root.txt file inside the Repositories folder.

```
/Repositories/
          .../chibi-root.txt
```

Our own chibi-root.txt file will set up references to the workspace inside the fancy repository and our own project

chibi-root.txt:

```
	add_root fancy
	add myProject
```

myProject/chibi.txt:

```
library myLibrary
	depend_library fancyLibrary
	
app myApp
	depend_library myLibrary
	add_files main.cpp
```

Note that the order in which chibi root files and chibi files are added doesn't matter much, as dependencies are resolved in a later step.

## Using chibi (command line)
todo

## Using chibi as a library
todo
