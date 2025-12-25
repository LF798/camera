#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "hv_event_writer.h"

namespace py = pybind11;

PYBIND11_MODULE(hv_event_writer_python, m) {
    py::class_<hv::HVEventWriter>(m, "HVEventWriter")
        .def(py::init<>())
        .def("open", &hv::HVEventWriter::open,
             py::arg("filename"),
             py::arg("width"),
             py::arg("height"),
             py::arg("start_timestamp") = 0,
             "Open a new file and write header")
        .def("close", &hv::HVEventWriter::close, "Close the file")
        .def("is_open", &hv::HVEventWriter::isOpen, "Check if file is open")
        .def("write_events", &hv::HVEventWriter::writeEvents,
             py::arg("events"),
             "Write a batch of events")
        .def("flush", &hv::HVEventWriter::flush, "Flush buffer to disk")
        .def("get_written_event_count", &hv::HVEventWriter::getWrittenEventCount,
             "Get number of written events")
        .def("get_file_size", &hv::HVEventWriter::getFileSize,
             "Get current file size in bytes")
        .def("__enter__", [](hv::HVEventWriter &self) { return &self; })
        .def("__exit__", [](hv::HVEventWriter &self, py::args) { self.close(); });
}