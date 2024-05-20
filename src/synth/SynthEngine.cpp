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
#include "SynthEngine.h"
#include "Synthesizer.h"
#include <thread>
#include <chrono>

#define SYNTH_ENGINE_TASK_NAME "Synth Engine"

typedef struct demo_synth_engine_t demo_synth_engine_t;
typedef struct demo_synth_channel_t demo_synth_channel_t;
typedef struct demo_synth_msg_t demo_synth_msg_t;

/** Declaration of synthesizer engine methods */
static apt_bool_t demo_synth_engine_destroy(mrcp_engine_t* engine);
static apt_bool_t demo_synth_engine_open(mrcp_engine_t* engine);
static apt_bool_t demo_synth_engine_close(mrcp_engine_t* engine);
static mrcp_engine_channel_t* demo_synth_engine_channel_create(mrcp_engine_t* engine, apr_pool_t* pool);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
    demo_synth_engine_destroy,
    demo_synth_engine_open,
    demo_synth_engine_close,
    demo_synth_engine_channel_create
};

/** Declaration of synthesizer channel methods */
static apt_bool_t demo_synth_channel_destroy(mrcp_engine_channel_t* channel);
static apt_bool_t demo_synth_channel_open(mrcp_engine_channel_t* channel);
static apt_bool_t demo_synth_channel_close(mrcp_engine_channel_t* channel);
static apt_bool_t demo_synth_channel_request_process(mrcp_engine_channel_t* channel, mrcp_message_t* request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
    demo_synth_channel_destroy,
    demo_synth_channel_open,
    demo_synth_channel_close,
    demo_synth_channel_request_process
};

/** Declaration of synthesizer audio stream methods */
static apt_bool_t demo_synth_stream_destroy(mpf_audio_stream_t* stream);
static apt_bool_t demo_synth_stream_open(mpf_audio_stream_t* stream, mpf_codec_t* codec);
static apt_bool_t demo_synth_stream_close(mpf_audio_stream_t* stream);
static apt_bool_t demo_synth_stream_read(mpf_audio_stream_t* stream, mpf_frame_t* frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    demo_synth_stream_destroy,
    demo_synth_stream_open,
    demo_synth_stream_close,
    demo_synth_stream_read,
    NULL,
    NULL,
    NULL,
    NULL
};

/** Declaration of demo synthesizer engine */
struct demo_synth_engine_t {
    apt_consumer_task_t* task;
};

/** Declaration of demo synthesizer channel */
struct demo_synth_channel_t {
    /** Back pointer to engine */
    demo_synth_engine_t* demo_engine;
    /** Engine channel base */
    mrcp_engine_channel_t* channel;

    /** Active (in-progress) speak request */
    mrcp_message_t* speak_request;
    /** Pending stop response */
    mrcp_message_t* stop_response;
    /** Estimated time to complete */
    apr_size_t time_to_complete;
    /** Is paused */
    apt_bool_t paused;
};

typedef enum {
    DEMO_SYNTH_MSG_OPEN_CHANNEL,
    DEMO_SYNTH_MSG_CLOSE_CHANNEL,
    DEMO_SYNTH_MSG_REQUEST_PROCESS,
    DEMO_SYNTH_MSG_SEND_COMPLETE
} demo_synth_msg_type_e;

/** Declaration of demo synthesizer task message */
struct demo_synth_msg_t {
    demo_synth_msg_type_e type;
    mrcp_engine_channel_t* channel;
    mrcp_message_t* request;
};

static apt_bool_t demo_synth_msg_signal(demo_synth_msg_type_e type, mrcp_engine_channel_t* channel, mrcp_message_t* request);
static apt_bool_t demo_synth_msg_process(apt_task_t* task, apt_task_msg_t* msg);

/** Declare this macro to set plugin version */
MM_MRCP_PLUGIN_VERSION_DECLARE

