#pragma once

#include "apr_general.h"
#include "apt_consumer_task.h"
#include "apt_string.h"
#include "log/Log.h"
#include "mpf_activity_detector.h"
#include "mrcp_recog_engine.h"

typedef struct demo_recog_engine_t demo_recog_engine_t;
typedef struct demo_recog_channel_t demo_recog_channel_t;

/** Declaration of demo recognizer engine */
struct demo_recog_engine_t {
    apt_consumer_task_t* task;
};

/** Declaration of demo recognizer channel */
struct demo_recog_channel_t {
    /** Back pointer to engine */
    demo_recog_engine_t* demo_engine;
    /** Engine channel base */
    mrcp_engine_channel_t* channel;

    /** Active (in-progress) recognition request */
    mrcp_message_t* recog_request;
    /** Pending stop response */
    mrcp_message_t* stop_response;
    /** Indicates whether input timers are started */
    apt_bool_t timers_started;
    /** Voice activity detector */
    mpf_activity_detector_t* detector;
};

typedef enum {
    DEMO_RECOG_MSG_OPEN_CHANNEL,
    DEMO_RECOG_MSG_CLOSE_CHANNEL,
    DEMO_RECOG_MSG_REQUEST_PROCESS,
    DEMO_RECOG_MSG_START_OF_INPUT,
    DEMO_RECOG_MSG_COMPLETE
} demo_recog_msg_type_e;

apt_bool_t demo_recog_msg_signal(demo_recog_msg_type_e type, mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_recog_completion_cause_e cause, void* data);