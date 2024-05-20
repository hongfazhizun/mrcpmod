/*
 * Copyright 2008-2015 Arsen Chaloyan
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Mandatory rules concerning plugin implementation.
 * 1. Each plugin MUST implement a plugin/engine creator function
 *    with the exact signature and name (the main entry point)
 *        MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
 * 2. Each plugin MUST declare its version number
 *        MRCP_PLUGIN_VERSION_DECLARE
 * 3. One and only one response MUST be sent back to the received request.
 * 4. Methods (callbacks) of the MRCP engine channel MUST not block.
 *   (asynchronous response can be sent from the context of other thread)
 * 5. Methods (callbacks) of the MPF engine stream MUST not block.
 */
#include "RecogEngine.h"
#include "Recognize.h"
#include "apr_general.h"
#include "apt.h"
#include "mrcp_recog_header.h"
#include "mrcp_types.h"
#include <memory>

#define RECOG_ENGINE_TASK_NAME "Recog Engine"

typedef struct demo_recog_msg_t demo_recog_msg_t;

/** Declaration of recognizer engine methods */
static apt_bool_t demo_recog_engine_destroy(mrcp_engine_t* engine);
static apt_bool_t demo_recog_engine_open(mrcp_engine_t* engine);
static apt_bool_t demo_recog_engine_close(mrcp_engine_t* engine);
static mrcp_engine_channel_t* demo_recog_engine_channel_create(mrcp_engine_t* engine, apr_pool_t* pool);
static apt_bool_t demo_recog_recognition_complete(demo_recog_channel_t* recog_channel, mrcp_recog_completion_cause_e cause, string body);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
    demo_recog_engine_destroy,
    demo_recog_engine_open,
    demo_recog_engine_close,
    demo_recog_engine_channel_create
};

/** Declaration of recognizer channel methods */
static apt_bool_t demo_recog_channel_destroy(mrcp_engine_channel_t* channel);
static apt_bool_t demo_recog_channel_open(mrcp_engine_channel_t* channel);
static apt_bool_t demo_recog_channel_close(mrcp_engine_channel_t* channel);
static apt_bool_t demo_recog_channel_request_process(mrcp_engine_channel_t* channel, mrcp_message_t* request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
    demo_recog_channel_destroy,
    demo_recog_channel_open,
    demo_recog_channel_close,
    demo_recog_channel_request_process
};

/** Declaration of recognizer audio stream methods */
static apt_bool_t demo_recog_stream_destroy(mpf_audio_stream_t* stream);
static apt_bool_t demo_recog_stream_open(mpf_audio_stream_t* stream, mpf_codec_t* codec);
static apt_bool_t demo_recog_stream_close(mpf_audio_stream_t* stream);
static apt_bool_t demo_recog_stream_write(mpf_audio_stream_t* stream, const mpf_frame_t* frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    demo_recog_stream_destroy,
    NULL,
    NULL,
    NULL,
    demo_recog_stream_open,
    demo_recog_stream_close,
    demo_recog_stream_write,
    NULL
};

/** Declaration of demo recognizer task message */
struct demo_recog_msg_t {
    demo_recog_msg_type_e type;
    mrcp_engine_channel_t* channel;
    mrcp_message_t* request;
    mrcp_recog_completion_cause_e cause;
    void* data;
};

static apt_bool_t demo_recog_msg_process(apt_task_t* task, apt_task_msg_t* msg);

/** Declare this macro to set plugin version */
MM_MRCP_PLUGIN_VERSION_DECLARE

/**
 * Declare this macro to use log routine of the server, plugin is loaded from.
 * Enable/add the corresponding entry in logger.xml to set a cutsom log source priority.
 *    <source name="RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(LOG_PLUGIN, "RECOG-PLUGIN")

/** Create demo recognizer engine */
MM_MRCP_PLUGIN_DECLARE(mrcp_engine_t*)
mrcp_plugin_create(apr_pool_t* pool)
{
    demo_recog_engine_t* demo_engine = (demo_recog_engine_t*)apr_palloc(pool, sizeof(demo_recog_engine_t));
    apt_task_t* task;
    apt_task_vtable_t* vtable;
    apt_task_msg_pool_t* msg_pool;
    INFOLN("begin create recog engine");

    apt_log_masking_set(APT_LOG_MASKING_NONE);
    msg_pool = apt_task_msg_pool_create_dynamic(sizeof(demo_recog_msg_t), pool);
    demo_engine->task = apt_consumer_task_create(demo_engine, msg_pool, pool);
    if (!demo_engine->task) {
        ERRLN("recog engine task is NULL");
        return NULL;
    }
    task = apt_consumer_task_base_get(demo_engine->task);
    apt_task_name_set(task, RECOG_ENGINE_TASK_NAME);
    vtable = apt_task_vtable_get(task);
    if (vtable) {
        vtable->process_msg = demo_recog_msg_process;
    }

    INFOLN("end create recog engine");
    /* create engine base */
    return mrcp_engine_create(
        MRCP_RECOGNIZER_RESOURCE, /* MRCP resource identifier */
        demo_engine, /* object to associate */
        &engine_vtable, /* virtual methods table of engine */
        pool); /* pool to allocate memory from */
}

