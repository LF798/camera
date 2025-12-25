cmake_minimum_required( VERSION 2.8.8 )

set(RK_TEST_COMMON_SRC
    test_comm_argparse.cpp
    test_comm_utils.cpp
    test_comm_bmp.cpp
    test_comm_imgproc.cpp
    ffmpeg/test_comm_ffm.cpp
    test_comm_sys.cpp
)

if (HAVE_TEST_COMMON_RGN)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_rgn.cpp
    )
endif()

if (HAVE_TEST_COMMON_VENC)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_venc.cpp
    )
endif()

if (HAVE_TEST_COMMON_VDEC)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_vdec.cpp
        test_comm_app_vdec.cpp
    )
endif()

if (HAVE_TEST_COMMON_VPSS)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_vpss.cpp
    )
    set(RK_TEST_MOD_SRC
        ${RK_TEST_MOD_SRC}
        test_mod_vpss.cpp
    )
endif()

if (HAVE_TEST_COMMON_VO)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_vo.cpp
        test_comm_app_vo.cpp
    )
endif()

if (HAVE_TEST_COMMON_AO)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_ao.cpp
    )
endif()

if (HAVE_TEST_COMMON_TDE)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_tde.cpp
    )
endif()

if (HAVE_TEST_COMMON_VGS)
    set(RK_TEST_COMMON_SRC
        ${RK_TEST_COMMON_SRC}
        test_comm_vgs.cpp
    )
endif()

add_library(${RT_TEST_COMM_STATIC} STATIC ${RK_TEST_COMMON_SRC} ${RK_TEST_MOD_SRC})
#add_dependencies(${RT_TEST_COMM_STATIC} ${RT_SHARED})
set_target_properties(${RT_TEST_COMM_STATIC} PROPERTIES FOLDER "rt_test_comm")
#target_link_libraries(${RT_TEST_COMM_STATIC})
