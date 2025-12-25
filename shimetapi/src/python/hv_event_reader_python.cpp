#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include "hv_event_reader.h"

namespace py = pybind11;
using namespace hv;

PYBIND11_MODULE(hv_event_reader_python, m) {
    m.doc() = "Python bindings for HV Event Reader";

    // 封装 EventCallback 类型
    py::class_<HVEventReader>(m, "HVEventReader")
        .def(py::init<>())

        // 文件操作
        .def("open", &HVEventReader::open, py::arg("filename"), 
             "Open an event file")
        .def("close", &HVEventReader::close, 
             "Close the current file")
        .def("is_open", &HVEventReader::isOpen,
             "Check if a file is currently open")
        .def("reset", &HVEventReader::reset,
             "Reset read position to start of file")
             
        // 读取方法
//        .def("read_events", &HVEventReader::readEvents,
//             py::arg("num_events"), py::arg("events"),
//             "Read a specified number of events")
//        .def("read_events",
//            [](HVEventReader& self, size_t num_events) {
//                std::vector<Metavision::EventCD> events;
//                size_t count = self.readEvents(num_events, events);
//                return py::make_tuple(count, events);  // 返回数量和事件列表
//            },
//            py::arg("num_events"),
//            "Read events and return (count, events)"
//        )
        .def("read_events",
            [](HVEventReader& self, size_t num_events) {
                std::vector<Metavision::EventCD> events;
                size_t count = self.readEvents(num_events, events);

                // 创建 NumPy 数组
                py::array_t<Metavision::EventCD> array(events.size());
                py::buffer_info buf = array.request();
                Metavision::EventCD* ptr = static_cast<Metavision::EventCD*>(buf.ptr);

                std::copy(events.begin(), events.end(), ptr);

                return py::make_tuple(count, array);
            },
            py::arg("num_events"),
            "Read events and return (count, events_np)"
        )
        .def("read_all_events",
            [](HVEventReader& self) {
                std::vector<Metavision::EventCD> events;
                size_t count = self.readAllEvents(events);

                // 创建 NumPy 数组
                py::array_t<Metavision::EventCD> array(events.size());
                py::buffer_info buf = array.request();
                Metavision::EventCD* ptr = static_cast<Metavision::EventCD*>(buf.ptr);

                std::copy(events.begin(), events.end(), ptr);

                return py::make_tuple(count, array);
            },
            "Read events and return (count, events_np)"
        )
        .def("stream_events", [](HVEventReader& self, size_t batch_size, py::function callback) {
            return self.streamEvents(batch_size, [callback](const std::vector<Metavision::EventCD>& events) {
                py::gil_scoped_acquire acquire;
                callback(events);
            });
        }, py::arg("batch_size"), py::arg("callback"),
           "Stream events in batches using a callback")
           
        // 获取信息
        .def("get_header", &HVEventReader::getHeader,
             "Get the file header information")
        .def("get_image_size", &HVEventReader::getImageSize,
             "Get the sensor image size (width, height)");
}