/** Destroy recognizer engine */
static apt_bool_t demo_recog_engine_destroy(mrcp_engine_t* engine)
{
    INFOLN("begin destroy recog engine");
    demo_recog_engine_t* demo_engine = (demo_recog_engine_t*)engine->obj;
    if (demo_engine->task) {
        apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
        apt_task_destroy(task);
        demo_engine->task = NULL;
    }
    INFOLN("end destroy recog engine");
    return TRUE;
}

/** Open recognizer engine */
static apt_bool_t demo_recog_engine_open(mrcp_engine_t* engine)
{
    INFOLN("begin open recog engine");
    demo_recog_engine_t* demo_engine = (demo_recog_engine_t*)engine->obj;
    if (demo_engine->task) {
        apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
        apt_task_start(task);
    }
    INFOLN("end open recog engine");
    return mrcp_engine_open_respond(engine, TRUE);
}

/** Close recognizer engine */
static apt_bool_t demo_recog_engine_close(mrcp_engine_t* engine)
{
    INFOLN("begin close recog engine");
    demo_recog_engine_t* demo_engine = (demo_recog_engine_t*)engine->obj;
    if (demo_engine->task) {
        apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
        apt_task_terminate(task, TRUE);
    }
    INFOLN("end close recog engine");
    return mrcp_engine_close_respond(engine);
}

