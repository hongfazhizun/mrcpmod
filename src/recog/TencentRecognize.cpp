#include "TencentRecognize.h"
#include "Recognize.h"
#include <mutex>

static void OnRecognitionStart(SpeechRecognitionResponse *rsp) {
    INFOLN("OnRecognitionStart voiceId:%s", rsp->voice_id.c_str());
}

// 识别失败回调
static void OnFail(SpeechRecognitionResponse *rsp) {
    ERRLN("OnFail code:%d message:%s voiceId:%s", rsp->code, rsp->message.c_str(), rsp->voice_id.c_str());
    auto recognize = Recognize::GetRecognize(rsp->voice_id);
    if (!recognize) {
        WARNLN("recognize is nullptr, voiceId:%s", rsp->voice_id.c_str());
        return;
    }
    recognize->sendComplete("");
}

// 识别到一句话的开始
static void OnSentenceBegin(SpeechRecognitionResponse *rsp) {
    std::string text = rsp->result.voice_text_str;
    INFOLN("OnSentenceBegin, text:%s voiceId:%s", text.c_str(), rsp->voice_id.c_str());
    auto recognize = Recognize::GetRecognize(rsp->voice_id);
    if (!recognize) {
        WARNLN("recognize is nullptr, voiceId:%s", rsp->voice_id.c_str());
        return;
    }
    recognize->sendStartOfInput();
}

// 识别到一句话的结束
static void OnSentenceEnd(SpeechRecognitionResponse *rsp) {
    std::string text = rsp->result.voice_text_str;
    INFOLN("OnSentenceEnd text:%s voiceId:%s", text.c_str(), rsp->voice_id.c_str());
    auto recognize = Recognize::GetRecognize(rsp->voice_id);
    if (!recognize) {
        WARNLN("recognize is nullptr, voiceId:%s", rsp->voice_id.c_str());
        return;
    }
    recognize->sendComplete(text);
}

// 识别结果发生变化回调
static void OnRecognitionResultChange(SpeechRecognitionResponse *rsp) {
    std::string text = rsp->result.voice_text_str;
    INFOLN("OnRecognitionResultChange text:%s voiceId:%s", text.c_str(), rsp->voice_id.c_str());
}

// 识别完成回调
static void OnRecognitionComplete(SpeechRecognitionResponse *rsp) {
    std::string text = rsp->result.voice_text_str;
    INFOLN("OnRecognitionComplete text:%s voiceId:%s", text.c_str(), rsp->voice_id.c_str());
}

TencentRecognize::~TencentRecognize() {
    stop();
    INFOLN("TencentRecognize destruct, channelId:%s voiceId:%s", mChannelId.c_str(), mVoiceId.c_str());
}

int TencentRecognize::init()
{
    loadConfig();
    SpeechRecognizer *recognizer = new SpeechRecognizer(mAppId, mSecretId, mSecretKey);
    recognizer->SetVoiceId(mVoiceId);
    recognizer->SetOnRecognitionStart(OnRecognitionStart);
    recognizer->SetOnFail(OnFail);
    recognizer->SetOnRecognitionComplete(OnRecognitionComplete);
    recognizer->SetOnRecognitionResultChanged(OnRecognitionResultChange);
    recognizer->SetOnSentenceBegin(OnSentenceBegin);
    recognizer->SetOnSentenceEnd(OnSentenceEnd);
    recognizer->SetEngineModelType("8k_zh");
    recognizer->SetNeedVad(1); // 0：关闭 vad，1： 开启 vad。语音时长超过一分钟需要开启,如果对实时性要求较高。
    recognizer->SetHotwordId(""); // 热词 id。用于调用对应的热词表，如果在调用语音识别服务时，不进行单独的热词 id 设置，自动生效默认热词；如果进行了单独的热词 id 设置，那么将生效单独设置的热词 id。
    recognizer->SetCustomizationId(""); // 自学习模型 id。如不设置该参数，自动生效最后一次上线的自学习模型；如果设置了该参数，那么将生效对应的自学习模型。
    recognizer->SetFilterDirty(1); // 0 ：不过滤脏话 1：过滤脏话
    recognizer->SetFilterModal(1); // 0 ：不过滤语气词 1：过滤部分语气词  2:严格过滤
    recognizer->SetFilterPunc(1); // 0 ：不过滤句末的句号 1：过滤句末的句号
    recognizer->SetConvertNumMode(1); // 1： 根据场景智能转换为阿拉伯数字；0：全部转为中文数字。
    recognizer->SetWordInfo(0); // 是否显示词级别时间戳。0：不显示；1：显示，不包含标点时间戳，2：显示，包含标点时间戳。时间戳信息需要自行解析 AudioRecognizeResult.resultJson 获取
    INFOLN("begin recognizer start, channelId:%s voiceId:%s", mChannelId.c_str(), mVoiceId.c_str());
    int ret = recognizer->Start();
    if (ret < 0) {
        ERRLN("recognizer start failed, ret:%d channelId:%s voiceId:%s", ret, mChannelId.c_str(), mVoiceId.c_str());
        delete recognizer;
        return -1;
    }
    INFOLN("end recognizer start, channelId:%s voiceId:%s", mChannelId.c_str(), mVoiceId.c_str());
    mSpeechRecognizer.reset(recognizer);
    return 0;
}

void TencentRecognize::stop()
{
    std::lock_guard<std::mutex> l(mMutex);
    if (mIsStop) {
        return;
    }
    mIsStop = true;
    if (mSpeechRecognizer) {
        INFOLN("stop tencent recognize, channelId:%s", mChannelId.c_str());
        mSpeechRecognizer->Stop();
    }
}

int TencentRecognize::write(char* buff, int len)
{
    std::lock_guard<std::mutex> l(mMutex);
    if (mIsStop) {
        return 0;
    }
    mSpeechRecognizer->Write(buff, len);
    return 0;
}
