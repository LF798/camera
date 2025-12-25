#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "hv_evt2_codec.h"

namespace py = pybind11;
using namespace hv::evt2;

// Helper functions to access bit-fields in RawEvent structures
namespace {

struct PyRawEvent {
    static uint32_t get_pad(const RawEvent &e) { return e.pad; }
    static void set_pad(RawEvent &e, uint32_t val) { e.pad = val; }
    static uint8_t get_type(const RawEvent &e) { return e.type; }
    static void set_type(RawEvent &e, uint8_t val) { e.type = val; }
};

// RawEventTime wrapper
struct PyRawEventTime {
    static uint32_t get_timestamp(const RawEventTime &e) { return e.timestamp; }
    static void set_timestamp(RawEventTime &e, uint32_t val) { e.timestamp = val; }
    static uint8_t get_type(const RawEventTime &e) { return e.type; }
    static void set_type(RawEventTime &e, uint8_t val) { e.type = val; }
};

// RawEventCD wrapper
struct PyRawEventCD {
    static uint16_t get_x(const RawEventCD &e) { return e.x; }
    static void set_x(RawEventCD &e, uint16_t val) { e.x = val; }
    static uint16_t get_y(const RawEventCD &e) { return e.y; }
    static void set_y(RawEventCD &e, uint16_t val) { e.y = val; }
    static uint8_t get_timestamp(const RawEventCD &e) { return e.timestamp; }
    static void set_timestamp(RawEventCD &e, uint8_t val) { e.timestamp = val; }
    static uint8_t get_type(const RawEventCD &e) { return e.type; }
    static void set_type(RawEventCD &e, uint8_t val) { e.type = val; }
};

// RawEventExtTrigger wrapper
struct PyRawEventExtTrigger {
    static uint8_t get_value(const RawEventExtTrigger &e) { return e.value; }
    static void set_value(RawEventExtTrigger &e, uint8_t val) { e.value = val; }
    static uint8_t get_unused2(const RawEventExtTrigger &e) { return e.unused2; }
    static void set_unused2(RawEventExtTrigger &e, uint8_t val) { e.unused2 = val; }
    static uint8_t get_id(const RawEventExtTrigger &e) { return e.id; }
    static void set_id(RawEventExtTrigger &e, uint8_t val) { e.id = val; }
    static uint16_t get_unused1(const RawEventExtTrigger &e) { return e.unused1; }
    static void set_unused1(RawEventExtTrigger &e, uint16_t val) { e.unused1 = val; }
    static uint8_t get_timestamp(const RawEventExtTrigger &e) { return e.timestamp; }
    static void set_timestamp(RawEventExtTrigger &e, uint8_t val) { e.timestamp = val; }
    static uint8_t get_type(const RawEventExtTrigger &e) { return e.type; }
    static void set_type(RawEventExtTrigger &e, uint8_t val) { e.type = val; }
};

} // anonymous namespace

