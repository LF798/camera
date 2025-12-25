GSF_HOME ?= $(PWD)
GSF_CPU_ARCH ?= XXX

MOD_DIRS := mod/apx003_mpi_sample \
 			mod/apx003_v4l2_sample \
			mod/usb_app \
			mod/aps_rtsp_server        

FW_DIRS := common/libaio common/shmfifo

CLEAN_DIRS := $(addprefix _cls_, $(FW_DIRS) $(MOD_DIRS))

.PHONY: mod fw fw2 $(FW_DIRS) $(MOD_DIRS) CHECK_ENV SUMMARY clean

mod: CHECK_ENV $(MOD_DIRS) SUMMARY
	@echo "..."

fw: CHECK_ENV $(FW_DIRS) SUMMARY
	@echo "..."

SUMMARY:
	@echo ""
	@echo "================$(GSF_DEV_TYPE) ================"
	@echo "All Done."
	@echo "Env: $(GSF_CPU_ARCH)"
	@echo "Output: $(GSF_HOME)/lib/$(GSF_CPU_ARCH)"
	@echo "Output: `ls -l $(GSF_HOME)/lib/$(GSF_CPU_ARCH)`"
	@echo "Output: $(GSF_HOME)/bin/$(GSF_CPU_ARCH)"
	@echo "Output: `ls -l $(GSF_HOME)/bin/$(GSF_CPU_ARCH)`"

CHECK_ENV:
	@echo "================ IPC ================"
	@echo "Env: $(GSF_CPU_ARCH)"
ifeq ($(GSF_CPU_ARCH), XXX)
	@echo "Env Error."
	@exit 1
endif

$(MOD_DIRS):
	@$(MAKE) -C $@ || exit "$$?"

$(FW_DIRS):
	@$(MAKE) -C $@ || exit "$$?"
	
clean: $(CLEAN_DIRS)
	-rm $(GSF_HOME)/bin/$(GSF_CPU_ARCH)/*.exe -rf
	#-rm $(GSF_HOME)/lib/$(GSF_CPU_ARCH)/*.so -rf

$(CLEAN_DIRS):
	$(MAKE) -C $(patsubst _cls_%, %, $@) clean