static mrcp_engine_channel_t* demo_recog_engine_channel_create(mrcp_engine_t* engine, apr_pool_t* pool)
{
    mpf_stream_capabilities_t* capabilities;
    mpf_termination_t* termination;
    INFOLN("begin create recog channel");

    /* create demo recog channel */
    demo_recog_channel_t* recog_channel = (demo_recog_channel_t*)apr_palloc(pool, sizeof(demo_recog_channel_t));
    recog_channel->demo_engine = (demo_recog_engine_t*)engine->obj;
    recog_channel->recog_request = NULL;
    recog_channel->stop_response = NULL;
    recog_channel->detector = mpf_activity_detector_create(pool);

    capabilities = mpf_sink_stream_capabilities_create(pool);
    mpf_codec_capabilities_add(&capabilities->codecs, MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000, "LPCM");

    /* create media termination */
    termination = mrcp_engine_audio_termination_create(
        recog_channel, /* object to associate */
        &audio_stream_vtable, /* virtual methods table of audio stream */
        capabilities, /* stream capabilities */
        pool); /* pool to allocate memory from */

    /* create engine channel base */
    recog_channel->channel = mrcp_engine_channel_create(
        engine, /* engine */
        &channel_vtable, /* virtual methods table of engine channel */
        recog_channel, /* object to associate */
        termination, /* associated media termination */
        pool); /* pool to allocate memory from */

    INFOLN("end create recog channel");
    return recog_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t demo_recog_channel_destroy(mrcp_engine_channel_t* channel)
{
    string channelId(channel->id.buf, channel->id.length);
    INFOLN("demo_recog_channel_destroy, channelId:%s", channelId.c_str());
    /* nothing to destrtoy */
    return TRUE;
}

/** Open engine channel (asynchronous response MUST be sent)*/
static apt_bool_t demo_recog_channel_open(mrcp_engine_channel_t* channel)
{
    string channelId(channel->id.buf, channel->id.length);
    INFOLN("begin open recog channel, channelId:%s", channelId.c_str());
    if (channel->attribs) {
        /* process attributes */
        const apr_array_header_t* header = apr_table_elts(channel->attribs);
        apr_table_entry_t* entry = (apr_table_entry_t*)header->elts;
        int i;
        for (i = 0; i < header->nelts; i++) {
            INFOLN("Attrib name [%s] value [%s] channelId:%s", entry[i].key, entry[i].val, channelId.c_str());
        }
    }
    INFOLN("end open recog channel, channelId:%s", channelId.c_str());
    return demo_recog_msg_signal(DEMO_RECOG_MSG_OPEN_CHANNEL, channel, NULL, RECOGNIZER_COMPLETION_CAUSE_SUCCESS, NULL);
}

/** Close engine channel (asynchronous response MUST be sent)*/
static apt_bool_t demo_recog_channel_close(mrcp_engine_channel_t* channel)
{
    string channelId(channel->id.buf, channel->id.length);
    INFOLN("close recog channel, channelId:%s", channelId.c_str());
    Recognize::Del(channelId);
    return demo_recog_msg_signal(DEMO_RECOG_MSG_CLOSE_CHANNEL, channel, NULL, RECOGNIZER_COMPLETION_CAUSE_SUCCESS, NULL);
}

/** Process MRCP channel request (asynchronous response MUST be sent)*/
static apt_bool_t demo_recog_channel_request_process(mrcp_engine_channel_t* channel, mrcp_message_t* request)
{
    string channelId(channel->id.buf, channel->id.length);
    INFOLN("demo_recog_channel_request_process, channelId:%s", channelId.c_str());
    apt_bool_t needSendResponse = FALSE;
    switch (request->start_line.method_id) {
    case RECOGNIZER_SET_PARAMS:
        break;
    case RECOGNIZER_GET_PARAMS:
        break;
    case RECOGNIZER_DEFINE_GRAMMAR:
        break;
    case RECOGNIZER_RECOGNIZE:
        needSendResponse = TRUE;
        break;
    case RECOGNIZER_GET_RESULT:
        break;
    case RECOGNIZER_START_INPUT_TIMERS:
        break;
    case RECOGNIZER_STOP:
        break;
    default:
        break;
    }
    if (needSendResponse == TRUE) {
        mrcp_message_t* response = mrcp_response_create(request, request->pool);
        response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
        /* send asynchronous response for not handled request */
        mrcp_engine_channel_message_send(channel, response);
    }
    return demo_recog_msg_signal(DEMO_RECOG_MSG_REQUEST_PROCESS, channel, request, RECOGNIZER_COMPLETION_CAUSE_SUCCESS, NULL);
}

/** Process RECOGNIZE request */
static apt_bool_t demo_recog_channel_recognize(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    string channelId(channel->id.buf, channel->id.length);
    /* process RECOGNIZE request */
    mrcp_recog_header_t* recog_header;
    demo_recog_channel_t* recog_channel = (demo_recog_channel_t*)channel->method_obj;
    const mpf_codec_descriptor_t* descriptor = mrcp_engine_sink_stream_codec_get(channel);
    string body(request->body.buf, request->body.length);

    INFOLN("begin recognize, body:%s channelId:%s", body.c_str(), channelId.c_str());
    recog_channel->recog_request = request;
    if (!descriptor) {
        WARNLN("Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        demo_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_ERROR, "");
        return TRUE;
    }

    string voiceId = Recognize::GetVoiceId(channelId);
    if (!voiceId.empty()) {
        WARNLN("channel is already recognize, channelId:%s voiceId:%s", channelId.c_str(), voiceId.c_str());
        demo_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_ERROR, "");
        return TRUE;
    }
    auto recognize = Recognize::Create(channelId);
    if (NULL == recognize) {
        ERRLN("create recognize error");
        demo_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_ERROR, "");
        return TRUE;
    }
    voiceId = recognize->getVoiceId();
    recognize->setRecogChannel(recog_channel);
    if (body == "builtin:partial") {
        recognize->setPartial(true);
        INFOLN("set partial match, channelId:%s", channelId.c_str());
    }
    int ret = recognize->init();
    if (ret < 0) {
        ERRLN("recognize init error, ret:%d channelId:%s voiceId:%s", ret, channelId.c_str(), voiceId.c_str());
        demo_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_ERROR, "");
        return TRUE;
    }
    Recognize::Set(channelId, recognize);

    recog_channel->timers_started = TRUE;

    /* get recognizer header */
    recog_header = (mrcp_recog_header_t*)mrcp_resource_header_get(request);
    if (recog_header) {
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_START_INPUT_TIMERS) == TRUE) {
            recog_channel->timers_started = recog_header->start_input_timers;
        }
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_NO_INPUT_TIMEOUT) == TRUE) {
            mpf_activity_detector_noinput_timeout_set(recog_channel->detector, recog_header->no_input_timeout);
        }
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT) == TRUE) {
            mpf_activity_detector_silence_timeout_set(recog_channel->detector, recog_header->speech_complete_timeout);
        }
        INFOLN("recognize param, start_input_timers:%d no_input_timeout:%d speech_complete_timeout:%d channelId:%s voiceId:%s", recog_header->start_input_timers, recog_header->no_input_timeout, recog_header->speech_complete_timeout, channelId.c_str(), voiceId.c_str());
    }

    INFOLN("end recognize, channelId:%s voiceId:%s", channelId.c_str(), voiceId.c_str());
    return TRUE;
}