/**
 * Declare this macro to use log routine of the server, plugin is loaded from.
 * Enable/add the corresponding entry in logger.xml to set a cutsom log source priority.
 *    <source name="SYNTH-PLUGIN" priority="DEBUG" masking="NONE"/>
 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(LOG_PLUGIN, "SYNTH-PLUGIN")

/** Create demo synthesizer engine */
MM_MRCP_PLUGIN_DECLARE(mrcp_engine_t*)
mrcp_plugin_create(apr_pool_t* pool)
{
    /* create demo engine */
    demo_synth_engine_t* demo_engine = (demo_synth_engine_t*)apr_palloc(pool, sizeof(demo_synth_engine_t));
    apt_task_t* task;
    apt_task_vtable_t* vtable;
    apt_task_msg_pool_t* msg_pool;
    INFOLN("begin create synthesizer engine");

    apt_log_masking_set(APT_LOG_MASKING_NONE);
    /* create task/thread to run demo engine in the context of this task */
    msg_pool = apt_task_msg_pool_create_dynamic(sizeof(demo_synth_msg_t), pool);
    demo_engine->task = apt_consumer_task_create(demo_engine, msg_pool, pool);
    if (!demo_engine->task) {
        ERRLN("create engine task error, task is NULL");
        return NULL;
    }
    task = apt_consumer_task_base_get(demo_engine->task);
    apt_task_name_set(task, SYNTH_ENGINE_TASK_NAME);
    vtable = apt_task_vtable_get(task);
    if (vtable) {
        vtable->process_msg = demo_synth_msg_process;
    }

    INFOLN("end create synthesizer engine");
    /* create engine base */
    return mrcp_engine_create(
        MRCP_SYNTHESIZER_RESOURCE, /* MRCP resource identifier */
        demo_engine, /* object to associate */
        &engine_vtable, /* virtual methods table of engine */
        pool); /* pool to allocate memory from */
}

/** Destroy synthesizer engine */
static apt_bool_t demo_synth_engine_destroy(mrcp_engine_t* engine)
{
    INFOLN("begin destroy synthesizer engine");
    demo_synth_engine_t* demo_engine = (demo_synth_engine_t*)engine->obj;
    if (demo_engine->task) {
        apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
        apt_task_destroy(task);
        demo_engine->task = NULL;
    }
    INFOLN("end destroy synthesizer engine");
    return TRUE;
}

/** Open synthesizer engine */
static apt_bool_t demo_synth_engine_open(mrcp_engine_t* engine)
{
    INFOLN("begin open synthesizer engine");
    demo_synth_engine_t* demo_engine = (demo_synth_engine_t*)engine->obj;
    if (demo_engine->task) {
        apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
        apt_task_start(task);
    }
    INFOLN("end open synthesizer engine");
    return mrcp_engine_open_respond(engine, TRUE);
}

/** Close synthesizer engine */
static apt_bool_t demo_synth_engine_close(mrcp_engine_t* engine)
{
    INFOLN("begin close synthesizer engine");
    demo_synth_engine_t* demo_engine = (demo_synth_engine_t*)engine->obj;
    if (demo_engine->task) {
        apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
        apt_task_terminate(task, TRUE);
    }
    INFOLN("end close synthesizer engine");
    return mrcp_engine_close_respond(engine);
}

