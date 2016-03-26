from conans import ConanFile, CMake
import os

class ImageFlowConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = "littlecms/2.7@lasote/stable", "libpng/1.6.21@lasote/stable", "libjpeg-turbo/1.4.2@lasote/stable" , "giflib/5.1.2@lasote/stable"
    options = {"build_tests": [True, False]}
    generators = "cmake"
    default_options = "build_tests=True", "libjpeg-turbo:shared=False", "libpng:shared=False", "giflib:shared=False", \
   					  "zlib:shared=False", "libcurl:shared=False", "OpenSSL:shared=True", \
   					  "imageflow:shared=True"

    def config(self):
        if self.options.build_tests:
            self.requires("catch/1.3.0@TyRoXx/stable")
            self.requires("libcurl/7.47.1@lasote/stable")
            if self.settings.os != "Windows":  # Not supported in windows
                self.requires("theft/0.2.0@lasote/stable")

    def imports(self):
        self.copy("*.so", dst="bin", src="bin")  # From bin to bin
        self.copy("*.dll", dst="bin", src="bin")  # From bin to bin
        self.copy("*.dylib*", dst="bin", src="lib")  # From lib to bin
        self.copy("*cacert.pem", dst="bin")  # Allows use libcurl with https without problems 
        self.copy("*cacert.pem", dst=".")  # Allows use libcurl with https without problems

    def build(self):
        if not os.path.exists("./build"):
            os.mkdir("./build")
        os.chdir("./build")
        cmake = CMake(self.settings)
        cmake_settings = ""
        if self.options.build_tests:
            cmake_settings += " -DENABLE_TEST=ON"
        self.run('cmake "%s" %s %s' % (self.conanfile_directory, cmake.command_line, cmake_settings))
        self.run('cmake --build . %s' % cmake.build_config)
        self.run('ctest -V -C Release')
        