/** Process STOP request */
static apt_bool_t demo_recog_channel_stop(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    string channelId(channel->id.buf, channel->id.length);
    INFOLN("begin recognize stop, channelId:%s", channelId.c_str());
    Recognize::Del(channelId);
    /* process STOP request */
    demo_recog_channel_t* recog_channel = (demo_recog_channel_t*)channel->method_obj;
    /* store STOP request, make sure there is no more activity and only then send the response */
    recog_channel->stop_response = response;
    recog_channel->recog_request = NULL;
    INFOLN("end recognize stop, channelId:%s", channelId.c_str());
    return TRUE;
}

/** Process START-INPUT-TIMERS request */
static apt_bool_t demo_recog_channel_timers_start(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    string channelId(channel->id.buf, channel->id.length);
    demo_recog_channel_t* recog_channel = (demo_recog_channel_t*)channel->method_obj;
    recog_channel->timers_started = TRUE;
    INFOLN("demo_recog_channel_timers_start, channelId:%s", channelId.c_str());
    return mrcp_engine_channel_message_send(channel, response);
}

/** Dispatch MRCP request */
static apt_bool_t demo_recog_channel_request_dispatch(mrcp_engine_channel_t* channel, mrcp_message_t* request)
{
    apt_bool_t processed = FALSE;
    mrcp_message_t* response = mrcp_response_create(request, request->pool);
    switch (request->start_line.method_id) {
    case RECOGNIZER_SET_PARAMS:
        break;
    case RECOGNIZER_GET_PARAMS:
        break;
    case RECOGNIZER_DEFINE_GRAMMAR:
        break;
    case RECOGNIZER_RECOGNIZE:
        processed = demo_recog_channel_recognize(channel, request, response);
        break;
    case RECOGNIZER_GET_RESULT:
        break;
    case RECOGNIZER_START_INPUT_TIMERS:
        processed = demo_recog_channel_timers_start(channel, request, response);
        break;
    case RECOGNIZER_STOP:
        processed = demo_recog_channel_stop(channel, request, response);
        break;
    default:
        break;
    }
    if (processed == FALSE) {
        /* send asynchronous response for not handled request */
        mrcp_engine_channel_message_send(channel, response);
    }
    return TRUE;
}

/** Callback is called from MPF engine context to destroy any additional data associated with audio stream */
static apt_bool_t demo_recog_stream_destroy(mpf_audio_stream_t* stream)
{
    INFOLN("demo_recog_stream_destroy");
    return TRUE;
}

/** Callback is called from MPF engine context to perform any action before open */
static apt_bool_t demo_recog_stream_open(mpf_audio_stream_t* stream, mpf_codec_t* codec)
{
    INFOLN("demo_recog_stream_open");
    return TRUE;
}

/** Callback is called from MPF engine context to perform any action after close */
static apt_bool_t demo_recog_stream_close(mpf_audio_stream_t* stream)
{
    INFOLN("demo_recog_stream_close");
    return TRUE;
}

/* Raise demo START-OF-INPUT event */
static apt_bool_t demo_recog_start_of_input(demo_recog_channel_t* recog_channel)
{
    /* create START-OF-INPUT event */
    mrcp_message_t* message = mrcp_event_create(
        recog_channel->recog_request,
        RECOGNIZER_START_OF_INPUT,
        recog_channel->recog_request->pool);
    if (!message) {
        return FALSE;
    }

    /* set request state */
    message->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
    /* send asynch event */
    return mrcp_engine_channel_message_send(recog_channel->channel, message);
}

