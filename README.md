Nupic.Base is a very slim cpp library containing mostly the htm algorithms. It's cpp only with very few dependencies:

* boost filesytem

No python is needed!


Changes to Numenta's nupic.core

* removed bindings/
* removed external/common/
* removed external/windows32-gcc
* removed external/windows64
* removed external/windows64-gcc

* revamped cmake script

* all dependencies need to be supplied, see cmake script




# Building under windows using Visual Studio 2017
##Prerequisites are 
- Visual Studio 2017, or newer (free Community version is ok)
	- on X64 OS (Windows 7, 8, or 10) 64 bit
	- with Python Development 
	- with .NET desktop development  (for C#) 
	- with Desktop development with C++  
	   (be sure to check sub component "Visual C++ tools for CMake")
	- with Universal Windows Platform Development
	- Also note that Visual Studio 2015 will also work.
- CMake 3.11 or newer
     Add path to cmake.exe (i.e. C:\Program Files\CMake\bin) to your path environment variable.
- Python 3.6 or newer  (must be 64bit version)  https://www.python.org/downloads/windows/
  (the path to the file Python36.dll must be in your path environment variable  "C:\Program Files (x86)\Microsoft Visual Studio\Shared\Python36_64")
  Note: Anaconda Python that comes with Visual Studio 2017 may not work.
- numpy, needed for some of the Python testing.
	To install, type this is in a Command prompt which was started with run-as-administrator.
	pip install numpy

##Procedures
1 Clone the community version of ['nupic.cpp' repository](https://github.com/htm-community/nupic.cpp)
  You may want to install git to perform this job.

2 Create the Visual Studio solution and project files (.sln and .vcxproj)
Run as administrator 'Visual Studio 2017 Developer version' of Command Prompt (a shortcut is in your start menu). 
CD to the external/Windows under the repository. Execute build.bat


**Note: This can take a while the first time you run this because it needs to download, build and install some prerequiete packages. If there is a major change to CMake structure you may need to start over with this step, clear all files/folders from build/ folder before running the build.bat command again.

3) Now you can start Visual Studio by double clicking the solution file at build/nupic.base.sln.  It will setup its configuration based on CMake.  
4) Build the "ALL_BUILD" project.  This will build all of the libraries.
5) Build the "INSTALL" project.  This will put the products in build/bin (unless you specified elsewhere on the command line in step 2.)


