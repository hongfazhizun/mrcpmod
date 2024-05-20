#include "Recognize.h"
#include "RecogEngine.h"
#include "TencentRecognize.h"
#include "mrcp_recog_header.h"
#include <mutex>

string Recognize::sConfigFile = "conf/config.ini";

std::shared_ptr<Recognize> Recognize::Create(string channelId)
{
    string type;
    auto ini = std::make_shared<IniParser>();
    ini->setFileName(sConfigFile);
    ini->get("generic", "type", type);
    if (RECOGNIZE_TYPE_TENCENT == type) {
        INFOLN("create tencent recognize, channelId:%s", channelId.c_str());
        auto recognize = std::make_shared<TencentRecognize>();
        recognize->mChannelId = channelId;
        recognize->mRecognizeType = TENCENT;
        recognize->mIniParser = ini;
        // gen unique voice_id
        boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
        recognize->mVoiceId = boost::uuids::to_string(a_uuid);
        return recognize;
    }
    INFOLN("recognize type is not support, type:%s channelId:%s", type.c_str(), channelId.c_str());
    return nullptr;
}

void Recognize::setPartial(bool val)
{
    mIsPartial = val;
}

void Recognize::setRecogChannel(demo_recog_channel_t* val)
{
    mRecogChannel = val;
}

string Recognize::getVoiceId()
{
    return mVoiceId;
}

void Recognize::loadConfig()
{
    string type;
    mIniParser->get("generic", "type", type);
    mIniParser->get(type, "appid", mAppId);
    mIniParser->get(type, "secretid", mSecretId);
    mIniParser->get(type, "secretkey", mSecretKey);
}

std::mutex Recognize::sMutex;
map<string, string> Recognize::sChannelIdMap;
map<string, std::shared_ptr<Recognize>> Recognize::sMap;

string Recognize::GetVoiceId(string channelId)
{
    std::lock_guard<std::mutex> l(sMutex);
    auto it = sChannelIdMap.find(channelId);
    if (it == sChannelIdMap.end()) {
        return "";
    }
    return it->second;
}

std::shared_ptr<Recognize> Recognize::GetRecognize(string voiceId)
{
    std::lock_guard<std::mutex> l(sMutex);
    auto it = sMap.find(voiceId);
    if (it == sMap.end()) {
        return nullptr;
    }
    return it->second;
}

void Recognize::Del(string channelId, string voiceId)
{
    std::lock_guard<std::mutex> l(sMutex);
    sChannelIdMap.erase(channelId);
    sMap.erase(voiceId);
}

void Recognize::Del(string channelId)
{
    string voiceId = Recognize::GetVoiceId(channelId);
    if (voiceId.empty()) {
        WARNLN("voiceId is empty, channelId:%s", channelId.c_str());
        return;
    }
    auto recognize = Recognize::GetRecognize(voiceId);
    if (!recognize) {
        WARNLN("recognize is nullptr, channelId:%s voiceId:%s", channelId.c_str(), voiceId.c_str());
        Recognize::Del(channelId, voiceId);
        return;
    }
    recognize->stop();
    Recognize::Del(channelId, voiceId);
    INFOLN("delete recognize, channelId:%s voiceId:%s", channelId.c_str(), voiceId.c_str());
}

void Recognize::Set(string channelId, std::shared_ptr<Recognize> val)
{
    std::lock_guard<std::mutex> l(sMutex);
    sChannelIdMap.erase(channelId);
    sChannelIdMap.emplace(channelId, val->mVoiceId);
    sMap.erase(val->mVoiceId);
    sMap.emplace(val->mVoiceId, val);
}

void Recognize::sendStartOfInput()
{
    INFOLN("send start of input, channelId:%s voiceId:%s", mChannelId.c_str(), mVoiceId.c_str());
    demo_recog_msg_signal(DEMO_RECOG_MSG_START_OF_INPUT, mRecogChannel->channel, nullptr, RECOGNIZER_COMPLETION_CAUSE_SUCCESS, nullptr);
}

void Recognize::sendComplete(string text)
{
    text = mVoiceId + "|" + text;
    char* body = nullptr;
    if (!text.empty()) {
        int size = text.size() + 1;
        body = new char[size]();
        memset(body, 0, size);
        memcpy(body, text.c_str(), text.size());
    }
    INFOLN("send complete, partial:%d body:%s channelId:%s voiceId:%s", mIsPartial, body, mChannelId.c_str(), mVoiceId.c_str());
    mrcp_recog_completion_cause_e cause = RECOGNIZER_COMPLETION_CAUSE_SUCCESS;
    if (mIsPartial) {
        cause = RECOGNIZER_COMPLETION_CAUSE_PARTIAL_MATCH;
    }
    demo_recog_msg_signal(DEMO_RECOG_MSG_COMPLETE, mRecogChannel->channel, nullptr, cause, body);
}