/* Load demo recognition result */
static apt_bool_t demo_recog_result_load(demo_recog_channel_t* recog_channel, mrcp_message_t* message, string body)
{
    mrcp_engine_channel_t* channel = recog_channel->channel;
    if (body.empty()) {
        return FALSE;
    }
    mrcp_generic_header_t* generic_header;
    apt_string_assign_n(&message->body, body.c_str(), body.size(), message->pool);

    /* get/allocate generic header */
    generic_header = mrcp_generic_header_prepare(message);
    if (!generic_header) {
        return FALSE;
    }
    /* set content types */
    apt_string_assign(&generic_header->content_type, "application/x-nlsml", message->pool);
    mrcp_generic_header_property_add(message, GENERIC_HEADER_CONTENT_TYPE);
    return TRUE;
}

/* Raise demo RECOGNITION-COMPLETE event */
static apt_bool_t demo_recog_recognition_complete(demo_recog_channel_t* recog_channel, mrcp_recog_completion_cause_e cause, string body)
{
    string channelId(recog_channel->channel->id.buf, recog_channel->channel->id.length);
    if (NULL == recog_channel->recog_request) {
        WARNLN("recog_request is nullptr, channelId:%s", channelId.c_str());
        return FALSE;
    }
    mrcp_recog_header_t* recog_header;
    /* create RECOGNITION-COMPLETE event */
    mrcp_message_t* message = mrcp_event_create(
        recog_channel->recog_request,
        RECOGNIZER_RECOGNITION_COMPLETE,
        recog_channel->recog_request->pool);
    if (!message) {
        return FALSE;
    }

    /* get/allocate recognizer header */
    recog_header = (mrcp_recog_header_t*)mrcp_resource_header_prepare(message);
    if (recog_header) {
        /* set completion cause */
        recog_header->completion_cause = cause;
        mrcp_resource_header_property_add(message, RECOGNIZER_HEADER_COMPLETION_CAUSE);
    }

    /* set request state */
    message->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
    demo_recog_result_load(recog_channel, message, body);
    if (cause == RECOGNIZER_COMPLETION_CAUSE_SUCCESS) {
        message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;
        recog_channel->recog_request = NULL;
    }
    /* send asynch event */
    return mrcp_engine_channel_message_send(recog_channel->channel, message);
}

/** Callback is called from MPF engine context to write/send new frame */
static apt_bool_t demo_recog_stream_write(mpf_audio_stream_t* stream, const mpf_frame_t* frame)
{
    demo_recog_channel_t* recog_channel = (demo_recog_channel_t*)stream->obj;
    string channelId(recog_channel->channel->id.buf, recog_channel->channel->id.length);
    if (recog_channel->stop_response) {
        INFOLN("send stop response in demo_recog_stream_write, channelId:%s", channelId.c_str());
        /* send asynchronous response to STOP request */
        mrcp_engine_channel_message_send(recog_channel->channel, recog_channel->stop_response);
        recog_channel->stop_response = NULL;
        return TRUE;
    }
    
    // mpf_detector_event_e det_event = mpf_activity_detector_process(recog_channel->detector, frame);
    // switch (det_event) {
    // case MPF_DETECTOR_EVENT_ACTIVITY:
    //     INFOLN("Detected Voice Activity " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
    //     demo_recog_start_of_input(recog_channel);
    //     break;
    // case MPF_DETECTOR_EVENT_INACTIVITY:
    //     INFOLN("Detected Voice Inactivity " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
    //     demo_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_SUCCESS, "asr识别内容");
    //     break;
    // case MPF_DETECTOR_EVENT_NOINPUT:
    //     INFOLN("Detected Noinput " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
    //     if (recog_channel->timers_started == TRUE) {
    //         demo_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT, "");
    //     }
    //     break;
    // default:
    //     break;
    // }

    // if ((frame->type & MEDIA_FRAME_TYPE_EVENT) == MEDIA_FRAME_TYPE_EVENT) {
    //     if (frame->marker == MPF_MARKER_START_OF_EVENT) {
    //         INFOLN("Detected Start of Event " APT_SIDRES_FMT " id:%d", MRCP_MESSAGE_SIDRES(recog_channel->recog_request), frame->event_frame.event_id);
    //     } else if (frame->marker == MPF_MARKER_END_OF_EVENT) {
    //         INFOLN("Detected End of Event " APT_SIDRES_FMT " id:%d duration:%d ts", MRCP_MESSAGE_SIDRES(recog_channel->recog_request), frame->event_frame.event_id, frame->event_frame.duration);
    //     }
    // }
    if ((frame->type & MEDIA_FRAME_TYPE_AUDIO) != MEDIA_FRAME_TYPE_AUDIO) {
        return TRUE;
    }
    string voiceId = Recognize::GetVoiceId(channelId);
    if (voiceId.empty()) {
        return TRUE;
    }
    auto recognize = Recognize::GetRecognize(voiceId);
    if (!recognize) {
        return TRUE;
    }
    recognize->write((char*)frame->codec_frame.buffer, frame->codec_frame.size);
    return TRUE;
}