/** Create demo synthesizer channel derived from engine channel base */
static mrcp_engine_channel_t* demo_synth_engine_channel_create(mrcp_engine_t* engine, apr_pool_t* pool)
{
    mpf_stream_capabilities_t* capabilities;
    mpf_termination_t* termination;
    INFOLN("begin creaate synthesizer channel");

    /* create demo synth channel */
    demo_synth_channel_t* synth_channel = (demo_synth_channel_t*)apr_palloc(pool, sizeof(demo_synth_channel_t));
    synth_channel->demo_engine = (demo_synth_engine_t*)engine->obj;
    synth_channel->speak_request = NULL;
    synth_channel->stop_response = NULL;
    synth_channel->time_to_complete = 0;
    synth_channel->paused = FALSE;

    capabilities = mpf_source_stream_capabilities_create(pool);
    mpf_codec_capabilities_add(
        &capabilities->codecs,
        MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
        "LPCM");

    /* create media termination */
    termination = mrcp_engine_audio_termination_create(
        synth_channel, /* object to associate */
        &audio_stream_vtable, /* virtual methods table of audio stream */
        capabilities, /* stream capabilities */
        pool); /* pool to allocate memory from */

    /* create engine channel base */
    synth_channel->channel = mrcp_engine_channel_create(
        engine, /* engine */
        &channel_vtable, /* virtual methods table of engine channel */
        synth_channel, /* object to associate */
        termination, /* associated media termination */
        pool); /* pool to allocate memory from */

    INFOLN("end creaate synthesizer channel");
    return synth_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t demo_synth_channel_destroy(mrcp_engine_channel_t* channel)
{
    INFOLN("synthesizer channel destroy");
    /* nothing to destroy */
    return TRUE;
}

/** Open engine channel (asynchronous response MUST be sent)*/
static apt_bool_t demo_synth_channel_open(mrcp_engine_channel_t* channel)
{
    INFOLN("demo_synth_channel_open");
    return demo_synth_msg_signal(DEMO_SYNTH_MSG_OPEN_CHANNEL, channel, NULL);
}

/** Close engine channel (asynchronous response MUST be sent)*/
static apt_bool_t demo_synth_channel_close(mrcp_engine_channel_t* channel)
{
    string channelId(channel->id.buf, channel->id.length);
    INFOLN("demo_synth_channel_close, channelId:%s", channelId.c_str());
    Synthesizer::Del(channelId);
    return demo_synth_msg_signal(DEMO_SYNTH_MSG_CLOSE_CHANNEL, channel, NULL);
}

/** Process MRCP channel request (asynchronous response MUST be sent)*/
static apt_bool_t demo_synth_channel_request_process(mrcp_engine_channel_t* channel, mrcp_message_t* request)
{
    mrcp_message_type_e msgType = request->start_line.message_type;
    string method(request->start_line.method_name.buf, request->start_line.method_name.length);
    string channelId(request->channel_id.session_id.buf, request->channel_id.session_id.length);
    string resource(request->channel_id.resource_name.buf, request->channel_id.resource_name.length);
    INFOLN("demo_synth_channel_request_process, msgType:%d method:%s channelId:%s resource:%s", msgType, method.c_str(), channelId.c_str(), resource.c_str());
    
    apt_bool_t needSendResponse = FALSE;
    switch (request->start_line.method_id) {
    case SYNTHESIZER_SET_PARAMS:
        break;
    case SYNTHESIZER_GET_PARAMS:
        break;
    case SYNTHESIZER_SPEAK:
        needSendResponse = TRUE;
        break;
    case SYNTHESIZER_STOP:
        break;
    case SYNTHESIZER_PAUSE:
        break;
    case SYNTHESIZER_RESUME:
        break;
    case SYNTHESIZER_BARGE_IN_OCCURRED:
        break;
    case SYNTHESIZER_CONTROL:
        break;
    case SYNTHESIZER_DEFINE_LEXICON:
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
    return demo_synth_msg_signal(DEMO_SYNTH_MSG_REQUEST_PROCESS, channel, request);
}

static void sendError(demo_synth_channel_t* synth_channel)
{
    string channelId(synth_channel->channel->id.buf, synth_channel->channel->id.length);
    if (!synth_channel->speak_request) {
        WARNLN("speak request is NULL when sendError, channelId:%s", channelId.c_str());
        return;
    }
    /* raise SPEAK-COMPLETE event */
    mrcp_message_t* message = mrcp_event_create(
        synth_channel->speak_request,
        SYNTHESIZER_SPEAK_COMPLETE,
        synth_channel->speak_request->pool);
    if (message) {
        /* get/allocate synthesizer header */
        mrcp_synth_header_t* synth_header = (mrcp_synth_header_t*)mrcp_resource_header_prepare(message);
        if (synth_header) {
            /* set completion cause */
            synth_header->completion_cause = SYNTHESIZER_COMPLETION_CAUSE_ERROR;
            mrcp_resource_header_property_add(message, SYNTHESIZER_HEADER_COMPLETION_CAUSE);
        }
        /* set request state */
        message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

        synth_channel->speak_request = NULL;
        
        INFOLN("send speak complete error, channelId:%s", channelId.c_str());
        /* send asynch event */
        mrcp_engine_channel_message_send(synth_channel->channel, message);
    }
}

/** Process SPEAK request */
static apt_bool_t demo_synth_channel_speak(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    string channelId(channel->id.buf, channel->id.length);
    demo_synth_channel_t* synth_channel = (demo_synth_channel_t*)channel->method_obj;
    const mpf_codec_descriptor_t* descriptor = mrcp_engine_source_stream_codec_get(channel);
    string body(request->body.buf, request->body.length);
    string voiceName;
    
    INFOLN("begin demo_synth_channel_speak text:%s channelId:%s", body.c_str(), channelId.c_str());
    synth_channel->speak_request = request;
    if (!descriptor) {
        WARNLN("Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        sendError(synth_channel);
        return TRUE;
    }
    
    mrcp_synth_header_t* req_synth_header;
    /* get synthesizer header */
    req_synth_header = (mrcp_synth_header_t*)mrcp_resource_header_get(request);
    if (req_synth_header) {
        /* check voice age header */
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_AGE) == TRUE) {
            INFOLN("Set Voice Age [%" APR_SIZE_T_FMT "], channelId:%s", req_synth_header->voice_param.age, channelId.c_str());
        }
        /* check voice name header */
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
            voiceName = req_synth_header->voice_param.name.buf;
            INFOLN("Set Voice Name [%s], channelId:%s", voiceName.c_str(), channelId.c_str());
        }
    }

    string voiceId = Synthesizer::GetVoiceId(channelId);
    if (!voiceId.empty()) {
        WARNLN("channel is already synthesize, channelId:%s voiceId:%s", channelId.c_str(), voiceId.c_str());
        sendError(synth_channel);
        return TRUE;
    }
    auto synthesizer = Synthesizer::Create(channelId);
    if (NULL == synthesizer) {
        ERRLN("create synthesizer error, channelId:%s", channelId.c_str());
        sendError(synth_channel);
        return TRUE;
    }
    synthesizer->setSynthChannel(synth_channel);
    synthesizer->setVoiceName(voiceName);
    synthesizer->setText(body);
    voiceId = synthesizer->getVoiceId();
    int ret = synthesizer->init();
    if (ret < 0) {
        ERRLN("synthesizer init error, ret:%d channelId:%s voiceId:%s", ret, channelId.c_str(), voiceId.c_str());
        sendError(synth_channel);
        return TRUE;
    }
    Synthesizer::Set(channelId, synthesizer);
    INFOLN("end demo_synth_channel_speak voiceName:%s text:%s channelId:%s voiceId:%s", voiceName.c_str(), body.c_str(), channelId.c_str(), voiceId.c_str());
    return TRUE;
}

