#include <pybind11/pybind11.h>
#include <pybind11/functional.h>  // 支持 std::function 回调
#include <pybind11/stl.h>        // 支持 STL 容器
#include "hv_camera.h"
#include "hv_usb_device.h"
#include <pybind11/numpy.h>
#include <opencv2/opencv.hpp>
#include <metavision/utils/pybind/py_array_to_cv_mat.h>


namespace py = pybind11;



namespace pybind11 {
namespace detail {

template <>
struct type_caster<cv::Mat> {
public:
    PYBIND11_TYPE_CASTER(cv::Mat, _("numpy.ndarray"));

    // numpy -> cv::Mat
    bool load(handle src, bool) {
    	std::cout << "load" << std::endl;
        if (!src)
            return false;

        auto array = py::array::ensure(src);
        if (!array)
            return false;

        auto buf = array.request();

        // 验证维度
        if (buf.ndim != 2 && buf.ndim != 3)
            return false;

        // 确定数据类型
        int type = CV_8U;
        if (buf.format == format_descriptor<float>::format()) type = CV_32F;
        else if (buf.format == format_descriptor<double>::format()) type = CV_64F;

        // 创建 cv::Mat
        if (buf.ndim == 2) {
            value = cv::Mat(
                buf.shape[0], buf.shape[1],
                type,
                buf.ptr
            ).clone();
        } else {
            value = cv::Mat(
                buf.shape[0], buf.shape[1],
                CV_MAKETYPE(type, buf.shape[2]),
                buf.ptr
            ).clone();
        }

        return true;
    }


    // 将 C++ cv::Mat 转为 Python numpy 数组
    static handle cast(const cv::Mat &mat, return_value_policy, handle) {
        return (Metavision::cv_mat_to_py_array(mat)).release();
    }
};

} // namespace detail
} // namespace pybind11

PYBIND11_MODULE(hv_camera_python, m) {
    m.doc() = "Python binding for HV_Camera";
    py::class_<cv::Mat>(m, "Mat");
    // 绑定 USBDevice 类
    py::class_<hv::USBDevice>(m, "USBDevice")
        .def(py::init<uint16_t, uint16_t>())
        .def("open", &hv::USBDevice::open)
        .def("isOpen", &hv::USBDevice::isOpen)
        .def("close", &hv::USBDevice::close)
        .def("bulkTransfer", &hv::USBDevice::bulkTransfer);

    // 绑定 HV_Camera 类
    py::class_<hv::HV_Camera>(m, "HV_Camera")
        // 构造函数
        .def(py::init<uint16_t, uint16_t>())
        
        // 基本操作
        .def("open", &hv::HV_Camera::open)
        .def("isOpen", &hv::HV_Camera::isOpen)
        .def("close", &hv::HV_Camera::close)
        
        // 事件采集
        .def("startEventCapture", &hv::HV_Camera::startEventCapture)
        .def("stopEventCapture", &hv::HV_Camera::stopEventCapture)
        
        // 图像采集
        .def("startImageCapture", &hv::HV_Camera::startImageCapture)
        .def("stopImageCapture", &hv::HV_Camera::stopImageCapture)
        .def("getLatestImage", &hv::HV_Camera::getLatestImage);
}