apt_bool_t demo_recog_msg_signal(demo_recog_msg_type_e type, mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_recog_completion_cause_e cause, void* data)
{
    apt_bool_t status = FALSE;
    demo_recog_channel_t* demo_channel = (demo_recog_channel_t*)channel->method_obj;
    demo_recog_engine_t* demo_engine = demo_channel->demo_engine;
    apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
    apt_task_msg_t* msg = apt_task_msg_get(task);
    if (msg) {
        demo_recog_msg_t* demo_msg;
        msg->type = TASK_MSG_USER;
        demo_msg = (demo_recog_msg_t*)msg->data;

        demo_msg->type = type;
        demo_msg->channel = channel;
        demo_msg->request = request;
        demo_msg->cause = cause;
        demo_msg->data = data;
        status = apt_task_msg_signal(task, msg);
    }
    return status;
}

static void sendComplete(demo_recog_msg_t* demo_msg)
{
    demo_recog_channel_t* recog_channel = (demo_recog_channel_t*)demo_msg->channel->method_obj;
    mrcp_recog_completion_cause_e cause = demo_msg->cause;
    string channelId(recog_channel->channel->id.buf, recog_channel->channel->id.length);
    if (cause == RECOGNIZER_COMPLETION_CAUSE_SUCCESS) {
        Recognize::Del(channelId);
    }
    string body;
    if (demo_msg->data) {
        char* body_str = (char*)demo_msg->data;
        demo_msg->data = nullptr;
        body = body_str;
        delete body_str;
    }
    std::ostringstream oss;
    oss << R"(<?xml version="1.0" encoding="UTF-8" ?>
<result> 
  <interpretation grammar="session:default" confidence="0.97">
    <instance><nlresult>)" << body << R"(</nlresult></instance>
    <input mode="speech"></input>
  </interpretation>
</result>)";
    body = oss.str();
    const apt_str_t* str = mrcp_recog_completion_cause_get(cause, MRCP_VERSION_2);
    string cause_str(str->buf, str->length);
    INFOLN("sendComplete cause:%s body:%s channelId:%s", cause_str.c_str(), body.c_str(), channelId.c_str());
    demo_recog_recognition_complete(recog_channel, cause, body);
}

static apt_bool_t demo_recog_msg_process(apt_task_t* task, apt_task_msg_t* msg)
{
    demo_recog_msg_t* demo_msg = (demo_recog_msg_t*)msg->data;
    demo_recog_channel_t* recog_channel = (demo_recog_channel_t*)demo_msg->channel->method_obj;
    string channelId(recog_channel->channel->id.buf, recog_channel->channel->id.length);
    switch (demo_msg->type) {
    case DEMO_RECOG_MSG_OPEN_CHANNEL:
        /* open channel and send asynch response */
        mrcp_engine_channel_open_respond(demo_msg->channel, TRUE);
        break;
    case DEMO_RECOG_MSG_CLOSE_CHANNEL: {
        /* close channel, make sure there is no activity and send asynch response */
        mrcp_engine_channel_close_respond(demo_msg->channel);
        break;
    }
    case DEMO_RECOG_MSG_START_OF_INPUT: {
        apt_bool_t ret = demo_recog_start_of_input(recog_channel);
        INFOLN("send start of input, ret:%d channelId:%s", ret, channelId.c_str());
        break;
    }
    case DEMO_RECOG_MSG_COMPLETE: {
        sendComplete(demo_msg);
        INFOLN("send sendComplete, channelId:%s", channelId.c_str());
        break;
    }
    case DEMO_RECOG_MSG_REQUEST_PROCESS:
        demo_recog_channel_request_dispatch(demo_msg->channel, demo_msg->request);
        break;
    default:
        break;
    }
    return TRUE;
}