/** Process STOP request */
static apt_bool_t demo_synth_channel_stop(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    string channelId(channel->id.buf, channel->id.length);
    INFOLN("begin synthesizer stop, channelId:%s", channelId.c_str());
    Synthesizer::Del(channelId);
    demo_synth_channel_t* synth_channel = (demo_synth_channel_t*)channel->method_obj;
    /* store the request, make sure there is no more activity and only then send the response */
    synth_channel->stop_response = response;
    synth_channel->speak_request = NULL;
    INFOLN("end synthesizer stop, channelId:%s", channelId.c_str());
    return TRUE;
}

/** Process PAUSE request */
static apt_bool_t demo_synth_channel_pause(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    demo_synth_channel_t* synth_channel = (demo_synth_channel_t*)channel->method_obj;
    synth_channel->paused = TRUE;
    /* send asynchronous response */
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Process RESUME request */
static apt_bool_t demo_synth_channel_resume(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    demo_synth_channel_t* synth_channel = (demo_synth_channel_t*)channel->method_obj;
    synth_channel->paused = FALSE;
    /* send asynchronous response */
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Process SET-PARAMS request */
static apt_bool_t demo_synth_channel_set_params(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    mrcp_synth_header_t* req_synth_header;
    /* get synthesizer header */
    req_synth_header = (mrcp_synth_header_t*)mrcp_resource_header_get(request);
    if (req_synth_header) {
        /* check voice age header */
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_AGE) == TRUE) {
            INFOLN("Set Voice Age [%" APR_SIZE_T_FMT "]", req_synth_header->voice_param.age);
        }
        /* check voice name header */
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
            INFOLN("Set Voice Name [%s]", req_synth_header->voice_param.name.buf);
        }
    }

    /* send asynchronous response */
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Process GET-PARAMS request */
static apt_bool_t demo_synth_channel_get_params(mrcp_engine_channel_t* channel, mrcp_message_t* request, mrcp_message_t* response)
{
    mrcp_synth_header_t* req_synth_header;
    /* get synthesizer header */
    req_synth_header = (mrcp_synth_header_t*)mrcp_resource_header_get(request);
    if (req_synth_header) {
        mrcp_synth_header_t* res_synth_header = (mrcp_synth_header_t*)mrcp_resource_header_prepare(response);
        /* check voice age header */
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_AGE) == TRUE) {
            res_synth_header->voice_param.age = 25;
            mrcp_resource_header_property_add(response, SYNTHESIZER_HEADER_VOICE_AGE);
        }
        /* check voice name header */
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
            apt_string_set(&res_synth_header->voice_param.name, "David");
            mrcp_resource_header_property_add(response, SYNTHESIZER_HEADER_VOICE_NAME);
        }
    }

    /* send asynchronous response */
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Dispatch MRCP request */
static apt_bool_t demo_synth_channel_request_dispatch(mrcp_engine_channel_t* channel, mrcp_message_t* request)
{
    apt_bool_t processed = FALSE;
    mrcp_message_t* response = mrcp_response_create(request, request->pool);
    switch (request->start_line.method_id) {
    case SYNTHESIZER_SET_PARAMS:
        processed = demo_synth_channel_set_params(channel, request, response);
        break;
    case SYNTHESIZER_GET_PARAMS:
        processed = demo_synth_channel_get_params(channel, request, response);
        break;
    case SYNTHESIZER_SPEAK:
        processed = demo_synth_channel_speak(channel, request, response);
        break;
    case SYNTHESIZER_STOP:
        processed = demo_synth_channel_stop(channel, request, response);
        break;
    case SYNTHESIZER_PAUSE:
        processed = demo_synth_channel_pause(channel, request, response);
        break;
    case SYNTHESIZER_RESUME:
        processed = demo_synth_channel_resume(channel, request, response);
        break;
    case SYNTHESIZER_BARGE_IN_OCCURRED:
        processed = demo_synth_channel_stop(channel, request, response);
        break;
    case SYNTHESIZER_CONTROL:
        break;
    case SYNTHESIZER_DEFINE_LEXICON:
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
static apt_bool_t demo_synth_stream_destroy(mpf_audio_stream_t* stream)
{
    INFOLN("demo_synth_stream_destroy");
    return TRUE;
}

/** Callback is called from MPF engine context to perform any action before open */
static apt_bool_t demo_synth_stream_open(mpf_audio_stream_t* stream, mpf_codec_t* codec)
{
    INFOLN("demo_synth_stream_open");
    return TRUE;
}

/** Callback is called from MPF engine context to perform any action after close */
static apt_bool_t demo_synth_stream_close(mpf_audio_stream_t* stream)
{
    INFOLN("demo_synth_stream_close");
    return TRUE;
}

/** Callback is called from MPF engine context to read/get new frame */
static apt_bool_t demo_synth_stream_read(mpf_audio_stream_t* stream, mpf_frame_t* frame)
{
    demo_synth_channel_t* synth_channel = (demo_synth_channel_t*)stream->obj;
    string channelId(synth_channel->channel->id.buf, synth_channel->channel->id.length);
    /* check if STOP was requested */
    if (synth_channel->stop_response) {
        /* send asynchronous response to STOP request */
        mrcp_engine_channel_message_send(synth_channel->channel, synth_channel->stop_response);
        synth_channel->stop_response = NULL;
        synth_channel->paused = FALSE;
        return TRUE;
    }

    if (synth_channel->paused == TRUE) {
        return TRUE;
    }
    
    string voiceId = Synthesizer::GetVoiceId(channelId);
    if (voiceId.empty()) {
        return TRUE;
    }
    auto synthesizer = Synthesizer::GetSynthesizer(voiceId);
    if (!synthesizer) {
        return TRUE;
    }
    frame->type |= MEDIA_FRAME_TYPE_AUDIO;
    int ret = synthesizer->read((char*)frame->codec_frame.buffer, frame->codec_frame.size);
    if (ret < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        demo_synth_msg_signal(DEMO_SYNTH_MSG_SEND_COMPLETE, synth_channel->channel, NULL);
        return TRUE;
    }
    return TRUE;
}

static apt_bool_t demo_synth_msg_signal(demo_synth_msg_type_e type, mrcp_engine_channel_t* channel, mrcp_message_t* request)
{
    apt_bool_t status = FALSE;
    demo_synth_channel_t* demo_channel = (demo_synth_channel_t*)channel->method_obj;
    demo_synth_engine_t* demo_engine = demo_channel->demo_engine;
    apt_task_t* task = apt_consumer_task_base_get(demo_engine->task);
    apt_task_msg_t* msg = apt_task_msg_get(task);
    if (msg) {
        demo_synth_msg_t* demo_msg;
        msg->type = TASK_MSG_USER;
        demo_msg = (demo_synth_msg_t*)msg->data;

        demo_msg->type = type;
        demo_msg->channel = channel;
        demo_msg->request = request;
        status = apt_task_msg_signal(task, msg);
    }
    return status;
}

static void sendComplete(demo_synth_msg_t* demo_msg)
{
    demo_synth_channel_t* synth_channel = (demo_synth_channel_t*)demo_msg->channel->method_obj;
    string channelId(synth_channel->channel->id.buf, synth_channel->channel->id.length);
    Synthesizer::Del(channelId);
    if (!synth_channel->speak_request) {
        WARNLN("speak request is NULL when sendComplate, channelId:%s", channelId.c_str());
        return;
    }
    /* raise SPEAK-COMPLETE event */
    mrcp_message_t* message = mrcp_event_create(
        synth_channel->speak_request,
        SYNTHESIZER_SPEAK_COMPLETE,
        synth_channel->speak_request->pool);
    if (message) {
        /* get/allocate synthesizer header */
        mrcp_synth_header_t* synth_header = (mrcp_synth_header_t*)mrcp_resource_header_prepare(message);
        if (synth_header) {
            /* set completion cause */
            synth_header->completion_cause = SYNTHESIZER_COMPLETION_CAUSE_NORMAL;
            mrcp_resource_header_property_add(message, SYNTHESIZER_HEADER_COMPLETION_CAUSE);
        }
        /* set request state */
        message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

        synth_channel->speak_request = NULL;
        
        INFOLN("send speak complete, channelId:%s", channelId.c_str());
        /* send asynch event */
        mrcp_engine_channel_message_send(synth_channel->channel, message);
    }
}

static apt_bool_t demo_synth_msg_process(apt_task_t* task, apt_task_msg_t* msg)
{
    demo_synth_msg_t* demo_msg = (demo_synth_msg_t*)msg->data;
    switch (demo_msg->type) {
    case DEMO_SYNTH_MSG_OPEN_CHANNEL:
        /* open channel and send asynch response */
        mrcp_engine_channel_open_respond(demo_msg->channel, TRUE);
        break;
    case DEMO_SYNTH_MSG_CLOSE_CHANNEL:
        /* close channel, make sure there is no activity and send asynch response */
        mrcp_engine_channel_close_respond(demo_msg->channel);
        break;
    case DEMO_SYNTH_MSG_SEND_COMPLETE: {
        sendComplete(demo_msg);
        break;
    }
    case DEMO_SYNTH_MSG_REQUEST_PROCESS:
        demo_synth_channel_request_dispatch(demo_msg->channel, demo_msg->request);
        break;
    default:
        break;
    }
    return TRUE;
}