PYBIND11_MODULE(hv_evt2_codec_python, m) {
    m.doc() = "Python bindings for HV EVT2 codec";

    // Enums
    py::enum_<EventTypes>(m, "EventTypes")
        .value("CD_OFF", EventTypes::CD_OFF)
        .value("CD_ON", EventTypes::CD_ON)
        .value("EVT_TIME_HIGH", EventTypes::EVT_TIME_HIGH)
        .value("EXT_TRIGGER", EventTypes::EXT_TRIGGER)
        .export_values();

    py::class_<RawEvent>(m, "RawEvent")
	.def(py::init<>())
        .def_property("pad",
            [](const RawEvent &e) { return PyRawEvent::get_pad(e); },
            [](RawEvent &e, uint32_t val) { PyRawEvent::set_pad(e, val); })
        .def_property("type",
            [](const RawEvent &e) { return PyRawEvent::get_type(e); },
            [](RawEvent &e, uint32_t val) { PyRawEvent::set_type(e, val); });
            
            
    // RawEventTime - don't expose RawEvent directly as it's just a base class
    py::class_<RawEventTime>(m, "RawEventTime")
        .def(py::init<>())
        .def_property("timestamp",
            [](const RawEventTime &e) { return PyRawEventTime::get_timestamp(e); },
            [](RawEventTime &e, uint32_t val) { PyRawEventTime::set_timestamp(e, val); })
        .def_property("type",
            [](const RawEventTime &e) { return PyRawEventTime::get_type(e); },
            [](RawEventTime &e, uint8_t val) { PyRawEventTime::set_type(e, val); });

    // RawEventCD
    py::class_<RawEventCD>(m, "RawEventCD")
        .def(py::init<>())
        .def_property("x",
            [](const RawEventCD &e) { return PyRawEventCD::get_x(e); },
            [](RawEventCD &e, uint16_t val) { PyRawEventCD::set_x(e, val); })
        .def_property("y",
            [](const RawEventCD &e) { return PyRawEventCD::get_y(e); },
            [](RawEventCD &e, uint16_t val) { PyRawEventCD::set_y(e, val); })
        .def_property("timestamp",
            [](const RawEventCD &e) { return PyRawEventCD::get_timestamp(e); },
            [](RawEventCD &e, uint8_t val) { PyRawEventCD::set_timestamp(e, val); })
        .def_property("type",
            [](const RawEventCD &e) { return PyRawEventCD::get_type(e); },
            [](RawEventCD &e, uint8_t val) { PyRawEventCD::set_type(e, val); });

    // RawEventExtTrigger
    py::class_<RawEventExtTrigger>(m, "RawEventExtTrigger")
        .def(py::init<>())
        .def_property("value",
            [](const RawEventExtTrigger &e) { return PyRawEventExtTrigger::get_value(e); },
            [](RawEventExtTrigger &e, uint8_t val) { PyRawEventExtTrigger::set_value(e, val); })
        .def_property("unused2",
            [](const RawEventExtTrigger &e) { return PyRawEventExtTrigger::get_unused2(e); },
            [](RawEventExtTrigger &e, uint8_t val) { PyRawEventExtTrigger::set_unused2(e, val); })
        .def_property("id",
            [](const RawEventExtTrigger &e) { return PyRawEventExtTrigger::get_id(e); },
            [](RawEventExtTrigger &e, uint8_t val) { PyRawEventExtTrigger::set_id(e, val); })
        .def_property("unused1",
            [](const RawEventExtTrigger &e) { return PyRawEventExtTrigger::get_unused1(e); },
            [](RawEventExtTrigger &e, uint16_t val) { PyRawEventExtTrigger::set_unused1(e, val); })
        .def_property("timestamp",
            [](const RawEventExtTrigger &e) { return PyRawEventExtTrigger::get_timestamp(e); },
            [](RawEventExtTrigger &e, uint8_t val) { PyRawEventExtTrigger::set_timestamp(e, val); })
        .def_property("type",
            [](const RawEventExtTrigger &e) { return PyRawEventExtTrigger::get_type(e); },
            [](RawEventExtTrigger &e, uint8_t val) { PyRawEventExtTrigger::set_type(e, val); });

    // EVT2Header
    py::class_<EVT2Header>(m, "EVT2Header")
        .def(py::init<>())
        .def_readwrite("format_line", &EVT2Header::format_line)
        .def_readwrite("integrator", &EVT2Header::integrator)
        .def_readwrite("date", &EVT2Header::date)
        .def_readwrite("width", &EVT2Header::width)
        .def_readwrite("height", &EVT2Header::height)
        .def_readwrite("start_timestamp", &EVT2Header::start_timestamp);

    // Encoders
    py::class_<EventCDEncoder>(m, "EventCDEncoder")
        .def(py::init<>())
        .def_readwrite("x", &EventCDEncoder::x)
        .def_readwrite("y", &EventCDEncoder::y)
        .def_readwrite("p", &EventCDEncoder::p)
        .def_readwrite("t", &EventCDEncoder::t)
/*	.def("encode", [](EventCDEncoder& self, py::buffer buf) {
	    py::buffer_info info = buf.request();
	    if (info.size * info.itemsize != sizeof(RawEvent)) {
		throw std::runtime_error("Buffer size must be exactly 4 bytes");
	    }
	    self.encode(static_cast<RawEvent*>(info.ptr));
	}, py::arg("buffer"))*/
	.def("encode", &EventCDEncoder::encode)
        .def("set_event", &EventCDEncoder::setEvent);

    py::class_<EventTriggerEncoder>(m, "EventTriggerEncoder")
        .def(py::init<>())
        .def_readwrite("p", &EventTriggerEncoder::p)
        .def_readwrite("t", &EventTriggerEncoder::t)
        .def_readwrite("id", &EventTriggerEncoder::id)
	.def("encode", &EventTriggerEncoder::encode)
        .def("set_event", &EventTriggerEncoder::setEvent);

    py::class_<EventTimeEncoder>(m, "EventTimeEncoder")
        .def(py::init<Timestamp>())
	.def("encode", &EventTimeEncoder::encode)
        .def("get_next_time_high", &EventTimeEncoder::getNextTimeHigh)
        .def("reset", &EventTimeEncoder::reset);

    // Decoder
    py::class_<EVT2Decoder>(m, "EVT2Decoder")
        .def(py::init<>())
        .def("decode", [](EVT2Decoder& self, const std::vector<uint8_t>& buffer, bool include_triggers) {
            std::vector<Metavision::EventCD> cd_events;
            std::vector<std::tuple<short, short, Timestamp>> trigger_events;
            
            size_t count = self.decode(buffer.data(), buffer.size(), cd_events, 
                                    include_triggers ? &trigger_events : nullptr);
            
            py::dict result;
            result["count"] = count;
            result["cd_events"] = cd_events;
            if (include_triggers) {
                result["trigger_events"] = trigger_events;
            }
            return result;
        }, py::arg("buffer"), py::arg("include_triggers") = false)
        .def("reset", &EVT2Decoder::reset)
        .def("get_current_time_base", &EVT2Decoder::getCurrentTimeBase);

    // Utility functions
    m.def("parse_evt2_header", &utils::parseEVT2Header);
    m.def("generate_evt2_header", py::overload_cast<const EVT2Header&>(&utils::generateEVT2Header));
    m.def("generate_evt2_header", py::overload_cast<uint32_t, uint32_t, const std::string&>(&utils::generateEVT2Header));
    m.def("convert_to_evt2", &utils::convertToEVT2);
}
