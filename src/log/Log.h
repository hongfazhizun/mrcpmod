#pragma once

#include "apt_log.h"
#include <string>

using std::string;
extern apt_log_source_t* LOG_PLUGIN;
/** Use custom log source mark */
#define LOG_MARK   APT_LOG_MARK_DECLARE(LOG_PLUGIN)

#define ERRLN(format, ...) apt_log(LOG_MARK, APT_PRIO_ERROR, format, ##__VA_ARGS__)
#define WARNLN(format, ...) apt_log(LOG_MARK, APT_PRIO_WARNING, format, ##__VA_ARGS__)
#define INFOLN(format, ...) apt_log(LOG_MARK, APT_PRIO_INFO, format, ##__VA_ARGS__)

#define API_EXPORT __attribute__((visibility("default")))
#define MM_MRCP_PLUGIN_DECLARE(type) MRCP_PLUGIN_EXTERN_C API_EXPORT type
#define MM_MRCP_PLUGIN_VERSION_DECLARE \
	MM_MRCP_PLUGIN_DECLARE(mrcp_plugin_version_t) mrcp_plugin_version; \
	mrcp_plugin_version_t mrcp_plugin_version =  \
		{PLUGIN_MAJOR_VERSION, PLUGIN_MINOR_VERSION, PLUGIN_PATCH_VERSION};