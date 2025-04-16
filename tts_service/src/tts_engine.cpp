#include "tts_engine.h"
#include <pybind11/embed.h>
#include <iostream>
#include <fstream>

namespace py = pybind11;
using namespace py::literals;

bool run_open_tts_to_file(const std::string& text, const std::string& output_path) {
    try {
        py::scoped_interpreter guard{};
        py::module sys = py::module::import("sys");
        sys.attr("path").attr("insert")(1, "../python");

        py::module tts_module = py::module::import("tts_wrapper");
        py::object result = tts_module.attr("synthesize")(text, output_path);

        std::cout << "TTS output saved to: " << result.cast<std::string>() << std::endl;
        return true;
    } catch (const py::error_already_set& e) {
        std::cerr << "Python error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "C++ exception: " << e.what() << std::endl;
        return false;
    }
}

std::vector<uint8_t> run_open_tts_to_memory(const std::string& text, const std::string& voice) {
    try {
        py::scoped_interpreter guard{};
        py::module sys = py::module::import("sys");
        sys.attr("path").attr("insert")(1, "../python");

        py::module tts_module = py::module::import("tts_wrapper");
        py::object result = tts_module.attr("synthesize_to_memory")(text, voice);

        // Python bytes → C++ vector<uint8_t>
        py::bytes audio_bytes = result.cast<py::bytes>();
        std::string audio_str = audio_bytes;

        return std::vector<uint8_t>(audio_str.begin(), audio_str.end());
    } catch (const py::error_already_set& e) {
        std::cerr << "Python error: " << e.what() << std::endl;
        return {};
    } catch (const std::exception& e) {
        std::cerr << "C++ exception: " << e.what() << std::endl;
        return {};
    }
}
